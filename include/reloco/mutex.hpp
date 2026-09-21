// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file mutex.hpp
 * @brief OS-backed mutex, recursive mutex, error-checking mutex, shared
 * mutex, and condition variable, reporting failure through
 * `reloco::result<void>` instead of throwing.
 *
 * Ported from `reloco_legacy/include/reloco/mutex.hpp`, with the backend
 * selection reworked into an explicit, configurable customization point
 * instead of a hardcoded `#if defined(_WIN32)` / pthread split:
 *
 * - **`RELOCO_MUTEX_BACKEND_STD`**: wraps `<mutex>`/`<shared_mutex>`/
 *   `<condition_variable>`. Portable to any hosted C++17 target.
 * - **`RELOCO_MUTEX_BACKEND_PTHREAD`**: wraps `<pthread.h>` directly, for
 *   POSIX targets, matching legacy's original POSIX backend (its portable
 *   subset -- see below).
 * - Neither defined: auto-selected -- `RELOCO_MUTEX_BACKEND_PTHREAD` when
 *   `<pthread.h>` is available (`RELOCO_HAS_INCLUDE`), otherwise
 *   `RELOCO_MUTEX_BACKEND_STD`.
 * - **`RELOCO_MUTEX_BACKEND_CUSTOM`**: suppresses both built-in backends
 *   above entirely. An application targeting a platform with neither
 *   pthread nor a hosted `<mutex>` -- an RTOS, a native Win32 backend
 *   (SRWLOCK/CRITICAL_SECTION/CONDITION_VARIABLE, legacy's other backend,
 *   not ported here), a freestanding target, ... -- supplies its own
 *   `reloco::mutex`/`recursive_mutex`/`error_checking_mutex`/
 *   `shared_mutex`/`condition_variable` matching the same public API, in
 *   its own header, included by the application through the normal path,
 *   exactly like `RELOCO_DEFAULT_ALLOCATOR_CUSTOM` (see
 *   `default_allocator.hpp`):
 *
 * @code
 * // reloco_user_config.hpp
 * #define RELOCO_MUTEX_BACKEND_CUSTOM
 *
 * // my_platform_mutex.hpp, included normally elsewhere by the app.
 * namespace reloco {
 * class mutex { ... };            // lock()/unlock() -> result<void>, try_lock() -> bool,
 * class recursive_mutex { ... };  // native_handle(); see mutex.hpp's built-in
 * class error_checking_mutex { ... }; // backends for the exact shape to match.
 * class shared_mutex { ... };
 * class condition_variable { ... };
 * } // namespace reloco
 * @endcode
 *
 * `error_checking_mutex` is implemented once, generically, on top of
 * whichever `mutex` backend is active plus an `std::atomic<std::thread::id>`
 * owner tag -- unlike legacy, which relied on glibc's non-portable
 * `PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP` -- so it is portable to every
 * POSIX libc, not just glibc, and identical between backends.
 * `recursive_mutex`'s PTHREAD backend similarly uses the portable
 * `pthread_mutexattr_settype(PTHREAD_MUTEX_RECURSIVE)` initialization
 * sequence rather than glibc's non-portable
 * `PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP`.
 *
 * Legacy also defined (but never called from any public method)
 * `to_timespec`/`do_timed_lock` helpers wired to nothing -- dead code, not
 * ported. Timed locking (`try_lock_for`/`try_lock_until`) can be added
 * later as real public API if a use case needs it.
 */

#include "detail/compat.hpp"
#include "error.hpp"

#if !defined(RELOCO_MUTEX_BACKEND_CUSTOM)

#if !defined(RELOCO_MUTEX_BACKEND_STD) && !defined(RELOCO_MUTEX_BACKEND_PTHREAD)
#if RELOCO_HAS_INCLUDE(<pthread.h>)
#define RELOCO_MUTEX_BACKEND_PTHREAD 1
#else
#define RELOCO_MUTEX_BACKEND_STD 1
#endif
#endif

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

#if defined(RELOCO_MUTEX_BACKEND_PTHREAD)

#include <pthread.h>

namespace reloco {

namespace detail {

inline error mutex_error_from_posix_errno(int errno_value) noexcept {
  switch (errno_value) {
  case EINVAL:
    return error::invalid_argument;
  case EDEADLK:
    return error::deadlock;
  case ENOMEM:
    return error::allocation_failed;
  case EPERM:
    return error::invalid_owner;
  case EBUSY:
    return error::still_locked;
  case ETIMEDOUT:
    return error::timed_out;
  case EAGAIN:
    return error::try_again;
  default:
    return error::invalid_argument;
  }
}

} // namespace detail

/**
 * @brief Non-recursive mutex backed directly by `pthread_mutex_t`.
 */
class mutex {
public:
  using native_handle_type = pthread_mutex_t *;

  constexpr mutex() noexcept : handle_(PTHREAD_MUTEX_INITIALIZER) {}

  ~mutex() noexcept { pthread_mutex_destroy(&handle_); }

  mutex(const mutex &) = delete;
  mutex &operator=(const mutex &) = delete;

  [[nodiscard]] result<void> lock() & noexcept {
    int r = pthread_mutex_lock(&handle_);
    if (r == 0)
      return {};
    return unexpected(detail::mutex_error_from_posix_errno(r));
  }

  [[nodiscard]] result<void> unlock() & noexcept {
    int r = pthread_mutex_unlock(&handle_);
    if (r == 0)
      return {};
    return unexpected(detail::mutex_error_from_posix_errno(r));
  }

  [[nodiscard]] bool try_lock() & noexcept { return pthread_mutex_trylock(&handle_) == 0; }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  pthread_mutex_t handle_;
};

/**
 * @brief Recursive mutex backed by `pthread_mutex_t` configured with
 * `PTHREAD_MUTEX_RECURSIVE` via `pthread_mutexattr_t` -- portable to every
 * POSIX libc, unlike glibc's `PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP`.
 */
class recursive_mutex {
public:
  using native_handle_type = pthread_mutex_t *;

  recursive_mutex() noexcept {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&handle_, &attr);
    pthread_mutexattr_destroy(&attr);
  }

  ~recursive_mutex() noexcept { pthread_mutex_destroy(&handle_); }

  recursive_mutex(const recursive_mutex &) = delete;
  recursive_mutex &operator=(const recursive_mutex &) = delete;

  [[nodiscard]] result<void> lock() & noexcept {
    int r = pthread_mutex_lock(&handle_);
    if (r == 0)
      return {};
    return unexpected(detail::mutex_error_from_posix_errno(r));
  }

  [[nodiscard]] result<void> unlock() & noexcept {
    int r = pthread_mutex_unlock(&handle_);
    if (r == 0)
      return {};
    return unexpected(detail::mutex_error_from_posix_errno(r));
  }

  [[nodiscard]] bool try_lock() & noexcept { return pthread_mutex_trylock(&handle_) == 0; }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  pthread_mutex_t handle_;
};

/**
 * @brief Reader/writer lock backed by `pthread_rwlock_t`.
 */
class shared_mutex {
public:
  using native_handle_type = pthread_rwlock_t *;

  constexpr shared_mutex() noexcept = default;

  ~shared_mutex() noexcept { pthread_rwlock_destroy(&handle_); }

  shared_mutex(const shared_mutex &) = delete;
  shared_mutex &operator=(const shared_mutex &) = delete;

  [[nodiscard]] result<void> lock() & noexcept {
    int r = pthread_rwlock_wrlock(&handle_);
    if (r == 0)
      return {};
    return unexpected(detail::mutex_error_from_posix_errno(r));
  }

  [[nodiscard]] result<void> unlock() & noexcept {
    int r = pthread_rwlock_unlock(&handle_);
    if (r == 0)
      return {};
    return unexpected(detail::mutex_error_from_posix_errno(r));
  }

  [[nodiscard]] bool try_lock() & noexcept { return pthread_rwlock_trywrlock(&handle_) == 0; }

  [[nodiscard]] result<void> lock_shared() & noexcept {
    int r = pthread_rwlock_rdlock(&handle_);
    if (r == 0)
      return {};
    return unexpected(detail::mutex_error_from_posix_errno(r));
  }

  [[nodiscard]] result<void> unlock_shared() & noexcept {
    int r = pthread_rwlock_unlock(&handle_);
    if (r == 0)
      return {};
    return unexpected(detail::mutex_error_from_posix_errno(r));
  }

  [[nodiscard]] bool try_lock_shared() & noexcept { return pthread_rwlock_tryrdlock(&handle_) == 0; }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  pthread_rwlock_t handle_ = PTHREAD_RWLOCK_INITIALIZER;
};

/**
 * @brief Condition variable backed by `pthread_cond_t`, usable only with
 * `std::unique_lock<mutex>` (not `recursive_mutex`/`shared_mutex`),
 * matching `std::condition_variable`'s own restriction to `std::mutex`.
 *
 * @note `wait` assumes `locker` already owns the lock (checked via
 * `owns_lock()`); the internal reacquire after waking goes straight
 * through `pthread_cond_wait`'s own native re-lock, not through `mutex`'s
 * checked `lock()`, exactly like legacy.
 */
class condition_variable {
public:
  using native_handle_type = pthread_cond_t *;

  constexpr condition_variable() noexcept = default;

  ~condition_variable() noexcept { pthread_cond_destroy(&cond_); }

  condition_variable(const condition_variable &) = delete;
  condition_variable &operator=(const condition_variable &) = delete;

  [[nodiscard]] result<void> wait(std::unique_lock<mutex> &locker) & noexcept {
    if (!locker.owns_lock())
      return unexpected(error::not_locked);
    pthread_cond_wait(&cond_, locker.mutex()->native_handle());
    return {};
  }

  template <typename Predicate> result<void> wait(std::unique_lock<mutex> &locker, Predicate pred) & {
    if (!locker.owns_lock())
      return unexpected(error::not_locked);
    while (!pred())
      pthread_cond_wait(&cond_, locker.mutex()->native_handle());
    return {};
  }

  void notify_one() & noexcept { pthread_cond_signal(&cond_); }

  void notify_all() & noexcept { pthread_cond_broadcast(&cond_); }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &cond_; }

private:
  pthread_cond_t cond_ = PTHREAD_COND_INITIALIZER;
};

} // namespace reloco

#elif defined(RELOCO_MUTEX_BACKEND_STD)

#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <system_error>

namespace reloco {

namespace detail {

#if RELOCO_HAS_EXCEPTIONS

inline error mutex_error_from_system_error(const std::system_error &e) noexcept {
  switch (e.code().value()) {
  case static_cast<int>(std::errc::resource_deadlock_would_occur):
    return error::deadlock;
  case static_cast<int>(std::errc::operation_not_permitted):
    return error::invalid_owner;
  case static_cast<int>(std::errc::device_or_resource_busy):
    return error::still_locked;
  case static_cast<int>(std::errc::timed_out):
    return error::timed_out;
  case static_cast<int>(std::errc::resource_unavailable_try_again):
    return error::try_again;
  case static_cast<int>(std::errc::not_enough_memory):
    return error::allocation_failed;
  default:
    return error::invalid_argument;
  }
}

#endif // RELOCO_HAS_EXCEPTIONS

} // namespace detail

// Calls `handle_.<locking_call>()` and translates any thrown
// `std::system_error` into a `result<void>`. When `RELOCO_HAS_EXCEPTIONS` is
// `0` (e.g. built with `-fno-exceptions`), the call is made unguarded:
// `try`/`catch` is not valid syntax in that mode, and the standard library's
// own throwing paths become terminating calls anyway, so there is nothing
// left for reloco to translate.
#if RELOCO_HAS_EXCEPTIONS
#define RELOCO_DETAIL_MUTEX_TRY_LOCK(locking_call)                                                                     \
  try {                                                                                                                \
    handle_.locking_call();                                                                                            \
    return {};                                                                                                         \
  } catch (const std::system_error &e) {                                                                               \
    return unexpected(detail::mutex_error_from_system_error(e));                                                       \
  }
#else
#define RELOCO_DETAIL_MUTEX_TRY_LOCK(locking_call)                                                                     \
  handle_.locking_call();                                                                                              \
  return {};
#endif

/**
 * @brief Non-recursive mutex wrapping `std::mutex`.
 */
class mutex {
public:
  using native_handle_type = std::mutex *;

  mutex() noexcept = default;

  mutex(const mutex &) = delete;
  mutex &operator=(const mutex &) = delete;

  [[nodiscard]] result<void> lock() & noexcept { RELOCO_DETAIL_MUTEX_TRY_LOCK(lock) }

  [[nodiscard]] result<void> unlock() & noexcept {
    handle_.unlock();
    return {};
  }

  [[nodiscard]] bool try_lock() & noexcept { return handle_.try_lock(); }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  std::mutex handle_;
};

/**
 * @brief Recursive mutex wrapping `std::recursive_mutex`.
 */
class recursive_mutex {
public:
  using native_handle_type = std::recursive_mutex *;

  recursive_mutex() noexcept = default;

  recursive_mutex(const recursive_mutex &) = delete;
  recursive_mutex &operator=(const recursive_mutex &) = delete;

  [[nodiscard]] result<void> lock() & noexcept { RELOCO_DETAIL_MUTEX_TRY_LOCK(lock) }

  [[nodiscard]] result<void> unlock() & noexcept {
    handle_.unlock();
    return {};
  }

  [[nodiscard]] bool try_lock() & noexcept { return handle_.try_lock(); }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  std::recursive_mutex handle_;
};

/**
 * @brief Reader/writer lock wrapping `std::shared_mutex`.
 */
class shared_mutex {
public:
  using native_handle_type = std::shared_mutex *;

  shared_mutex() noexcept = default;

  shared_mutex(const shared_mutex &) = delete;
  shared_mutex &operator=(const shared_mutex &) = delete;

  [[nodiscard]] result<void> lock() & noexcept { RELOCO_DETAIL_MUTEX_TRY_LOCK(lock) }

  [[nodiscard]] result<void> unlock() & noexcept {
    handle_.unlock();
    return {};
  }

  [[nodiscard]] bool try_lock() & noexcept { return handle_.try_lock(); }

  [[nodiscard]] result<void> lock_shared() & noexcept { RELOCO_DETAIL_MUTEX_TRY_LOCK(lock_shared) }

  [[nodiscard]] result<void> unlock_shared() & noexcept {
    handle_.unlock_shared();
    return {};
  }

  [[nodiscard]] bool try_lock_shared() & noexcept { return handle_.try_lock_shared(); }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  std::shared_mutex handle_;
};

#undef RELOCO_DETAIL_MUTEX_TRY_LOCK

/**
 * @brief Condition variable wrapping `std::condition_variable`, usable
 * only with `std::unique_lock<mutex>` (not `recursive_mutex`/
 * `shared_mutex`), matching `std::condition_variable`'s own restriction to
 * `std::mutex`.
 *
 * @note Internally adopts `locker`'s underlying `std::mutex` into a
 * temporary `std::unique_lock<std::mutex>` so `std::condition_variable`
 * itself (not the heavier, type-erased `std::condition_variable_any`) can
 * be used -- the internal reacquire after waking goes straight through
 * `std::condition_variable`'s own native re-lock, not through `mutex`'s
 * checked `lock()`, exactly like the PTHREAD backend.
 */
class condition_variable {
public:
  using native_handle_type = std::condition_variable *;

  condition_variable() noexcept = default;

  condition_variable(const condition_variable &) = delete;
  condition_variable &operator=(const condition_variable &) = delete;

  [[nodiscard]] result<void> wait(std::unique_lock<mutex> &locker) & noexcept {
    if (!locker.owns_lock())
      return unexpected(error::not_locked);
    std::unique_lock<std::mutex> native_lock(*locker.mutex()->native_handle(), std::adopt_lock);
    cv_.wait(native_lock);
    native_lock.release();
    return {};
  }

  template <typename Predicate> result<void> wait(std::unique_lock<mutex> &locker, Predicate pred) & {
    if (!locker.owns_lock())
      return unexpected(error::not_locked);
    std::unique_lock<std::mutex> native_lock(*locker.mutex()->native_handle(), std::adopt_lock);
    cv_.wait(native_lock, std::move(pred));
    native_lock.release();
    return {};
  }

  void notify_one() & noexcept { cv_.notify_one(); }

  void notify_all() & noexcept { cv_.notify_all(); }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &cv_; }

private:
  std::condition_variable cv_;
};

} // namespace reloco

#endif // RELOCO_MUTEX_BACKEND_*

namespace reloco {

/**
 * @brief Error-checking mutex: like `mutex`, but `lock()` fails with
 * `error::deadlock` instead of blocking forever if the calling thread
 * already holds it, and `unlock()` fails with `error::invalid_owner` if
 * called by a thread that doesn't hold it.
 *
 * Implemented once, generically, on top of whichever `mutex` backend is
 * active plus an `std::atomic<std::thread::id>` owner tag, rather than a
 * backend-specific native error-checking mutex type (e.g. glibc's
 * non-portable `PTHREAD_MUTEX_ERRORCHECK`), so behavior is identical
 * between backends and portable to every POSIX libc.
 */
class error_checking_mutex {
public:
  using native_handle_type = mutex::native_handle_type;

  error_checking_mutex() noexcept = default;

  error_checking_mutex(const error_checking_mutex &) = delete;
  error_checking_mutex &operator=(const error_checking_mutex &) = delete;

  [[nodiscard]] result<void> lock() & noexcept {
    auto self = std::this_thread::get_id();
    if (owner_.load(std::memory_order_relaxed) == self)
      return unexpected(error::deadlock);
    auto res = mutex_.lock();
    if (!res)
      return res;
    owner_.store(self, std::memory_order_release);
    return {};
  }

  [[nodiscard]] result<void> unlock() & noexcept {
    if (owner_.load(std::memory_order_acquire) != std::this_thread::get_id())
      return unexpected(error::invalid_owner);
    owner_.store(std::thread::id{}, std::memory_order_relaxed);
    return mutex_.unlock();
  }

  [[nodiscard]] bool try_lock() & noexcept {
    auto self = std::this_thread::get_id();
    if (owner_.load(std::memory_order_relaxed) == self)
      return false;
    if (!mutex_.try_lock())
      return false;
    owner_.store(self, std::memory_order_release);
    return true;
  }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return mutex_.native_handle(); }

private:
  mutex mutex_;
  std::atomic<std::thread::id> owner_{};
};

} // namespace reloco

#endif // !RELOCO_MUTEX_BACKEND_CUSTOM
