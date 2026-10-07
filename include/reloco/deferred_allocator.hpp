// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file deferred_allocator.hpp
 * @brief `deferred_allocator`: an `allocator_ref` whose real backend is
 * bound exactly once, after the fact -- for early (e.g. static-init-time)
 * allocator references in embedded/RTOS code where the real heap/arena
 * isn't ready yet.
 *
 * @details
 * A common bare-metal/RTOS problem: some global/static object needs an
 * `allocator_ref` *before* the real allocator (a `heap_allocator_tag`, a
 * `pool_allocator`/`bucket_allocator` sized from a runtime-detected memory
 * map, an arena carved out of a linker-script region not yet mapped, ...)
 * has been constructed -- typically because C++ static-initialization
 * order is not under the caller's control, or because the real allocator
 * depends on hardware/bootloader state only available once `main()` (or
 * an init task) actually starts running.
 *
 * `deferred_allocator` is a stable placeholder for exactly that: construct
 * one anywhere (global, static, or a plain stack variable created early),
 * hand its `.ref()` out to every object that needs an `allocator_ref` right
 * away, and call `try_bind(real)` exactly once -- later, once the real
 * backend exists -- to make every `allocator_ref` obtained from `.ref()`
 * (including ones already stored away) start forwarding to it. Until
 * `try_bind()` succeeds, every `allocate()` call through `.ref()` fails
 * with `error::not_initialized` instead of crashing/dereferencing a null
 * backend -- the same "fail fast and explicit, never UB" contract as
 * every other reloco API (see `docs/hardened-containers.md`).
 *
 * Built directly on `once_lock<allocator_ref>` (see `once_lock.hpp`) --
 * same "compose, don't reinvent" approach as `lazy_lock.hpp`/`lru_cache.hpp`
 * -- rather than new low-level plumbing: `try_bind()` is `once_lock::
 * try_set()`, so a second `try_bind()` call (double-binding, a programming
 * error) fails with `error::already_exists` rather than silently
 * overwriting an already-forwarding allocator out from under in-flight
 * callers. Binding is lock-free-fast-pathed exactly like `once_lock<T>`
 * itself: every `allocate`/`deallocate`/... call after the first
 * successful `try_bind()` only pays for one acquire-load before forwarding
 * straight into the real `allocator_ref`.
 *
 * `expand_in_place`/`reallocate`/`advise` are always present on the
 * `allocator_traits<deferred_allocator_tag>` specialization (unlike
 * `pool_allocator`'s, which omits them entirely because its backend truly
 * never supports them), so `can_expand_in_place()`/`can_reallocate()`/
 * `can_advise()` are always `true` on a `deferred_allocator`'s `.ref()`
 * (whether the eventual real backend actually supports each one cannot be
 * known at compile time). Before `try_bind()` succeeds they each fail
 * with `error::unsupported_operation`; afterwards they forward to the
 * bound real allocator and reflect *its* actual support.
 *
 * @code
 * // Constructed at static-init time, long before the real heap exists:
 * reloco::deferred_allocator g_early_alloc;
 *
 * // Some other global object, constructed even earlier, captures the
 * // not-yet-usable allocator_ref right away:
 * reloco::vector<int> g_early_vector(g_early_alloc.ref());
 *
 * // Any try_reserve()/try_push_back() before main() runs fails with
 * // error::not_initialized -- never UB, never a null-pointer crash.
 *
 * int main() {
 *   static reloco::heap_allocator_tag real_tag;
 *   std::ignore = g_early_alloc.try_bind(reloco::allocator_ref(real_tag));
 *   // g_early_vector (and every other holder of g_early_alloc.ref())
 *   // now allocates through the real heap allocator, transparently.
 *   std::ignore = g_early_vector.try_push_back(42);
 * }
 * @endcode
 */

#include "allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "once_lock.hpp"

#include <cstddef>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief Owning state backing `deferred_allocator`: a write-once cell
 * holding the real `allocator_ref` once bound. See the file-level docs.
 *
 * Neither copyable nor movable (matches `once_lock<T>`'s own restriction,
 * which it is built directly on).
 */
class RELOCO_OWNER deferred_allocator_context {
public:
  constexpr deferred_allocator_context() noexcept = default;

  deferred_allocator_context(const deferred_allocator_context &) = delete;
  deferred_allocator_context &operator=(const deferred_allocator_context &) = delete;
  deferred_allocator_context(deferred_allocator_context &&) = delete;
  deferred_allocator_context &operator=(deferred_allocator_context &&) = delete;

  /**
   * @brief Binds @p real as the backend every `allocator_ref` obtained
   * from this context forwards to, from this point on.
   * @return `error::already_exists` if this context is already bound
   * (by this call or an earlier one, including a racing concurrent
   * call) -- binding happens at most once, ever.
   */
  [[nodiscard]] result<void> try_bind(allocator_ref real) noexcept { return cell_.try_set(real); }

  /** @brief Whether `try_bind()` has already succeeded. Never blocks. */
  [[nodiscard]] bool is_bound() const noexcept { return cell_.get() != nullptr; }

  /** @brief The bound real allocator, or `nullptr` if not yet bound. Never blocks. */
  [[nodiscard]] const allocator_ref *bound() const noexcept RELOCO_LIFETIMEBOUND { return cell_.get(); }

private:
  once_lock<allocator_ref> cell_;
};

/**
 * @brief Tag identifying the `deferred_allocator` backend.
 */
struct deferred_allocator_tag {};

template <> struct allocator_traits<deferred_allocator_tag> {
  using context_type = deferred_allocator_context;

  [[nodiscard]] static result<mem_block> allocate(value_ref<context_type> ctx, std::size_t bytes,
                                                  std::size_t alignment) noexcept {
    const allocator_ref *real = ctx->bound();
    if (!real)
      return unexpected(error::not_initialized);
    return real->allocate(bytes, alignment);
  }

  static void deallocate(value_ref<context_type> ctx, void *ptr, std::size_t bytes) noexcept {
    const allocator_ref *real = ctx->bound();
    // A deallocate() call can only ever name a pointer obtained from a
    // prior successful allocate() above, which already requires `real` to
    // have been bound -- `real` being null here would mean the caller
    // somehow holds a pointer this context never actually handed out.
    RELOCO_DEBUG_ASSERT(real != nullptr, "deferred_allocator: deallocate() called while still unbound");
    if (real)
      real->deallocate(ptr, bytes);
  }

  [[nodiscard]] static result<std::size_t> expand_in_place(value_ref<context_type> ctx, void *ptr,
                                                           std::size_t old_size, std::size_t new_size) noexcept {
    const allocator_ref *real = ctx->bound();
    if (!real)
      return unexpected(error::unsupported_operation);
    return real->expand_in_place(ptr, old_size, new_size);
  }

  [[nodiscard]] static result<mem_block> reallocate(value_ref<context_type> ctx, void *ptr, std::size_t old_size,
                                                    std::size_t new_size, std::size_t alignment) noexcept {
    const allocator_ref *real = ctx->bound();
    if (!real)
      return unexpected(error::unsupported_operation);
    return real->reallocate(ptr, old_size, new_size, alignment);
  }

  static void advise(value_ref<context_type> ctx, void *ptr, std::size_t bytes, usage_hint hint) noexcept {
    const allocator_ref *real = ctx->bound();
    if (real)
      real->advise(ptr, bytes, hint);
  }
};

/**
 * @brief Bind-once placeholder `allocator_ref` source; see the file-level
 * docs for the full early-init/RTOS rationale and usage.
 *
 * Neither copyable nor movable: only ever exposes a type-erased
 * `allocator_ref` (via `.ref()`), so nothing needs (or is able) to move
 * the context itself once other code may already hold that handle --
 * matches `pool_allocator`/`bucket_allocator`/`malloc_allocator`'s own
 * choice, for the same reasons.
 */
class RELOCO_OWNER deferred_allocator {
public:
  constexpr deferred_allocator() noexcept = default;

  deferred_allocator(const deferred_allocator &) = delete;
  deferred_allocator &operator=(const deferred_allocator &) = delete;
  deferred_allocator(deferred_allocator &&) = delete;
  deferred_allocator &operator=(deferred_allocator &&) = delete;

  /** @brief See `deferred_allocator_context::try_bind`. */
  [[nodiscard]] result<void> try_bind(allocator_ref real) noexcept { return context_.try_bind(real); }

  /** @brief See `deferred_allocator_context::is_bound`. */
  [[nodiscard]] bool is_bound() const noexcept { return context_.is_bound(); }

  /**
   * @brief Type-erased handle forwarding to whatever `try_bind()` binds
   * (or, before that, failing every operation with `error::
   * not_initialized`/`error::unsupported_operation`, never UB).
   */
  [[nodiscard]] constexpr allocator_ref ref() & noexcept RELOCO_LIFETIMEBOUND {
    return allocator_ref(deferred_allocator_tag{}, context_);
  }

  allocator_ref ref() && = delete;

private:
  deferred_allocator_context context_;
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
