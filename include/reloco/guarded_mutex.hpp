// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file guarded_mutex.hpp
 * @brief Rust `std::sync::Mutex<T>` equivalent: a mutex that owns the
 * value it protects, instead of C++'s usual convention of pairing a bare
 * `std::mutex` with a separately-declared variable the caller has to
 * remember to lock before touching.
 *
 * `mutex.hpp`'s `mutex`/`recursive_mutex`/`shared_mutex` protect nothing
 * by themselves -- there is nothing stopping code from reading or writing
 * the guarded variable without holding the lock at all. `guarded_mutex<T>`
 * closes that gap: the protected `T` lives inside the `guarded_mutex<T>`
 * itself, and the only way to reach it is through the RAII `guard`
 * returned by `lock()`/`try_lock()`, which releases the lock automatically
 * on destruction. This mirrors Rust's `Mutex<T>`/`MutexGuard<'a, T>`
 * exactly, and is the thread-safe counterpart of `cell.hpp`'s single-
 * threaded `ref_cell<T>`/`mut_guard`.
 */

#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "mutex.hpp"

#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief A mutex that owns the value it protects, matching Rust's
 * `std::sync::Mutex<T>`. `MutexT` must provide `lock()`/`unlock()`/
 * `try_lock()` with the same signatures as `reloco::mutex` (the default);
 * `recursive_mutex` and `shared_mutex` both satisfy this too, though
 * `shared_mutex`'s `lock_shared()`/`unlock_shared()` are not exposed here
 * -- only exclusive access is modeled, matching Rust's `Mutex<T>` (see
 * `mutex.hpp` directly for reader/writer locking without an owned value).
 */
template <typename T, typename MutexT = mutex> class RELOCO_CAPABILITY("mutex") guarded_mutex {
public:
  /**
   * @brief A live exclusive lock on the protected value. Move-only;
   * releases the lock automatically on destruction, matching Rust's
   * `MutexGuard<'a, T>`.
   */
  class RELOCO_SCOPED_CAPABILITY guard {
  public:
    guard(guard &&other) noexcept : cell_(other.cell_) { other.cell_ = nullptr; }
    guard(const guard &) = delete;
    guard &operator=(guard &&) = delete;
    guard &operator=(const guard &) = delete;

    ~guard() noexcept RELOCO_RELEASE() {
      if (cell_ != nullptr)
        cell_->mutex_.unlock();
    }

    [[nodiscard]] T &operator*() const & noexcept RELOCO_LIFETIMEBOUND {
      RELOCO_ASSERT(cell_ != nullptr, "guard used after being moved from");
      return cell_->value_;
    }

    [[nodiscard]] T *operator->() const & noexcept RELOCO_LIFETIMEBOUND {
      RELOCO_ASSERT(cell_ != nullptr, "guard used after being moved from");
      return &cell_->value_;
    }

  private:
    friend class guarded_mutex;
    explicit guard(guarded_mutex *cell) noexcept : cell_(cell) {}
    guarded_mutex *cell_;
  };

  guarded_mutex() noexcept(std::is_nothrow_default_constructible_v<T>) : value_() {}
  explicit guarded_mutex(T value) noexcept(std::is_nothrow_move_constructible_v<T>) : value_(std::move(value)) {}

  guarded_mutex(const guarded_mutex &) = delete;
  guarded_mutex &operator=(const guarded_mutex &) = delete;

  /**
   * @brief Blocks until the lock is acquired, then returns a `guard`
   * granting exclusive access to the protected value.
   */
  [[nodiscard]] guard lock() & noexcept RELOCO_ACQUIRE() {
    mutex_.lock();
    return guard(this);
  }

  /**
   * @brief Attempts to acquire the lock without blocking. Fails with
   * `error::busy` if it is already held elsewhere, matching Rust's
   * `Mutex::try_lock() -> Result<MutexGuard<T>, TryLockError<...>>`.
   */
  [[nodiscard]] result<guard> try_lock() & noexcept RELOCO_TRY_ACQUIRE(true) {
    if (!mutex_.try_lock())
      return unexpected(error::busy);
    return guard(this);
  }

  /**
   * @brief Direct, unguarded mutable access -- sound exactly when the
   * caller already holds an exclusive `guarded_mutex&` (matching Rust's
   * `Mutex::get_mut()`, which borrows `&mut self` at compile time instead
   * of taking the lock at runtime).
   */
  [[nodiscard]] T &get_mut() & noexcept RELOCO_LIFETIMEBOUND { return value_; }

private:
  T value_;
  MutexT mutex_;
};

} // namespace reloco
