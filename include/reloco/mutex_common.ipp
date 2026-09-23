// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file mutex_common.ipp @brief Out-of-line bodies for
 * error_checking_mutex (see mutex.hpp), which is defined once regardless
 * of the active RELOCO_MUTEX_BACKEND_* backend. Included from mutex.hpp
 * itself, guarded on RELOCO_SHARED_PROVIDE_DEFINITIONS (see
 * reloco/detail/compat.hpp). Never included directly. */

RELOCO_API result<void> error_checking_mutex::lock() & noexcept {
  auto self = std::this_thread::get_id();
  if (owner_.load(std::memory_order_relaxed) == self)
    return unexpected(error::deadlock);
  mutex_.lock();
  owner_.store(self, std::memory_order_release);
  return {};
}

RELOCO_API result<void> error_checking_mutex::unlock() & noexcept {
  if (owner_.load(std::memory_order_acquire) != std::this_thread::get_id())
    return unexpected(error::invalid_owner);
  owner_.store(std::thread::id{}, std::memory_order_relaxed);
  mutex_.unlock();
  return {};
}

RELOCO_API bool error_checking_mutex::try_lock() & noexcept {
  auto self = std::this_thread::get_id();
  if (owner_.load(std::memory_order_relaxed) == self)
    return false;
  if (!mutex_.try_lock())
    return false;
  owner_.store(self, std::memory_order_release);
  return true;
}
