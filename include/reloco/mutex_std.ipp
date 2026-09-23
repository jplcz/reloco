// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file mutex_std.ipp @brief Out-of-line bodies for the
 * RELOCO_MUTEX_BACKEND_STD classes (mutex, recursive_mutex, shared_mutex,
 * condition_variable -- see mutex.hpp). Included from mutex.hpp itself,
 * guarded on RELOCO_SHARED_PROVIDE_DEFINITIONS (see reloco/detail/
 * compat.hpp), before RELOCO_DETAIL_MUTEX_LOCKING_CALL is #undef-ed. Never
 * included directly. */

RELOCO_API void mutex::lock() & noexcept { RELOCO_DETAIL_MUTEX_LOCKING_CALL(lock) }

RELOCO_API void recursive_mutex::lock() & noexcept { RELOCO_DETAIL_MUTEX_LOCKING_CALL(lock) }

RELOCO_API void shared_mutex::lock() & noexcept { RELOCO_DETAIL_MUTEX_LOCKING_CALL(lock) }

RELOCO_API void shared_mutex::lock_shared() & noexcept { RELOCO_DETAIL_MUTEX_LOCKING_CALL(lock_shared) }

RELOCO_API result<void> condition_variable::wait(std::unique_lock<mutex> &locker) & noexcept {
  if (!locker.owns_lock())
    return unexpected(error::not_locked);
  std::unique_lock<std::mutex> native_lock(*locker.mutex()->native_handle(), std::adopt_lock);
  cv_.wait(native_lock);
  native_lock.release();
  return {};
}

RELOCO_API void condition_variable::notify_one() & noexcept { cv_.notify_one(); }

RELOCO_API void condition_variable::notify_all() & noexcept { cv_.notify_all(); }
