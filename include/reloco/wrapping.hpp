// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file wrapping.hpp
 * @brief Rust `std::num::Wrapping<T>` equivalent: an integral newtype whose
 * `+`/`-`/`*`/unary `-`/`++`/`--` operators always wrap on overflow
 * (modulo-2^N, exactly `wrapping_add`/`wrapping_sub`/`wrapping_mul` from
 * `int_ops.hpp`) instead of invoking undefined behavior (signed overflow)
 * or requiring the caller to remember to call those free functions
 * explicitly at every arithmetic expression.
 *
 * Plain `T` arithmetic is left exactly as-is elsewhere in reloco (still
 * UB-on-signed-overflow, per the language); `wrapping<T>` is an opt-in
 * newtype for the specific expressions where wraparound is the *intended*
 * behavior (checksums, hashing, ring-buffer indices, ...), matching how
 * Rust's `Wrapping<T>` is used instead of raw `+`/`-`/`*` for the same
 * cases.
 */

#include "int_ops.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>

namespace reloco {

/**
 * @brief An integral newtype whose arithmetic operators always wrap
 * (modulo-2^N) on overflow instead of invoking undefined behavior
 * (signed) or requiring an explicit `wrapping_add`/`wrapping_sub`/
 * `wrapping_mul` call (see `int_ops.hpp`, which this forwards to).
 */
template <typename T> class wrapping {
  static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>,
                "wrapping<T> requires a non-bool integral T");

public:
  using value_type = T;

  constexpr wrapping() noexcept : value_(T(0)) {}
  constexpr explicit wrapping(T value) noexcept : value_(value) {}

  /** @brief Returns the wrapped value. */
  [[nodiscard]] constexpr T get() const noexcept { return value_; }

  /**
   * @brief Implicitly converts to the wrapped value, so a `wrapping<T>`
   * can be used anywhere a `T` is expected (e.g. comparisons, formatting).
   */
  [[nodiscard]] constexpr operator T() const noexcept { return value_; }

  constexpr wrapping &operator+=(wrapping other) noexcept {
    value_ = wrapping_add(value_, other.value_);
    return *this;
  }

  constexpr wrapping &operator-=(wrapping other) noexcept {
    value_ = wrapping_sub(value_, other.value_);
    return *this;
  }

  constexpr wrapping &operator*=(wrapping other) noexcept {
    value_ = wrapping_mul(value_, other.value_);
    return *this;
  }

  [[nodiscard]] friend constexpr wrapping operator+(wrapping a, wrapping b) noexcept {
    return wrapping(wrapping_add(a.value_, b.value_));
  }

  [[nodiscard]] friend constexpr wrapping operator-(wrapping a, wrapping b) noexcept {
    return wrapping(wrapping_sub(a.value_, b.value_));
  }

  [[nodiscard]] friend constexpr wrapping operator*(wrapping a, wrapping b) noexcept {
    return wrapping(wrapping_mul(a.value_, b.value_));
  }

  /** @brief Unary negation, wrapping modulo-2^N exactly like `0 - *this`. */
  [[nodiscard]] constexpr wrapping operator-() const noexcept { return wrapping(wrapping_sub(T(0), value_)); }

  constexpr wrapping &operator++() noexcept {
    value_ = wrapping_add(value_, T(1));
    return *this;
  }

  constexpr wrapping operator++(int) noexcept {
    wrapping old = *this;
    ++*this;
    return old;
  }

  constexpr wrapping &operator--() noexcept {
    value_ = wrapping_sub(value_, T(1));
    return *this;
  }

  constexpr wrapping operator--(int) noexcept {
    wrapping old = *this;
    --*this;
    return old;
  }

  [[nodiscard]] constexpr bool operator==(wrapping other) const noexcept { return value_ == other.value_; }
  [[nodiscard]] constexpr bool operator!=(wrapping other) const noexcept { return value_ != other.value_; }
  [[nodiscard]] constexpr bool operator<(wrapping other) const noexcept { return value_ < other.value_; }
  [[nodiscard]] constexpr bool operator>(wrapping other) const noexcept { return value_ > other.value_; }
  [[nodiscard]] constexpr bool operator<=(wrapping other) const noexcept { return value_ <= other.value_; }
  [[nodiscard]] constexpr bool operator>=(wrapping other) const noexcept { return value_ >= other.value_; }

private:
  T value_;
};

} // namespace reloco

namespace std {

template <typename T> struct hash<reloco::wrapping<T>> {
  std::size_t operator()(const reloco::wrapping<T> &w) const noexcept { return hash<T>{}(w.get()); }
};

} // namespace std
