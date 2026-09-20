// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file optional.hpp
 * @brief Zero-allocation, conditionally-present value wrapper mirroring
 * the strict safety and tri-tier access patterns of reloco containers.
 *
 * `reloco::optional<T>` replaces `std::optional<T>` by eliminating
 * undefined behavior on empty access. Instead, it follows the `reloco` way:
 *
 * 1. **Checked (Default):** `value()`, `operator*`, and `operator->` use
 *    `RELOCO_ASSERT` to trap safely if empty.
 * 2. **Fallible:** `try_value()` returns `result<reference_wrapper<T>>`, and
 *    `ok_or(error)` bridges the optional directly into a `result<T>` pipeline.
 * 3. **Unsafe:** `unsafe_value()` and `unsafe_ptr()` are explicitly gated
 *    behind `RELOCO_UNSAFE_BUFFER_USAGE` and only checked via `RELOCO_DEBUG_ASSERT`.
 *
 * It manages memory inline without dynamic allocation and natively integrates
 * with `is_trivially_relocatable<T>` to ensure zero-overhead swaps and moves
 * where the underlying type permits.
 */

#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"

#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace reloco {

struct nullopt_t {
  struct init {};
  constexpr explicit nullopt_t(init) noexcept {}
};

inline constexpr nullopt_t nullopt{nullopt_t::init{}};

template <typename T> class RELOCO_CONSUMABLE(unconsumed) optional {
public:
  using value_type = T;

  constexpr optional() noexcept RELOCO_RETURN_TYPESTATE(consumed) : dummy_('\0'), has_value_(false) {}
  constexpr optional(nullopt_t) noexcept RELOCO_RETURN_TYPESTATE(consumed) : dummy_('\0'), has_value_(false) {}

  constexpr optional(const T &value) noexcept(std::is_nothrow_copy_constructible_v<T>) : has_value_(false) {
    construct(value);
  }

  constexpr optional(T &&value) noexcept(std::is_nothrow_move_constructible_v<T>) RELOCO_RETURN_TYPESTATE(unconsumed)
      : has_value_(false) {
    construct(std::move(value));
  }

  optional(const optional &other) noexcept(std::is_nothrow_copy_constructible_v<T>) RELOCO_RETURN_TYPESTATE(unconsumed)
      : has_value_(false) {
    if (other.has_value_) {
      construct(other.value_);
    }
  }

  optional(optional &&other) noexcept(std::is_nothrow_move_constructible_v<T>) : has_value_(false) {
    if (other.has_value_) {
      construct(std::move(other.value_));
    }
  }

  template <typename... Args>
  constexpr explicit optional(std::in_place_t, Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
      RELOCO_RETURN_TYPESTATE(unconsumed)
      : has_value_(false) {
    construct(std::forward<Args>(args)...);
  }

  ~optional() { destroy(); }

  optional &operator=(nullopt_t) noexcept RELOCO_SET_TYPESTATE(consumed) {
    reset();
    return *this;
  }

  optional &operator=(const optional &other) noexcept(std::is_nothrow_copy_assignable_v<T> &&
                                                      std::is_nothrow_copy_constructible_v<T>) {
    if (this != &other) {
      if (other.has_value_) {
        if (has_value_)
          value_ = other.value_;
        else
          construct(other.value_);
      } else {
        destroy();
      }
    }
    return *this;
  }

  optional &operator=(optional &&other) noexcept(std::is_nothrow_move_assignable_v<T> &&
                                                 std::is_nothrow_move_constructible_v<T>) {
    if (this != &other) {
      if (other.has_value_) {
        if (has_value_)
          value_ = std::move(other.value_);
        else
          construct(std::move(other.value_));
      } else {
        destroy();
      }
    }
    return *this;
  }

  template <typename U = T>
  std::enable_if_t<std::is_constructible_v<T, U &&> && std::is_assignable_v<T &, U &&>, optional &>
  operator=(U &&value) noexcept(std::is_nothrow_assignable_v<T &, U &&> && std::is_nothrow_constructible_v<T, U &&>)
      RELOCO_SET_TYPESTATE(unconsumed) {
    if (has_value_) {
      value_ = std::forward<U>(value);
    } else {
      construct(std::forward<U>(value));
    }
    return *this;
  }

  [[nodiscard]] constexpr bool has_value() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) { return has_value_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) {
    return has_value_;
  }

  [[nodiscard]] T &value() & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(has_value_, "optional has no value");
    return value_;
  }

  [[nodiscard]] const T &value() const & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(has_value_, "optional has no value");
    return value_;
  }

  [[nodiscard]] T &&value() && noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(has_value_, "optional has no value");
    return std::move(value_);
  }

  [[nodiscard]] T &operator*() & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") { return value(); }
  [[nodiscard]] const T &operator*() const & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    return value();
  }

  [[nodiscard]] T *operator->() noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(has_value_, "optional has no value");
    return std::addressof(value_);
  }

  [[nodiscard]] const T *operator->() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(has_value_, "optional has no value");
    return std::addressof(value_);
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_value() & noexcept RELOCO_LIFETIMEBOUND {
    if (!has_value_)
      return unexpected(error::not_found);
    return std::ref(value_);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_value() const & noexcept RELOCO_LIFETIMEBOUND {
    if (!has_value_)
      return unexpected(error::not_found);
    return std::cref(value_);
  }

  /**
   * @brief Bridges the optional directly into a `reloco::result` pipeline.
   * Allows specifying the exact error to return if the optional is empty.
   */
  [[nodiscard]] result<T> ok_or(error e) const & noexcept(std::is_nothrow_copy_constructible_v<T>) {
    if (has_value_)
      return value_;
    return unexpected(e);
  }

  [[nodiscard]] result<T> ok_or(error e) && noexcept(std::is_nothrow_move_constructible_v<T>) {
    if (has_value_)
      return std::move(value_);
    return unexpected(e);
  }

  template <typename U>
  [[nodiscard]] T value_or(U &&default_value) const & noexcept(std::is_nothrow_copy_constructible_v<T>) {
    return has_value_ ? value_ : static_cast<T>(std::forward<U>(default_value));
  }

  template <typename U>
  [[nodiscard]] T value_or(U &&default_value) && noexcept(std::is_nothrow_move_constructible_v<T>) {
    return has_value_ ? std::move(value_) : static_cast<T>(std::forward<U>(default_value));
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_value() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(has_value_, "optional has no value");
    return value_;
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_value() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(has_value_, "optional has no value");
    return value_;
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T *unsafe_ptr() noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(has_value_, "optional has no value");
    return std::addressof(value_);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T *unsafe_ptr() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(has_value_, "optional has no value");
    return std::addressof(value_);
  }

  template <typename... Args>
  T &emplace(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>) RELOCO_SET_TYPESTATE(unconsumed) {
    destroy();
    construct(std::forward<Args>(args)...);
    return value_;
  }

  RELOCO_REINITIALIZES void reset() noexcept RELOCO_SET_TYPESTATE(consumed) { destroy(); }

  void swap(optional &other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>) {
    if (has_value_ == other.has_value_) {
      if (has_value_) {
        using std::swap;
        swap(value_, other.value_);
      }
    } else {
      if (has_value_) {
        other.construct(std::move(value_));
        destroy();
      } else {
        construct(std::move(other.value_));
        other.destroy();
      }
    }
  }

  /**
   * @brief Recovers from Clang's "unknown" typestate at a reference/pointer
   * boundary; see the primary template's @ref checked_value::as_known for
   * the full explanation.
   */
  [[nodiscard]] optional &as_known() & noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(has_value_, "optional: as_known() called on empty object");
    return *this;
  }

  [[nodiscard]] const optional &as_known() const & noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(has_value_, "optional: as_known() called on empty object");
    return *this;
  }

private:
  void destroy() noexcept {
    if (has_value_) {
      if constexpr (!std::is_trivially_destructible_v<T>) {
        value_.~T();
      }
      has_value_ = false;
    }
  }

  template <typename... Args> void construct(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    new (std::addressof(value_)) T(std::forward<Args>(args)...);
    has_value_ = true;
  }

  union {
    char dummy_;
    T value_;
  };
  bool has_value_;
};

/**
 * @brief Optional natively propagates the relocatability of its underlying type.
 */
template <typename T> struct is_trivially_relocatable<optional<T>> : is_trivially_relocatable<T> {};

template <typename T> [[nodiscard]] constexpr bool operator==(const optional<T> &lhs, const optional<T> &rhs) noexcept {
  if (lhs.has_value() != rhs.has_value())
    return false;
  if (!lhs.has_value())
    return true;
  // Safely bypass bounds checks internally since we already checked has_value()
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  return lhs.unsafe_value() == rhs.unsafe_value();
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

template <typename T> [[nodiscard]] constexpr bool operator!=(const optional<T> &lhs, const optional<T> &rhs) noexcept {
  return !(lhs == rhs);
}

template <typename T> [[nodiscard]] constexpr bool operator==(const optional<T> &opt, nullopt_t) noexcept {
  return !opt.has_value();
}

template <typename T> [[nodiscard]] constexpr bool operator==(nullopt_t, const optional<T> &opt) noexcept {
  return !opt.has_value();
}

template <typename T> [[nodiscard]] constexpr bool operator!=(const optional<T> &opt, nullopt_t) noexcept {
  return opt.has_value();
}

template <typename T> [[nodiscard]] constexpr bool operator!=(nullopt_t, const optional<T> &opt) noexcept {
  return opt.has_value();
}

template <typename T, typename U>
[[nodiscard]] constexpr bool operator==(const optional<T> &opt, const U &value) noexcept {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  return opt.has_value() ? opt.unsafe_value() == value : false;
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

template <typename T, typename U>
[[nodiscard]] constexpr bool operator==(const U &value, const optional<T> &opt) noexcept {
  return opt == value;
}

template <typename T, typename U>
[[nodiscard]] constexpr bool operator!=(const optional<T> &opt, const U &value) noexcept {
  return !(opt == value);
}

template <typename T, typename U>
[[nodiscard]] constexpr bool operator!=(const U &value, const optional<T> &opt) noexcept {
  return !(opt == value);
}

} // namespace reloco