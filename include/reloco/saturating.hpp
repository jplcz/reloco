// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file saturating.hpp
 * @brief Rust `std::num::Saturating<T>` equivalent: an integral newtype
 * whose `+`/`-`/`*`/unary `-`/`++`/`--` operators always clamp to
 * `[numeric_limits<T>::min(), numeric_limits<T>::max()]` on overflow
 * (exactly `saturating_add`/`saturating_sub`/`saturating_mul` from
 * `int_ops.hpp`) instead of invoking undefined behavior (signed overflow)
 * or requiring the caller to remember to call those free functions
 * explicitly at every arithmetic expression.
 *
 * Typical uses are counters/accumulators/health-bar-style values that
 * should stick to a bound instead of wrapping around or overflowing, e.g.
 * `saturating<std::uint8_t> health(200); health -= 255;` yields `0`
 * instead of wrapping to `201`.
 */

#include "int_ops.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>

namespace reloco {

/**
 * @brief An integral newtype whose arithmetic operators always saturate
 * (clamp to `[numeric_limits<T>::min(), numeric_limits<T>::max()]`) on
 * overflow instead of invoking undefined behavior (signed) or requiring
 * an explicit `saturating_add`/`saturating_sub`/`saturating_mul` call
 * (see `int_ops.hpp`, which this forwards to).
 */
template <typename T> class saturating {
  static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>, "saturating<T> requires a non-bool integral T");

public:
  using value_type = T;

  constexpr saturating() noexcept : value_(T(0)) {}
  constexpr explicit saturating(T value) noexcept : value_(value) {}

  /** @brief Returns the wrapped value. */
  [[nodiscard]] constexpr T get() const noexcept { return value_; }

  /**
   * @brief Implicitly converts to the wrapped value, so a
   * `saturating<T>` can be used anywhere a `T` is expected (e.g.
   * comparisons, formatting).
   */
  [[nodiscard]] constexpr operator T() const noexcept { return value_; }

  constexpr saturating &operator+=(saturating other) noexcept {
    value_ = saturating_add(value_, other.value_);
    return *this;
  }

  constexpr saturating &operator-=(saturating other) noexcept {
    value_ = saturating_sub(value_, other.value_);
    return *this;
  }

  constexpr saturating &operator*=(saturating other) noexcept {
    value_ = saturating_mul(value_, other.value_);
    return *this;
  }

  [[nodiscard]] friend constexpr saturating operator+(saturating a, saturating b) noexcept {
    return saturating(saturating_add(a.value_, b.value_));
  }

  [[nodiscard]] friend constexpr saturating operator-(saturating a, saturating b) noexcept {
    return saturating(saturating_sub(a.value_, b.value_));
  }

  [[nodiscard]] friend constexpr saturating operator*(saturating a, saturating b) noexcept {
    return saturating(saturating_mul(a.value_, b.value_));
  }

  /**
   * @brief Unary negation, saturating exactly like `0 - *this` (clamps to
   * `0` for an unsigned `T`, since `Wrapping`'s two's-complement
   * reinterpretation doesn't apply to a saturating clamp).
   */
  [[nodiscard]] constexpr saturating operator-() const noexcept { return saturating(saturating_sub(T(0), value_)); }

  constexpr saturating &operator++() noexcept {
    value_ = saturating_add(value_, T(1));
    return *this;
  }

  constexpr saturating operator++(int) noexcept {
    saturating old = *this;
    ++*this;
    return old;
  }

  constexpr saturating &operator--() noexcept {
    value_ = saturating_sub(value_, T(1));
    return *this;
  }

  constexpr saturating operator--(int) noexcept {
    saturating old = *this;
    --*this;
    return old;
  }

  [[nodiscard]] constexpr bool operator==(saturating other) const noexcept { return value_ == other.value_; }
  [[nodiscard]] constexpr bool operator!=(saturating other) const noexcept { return value_ != other.value_; }
  [[nodiscard]] constexpr bool operator<(saturating other) const noexcept { return value_ < other.value_; }
  [[nodiscard]] constexpr bool operator>(saturating other) const noexcept { return value_ > other.value_; }
  [[nodiscard]] constexpr bool operator<=(saturating other) const noexcept { return value_ <= other.value_; }
  [[nodiscard]] constexpr bool operator>=(saturating other) const noexcept { return value_ >= other.value_; }

private:
  T value_;
};

} // namespace reloco

namespace std {

template <typename T> struct hash<reloco::saturating<T>> {
  std::size_t operator()(const reloco::saturating<T> &s) const noexcept { return hash<T>{}(s.get()); }
};

} // namespace std
