// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file rw_lock.hpp
 * @brief Rust `std::sync::RwLock<T>` equivalent: a reader-writer lock that
 * owns the value it protects, instead of C++'s usual convention of
 * pairing a bare `std::shared_mutex` with a separately-declared variable
 * the caller has to remember to lock before touching.
 *
 * `mutex.hpp`'s `shared_mutex` protects nothing by itself -- there is
 * nothing stopping code from reading or writing the guarded variable
 * without holding the lock at all. `rw_lock<T>` closes that gap exactly
 * the way `guarded_mutex.hpp`'s `guarded_mutex<T>` closes it for a plain
 * `mutex`: the protected `T` lives inside the `rw_lock<T>` itself, and the
 * only way to reach it is through one of the two RAII guards returned by
 * `read()`/`try_read()` (shared) or `write()`/`try_write()` (exclusive),
 * both of which release their half of the lock automatically on
 * destruction. This mirrors Rust's `RwLock<T>`/`RwLockReadGuard<'a, T>`/
 * `RwLockWriteGuard<'a, T>` exactly.
 *
 * `T` must satisfy both `is_send_v<T>` and `is_sync_v<T>` (see
 * `send_sync.hpp`): unlike `guarded_mutex<T>` (whose `Mutex<T>` only ever
 * grants one thread at a time access, so `T` need only be `Send`),
 * `rw_lock<T>` may hand out any number of concurrent shared `const T &`
 * references via `read()`, so `T` itself must be safe to access
 * concurrently from multiple threads -- matching Rust's own `unsafe impl<T:
 * ?Sized + Send + Sync> Sync for RwLock<T>` bound.
 */

#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "mutex.hpp"
#include "send_sync.hpp"

#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief A reader-writer lock that owns the value it protects, matching
 * Rust's `std::sync::RwLock<T>`. `SharedMutexT` must provide
 * `lock()`/`unlock()`/`try_lock()`/`lock_shared()`/`unlock_shared()`/
 * `try_lock_shared()` with the same signatures as `reloco::shared_mutex`
 * (the default).
 */
template <typename T, typename SharedMutexT = shared_mutex> class RELOCO_CAPABILITY("mutex") rw_lock {
  static_assert(is_send_v<T>,
                "rw_lock<T>: T must be Send (see send_sync.hpp) -- write() hands exclusive access of T to whichever "
                "thread acquires the exclusive lock");
  static_assert(is_sync_v<T>,
                "rw_lock<T>: T must be Sync (see send_sync.hpp) -- read() may hand out any number of concurrent "
                "shared `const T &` references across threads at once, unlike guarded_mutex<T>'s exclusive-only "
                "Mutex<T> equivalent, so T itself must be safe to access concurrently");

public:
  /**
   * @brief A live shared (read) lock on the protected value. Move-only;
   * releases its half of the lock automatically on destruction, matching
   * Rust's `RwLockReadGuard<'a, T>`.
   */
  class RELOCO_SCOPED_CAPABILITY read_guard {
  public:
    read_guard(read_guard &&other) noexcept : cell_(other.cell_) { other.cell_ = nullptr; }
    read_guard(const read_guard &) = delete;
    read_guard &operator=(read_guard &&) = delete;
    read_guard &operator=(const read_guard &) = delete;

    ~read_guard() noexcept RELOCO_RELEASE_SHARED() {
      if (cell_ != nullptr)
        cell_->mutex_.unlock_shared();
    }

    [[nodiscard]] const T &operator*() const & noexcept RELOCO_LIFETIMEBOUND {
      RELOCO_ASSERT(cell_ != nullptr, "read_guard used after being moved from");
      return cell_->value_;
    }

    [[nodiscard]] const T *operator->() const & noexcept RELOCO_LIFETIMEBOUND {
      RELOCO_ASSERT(cell_ != nullptr, "read_guard used after being moved from");
      return &cell_->value_;
    }

  private:
    friend class rw_lock;
    explicit read_guard(rw_lock *cell) noexcept : cell_(cell) {}
    rw_lock *cell_;
  };

  /**
   * @brief A live exclusive (write) lock on the protected value.
   * Move-only; releases the lock automatically on destruction, matching
   * Rust's `RwLockWriteGuard<'a, T>`.
   */
  class RELOCO_SCOPED_CAPABILITY write_guard {
  public:
    write_guard(write_guard &&other) noexcept : cell_(other.cell_) { other.cell_ = nullptr; }
    write_guard(const write_guard &) = delete;
    write_guard &operator=(write_guard &&) = delete;
    write_guard &operator=(const write_guard &) = delete;

    ~write_guard() noexcept RELOCO_RELEASE() {
      if (cell_ != nullptr)
        cell_->mutex_.unlock();
    }

    [[nodiscard]] T &operator*() const & noexcept RELOCO_LIFETIMEBOUND {
      RELOCO_ASSERT(cell_ != nullptr, "write_guard used after being moved from");
      return cell_->value_;
    }

    [[nodiscard]] T *operator->() const & noexcept RELOCO_LIFETIMEBOUND {
      RELOCO_ASSERT(cell_ != nullptr, "write_guard used after being moved from");
      return &cell_->value_;
    }

  private:
    friend class rw_lock;
    explicit write_guard(rw_lock *cell) noexcept : cell_(cell) {}
    rw_lock *cell_;
  };

  rw_lock() noexcept(std::is_nothrow_default_constructible_v<T>) : value_() {}
  explicit rw_lock(T value) noexcept(std::is_nothrow_move_constructible_v<T>) : value_(std::move(value)) {}

  rw_lock(const rw_lock &) = delete;
  rw_lock &operator=(const rw_lock &) = delete;

  /**
   * @brief Blocks until a shared (read) lock is acquired, then returns a
   * `read_guard` granting shared `const` access to the protected value.
   * Any number of `read_guard`s may be held concurrently (by any number
   * of threads), so long as no `write_guard` is held at the same time.
   */
  [[nodiscard]] read_guard read() & noexcept RELOCO_ACQUIRE_SHARED() {
    mutex_.lock_shared();
    return read_guard(this);
  }

  /**
   * @brief Attempts to acquire a shared (read) lock without blocking.
   * Fails with `error::busy` if an exclusive lock is currently held
   * elsewhere, matching Rust's `RwLock::try_read() ->
   * Result<RwLockReadGuard<T>, TryLockError<...>>`.
   */
  [[nodiscard]] result<read_guard> try_read() & noexcept RELOCO_TRY_ACQUIRE_SHARED(true) {
    if (!mutex_.try_lock_shared())
      return unexpected(error::busy);
    return read_guard(this);
  }

  /**
   * @brief Blocks until the exclusive (write) lock is acquired, then
   * returns a `write_guard` granting exclusive access to the protected
   * value.
   */
  [[nodiscard]] write_guard write() & noexcept RELOCO_ACQUIRE() {
    mutex_.lock();
    return write_guard(this);
  }

  /**
   * @brief Attempts to acquire the exclusive (write) lock without
   * blocking. Fails with `error::busy` if it is already held (shared or
   * exclusive) elsewhere, matching Rust's `RwLock::try_write() ->
   * Result<RwLockWriteGuard<T>, TryLockError<...>>`.
   */
  [[nodiscard]] result<write_guard> try_write() & noexcept RELOCO_TRY_ACQUIRE(true) {
    if (!mutex_.try_lock())
      return unexpected(error::busy);
    return write_guard(this);
  }

  /**
   * @brief Direct, unguarded mutable access -- sound exactly when the
   * caller already holds an exclusive `rw_lock&` (matching Rust's
   * `RwLock::get_mut()`, which borrows `&mut self` at compile time
   * instead of taking the lock at runtime).
   */
  [[nodiscard]] T &get_mut() & noexcept RELOCO_LIFETIMEBOUND { return value_; }

private:
  T value_;
  SharedMutexT mutex_;
};

} // namespace reloco
