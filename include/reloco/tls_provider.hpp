// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file tls_provider.hpp
 * @brief Tag-differentiated, fallible, allocator-aware thread-local
 * storage provider framework.
 *
 * Ported from `microfmt/include/microfmt/detail/tls_provider.hpp`
 * (`MICROFMT_TLS_*` -> `RELOCO_TLS_*`, `MICROFMT_API_CLASS`/raw
 * `RELOCO_TRAP()` calls -> `RELOCO_ASSERT` with a message, matching
 * `reloco/detail/assert.hpp`'s own idiom), then reworked into a public
 * reloco API: every `get()`/`set()` now takes an `allocator_ref` (default
 * `default_allocator()`) and returns `reloco::result<...>` instead of
 * asserting/`new`-ing unconditionally, matching the rest of the library's
 * fallible-construction protocol (`concepts.hpp`/`construction_helpers.hpp`).
 * Storage is uniquely differentiated by both the stored type @p T and a
 * unique tag type @p Tag, so `tls_provider<int, struct foo_tag>` and
 * `tls_provider<int, struct bar_tag>` are two independent per-thread slots.
 *
 * - **`RELOCO_TLS_MODEL_THREAD_LOCAL`** (default): backed by C++11
 *   `thread_local`. Portable to any hosted C++17 target; requires no
 *   extra header; never actually fails (no allocation happens), but keeps
 *   the same fallible, allocator-accepting signature as every other model
 *   for source-level portability between them.
 * - **`RELOCO_TLS_MODEL_PTHREAD`**: backed by `pthread_key_create`/
 *   `pthread_getspecific`/`pthread_setspecific`, for POSIX targets that
 *   for whatever reason want to avoid compiler `thread_local` support
 *   (e.g. matching an existing pthread-only codebase). The one-time key
 *   creation every specialization shares (`detail::tls_key_holder<Tag>`)
 *   is guarded by a `futex.hpp` `futex_word` rather than `pthread_once`:
 *   a `compare_exchange` picks a single winner thread to call
 *   `pthread_key_create`, every other concurrent caller blocks via
 *   `futex_wait` until the winner publishes the outcome and wakes them
 *   via `futex_wake_all` -- same "run exactly once, even if it fails"
 *   contract `pthread_once` itself provides. Three internal
 *   specializations pick the cheapest representation for a given `T`:
 *     - a raw pointer (`T *`) stored directly as the key's value -- no
 *       heap allocation, `get()`/`set()` can only fail if the underlying
 *       one-time `pthread_key_create` call itself fails
 *       (`error::resource_exhausted`);
 *     - a small trivial value bit-packed into the key's `void *` slot --
 *       likewise zero heap allocation, same failure mode as above;
 *     - anything else: a `T` heap-allocated (via the `allocator_ref`
 *       passed to `get()`/`set()`, *not* `new`) alongside the very same
 *       `allocator_ref` it was allocated with, so the `pthread_key_create`
 *       destructor that runs on thread exit can deallocate it correctly
 *       through that same allocator regardless of which allocator any
 *       particular `set()`/`get()` call used. Can additionally fail with
 *       whatever `error` the allocator itself reports.
 * - **`RELOCO_TLS_MODEL_OS`**: `#include`s a fixed path,
 *   `detail/porting/tls_provider.hpp`, right at the point the built-in
 *   models above define `template <typename T, typename Tag> struct
 *   tls_provider`, instead of the built-in models -- the same
 *   fixed-include mechanism `mutex.hpp`/`thread.hpp`/`spin_lock.hpp` use
 *   for their own `_CUSTOM` backends (see `mutex.hpp` for the full
 *   rationale: it makes correctness independent of where else the
 *   application includes its replacement from). A kernel/RTOS port
 *   supplies one generic `tls_provider<T, Tag>` definition -- for every
 *   `T`/`Tag`, exactly like `RELOCO_TLS_MODEL_PTHREAD` does with its own
 *   `pthread_key_t`-per-`Tag` mechanism -- backed by its own
 *   per-task/per-thread storage (a TCB field, a scheduler-provided slot,
 *   ...), matching the escape hatch `RELOCO_MUTEX_BACKEND_CUSTOM`/
 *   `RELOCO_THREAD_BACKEND_CUSTOM`/`RELOCO_SPIN_LOCK_BACKEND_CUSTOM`
 *   provide elsewhere (see `mutex.hpp`/`thread.hpp`/`spin_lock.hpp`).
 *   `detail/porting/tls_provider.hpp` does not ship in this repository
 *   (only `detail/porting/tls_provider.template.hpp`, an unused
 *   documentation-only scaffold, does); supply your own, most
 *   conveniently through the `JPLCZ_RELOCO_PORTING_HEADERS` CMake
 *   variable (see `CMakeLists.txt`), which copies it into that exact path
 *   and defines `RELOCO_TLS_MODEL=RELOCO_TLS_MODEL_OS` automatically.
 * - **`RELOCO_TLS_MODEL_SINGLE`**: a single, global (not actually
 *   per-thread) static instance -- for a single-threaded build that still
 *   wants to link against code written against the `tls_provider<T, Tag>`
 *   interface. Never actually fails, same rationale as
 *   `RELOCO_TLS_MODEL_THREAD_LOCAL` above.
 *
 * `get()` returns `result<std::reference_wrapper<T>>` rather than
 * `result<T &>` (`expected<T, E>` requires its value type to be nothrow
 * move constructible, which no reference type satisfies) wherever the
 * model has genuine addressable per-thread storage to reference
 * (`THREAD_LOCAL`, `SINGLE`, `OS`, and the `PTHREAD` heap-allocated
 * specialization); the `PTHREAD` raw-pointer/small-trivial
 * specializations have no such addressable storage (the value lives only
 * as a bit pattern inside the key itself), so their `get()` returns
 * `result<T>` by value instead.
 *
 * Win32 Fiber-Local-Storage support (microfmt's `MICROFMT_TLS_MODEL_
 * WIN32`) is deliberately not ported here, matching `mutex.hpp`/
 * `thread.hpp`'s own decision to leave native Win32 backends out for now;
 * a Win32 target can supply its own model via `RELOCO_TLS_MODEL_OS`.
 *
 * @code
 * // reloco_user_config.hpp
 * #define RELOCO_TLS_MODEL RELOCO_TLS_MODEL_PTHREAD
 * @endcode
 */

#include "allocator.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "lifetime.hpp"

/** @name Thread-Local Storage Models */
/// @{
#define RELOCO_TLS_MODEL_THREAD_LOCAL 0 ///< Standard C++11 `thread_local` storage model.
#define RELOCO_TLS_MODEL_PTHREAD 1      ///< POSIX threads (pthreads) key-based TLS model.
#define RELOCO_TLS_MODEL_OS 2           ///< Custom OS-specific or bare-metal per-task storage model.
#define RELOCO_TLS_MODEL_SINGLE 3       ///< Single-threaded fallback model (one global static instance).
/// @}

#if !defined(RELOCO_TLS_MODEL)
#define RELOCO_TLS_MODEL RELOCO_TLS_MODEL_THREAD_LOCAL
#endif

#if (RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_PTHREAD)
#include "futex.hpp"

#include <cstdint>
#include <pthread.h>
#endif

#include <cstring>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

// `allocate_record()`/`deleter()` below (RELOCO_TLS_MODEL_PTHREAD's heap-allocated
// specialization) call allocator_ref::allocate/deallocate, which are
// RELOCO_UNSAFE_BUFFER_USAGE-marked -- see allocator.hpp/thread.hpp/unique_ptr.hpp.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

#if (RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_THREAD_LOCAL)

/**
 * @brief `thread_local`-backed storage provider.
 * @tparam T Type of the object being stored.
 * @tparam Tag Unique tag distinguishing this storage slot from others of type @p T.
 */
template <typename T, typename Tag> struct tls_provider {
  /**
   * @brief Gets a reference to the calling thread's instance. Never
   * actually fails (no allocation happens); @p alloc is accepted only to
   * keep the same signature as every other `RELOCO_TLS_MODEL`.
   */
  [[nodiscard]] static inline result<std::reference_wrapper<T>>
  get([[maybe_unused]] allocator_ref alloc = default_allocator()) noexcept {
    return std::ref(instance_);
  }

  /**
   * @brief Sets the calling thread's instance value. Never actually fails
   * -- see `get()`.
   */
  static inline result<void> set(T value, [[maybe_unused]] allocator_ref alloc = default_allocator()) noexcept {
    instance_ = std::move(value);
    return {};
  }

private:
  static inline thread_local T instance_ = {};
};

#elif (RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_PTHREAD)

namespace detail {

namespace pt_helpers {

/** @brief Creates a pthread TSD key, reporting a failure as
 * `error::resource_exhausted` (matching `thread_pthread.ipp`'s own
 * `pthread_create` failure mapping) instead of asserting. */
[[nodiscard]] inline result<void> create_key(pthread_key_t &key, void (*deleter)(void *)) noexcept {
  const int rc = pthread_key_create(&key, deleter);
  if (rc != 0)
    return unexpected(error::resource_exhausted);
  return {};
}

[[nodiscard]] inline void *get(pthread_key_t key) noexcept { return pthread_getspecific(key); }

inline void set(pthread_key_t key, void *p) noexcept {
  const int rc = pthread_setspecific(key, p);
  RELOCO_ASSERT(rc == 0, "pthread_setspecific failed");
}

} // namespace pt_helpers

template <typename U>
constexpr inline bool tls_can_fit_in_pointer_v = std::is_trivial_v<U> && (sizeof(U) <= sizeof(void *));

/** @brief One-time pthread TSD key holder shared by every specialization
 * below: key creation must run exactly once process-wide (per `Tag`), so
 * `state_` (a `futex.hpp` `futex_word`) implements the same "claim the
 * one-time init, block everyone else on it" pattern `once_lock<T>` uses
 * -- `compare_exchange` from `empty` to `initializing` picks a single
 * winner to call `pthread_key_create`, every other concurrent caller
 * blocks on `futex_wait(state_, initializing)` until the winner publishes
 * `ready` and wakes them via `futex_wake_all`. Any `pthread_key_create`
 * failure is stashed in `key_create_failed_` and surfaced through
 * `ensure()`'s `result<void>` from then on (every caller across every
 * thread observes the same outcome, since the attempt itself only ever
 * happens once, matching `pthread_once`'s own "run exactly once even if
 * the routine fails" semantics -- there is deliberately no retry path). */
template <typename Tag> struct tls_key_holder {
  [[nodiscard]] static result<void> ensure(void (*deleter)(void *)) noexcept {
    std::uint32_t s = state_.load(std::memory_order_acquire);
    if (s != ready) {
      for (;;) {
        std::uint32_t expected = empty;
        if (state_.compare_exchange_strong(expected, initializing, std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
          deleter_ = deleter;
          auto created = pt_helpers::create_key(key_, deleter_);
          key_create_failed_ = !created.has_value();
          state_.store(ready, std::memory_order_release);
          futex_wake_all(state_);
          break;
        }
        if (expected == ready)
          break;
        // expected == initializing: another thread is running create_key(); wait for it to publish `ready`.
        futex_wait(state_, initializing);
      }
    }
    if (key_create_failed_)
      return unexpected(error::resource_exhausted);
    return {};
  }

  [[nodiscard]] static pthread_key_t key() noexcept { return key_; }

private:
  static constexpr std::uint32_t empty = 0;
  static constexpr std::uint32_t initializing = 1;
  static constexpr std::uint32_t ready = 2;

  static inline futex_word state_ = empty;
  static inline pthread_key_t key_ = 0;
  static inline bool key_create_failed_ = false;
  static inline void (*deleter_)(void *) = nullptr;
};

} // namespace detail

template <typename T, typename Tag, typename Enable = void> struct tls_provider;

/// Specialization for raw pointers (`T *`): the pointer value is stored directly as the key's value, no heap
/// allocation.
template <typename T, typename Tag, typename Enable> struct tls_provider<T *, Tag, Enable> {
  /** @brief Fails only if the one-time `pthread_key_create` call itself
   * fails (`error::resource_exhausted`); @p alloc is unused (no heap
   * allocation for this specialization), accepted only to keep the same
   * signature as every other `RELOCO_TLS_MODEL`/specialization. */
  [[nodiscard]] static inline result<T *> get([[maybe_unused]] allocator_ref alloc = default_allocator()) noexcept {
    auto ensured = key_holder::ensure(nullptr);
    if (!ensured)
      return unexpected(ensured.error());
    return static_cast<T *>(detail::pt_helpers::get(key_holder::key()));
  }

  /** @brief Same failure mode as `get()`. */
  static inline result<void> set(T *value, [[maybe_unused]] allocator_ref alloc = default_allocator()) noexcept {
    auto ensured = key_holder::ensure(nullptr);
    if (!ensured)
      return unexpected(ensured.error());
    detail::pt_helpers::set(key_holder::key(), value);
    return {};
  }

private:
  using key_holder = detail::tls_key_holder<Tag>;
};

/// Specialization for non-trivial or oversized values: heap-allocated through the caller-supplied allocator.
template <typename T, typename Tag>
struct tls_provider<T, Tag, std::enable_if_t<!std::is_pointer_v<T> && !detail::tls_can_fit_in_pointer_v<T>>> {
  /**
   * @brief Gets a reference to the calling thread's instance, allocating
   * it lazily (through @p alloc, not `new`) if necessary. Fails if the
   * one-time `pthread_key_create` call fails, or if @p alloc's own
   * allocation fails (only possible the first time this thread calls
   * `get()`/`set()` for this `Tag`).
   */
  [[nodiscard]] static result<std::reference_wrapper<T>> get(allocator_ref alloc = default_allocator()) noexcept {
    auto ensured = key_holder::ensure(&deleter);
    if (!ensured)
      return unexpected(ensured.error());
    void *ptr = detail::pt_helpers::get(key_holder::key());
    if (!ptr) {
      auto created = allocate_record(alloc);
      if (!created)
        return unexpected(created.error());
      ptr = *created;
      detail::pt_helpers::set(key_holder::key(), ptr);
    }
    return std::ref(static_cast<record *>(ptr)->value);
  }

  /** @brief Same failure modes as `get()`. */
  static result<void> set(T value, allocator_ref alloc = default_allocator()) noexcept {
    auto ensured = key_holder::ensure(&deleter);
    if (!ensured)
      return unexpected(ensured.error());
    void *ptr = detail::pt_helpers::get(key_holder::key());
    if (ptr) {
      static_cast<record *>(ptr)->value = std::move(value);
      return {};
    }
    auto created = allocate_record(alloc, std::move(value));
    if (!created)
      return unexpected(created.error());
    detail::pt_helpers::set(key_holder::key(), *created);
    return {};
  }

private:
  using key_holder = detail::tls_key_holder<Tag>;

  /** @brief Heap block holding both the value and the very `allocator_ref`
   * it was allocated with, so `deleter()` (run by pthread on thread exit,
   * given only a `void *`) can deallocate it through the correct
   * allocator regardless of which allocator any particular `set()`/
   * `get()` call passed in. */
  struct record {
    allocator_ref alloc;
    T value;
  };

  template <typename... Args>
  [[nodiscard]] static result<void *> allocate_record(allocator_ref alloc, Args &&...args) noexcept {
    auto block = alloc.allocate(sizeof(record), alignof(record));
    if (!block)
      return unexpected(block.error());
    auto *rec = ::new (block->ptr) record{alloc, T(std::forward<Args>(args)...)};
    return static_cast<void *>(rec);
  }

  static void deleter(void *p) noexcept {
    auto *rec = static_cast<record *>(p);
    allocator_ref alloc = rec->alloc;
    rec->~record();
    alloc.deallocate(rec, sizeof(record));
  }
};

/// Specialization for small trivial values: bit-packed directly into the key's `void *` slot, no heap allocation.
template <typename T, typename Tag>
struct tls_provider<T, Tag, std::enable_if_t<!std::is_pointer_v<T> && detail::tls_can_fit_in_pointer_v<T>>> {
  /** @brief Same failure mode as the raw-pointer specialization's `get()`
   * above (no heap allocation here either). */
  [[nodiscard]] static result<T> get([[maybe_unused]] allocator_ref alloc = default_allocator()) noexcept {
    auto ensured = key_holder::ensure(nullptr);
    if (!ensured)
      return unexpected(ensured.error());
    T value{};
    void *ptr = detail::pt_helpers::get(key_holder::key());
    std::memcpy(&value, &ptr, sizeof(T));
    return value;
  }

  /** @brief Same failure mode as `get()`. */
  static result<void> set(T value, [[maybe_unused]] allocator_ref alloc = default_allocator()) noexcept {
    auto ensured = key_holder::ensure(nullptr);
    if (!ensured)
      return unexpected(ensured.error());
    void *ptr = nullptr;
    std::memcpy(&ptr, &value, sizeof(T));
    detail::pt_helpers::set(key_holder::key(), ptr);
    return {};
  }

private:
  using key_holder = detail::tls_key_holder<Tag>;
};

#elif (RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_OS)

} // namespace reloco

// See this file's top-level docs and
// `detail/porting/tls_provider.template.hpp` for the exact
// `tls_provider<T, Tag>` shape this must define -- including opening its
// own `namespace reloco { ... }`, exactly like the built-in models above.
#include "detail/porting/tls_provider.hpp"

namespace reloco {

#elif (RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_SINGLE)

/**
 * @brief Single-threaded fallback storage provider: one global static instance, not actually per-thread.
 * @tparam T Type of the object being stored.
 * @tparam Tag Unique tag distinguishing this storage slot from others of type @p T.
 */
template <typename T, typename Tag> struct tls_provider {
  /** @brief Gets a reference to the global static instance. Never
   * actually fails -- see the file-level documentation above. */
  [[nodiscard]] static inline result<std::reference_wrapper<T>>
  get([[maybe_unused]] allocator_ref alloc = default_allocator()) noexcept {
    return std::ref(instance_);
  }

  /** @brief Sets the global static instance value. Never actually fails
   * -- see `get()`. */
  static inline result<void> set(T value, [[maybe_unused]] allocator_ref alloc = default_allocator()) noexcept {
    instance_ = std::move(value);
    return {};
  }

private:
  static inline T instance_ = {};
};

#else
#error "RELOCO_TLS_MODEL not set properly"
#endif

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
