// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file mutex.hpp
 * @brief OS-backed mutex, recursive mutex, error-checking mutex, shared
 * mutex, and condition variable. `lock()`/`unlock()` (and the `_shared`
 * variants) are infallible from the caller's perspective: any underlying
 * OS/library failure indicates a programming error (e.g. relocking a
 * non-recursive mutex already held by the calling thread, or unlocking one
 * not held) and is reported via `RELOCO_ASSERT` rather than
 * `reloco::result<void>`. `error_checking_mutex` is the deliberate
 * exception -- its whole purpose is turning exactly those misuses into a
 * reportable `result<void>` (`error::deadlock`/`error::invalid_owner`)
 * instead of asserting.
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
 * class mutex { ... };            // lock()/unlock() -> void (assert on failure),
 * class recursive_mutex { ... };  // try_lock() -> bool, native_handle();
 * class error_checking_mutex { ... }; // see mutex.hpp's built-in backends
 * class shared_mutex { ... };         // for the exact shape to match.
 * class RELOCO_EXPORT condition_variable { ... };
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
 * ported. Timed *locking* (`try_lock_for`/`try_lock_until`) can still be
 * added later as real public API if a use case needs it, but
 * `condition_variable::wait_for` (a bounded wait, not a bounded lock
 * acquisition) is provided below:
 *
 * - **`RELOCO_MUTEX_BACKEND_PTHREAD`**: backed by `pthread_cond_timedwait`
 *   against an absolute deadline. The condition variable is initialized
 *   with `pthread_condattr_setclock(CLOCK_MONOTONIC)` whenever the
 *   platform advertises POSIX Clock Selection support (`_POSIX_CLOCK_
 *   SELECTION`, from `<unistd.h>`) -- a timed wait against a monotonic
 *   clock is not affected by a concurrent wall-clock adjustment (NTP sync,
 *   manual `date` change, ...), unlike `CLOCK_REALTIME`. Falls back to the
 *   default `CLOCK_REALTIME` on a platform that does not support clock
 *   selection at all (e.g. Darwin/macOS, whose pthread does not implement
 *   `pthread_condattr_setclock`). Define `RELOCO_MUTEX_NO_MONOTONIC_CLOCK`
 *   (see `reloco_config.hpp`) to force `CLOCK_REALTIME` even on a platform
 *   that would otherwise be detected as supporting the monotonic clock.
 * - **`RELOCO_MUTEX_BACKEND_STD`**: backed directly by
 *   `std::condition_variable::wait_for`.
 *
 * `wait_for`'s timeout is a `reloco::duration` (`duration.hpp`), not
 * `std::chrono::duration<Rep, Period>` -- see that file's own
 * documentation for why (some `<chrono>` implementations compute
 * cross-`Period` conversions through floating-point intermediates,
 * unusable in kernel/freestanding builds; `<chrono>` may also simply not
 * exist on such a target at all). The absolute deadline passed to
 * `pthread_cond_timedwait` is itself built directly as a `struct
 * timespec`, via `duration_cast<struct timespec>` plus a small
 * `timespec + timespec` normalization helper -- never through `<chrono>`.
 */

#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "duration.hpp"
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
#include <time.h>
#include <unistd.h>

#include <cerrno>

namespace reloco {
namespace detail {

/** @brief Adds two `struct timespec` values, carrying a `tv_nsec`
 * overflow into `tv_sec` -- used to build an absolute
 * `pthread_cond_timedwait` deadline from "now" (via `clock_gettime`) plus
 * a relative `duration_cast<struct timespec>(timeout)`, without going
 * through `<chrono>`. */
[[nodiscard]] inline struct timespec add_timespec(struct timespec a, struct timespec b) noexcept {
  struct timespec r {};
  r.tv_sec = a.tv_sec + b.tv_sec;
  r.tv_nsec = a.tv_nsec + b.tv_nsec;
  if (r.tv_nsec >= 1'000'000'000L) {
    r.tv_nsec -= 1'000'000'000L;
    ++r.tv_sec;
  }
  return r;
}

} // namespace detail
} // namespace reloco

// Detects whether this platform's pthread implementation supports
// selecting CLOCK_MONOTONIC for condition-variable timed waits via
// pthread_condattr_setclock (POSIX's optional "Clock Selection" option --
// not universally implemented, notably absent on Darwin/macOS).
// RELOCO_MUTEX_NO_MONOTONIC_CLOCK (see reloco_config.hpp) forces
// CLOCK_REALTIME instead, even where detection would otherwise enable the
// monotonic clock.
#if defined(RELOCO_MUTEX_NO_MONOTONIC_CLOCK)
#define RELOCO_DETAIL_MUTEX_MONOTONIC_CLOCK 0
#elif defined(__APPLE__) || defined(__MACH__)
// Darwin's pthread does not implement pthread_condattr_setclock at all.
#define RELOCO_DETAIL_MUTEX_MONOTONIC_CLOCK 0
#elif defined(_POSIX_CLOCK_SELECTION) && _POSIX_CLOCK_SELECTION >= 0 && defined(CLOCK_MONOTONIC)
#define RELOCO_DETAIL_MUTEX_MONOTONIC_CLOCK 1
#else
#define RELOCO_DETAIL_MUTEX_MONOTONIC_CLOCK 0
#endif

#if RELOCO_DETAIL_MUTEX_MONOTONIC_CLOCK
#define RELOCO_DETAIL_MUTEX_COND_CLOCKID CLOCK_MONOTONIC
#else
#define RELOCO_DETAIL_MUTEX_COND_CLOCKID CLOCK_REALTIME
#endif

namespace reloco {

/**
 * @brief Non-recursive mutex backed directly by `pthread_mutex_t`.
 */
class RELOCO_EXPORT RELOCO_CAPABILITY("mutex") mutex {
public:
  using native_handle_type = pthread_mutex_t *;

  constexpr mutex() noexcept : handle_(PTHREAD_MUTEX_INITIALIZER) {}

  RELOCO_API ~mutex() noexcept;

  mutex(const mutex &) = delete;
  mutex &operator=(const mutex &) = delete;

  RELOCO_API void lock() & noexcept RELOCO_ACQUIRE();

  RELOCO_API void unlock() & noexcept RELOCO_RELEASE();

  [[nodiscard]] RELOCO_API bool try_lock() & noexcept RELOCO_TRY_ACQUIRE(true);

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  pthread_mutex_t handle_;
};

/**
 * @brief Recursive mutex backed by `pthread_mutex_t` configured with
 * `PTHREAD_MUTEX_RECURSIVE` via `pthread_mutexattr_t` -- portable to every
 * POSIX libc, unlike glibc's `PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP`.
 */
class RELOCO_EXPORT RELOCO_CAPABILITY("mutex") recursive_mutex {
public:
  using native_handle_type = pthread_mutex_t *;

  RELOCO_API recursive_mutex() noexcept;

  RELOCO_API ~recursive_mutex() noexcept;

  recursive_mutex(const recursive_mutex &) = delete;
  recursive_mutex &operator=(const recursive_mutex &) = delete;

  RELOCO_API void lock() & noexcept RELOCO_ACQUIRE();

  RELOCO_API void unlock() & noexcept RELOCO_RELEASE();

  [[nodiscard]] RELOCO_API bool try_lock() & noexcept RELOCO_TRY_ACQUIRE(true);

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  pthread_mutex_t handle_;
};

/**
 * @brief Reader/writer lock backed by `pthread_rwlock_t`.
 */
class RELOCO_EXPORT RELOCO_CAPABILITY("mutex") shared_mutex {
public:
  using native_handle_type = pthread_rwlock_t *;

  constexpr shared_mutex() noexcept = default;

  RELOCO_API ~shared_mutex() noexcept;

  shared_mutex(const shared_mutex &) = delete;
  shared_mutex &operator=(const shared_mutex &) = delete;

  RELOCO_API void lock() & noexcept RELOCO_ACQUIRE();

  RELOCO_API void unlock() & noexcept RELOCO_RELEASE();

  [[nodiscard]] RELOCO_API bool try_lock() & noexcept RELOCO_TRY_ACQUIRE(true);

  RELOCO_API void lock_shared() & noexcept RELOCO_ACQUIRE_SHARED();

  RELOCO_API void unlock_shared() & noexcept RELOCO_RELEASE_SHARED();

  [[nodiscard]] RELOCO_API bool try_lock_shared() & noexcept RELOCO_TRY_ACQUIRE_SHARED(true);

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
class RELOCO_EXPORT condition_variable {
public:
  using native_handle_type = pthread_cond_t *;

  RELOCO_API condition_variable() noexcept;

  RELOCO_API ~condition_variable() noexcept;

  condition_variable(const condition_variable &) = delete;
  condition_variable &operator=(const condition_variable &) = delete;

  [[nodiscard]] RELOCO_API result<void> wait(std::unique_lock<mutex> &locker) & noexcept;

  template <typename Predicate> result<void> wait(std::unique_lock<mutex> &locker, Predicate pred) & {
    if (!locker.owns_lock())
      return unexpected(error::not_locked);
    while (!pred())
      pthread_cond_wait(&cond_, locker.mutex()->native_handle());
    return {};
  }

  /**
   * @brief Bounded wait: blocks until @p pred() is `true` or @p timeout
   * has elapsed, whichever comes first, matching
   * `std::condition_variable::wait_for`'s predicate overload. Returns
   * `pred()`'s final value -- `true` unless @p timeout elapsed with
   * @p pred() still `false`. See the file-level documentation above for
   * which clock the underlying deadline is measured against.
   */
  template <typename Predicate>
  [[nodiscard]] result<bool> wait_for(std::unique_lock<mutex> &locker, duration timeout, Predicate pred) & {
    if (!locker.owns_lock())
      return unexpected(error::not_locked);

    struct timespec now {};
    clock_gettime(RELOCO_DETAIL_MUTEX_COND_CLOCKID, &now);
    struct timespec deadline = detail::add_timespec(now, duration_cast<struct timespec>(timeout));

    while (!pred()) {
      int rc = pthread_cond_timedwait(&cond_, locker.mutex()->native_handle(), &deadline);
      if (rc == ETIMEDOUT)
        break;
      RELOCO_ASSERT(rc == 0, "pthread_cond_timedwait failed");
    }
    return pred();
  }

  RELOCO_API void notify_one() & noexcept;

  RELOCO_API void notify_all() & noexcept;

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &cond_; }

private:
  pthread_cond_t cond_{};
};

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "mutex_pthread.ipp"
#endif

} // namespace reloco

#elif defined(RELOCO_MUTEX_BACKEND_STD)

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <system_error>

namespace reloco {

// Calls `handle_.<locking_call>()`. Any thrown `std::system_error` (e.g.
// libstdc++ detecting a self-relock deadlock on a non-recursive
// `std::mutex`) indicates a programming error -- undefined behavior per the
// standard in the first place -- so it is reported via `RELOCO_ASSERT`
// rather than surfaced as a `result<void>`. When `RELOCO_HAS_EXCEPTIONS` is
// `0` (e.g. built with `-fno-exceptions`), the call is made unguarded:
// `try`/`catch` is not valid syntax in that mode, and the standard library's
// own throwing paths become terminating calls anyway, so there is nothing
// left for reloco to translate.
#if RELOCO_HAS_EXCEPTIONS
#define RELOCO_DETAIL_MUTEX_LOCKING_CALL(locking_call)                                                                 \
  try {                                                                                                                 \
    handle_.locking_call();                                                                                            \
  } catch (const std::system_error &) {                                                                                \
    RELOCO_ASSERT(false, #locking_call "() failed");                                                                   \
  }
#else
#define RELOCO_DETAIL_MUTEX_LOCKING_CALL(locking_call) handle_.locking_call();
#endif

/**
 * @brief Non-recursive mutex wrapping `std::mutex`.
 */
class RELOCO_EXPORT RELOCO_CAPABILITY("mutex") mutex {
public:
  using native_handle_type = std::mutex *;

  mutex() noexcept = default;

  mutex(const mutex &) = delete;
  mutex &operator=(const mutex &) = delete;

  RELOCO_API void lock() & noexcept RELOCO_ACQUIRE();

  void unlock() & noexcept RELOCO_RELEASE() { handle_.unlock(); }

  [[nodiscard]] bool try_lock() & noexcept RELOCO_TRY_ACQUIRE(true) { return handle_.try_lock(); }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  std::mutex handle_;
};

/**
 * @brief Recursive mutex wrapping `std::recursive_mutex`.
 */
class RELOCO_EXPORT RELOCO_CAPABILITY("mutex") recursive_mutex {
public:
  using native_handle_type = std::recursive_mutex *;

  recursive_mutex() noexcept = default;

  recursive_mutex(const recursive_mutex &) = delete;
  recursive_mutex &operator=(const recursive_mutex &) = delete;

  RELOCO_API void lock() & noexcept RELOCO_ACQUIRE();

  void unlock() & noexcept RELOCO_RELEASE() { handle_.unlock(); }

  [[nodiscard]] bool try_lock() & noexcept RELOCO_TRY_ACQUIRE(true) { return handle_.try_lock(); }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  std::recursive_mutex handle_;
};

/**
 * @brief Reader/writer lock wrapping `std::shared_mutex`.
 */
class RELOCO_EXPORT RELOCO_CAPABILITY("mutex") shared_mutex {
public:
  using native_handle_type = std::shared_mutex *;

  shared_mutex() noexcept = default;

  shared_mutex(const shared_mutex &) = delete;
  shared_mutex &operator=(const shared_mutex &) = delete;

  RELOCO_API void lock() & noexcept RELOCO_ACQUIRE();

  void unlock() & noexcept RELOCO_RELEASE() { handle_.unlock(); }

  [[nodiscard]] bool try_lock() & noexcept RELOCO_TRY_ACQUIRE(true) { return handle_.try_lock(); }

  RELOCO_API void lock_shared() & noexcept RELOCO_ACQUIRE_SHARED();

  void unlock_shared() & noexcept RELOCO_RELEASE_SHARED() { handle_.unlock_shared(); }

  [[nodiscard]] bool try_lock_shared() & noexcept RELOCO_TRY_ACQUIRE_SHARED(true) { return handle_.try_lock_shared(); }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  std::shared_mutex handle_;
};

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
class RELOCO_EXPORT condition_variable {
public:
  using native_handle_type = std::condition_variable *;

  condition_variable() noexcept = default;

  condition_variable(const condition_variable &) = delete;
  condition_variable &operator=(const condition_variable &) = delete;

  [[nodiscard]] RELOCO_API result<void> wait(std::unique_lock<mutex> &locker) & noexcept;

  template <typename Predicate> result<void> wait(std::unique_lock<mutex> &locker, Predicate pred) & {
    if (!locker.owns_lock())
      return unexpected(error::not_locked);
    std::unique_lock<std::mutex> native_lock(*locker.mutex()->native_handle(), std::adopt_lock);
    cv_.wait(native_lock, std::move(pred));
    native_lock.release();
    return {};
  }

  /**
   * @brief Bounded wait: blocks until @p pred() is `true` or @p timeout
   * has elapsed, whichever comes first, matching
   * `std::condition_variable::wait_for`'s predicate overload. Returns
   * `pred()`'s final value -- `true` unless @p timeout elapsed with
   * @p pred() still `false`.
   */
  template <typename Predicate>
  [[nodiscard]] result<bool> wait_for(std::unique_lock<mutex> &locker, duration timeout, Predicate pred) & {
    if (!locker.owns_lock())
      return unexpected(error::not_locked);
    std::unique_lock<std::mutex> native_lock(*locker.mutex()->native_handle(), std::adopt_lock);
    // Plain integer arithmetic (never floating point): the STD backend
    // already fully depends on <chrono>/<thread>, so building an
    // std::chrono::nanoseconds here (itself always integer-`Rep`) is safe
    // -- unlike the PTHREAD backend, which builds a `struct timespec`
    // deadline directly instead (see the file-level documentation above).
    auto rel_time = std::chrono::seconds(timeout.as_secs()) + std::chrono::nanoseconds(timeout.subsec_nanos());
    bool ok = cv_.wait_for(native_lock, rel_time, pred);
    native_lock.release();
    return ok;
  }

  RELOCO_API void notify_one() & noexcept;

  RELOCO_API void notify_all() & noexcept;

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &cv_; }

private:
  std::condition_variable cv_;
};

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "mutex_std.ipp"
#endif

#undef RELOCO_DETAIL_MUTEX_LOCKING_CALL

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
class RELOCO_EXPORT RELOCO_CAPABILITY("mutex") error_checking_mutex {
public:
  using native_handle_type = mutex::native_handle_type;

  error_checking_mutex() noexcept = default;

  error_checking_mutex(const error_checking_mutex &) = delete;
  error_checking_mutex &operator=(const error_checking_mutex &) = delete;

  [[nodiscard]] RELOCO_API result<void> lock() & noexcept RELOCO_ACQUIRE();

  [[nodiscard]] RELOCO_API result<void> unlock() & noexcept RELOCO_RELEASE();

  [[nodiscard]] RELOCO_API bool try_lock() & noexcept RELOCO_TRY_ACQUIRE(true);

  [[nodiscard]] native_handle_type native_handle() & noexcept { return mutex_.native_handle(); }

private:
  mutex mutex_;
  std::atomic<std::thread::id> owner_{};
};

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "mutex_common.ipp"
#endif

} // namespace reloco

#endif // !RELOCO_MUTEX_BACKEND_CUSTOM
