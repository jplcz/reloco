// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once
#include "assert.hpp"

/** @file tls_provider.hpp
 * @brief Tag-differentiated thread-local storage provider framework.
 *
 * Ported from `microfmt/include/microfmt/detail/tls_provider.hpp`
 * (`MICROFMT_TLS_*` -> `RELOCO_TLS_*`, `MICROFMT_API_CLASS`/raw
 * `RELOCO_TRAP()` calls -> `RELOCO_ASSERT` with a message, matching
 * `reloco/detail/assert.hpp`'s own idiom). Provides a modular,
 * multi-backend thread-local storage (TLS) abstraction for freestanding,
 * bare-metal, and hosted environments alike. Storage is uniquely
 * differentiated by both the stored type @p T and a unique tag type
 * @p Tag, so `tls_provider<int, struct foo_tag>` and
 * `tls_provider<int, struct bar_tag>` are two independent per-thread
 * slots. This is an internal building block for higher-level reloco
 * features that need genuine per-thread state (e.g. the parker behind
 * `this_thread::park()`), not a public-facing API on its own.
 *
 * - **`RELOCO_TLS_MODEL_THREAD_LOCAL`** (default): backed by C++11
 *   `thread_local`. Portable to any hosted C++17 target; requires no
 *   extra header.
 * - **`RELOCO_TLS_MODEL_PTHREAD`**: backed by `pthread_key_create`/
 *   `pthread_getspecific`/`pthread_setspecific`, for POSIX targets that
 *   for whatever reason want to avoid compiler `thread_local` support
 *   (e.g. matching an existing pthread-only codebase). Three internal
 *   specializations pick the cheapest representation for a given `T`:
 *   a raw pointer stored directly as the key's value, a small trivial
 *   value bit-packed into the key's `void *` slot (zero heap allocation),
 *   or -- for anything else -- a heap-allocated `T` whose lifetime is
 *   managed by a pthread key destructor.
 * - **`RELOCO_TLS_MODEL_OS`**: declares the `tls_provider<T, Tag>`
 *   template with no definition; a kernel/RTOS port supplies `get()`/
 *   `set()` against its own per-task/per-thread storage (a TCB field, a
 *   scheduler-provided slot, ...), matching the escape hatch
 *   `RELOCO_MUTEX_BACKEND_CUSTOM`/`RELOCO_THREAD_BACKEND_CUSTOM` provide
 *   elsewhere (see `mutex.hpp`/`thread.hpp`).
 * - **`RELOCO_TLS_MODEL_SINGLE`**: a single, global (not actually
 *   per-thread) static instance -- for a single-threaded build that still
 *   wants to link against code written against the `tls_provider<T, Tag>`
 *   interface.
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
#include <pthread.h>
#endif

#include <cstring>
#include <type_traits>
#include <utility>

namespace reloco::detail {

#if (RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_THREAD_LOCAL)

/**
 * @brief `thread_local`-backed storage provider.
 * @tparam T Type of the object being stored.
 * @tparam Tag Unique tag distinguishing this storage slot from others of type @p T.
 */
template <typename T, typename Tag> struct tls_provider {
  /**
   * @brief Gets a reference to the calling thread's instance.
   * @return Reference to the thread-local object.
   */
  [[nodiscard]] static inline T &get() noexcept { return instance_; }

  /**
   * @brief Sets the calling thread's instance value.
   * @param value Value to assign.
   */
  static inline void set(T value) noexcept { instance_ = std::move(value); }

private:
  static inline thread_local T instance_ = {};
};

#elif (RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_PTHREAD)

namespace pt_helpers {

inline void create(pthread_key_t &key, void (*deleter)(void *)) noexcept {
  const int rc = pthread_key_create(&key, deleter);
  RELOCO_ASSERT(rc == 0, "pthread_key_create failed");
}

[[nodiscard]] inline void *get(pthread_key_t key) noexcept { return pthread_getspecific(key); }

inline void set(pthread_key_t key, void *p) noexcept {
  const int rc = pthread_setspecific(key, p);
  RELOCO_ASSERT(rc == 0, "pthread_setspecific failed");
}

template <typename T> void default_deleter(void *p) noexcept { delete static_cast<T *>(p); }

} // namespace pt_helpers

template <typename U>
constexpr inline bool tls_can_fit_in_pointer_v = std::is_trivial_v<U> && (sizeof(U) <= sizeof(void *));

template <typename T, typename Tag, typename Enable = void> struct tls_provider;

/// Specialization for raw pointers (`T *`): the pointer value is stored directly as the key's value.
template <typename T, typename Tag, typename Enable> struct tls_provider<T *, Tag, Enable> {
  [[nodiscard]] static inline T *get() noexcept {
    init_once();
    return static_cast<T *>(pt_helpers::get(key_));
  }

  /**
   * @brief Sets or updates the calling thread's instance value.
   * @param value Value to assign.
   */
  static inline void set(T *value) noexcept {
    init_once();
    pt_helpers::set(key_, value);
  }

private:
  /** @brief Key creation routine executed once, process-wide. */
  static void init_routine() noexcept { pt_helpers::create(key_, nullptr); }

  /** @brief Ensures thread-safe one-time initialization of the pthread key. */
  static void init_once() noexcept { pthread_once(&key_init_, init_routine); }

  static inline pthread_once_t key_init_ = PTHREAD_ONCE_INIT;
  static inline pthread_key_t key_ = 0;
};

/// Specialization for non-trivial or oversized values: heap-allocated, freed by a pthread key destructor.
template <typename T, typename Tag>
struct tls_provider<T, Tag, std::enable_if_t<!std::is_pointer_v<T> && !tls_can_fit_in_pointer_v<T>>> {
  /**
   * @brief Gets a reference to the calling thread's instance, allocating it lazily if necessary.
   * @return Reference to the thread-local object.
   */
  [[nodiscard]] static inline T &get() noexcept {
    init_once();
    void *ptr = pt_helpers::get(key_);
    if (!ptr) {
      ptr = new T();
      RELOCO_ASSERT(ptr != nullptr, "TLS object allocation failed");
      pt_helpers::set(key_, ptr);
    }
    return *static_cast<T *>(ptr);
  }

  /**
   * @brief Sets or updates the calling thread's instance value.
   * @param value Value to assign.
   */
  static inline void set(T value) noexcept {
    init_once();
    void *ptr = pt_helpers::get(key_);
    if (!ptr) {
      ptr = new T(std::move(value));
      RELOCO_ASSERT(ptr != nullptr, "TLS object allocation failed");
      pt_helpers::set(key_, ptr);
    } else {
      *static_cast<T *>(ptr) = std::move(value);
    }
  }

private:
  /** @brief Key creation routine executed once, process-wide. */
  static void init_routine() noexcept { pt_helpers::create(key_, &pt_helpers::default_deleter<T>); }

  /** @brief Ensures thread-safe one-time initialization of the pthread key. */
  static void init_once() noexcept { pthread_once(&key_init_, init_routine); }

  static inline pthread_once_t key_init_ = PTHREAD_ONCE_INIT;
  static inline pthread_key_t key_ = 0;
};

/// Specialization for small trivial values: bit-packed directly into the key's `void *` slot, no heap allocation.
template <typename T, typename Tag>
struct tls_provider<T, Tag, std::enable_if_t<!std::is_pointer_v<T> && tls_can_fit_in_pointer_v<T>>> {
  [[nodiscard]] static inline T get() noexcept {
    init_once();
    T value{};
    void *ptr = pt_helpers::get(key_);
    std::memcpy(&value, &ptr, sizeof(T));
    return value;
  }

  /**
   * @brief Sets or updates the calling thread's instance value.
   * @param value Value to assign.
   */
  static inline void set(T value) noexcept {
    init_once();
    void *ptr = nullptr;
    std::memcpy(&ptr, &value, sizeof(T));
    pt_helpers::set(key_, ptr);
  }

private:
  /** @brief Key creation routine executed once, process-wide. */
  static void init_routine() noexcept { pt_helpers::create(key_, nullptr); }

  /** @brief Ensures thread-safe one-time initialization of the pthread key. */
  static void init_once() noexcept { pthread_once(&key_init_, init_routine); }

  static inline pthread_once_t key_init_ = PTHREAD_ONCE_INIT;
  static inline pthread_key_t key_ = 0;
};

#elif (RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_OS)

/**
 * @brief Custom OS-specific or bare-metal storage provider, implemented by the target port.
 * @tparam T Type of the object being stored.
 * @tparam Tag Unique tag distinguishing this storage slot from others of type @p T.
 */
template <typename T, typename Tag> struct tls_provider {
  /** @brief Gets a reference to the calling task/thread's instance (implemented by the target port). */
  [[nodiscard]] static T &get() noexcept;
  /** @brief Sets the calling task/thread's instance value (implemented by the target port). */
  static void set(T value) noexcept;
};

#elif (RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_SINGLE)

/**
 * @brief Single-threaded fallback storage provider: one global static instance, not actually per-thread.
 * @tparam T Type of the object being stored.
 * @tparam Tag Unique tag distinguishing this storage slot from others of type @p T.
 */
template <typename T, typename Tag> struct tls_provider {
  /**
   * @brief Gets a reference to the global static instance.
   * @return Reference to the instance.
   */
  [[nodiscard]] static inline T &get() noexcept { return instance_; }

  /**
   * @brief Sets the global static instance value.
   * @param value Value to assign.
   */
  static inline void set(T value) noexcept { instance_ = std::move(value); }

private:
  static inline T instance_ = {};
};

#else
#error "RELOCO_TLS_MODEL not set properly"
#endif

} // namespace reloco::detail
