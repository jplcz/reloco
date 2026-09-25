// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file fallible_singleton.hpp
 * @brief Lazily, fallibly initialized function-local-static-style
 * singletons, without static initialization order fiasco.
 *
 * Ported from `reloco_legacy/include/reloco/fallible_singleton.hpp`, with
 * three changes to fit this repository's conventions:
 *
 * - Legacy dispatched construction through its own
 *   `is_fallible_initializable`/`fallible_constructed` protocol (a
 *   `T::try_init(constructor_key)` member, gated by a friend-only key type
 *   so only that machinery could call it). reloco already has a general
 *   tiered fallible-construction protocol for this
 *   (`construction_helpers::try_construct`, see
 *   `construction_helpers.hpp` and `docs/fallible-construction.md`), the
 *   same one `unique_ptr<T>` builds on -- so `T` here only needs to
 *   implement whichever of `try_construct`/`try_allocate`/`try_create`/
 *   plain nothrow construction makes sense for it, with no key type or
 *   dedicated singleton protocol required.
 * - Legacy took a `fallible_allocator &` (a virtual base). This takes a
 *   `reloco::allocator_ref` (see `allocator.hpp`) by value instead,
 *   matching every other consumer of the type-erased allocator handle.
 * - The inline storage buffer is aligned to `effective_alignment_v<T>`
 *   (see `alignment.hpp`), not plain `alignof(T)`, so a `T` that opts into
 *   SIMD-friendly over-alignment via `alignment_of<T>` gets it here too.
 *
 * `fallible_singleton<T>::instance()` is **not thread-safe**: concurrent
 * first calls from multiple threads race on `initialized_` and the
 * placement-new into `storage_`. Its intended use is controlled,
 * single-threaded initialization -- e.g. sequenced early in `main()`, or
 * from a single initialization thread -- specifically to sidestep C++'s
 * static initialization order fiasco for globals with non-trivial,
 * fallible setup, not to provide a thread-safe lazy singleton.
 *
 * `atomic_fallible_singleton<T>` is the thread-safe counterpart: a
 * `futex.hpp` `futex_word` state (`empty`/`initializing`/`ready`) lets
 * every thread skip locking once initialization has completed (a
 * lock-free acquire-load), falling back -- only for the first, contended
 * call -- to a `compare_exchange` claim of the `empty` -> `initializing`
 * transition, with every other concurrent caller blocking via
 * `futex_wait` until the winner publishes the outcome and wakes them via
 * `futex_wake_all`, exactly like `once_lock<T>`'s slow path (see
 * `once_lock.hpp`). If construction fails, the state reverts to `empty`
 * so a later call (from any thread) may retry -- unlike `once_lock<T>`,
 * `atomic_fallible_singleton<T>` embeds no caller-supplied lock at all
 * (an earlier revision took a `LockTraits`-satisfying external lock
 * instead; `futex.hpp` centralizes that same freestanding/kernel
 * customization point once, via `RELOCO_FUTEX_BACKEND_CUSTOM`, rather
 * than requiring every lazy-singleton call site to supply its own lock
 * type -- see `docs/extending.md` for the general tag/traits provider
 * pattern this superseded).
 *
 * Both classes' `instance()` placement-news `T` into a static storage
 * buffer via `construction_helpers::try_construct`, which is itself
 * `RELOCO_UNSAFE_BUFFER_USAGE` (see `construction_helpers.hpp`); this
 * file's `namespace reloco` body is wrapped in
 * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE` to
 * cover that call, matching `unique_ptr.hpp`. The public `instance()` API
 * itself is not marked unsafe: it never exposes caller-supplied storage or
 * a raw allocator call directly.
 *
 * Both classes are `RELOCO_EXPORT`-annotated (see `detail/compat.hpp` and
 * `type_id.hpp`, which has the same concern): their whole point is that
 * every caller of `instance()` for a given `T` shares one storage/state
 * pair, but each `static inline` data member backing that is, like
 * `type_id.hpp`'s `type_id_tag<T>::tag`, only guaranteed to be one
 * merged symbol if it keeps default visibility. Under
 * `-fvisibility=hidden` (a common shared-library default) without this,
 * two shared objects instantiating `fallible_singleton<T>`/
 * `atomic_fallible_singleton<T>` for the same, otherwise externally-visible
 * `T` would each silently get their own private, separately-initialized
 * instance instead of sharing one -- defeating the purpose of a singleton
 * across that boundary. As with `type_id.hpp`, this is opt-in: it does
 * nothing unless the consumer defines `RELOCO_ENABLE_EXPORT`, since it
 * only matters if `instance()` is actually called for a shared `T` from
 * more than one shared object. It is also, like `type_id.hpp`'s, only
 * effective for a `T` that itself has default visibility -- GCC/Clang
 * compute a template instantiation's visibility as the minimum of the
 * template's own visibility and each template argument's, so a
 * consumer-defined `T` needs its own
 * `__attribute__((visibility("default")))` (or equivalent) for its
 * `fallible_singleton<T>` to actually share one instance across a
 * `-fvisibility=hidden` shared-object boundary.
 */

#include "alignment.hpp"
#include "allocator.hpp"
#include "construction_helpers.hpp"
#include "default_allocator.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "futex.hpp"
#include "lifetime.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief A lazily, fallibly initialized singleton `T`, constructed on
 * first `instance()` call rather than at static-initialization time.
 *
 * Sidesteps the static initialization order fiasco for globals that need
 * non-trivial, potentially-failing setup: `T` is not constructed until the
 * first call to `instance()`, at a point the program controls, and that
 * construction can report failure through `reloco::result<T *>` instead of
 * throwing or running unchecked.
 *
 * @warning Not thread-safe. See @ref atomic_fallible_singleton for a
 * version safe to call from multiple threads.
 */
template <typename T> class RELOCO_EXPORT fallible_singleton {
public:
  fallible_singleton() = delete;

  /**
   * @brief Returns the singleton instance, constructing it on the first
   * call via `construction_helpers::try_construct` (see
   * `construction_helpers.hpp`) using the given allocator.
   *
   * Once constructed, `T` lives until program termination -- there is no
   * `reset()`; this mirrors a function-local `static T` with fallible,
   * explicit-allocator construction.
   *
   * @param alloc The allocator to use for tiers that require one (e.g.
   * `has_try_allocate_v<T>`); ignored by tiers that don't.
   */
  [[nodiscard]] static result<T *> instance(allocator_ref alloc) noexcept {
    if (initialized_)
      return ptr();

    auto res = construction_helpers::try_construct<T>(alloc, ptr());
    if (!res)
      return unexpected(res.error());

    initialized_ = true;
    return ptr();
  }

  /**
   * @brief Same as `instance(allocator_ref)`, using the process-wide
   * `reloco::default_allocator()`.
   */
  [[nodiscard]] static result<T *> instance() noexcept { return instance(default_allocator()); }

private:
  // Anonymous-union-style storage (see `optional.hpp`'s own `union { char
  // dummy_; T value_; }`), except named so a static data member can be
  // declared for it. `~storage_type` is a deliberate no-op: `T` is never
  // destroyed here (see the file-level doc comment -- the singleton lives
  // until program termination), but a union with a non-trivially
  // destructible alternative still needs *some* user-provided destructor
  // to remain usable as a static data member at all.
  union storage_type {
    constexpr storage_type() noexcept : dummy_('\0') {}
    ~storage_type() noexcept {}

    char dummy_;
    T value_;
  };

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) static T *ptr() noexcept {
    return std::addressof(storage_.value_);
  }

  alignas(effective_alignment_v<T>) static inline storage_type storage_;
  static inline bool initialized_ = false;
};

/**
 * @brief Thread-safe counterpart to @ref fallible_singleton, using a
 * `futex.hpp` `futex_word` state instead of an externally supplied lock.
 *
 * Every call first checks the state with an acquire load: once
 * initialization has completed, every subsequent call on every thread
 * takes the fast, lock-free path. Only a call that observes the
 * not-yet-ready state races to claim the `empty` -> `initializing`
 * transition via `compare_exchange`; the single winner performs
 * `construction_helpers::try_construct`, while every other concurrent
 * caller blocks via `futex_wait` until the winner publishes the outcome
 * (`ready` on success, back to `empty` -- allowing a later retry -- on
 * failure) and wakes them via `futex_wake_all`. No lock is ever held.
 */
template <typename T> class RELOCO_EXPORT atomic_fallible_singleton {
public:
  atomic_fallible_singleton() = delete;

  /**
   * @brief Returns the singleton instance, constructing it on the first
   * successful call via `construction_helpers::try_construct` using the
   * given allocator. If construction fails, the cell reverts to
   * uninitialized so a later call (from any thread) may retry.
   *
   * @param alloc The allocator to use for tiers that require one.
   */
  [[nodiscard]] static result<T *> instance(allocator_ref alloc) noexcept {
    // Acquire-load ensures we see T's fully-initialized memory once ready.
    if (state_.load(std::memory_order_acquire) == ready)
      return ptr();

    for (;;) {
      std::uint32_t expected = empty;
      if (state_.compare_exchange_strong(expected, initializing, std::memory_order_acq_rel, std::memory_order_acquire))
        break; // Claimed the transition: this call performs construction below.
      if (expected == ready)
        return ptr();
      // expected == initializing: another thread is constructing; wait for it to settle, then retry.
      futex_wait(state_, initializing);
    }

    auto ctor_res = construction_helpers::try_construct<T>(alloc, ptr());
    if (!ctor_res) {
      state_.store(empty, std::memory_order_release);
      futex_wake_all(state_);
      return unexpected(ctor_res.error());
    }

    state_.store(ready, std::memory_order_release);
    futex_wake_all(state_);
    return ptr();
  }

  /**
   * @brief Same as `instance(allocator_ref)`, using the process-wide
   * `reloco::default_allocator()`.
   */
  [[nodiscard]] static result<T *> instance() noexcept { return instance(default_allocator()); }

private:
  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) static T *ptr() noexcept {
    return std::addressof(storage_.value_);
  }

  // See `fallible_singleton::storage_type` above for why this union needs
  // its own no-op destructor.
  union storage_type {
    constexpr storage_type() noexcept : dummy_('\0') {}
    ~storage_type() noexcept {}

    char dummy_;
    T value_;
  };

  static constexpr std::uint32_t empty = 0;
  static constexpr std::uint32_t initializing = 1;
  static constexpr std::uint32_t ready = 2;

  alignas(effective_alignment_v<T>) static inline storage_type storage_;
  static inline futex_word state_{empty};
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
