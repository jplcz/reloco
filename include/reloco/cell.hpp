// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file cell.hpp
 * @brief Rust `Cell<T>`/`RefCell<T>` equivalents: interior mutability
 * through a shared (`const`) reference.
 *
 * Every other reloco container enforces exclusive/shared access through
 * C++'s ordinary `const`/non-`const` reference rules. `cell<T>` and
 * `ref_cell<T>` deliberately opt out of that for a single, contained
 * value, exactly like Rust's `std::cell::Cell`/`std::cell::RefCell`:
 *
 * - `cell<T>` allows `set()`/`replace()` through a `const cell<T>&` for
 *   any `T` (they only move the value in and out); `get()` additionally
 *   requires a `noexcept` copy constructor (Rust's `T: Copy` bound), and
 *   `take()` requires `T` to be default constructible (Rust's `T:
 *   Default` bound). No runtime bookkeeping is needed at all -- every
 *   access moves or copies the whole value in or out, so there is never
 *   a live reference into the cell for a concurrent mutation to
 *   invalidate.
 * - `ref_cell<T>` allows borrowing a reference to a non-`Copy` `T`
 *   in-place, tracking the borrow state at runtime (an exclusive borrow,
 *   any number of shared borrows, or none) instead of at compile time.
 *   `try_borrow()`/`try_borrow_mut()` return `result<ref_guard>`/
 *   `result<mut_guard>` (reloco's fallible tier: `error::busy` on
 *   conflict, never trapping), while `borrow()`/`borrow_mut()` are the
 *   checked tier that `RELOCO_ASSERT`s instead -- matching the
 *   checked/fallible/unsafe convention used throughout reloco (see
 *   `docs/hardened-containers.md`), and Rust's own `RefCell::borrow()`/
 *   `borrow_mut()`, which panic on conflict.
 *
 * Both types are single-threaded only, matching Rust's `Cell`/`RefCell`
 * (`!Sync`); see `mutex.hpp` for a thread-safe alternative.
 */

#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief Interior-mutability wrapper allowing mutation through a shared
 * (`const`) reference, matching Rust's `std::cell::Cell<T>`.
 *
 * `set()`/`replace()` move the value in and out, so they work for any
 * `T` (Rust's `Cell::set`/`Cell::replace` place no bound on `T` either).
 * Only `get()` requires a `noexcept` copy constructor (Rust's `T: Copy`
 * bound) since it is the sole accessor that hands back a copy rather
 * than moving; likewise `take()` only requires `T` to be default
 * constructible (Rust's `T: Default` bound on `Cell::take`), not
 * copyable. Because every access moves or copies the whole value in or
 * out, there is never a live reference into the cell for a concurrent
 * mutation to invalidate, so no runtime borrow tracking is needed.
 */
template <typename T> class cell {
public:
  constexpr cell() noexcept(std::is_nothrow_default_constructible_v<T>) : value_() {}
  constexpr explicit cell(T value) noexcept(std::is_nothrow_move_constructible_v<T>) : value_(std::move(value)) {}

  /**
   * @brief Returns a copy of the current value. Requires a `noexcept`
   * copy constructor (Rust's `Cell<T>: Copy` bound on `get()`); every
   * `cell<T>` method is `noexcept`, so a throwing copy could never be
   * reported and would call `std::terminate` instead.
   */
  [[nodiscard]] constexpr T get() const noexcept {
    static_assert(std::is_nothrow_copy_constructible_v<T>,
                  "cell<T>::get() requires a noexcept copy constructible T (Rust's Cell<T>::get() requires T: Copy)");
    return value_;
  }

  /**
   * @brief Overwrites the value with @p value, dropping the previous one.
   */
  constexpr void set(T value) const & noexcept(std::is_nothrow_move_assignable_v<T>) { value_ = std::move(value); }

  /**
   * @brief Overwrites the value with @p value, returning the previous one.
   */
  [[nodiscard]] constexpr T replace(T value) const &
      noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>) {
    T old = std::move(value_);
    value_ = std::move(value);
    return old;
  }

  /**
   * @brief Replaces the value with a default-constructed `T`, returning
   * the previous value. Requires a default constructible `T` (Rust's
   * `T: Default` bound on `Cell::take`), not a copyable one.
   */
  [[nodiscard]] constexpr T take() const &
      noexcept(std::is_nothrow_default_constructible_v<T> && std::is_nothrow_move_constructible_v<T> &&
               std::is_nothrow_move_assignable_v<T>) {
    return replace(T());
  }

private:
  mutable T value_;
};

/**
 * @brief Interior-mutability wrapper that tracks its borrow state at
 * runtime, matching Rust's `std::cell::RefCell<T>`.
 *
 * At most one exclusive (`mut_guard`) borrow, or any number of concurrent
 * shared (`ref_guard`) borrows, may be outstanding at once; both guard
 * types release their borrow automatically on destruction (they are
 * move-only, matching every other reloco RAII guard).
 */
template <typename T> class ref_cell {
public:
  /**
   * @brief A live shared borrow of the wrapped value. Move-only; releases
   * the borrow on destruction.
   */
  class RELOCO_CONSUMABLE(unconsumed) ref_guard {
  public:
    RELOCO_BLOCK_RVALUE_ACCESS(T);

    ref_guard(ref_guard &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : cell_(other.cell_) {
      other.cell_ = nullptr;
    }
    ref_guard(const ref_guard &) = delete;
    ref_guard &operator=(ref_guard &&) = delete;
    ref_guard &operator=(const ref_guard &) = delete;

    ~ref_guard() noexcept {
      if (cell_ != nullptr)
        --cell_->borrow_state_;
    }

    [[nodiscard]] const T &operator*() const & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
      RELOCO_ASSERT(cell_ != nullptr, "ref_guard used after being moved from");
      return cell_->value_;
    }

    [[nodiscard]] const T *operator->() const & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
      RELOCO_ASSERT(cell_ != nullptr, "ref_guard used after being moved from");
      return &cell_->value_;
    }

  private:
    friend class ref_cell;
    explicit ref_guard(const ref_cell *cell) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : cell_(cell) {}
    const ref_cell *cell_;
  };

  /**
   * @brief A live exclusive borrow of the wrapped value. Move-only;
   * releases the borrow on destruction.
   */
  class RELOCO_CONSUMABLE(unconsumed) mut_guard {
  public:
    RELOCO_BLOCK_RVALUE_ACCESS(T);

    mut_guard(mut_guard &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : cell_(other.cell_) {
      other.cell_ = nullptr;
    }
    mut_guard(const mut_guard &) = delete;
    mut_guard &operator=(mut_guard &&) = delete;
    mut_guard &operator=(const mut_guard &) = delete;

    ~mut_guard() noexcept {
      if (cell_ != nullptr)
        cell_->borrow_state_ = 0;
    }

    [[nodiscard]] T &operator*() const & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
      RELOCO_ASSERT(cell_ != nullptr, "mut_guard used after being moved from");
      return cell_->value_;
    }

    [[nodiscard]] T *operator->() const & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
      RELOCO_ASSERT(cell_ != nullptr, "mut_guard used after being moved from");
      return &cell_->value_;
    }

  private:
    friend class ref_cell;
    explicit mut_guard(ref_cell *cell) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : cell_(cell) {}
    ref_cell *cell_;
  };

  constexpr ref_cell() noexcept(std::is_nothrow_default_constructible_v<T>) : value_(), borrow_state_(0) {}
  constexpr explicit ref_cell(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
      : value_(std::move(value)), borrow_state_(0) {}

  /**
   * @brief Attempts to acquire a shared borrow. Fails with `error::busy`
   * if the value is currently exclusively borrowed.
   */
  [[nodiscard]] result<ref_guard> try_borrow() const & noexcept RELOCO_LIFETIMEBOUND {
    if (borrow_state_ < 0)
      return unexpected(error::busy);
    ++borrow_state_;
    return ref_guard(this);
  }

  /**
   * @brief Attempts to acquire an exclusive borrow. Fails with
   * `error::busy` if the value is currently borrowed at all (shared or
   * exclusive).
   */
  [[nodiscard]] result<mut_guard> try_borrow_mut() & noexcept RELOCO_LIFETIMEBOUND {
    if (borrow_state_ != 0)
      return unexpected(error::busy);
    borrow_state_ = -1;
    return mut_guard(this);
  }

  /**
   * @brief Acquires a shared borrow, `RELOCO_ASSERT`-trapping instead of
   * returning `error::busy` on conflict; matches Rust's panicking
   * `RefCell::borrow()`.
   */
  [[nodiscard]] ref_guard borrow() const & noexcept RELOCO_LIFETIMEBOUND {
    auto borrowed = try_borrow();
    RELOCO_ASSERT(borrowed.has_value(), "ref_cell: already mutably borrowed");
    return std::move(borrowed).value();
  }

  /**
   * @brief Acquires an exclusive borrow, `RELOCO_ASSERT`-trapping instead
   * of returning `error::busy` on conflict; matches Rust's panicking
   * `RefCell::borrow_mut()`.
   */
  [[nodiscard]] mut_guard borrow_mut() & noexcept RELOCO_LIFETIMEBOUND {
    auto borrowed = try_borrow_mut();
    RELOCO_ASSERT(borrowed.has_value(), "ref_cell: already borrowed");
    return std::move(borrowed).value();
  }

  /**
   * @brief Direct, uncounted mutable access -- sound exactly when the
   * caller already holds an exclusive `ref_cell&` (matching Rust's
   * `RefCell::get_mut()`, which borrows `&mut self` at compile time
   * instead of tracking a runtime count).
   */
  [[nodiscard]] T &get_mut() & noexcept RELOCO_LIFETIMEBOUND { return value_; }

private:
  mutable T value_;
  mutable std::ptrdiff_t borrow_state_;
};

} // namespace reloco
