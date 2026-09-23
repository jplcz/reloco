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
 * `atomic_fallible_singleton<T, LockTraits>` is the thread-safe
 * counterpart: an `std::atomic<int>` fast path lets every thread skip
 * locking once initialization has completed, falling back to an
 * externally supplied lock (`LockTraits::lock`/`LockTraits::unlock` on a
 * caller-owned `LockTraits::lock_type &`) only for the first, contended
 * initialization. The lock is caller-supplied and caller-owned, exactly
 * like legacy, rather than an embedded `std::mutex`, so this remains
 * usable on freestanding/kernel targets with their own critical-section or
 * spinlock primitive -- see `docs/extending.md` for the general
 * tag/traits provider pattern this follows.
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
 * `type_id.hpp`'s `detail::type_id_tag<T>::tag`, only guaranteed to be one
 * merged symbol if it keeps default visibility. Under
 * `-fvisibility=hidden` (a common shared-library default) without this,
 * two shared objects instantiating `fallible_singleton<T>`/
 * `atomic_fallible_singleton<T, LockTraits>` for the same, otherwise
 * externally-visible `T` would each silently get their own private,
 * separately-initialized instance instead of sharing one -- defeating the
 * purpose of a singleton across that boundary. As with `type_id.hpp`, this
 * is opt-in: it does nothing unless the consumer defines
 * `RELOCO_ENABLE_EXPORT`, since it only matters if `instance()` is
 * actually called for a shared `T` from more than one shared object. It
 * is also, like `type_id.hpp`'s, only effective for a `T` that itself has
 * default visibility -- GCC/Clang compute a template instantiation's
 * visibility as the minimum of the template's own visibility and each
 * template argument's, so a consumer-defined `T` needs its own
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
#include "lifetime.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

namespace detail {

template <typename T, typename = void> struct has_lock_traits_impl : std::false_type {};

template <typename T>
struct has_lock_traits_impl<
    T, std::void_t<typename T::lock_type, decltype(T::lock(std::declval<typename T::lock_type &>())),
                   decltype(T::unlock(std::declval<typename T::lock_type &>()))>> : std::true_type {};

} // namespace detail

/**
 * @brief Detects a lock-traits type usable with `atomic_fallible_singleton`.
 *
 * Satisfied by any `LockTraits` providing a `lock_type` member type plus
 * `static void lock(lock_type &)` and `static void unlock(lock_type &)`.
 */
template <typename T> inline constexpr bool has_lock_traits_v = detail::has_lock_traits_impl<T>::value;

#if RELOCO_CXX20

template <typename T>
concept lock_traits = has_lock_traits_v<T>;

#endif // RELOCO_CXX20

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
 * @brief Thread-safe counterpart to @ref fallible_singleton, guarded by a
 * caller-supplied lock satisfying @ref has_lock_traits_v.
 *
 * Every call first checks an `std::atomic<int>` state with acquire/release
 * ordering: once initialization has completed, every subsequent call on
 * every thread takes the fast, lock-free path. Only a call that observes
 * the not-yet-ready state acquires `external_lock` (via
 * `LockTraits::lock`/`unlock`) and re-checks under the lock before
 * attempting construction, so concurrent first callers safely race down to
 * exactly one winner performing `construction_helpers::try_construct`.
 *
 * `LockTraits::lock_type` is caller-owned (typically a function-local
 * `std::mutex`, a platform critical section, or an RTOS mutex) and passed
 * in by reference on every call -- `atomic_fallible_singleton` itself never
 * allocates or owns a lock, keeping it usable on freestanding/kernel
 * targets that have no `std::mutex`.
 */
template <typename T, typename LockTraits> class RELOCO_EXPORT atomic_fallible_singleton {
  static_assert(has_lock_traits_v<LockTraits>,
                "LockTraits must provide lock_type plus static lock(lock_type&)/unlock(lock_type&)");

public:
  atomic_fallible_singleton() = delete;

  /**
   * @brief Returns the singleton instance, constructing it on the first
   * call (under `external_lock`) via `construction_helpers::try_construct`
   * using the given allocator.
   *
   * @param external_lock Caller-owned lock, only acquired while
   * initialization has not yet completed.
   * @param alloc The allocator to use for tiers that require one.
   */
  [[nodiscard]] static result<T *> instance(typename LockTraits::lock_type &external_lock,
                                            allocator_ref alloc) noexcept {
    // Acquire-load ensures we see T's fully-initialized memory once ready.
    if (state_.load(std::memory_order_acquire) == ready) {
      return ptr();
    }

    LockTraits::lock(external_lock);

    result<T *> res = ptr();
    if (state_.load(std::memory_order_relaxed) != ready) {
      auto ctor_res = construction_helpers::try_construct<T>(alloc, ptr());
      if (!ctor_res) {
        res = unexpected(ctor_res.error());
      } else {
        state_.store(ready, std::memory_order_release);
      }
    }

    LockTraits::unlock(external_lock);
    return res;
  }

  /**
   * @brief Same as `instance(lock_type&, allocator_ref)`, using the
   * process-wide `reloco::default_allocator()`.
   */
  [[nodiscard]] static result<T *> instance(typename LockTraits::lock_type &external_lock) noexcept {
    return instance(external_lock, default_allocator());
  }

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

  static constexpr int uninitialized = 0;
  static constexpr int ready = 1;

  alignas(effective_alignment_v<T>) static inline storage_type storage_;
  static inline std::atomic<int> state_{uninitialized};
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
