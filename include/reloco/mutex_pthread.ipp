// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file mutex_pthread.ipp @brief Out-of-line bodies for the
 * RELOCO_MUTEX_BACKEND_PTHREAD classes (mutex, recursive_mutex,
 * shared_mutex, condition_variable -- see mutex.hpp). Included from
 * mutex.hpp itself, guarded on RELOCO_SHARED_PROVIDE_DEFINITIONS (see
 * reloco/detail/compat.hpp). Never included directly. */

RELOCO_API mutex::~mutex() noexcept { pthread_mutex_destroy(&handle_); }

RELOCO_API void mutex::lock() & noexcept {
  int r = pthread_mutex_lock(&handle_);
  RELOCO_ASSERT(r == 0, "pthread_mutex_lock failed");
}

RELOCO_API void mutex::unlock() & noexcept { RELOCO_ASSERT(pthread_mutex_unlock(&handle_) == 0, "pthread_mutex_unlock failed"); }

RELOCO_API bool mutex::try_lock() & noexcept { return pthread_mutex_trylock(&handle_) == 0; }

RELOCO_API recursive_mutex::recursive_mutex() noexcept {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&handle_, &attr);
  pthread_mutexattr_destroy(&attr);
}

RELOCO_API recursive_mutex::~recursive_mutex() noexcept { pthread_mutex_destroy(&handle_); }

RELOCO_API void recursive_mutex::lock() & noexcept {
  int r = pthread_mutex_lock(&handle_);
  RELOCO_ASSERT(r == 0, "pthread_mutex_lock failed");
}

RELOCO_API void recursive_mutex::unlock() & noexcept {
  int r = pthread_mutex_unlock(&handle_);
  RELOCO_ASSERT(r == 0, "pthread_mutex_unlock failed");
}

RELOCO_API bool recursive_mutex::try_lock() & noexcept { return pthread_mutex_trylock(&handle_) == 0; }

RELOCO_API shared_mutex::~shared_mutex() noexcept { pthread_rwlock_destroy(&handle_); }

RELOCO_API void shared_mutex::lock() & noexcept {
  int r = pthread_rwlock_wrlock(&handle_);
  RELOCO_ASSERT(r == 0, "pthread_rwlock_wrlock failed");
}

RELOCO_API void shared_mutex::unlock() & noexcept {
  int r = pthread_rwlock_unlock(&handle_);
  RELOCO_ASSERT(r == 0, "pthread_rwlock_unlock failed");
}

RELOCO_API bool shared_mutex::try_lock() & noexcept { return pthread_rwlock_trywrlock(&handle_) == 0; }

RELOCO_API void shared_mutex::lock_shared() & noexcept {
  int r = pthread_rwlock_rdlock(&handle_);
  RELOCO_ASSERT(r == 0, "pthread_rwlock_rdlock failed");
}

RELOCO_API void shared_mutex::unlock_shared() & noexcept {
  int r = pthread_rwlock_unlock(&handle_);
  RELOCO_ASSERT(r == 0, "pthread_rwlock_unlock failed");
}

RELOCO_API bool shared_mutex::try_lock_shared() & noexcept { return pthread_rwlock_tryrdlock(&handle_) == 0; }

RELOCO_API condition_variable::condition_variable() noexcept {
#if RELOCO_DETAIL_MUTEX_MONOTONIC_CLOCK
  pthread_condattr_t attr;
  pthread_condattr_init(&attr);
  pthread_condattr_setclock(&attr, RELOCO_DETAIL_MUTEX_COND_CLOCKID);
  pthread_cond_init(&cond_, &attr);
  pthread_condattr_destroy(&attr);
#else
  pthread_cond_init(&cond_, nullptr);
#endif
}

RELOCO_API condition_variable::~condition_variable() noexcept { pthread_cond_destroy(&cond_); }

RELOCO_API result<void> condition_variable::wait(std::unique_lock<mutex> &locker) & noexcept {
  if (!locker.owns_lock())
    return unexpected(error::not_locked);
  pthread_cond_wait(&cond_, locker.mutex()->native_handle());
  return {};
}

RELOCO_API void condition_variable::notify_one() & noexcept { pthread_cond_signal(&cond_); }

RELOCO_API void condition_variable::notify_all() & noexcept { pthread_cond_broadcast(&cond_); }
