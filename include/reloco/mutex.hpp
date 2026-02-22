#pragma once
#include <chrono>
#include <mutex>
#include <pthread.h>
#include <reloco/config.hpp>
#include <reloco/core.hpp>
#include <reloco/fallible_constructed.hpp>
#include <time.h>

namespace reloco {

class mutex_base {
protected:
  struct defer_init_t {};

  static error from_posix_errno(int errno_value) {
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

  template <typename Clock, typename Duration>
  static timespec
  to_timespec(const std::chrono::time_point<Clock, Duration> &tp) noexcept {
    auto duration = tp.time_since_epoch();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(duration);
    auto nsecs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration - secs);
    return {static_cast<time_t>(secs.count()),
            static_cast<long>(nsecs.count())};
  }

  static result<void> do_lock(pthread_mutex_t *m) noexcept {
    int result = pthread_mutex_lock(m);
    if (result == 0)
      return {};
    return unexpected(from_posix_errno(result));
  }

  static result<void> do_timed_lock(pthread_mutex_t *m,
                                    timespec *abs_timeout) noexcept {
    int result = pthread_mutex_timedlock(m, abs_timeout);
    if (result == 0)
      return {};
    return unexpected(from_posix_errno(result));
  }

  static result<void> do_unlock(pthread_mutex_t *m) noexcept {
    int result = pthread_mutex_unlock(m);
    if (result == 0)
      return {};
    return unexpected(from_posix_errno(result));
  }

  static result<void> do_lock(pthread_rwlock_t *m) noexcept {
    int result = pthread_rwlock_wrlock(m);
    if (result == 0)
      return {};
    return unexpected(from_posix_errno(result));
  }

  static result<void> do_rlock(pthread_rwlock_t *m) noexcept {
    int result = pthread_rwlock_rdlock(m);
    if (result == 0)
      return {};
    return unexpected(from_posix_errno(result));
  }

  static result<void> do_unlock(pthread_rwlock_t *m) noexcept {
    int result = pthread_rwlock_unlock(m);
    if (result == 0)
      return {};
    return unexpected(from_posix_errno(result));
  }
};

class mutex : protected mutex_base {
public:
  using native_handle_type = pthread_mutex_t *;

  constexpr mutex() noexcept : m_mutex(PTHREAD_MUTEX_INITIALIZER) {}

  ~mutex() noexcept { pthread_mutex_destroy(&m_mutex); }

  mutex(const mutex &) = delete;
  mutex &operator=(const mutex &) = delete;

  result<void> lock() noexcept { return this->do_lock(&m_mutex); }

  result<void> unlock() noexcept { return this->do_unlock(&m_mutex); }

  [[nodiscard]] bool try_lock() noexcept {
    return pthread_mutex_trylock(&m_mutex) == 0;
  }

  native_handle_type native_handle() noexcept { return &m_mutex; }

protected:
  mutex(defer_init_t) noexcept {}

  pthread_mutex_t m_mutex;
};

class recursive_mutex : mutex_base {
public:
  using native_handle_type = pthread_mutex_t *;

  constexpr recursive_mutex() noexcept
      : m_mutex(PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP) {}

  ~recursive_mutex() noexcept { pthread_mutex_destroy(&m_mutex); }

  recursive_mutex(const recursive_mutex &) = delete;
  recursive_mutex &operator=(const recursive_mutex &) = delete;

  result<void> lock() noexcept { return this->do_lock(&m_mutex); }

  result<void> unlock() noexcept { return this->do_unlock(&m_mutex); }

  [[nodiscard]] bool try_lock() noexcept {
    return pthread_mutex_trylock(&m_mutex) == 0;
  }

  native_handle_type native_handle() noexcept { return &m_mutex; }

protected:
  recursive_mutex(defer_init_t) {}

private:
  pthread_mutex_t m_mutex;
};

class error_checking_mutex : mutex_base {
public:
  using native_handle_type = pthread_mutex_t *;

  constexpr error_checking_mutex() noexcept
      : m_mutex(PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP) {}

  ~error_checking_mutex() noexcept { pthread_mutex_destroy(&m_mutex); }

  error_checking_mutex(const error_checking_mutex &) = delete;
  error_checking_mutex &operator=(const error_checking_mutex &) = delete;

  result<void> lock() noexcept { return this->do_lock(&m_mutex); }

  result<void> unlock() noexcept { return this->do_unlock(&m_mutex); }

  [[nodiscard]] bool try_lock() noexcept {
    return pthread_mutex_trylock(&m_mutex) == 0;
  }

  native_handle_type native_handle() noexcept { return &m_mutex; }

protected:
  error_checking_mutex(defer_init_t) {}

  pthread_mutex_t m_mutex;
};

class shared_mutex : mutex_base {
public:
  using native_handle_type = pthread_rwlock_t *;

  constexpr shared_mutex() noexcept = default;

  ~shared_mutex() noexcept { pthread_rwlock_destroy(&m_lock); }

  native_handle_type native_handle() noexcept { return &m_lock; }

  result<void> lock() noexcept { return this->do_lock(&m_lock); }

  result<void> unlock() noexcept { return this->do_unlock(&m_lock); }

  [[nodiscard]] bool try_lock() noexcept {
    return pthread_rwlock_trywrlock(&m_lock) == 0;
  }

  result<void> lock_shared() noexcept { return this->do_rlock(&m_lock); }

  result<void> unlock_shared() noexcept { return this->do_unlock(&m_lock); }

  [[nodiscard]] bool try_lock_shared() noexcept {
    return pthread_rwlock_tryrdlock(&m_lock) == 0;
  }

protected:
  shared_mutex(defer_init_t) {}

  pthread_rwlock_t m_lock = PTHREAD_RWLOCK_INITIALIZER;
};

class condition_variable : mutex_base {
public:
  using native_handle_type = pthread_rwlock_t *;

  constexpr condition_variable() noexcept = default;

  ~condition_variable() noexcept { pthread_cond_destroy(&m_cond); }

  result<void> wait(std::unique_lock<mutex> &locker) noexcept {
    if (!locker.owns_lock())
      return unexpected(error::not_locked);
    pthread_cond_wait(&m_cond, locker.mutex()->native_handle());
    return {};
  }

  template <class Predicate>
  result<void> wait(std::unique_lock<mutex> &locker, Predicate pred) {
    if (!locker.owns_lock())
      return unexpected(error::not_locked);
    while (!pred()) {
      pthread_cond_wait(&m_cond, locker.mutex()->native_handle());
    }
    return {};
  }

  void notify_one() noexcept { pthread_cond_signal(&m_cond); }

  void notify_all() noexcept { pthread_cond_broadcast(&m_cond); }

private:
  pthread_cond_t m_cond = PTHREAD_COND_INITIALIZER;
};

} // namespace reloco
