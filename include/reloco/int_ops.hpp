// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file int_ops.hpp
 * @brief Rust-style `checked_*`/`wrapping_*`/`saturating_*`/`overflowing_*`
 * integer arithmetic.
 *
 * Plain `+`/`-`/`*` on signed integers is undefined behavior on overflow,
 * and silently wraps on unsigned integers -- both are exactly the kind of
 * implicit, easy-to-miss failure mode `reloco` avoids elsewhere (see
 * `error.hpp`). This header ports Rust's four explicit integer-arithmetic
 * families (`i32::checked_add`, `wrapping_add`, `saturating_add`,
 * `overflowing_add`, and the `_sub`/`_mul` equivalents) to reloco's
 * `result<T>` idiom:
 *
 * - `checked_add/sub/mul(a, b)` returns `result<T>`, failing with
 *   `error::integer_overflow` instead of invoking undefined behavior or
 *   silently wrapping.
 * - `wrapping_add/sub/mul(a, b)` always returns a `T`, matching the
 *   well-defined modulo-2^N wraparound of unsigned arithmetic (applied to
 *   signed types via a two's-complement bit-pattern reinterpretation,
 *   exactly like Rust's `wrapping_*` on every mainstream two's-complement
 *   target this library supports).
 * - `saturating_add/sub/mul(a, b)` always returns a `T`, clamped to
 *   `[numeric_limits<T>::min(), numeric_limits<T>::max()]` instead of
 *   overflowing.
 * - `overflowing_add/sub/mul(a, b)` always returns an `overflowing_result<T>`
 *   pairing the wrapped result with a `bool` reporting whether it wrapped,
 *   for callers that want the wrapped value *and* to know if it was exact.
 *
 * All of the above are implemented with portable, standard-conforming
 * range/division checks (no `__builtin_*_overflow`/compiler intrinsics),
 * so they work identically across every compiler reloco supports,
 * including MSVC.
 */

#include "error.hpp"
#include "expected.hpp"

#include <limits>
#include <type_traits>

namespace reloco {

template <typename T> struct overflowing_result {
  T value;
  bool overflowed;
};

namespace detail {

template <typename T> constexpr void assert_supported_int() noexcept {
  static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>,
               "int_ops requires a non-bool integral type");
}

} // namespace detail

/**
 * @brief Adds @p a and @p b, failing with `error::integer_overflow` instead
 * of wrapping (unsigned) or invoking undefined behavior (signed).
 */
template <typename T> [[nodiscard]] constexpr result<T> checked_add(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  if constexpr (std::is_unsigned_v<T>) {
    if (b > static_cast<T>(std::numeric_limits<T>::max() - a))
      return unexpected(error::integer_overflow);
  } else {
    if (b > 0 && a > static_cast<T>(std::numeric_limits<T>::max() - b))
      return unexpected(error::integer_overflow);
    if (b < 0 && a < static_cast<T>(std::numeric_limits<T>::min() - b))
      return unexpected(error::integer_overflow);
  }
  return static_cast<T>(a + b);
}

/**
 * @brief Subtracts @p b from @p a, failing with `error::integer_overflow`
 * instead of wrapping (unsigned) or invoking undefined behavior (signed).
 */
template <typename T> [[nodiscard]] constexpr result<T> checked_sub(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  if constexpr (std::is_unsigned_v<T>) {
    if (b > a)
      return unexpected(error::integer_overflow);
  } else {
    if (b < 0 && a > static_cast<T>(std::numeric_limits<T>::max() + b))
      return unexpected(error::integer_overflow);
    if (b > 0 && a < static_cast<T>(std::numeric_limits<T>::min() + b))
      return unexpected(error::integer_overflow);
  }
  return static_cast<T>(a - b);
}

/**
 * @brief Multiplies @p a and @p b, failing with `error::integer_overflow`
 * instead of wrapping (unsigned) or invoking undefined behavior (signed).
 */
template <typename T> [[nodiscard]] constexpr result<T> checked_mul(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  if (a == 0 || b == 0)
    return static_cast<T>(0);
  if constexpr (std::is_unsigned_v<T>) {
    if (a > static_cast<T>(std::numeric_limits<T>::max() / b))
      return unexpected(error::integer_overflow);
  } else {
    const T max_v = std::numeric_limits<T>::max();
    const T min_v = std::numeric_limits<T>::min();
    if (a > 0) {
      if (b > 0) {
        if (a > static_cast<T>(max_v / b))
          return unexpected(error::integer_overflow);
      } else {
        if (b < static_cast<T>(min_v / a))
          return unexpected(error::integer_overflow);
      }
    } else {
      if (b > 0) {
        if (a < static_cast<T>(min_v / b))
          return unexpected(error::integer_overflow);
      } else {
        if (b < static_cast<T>(max_v / a))
          return unexpected(error::integer_overflow);
      }
    }
  }
  return static_cast<T>(a * b);
}

/**
 * @brief Adds @p a and @p b with well-defined modulo-2^N wraparound on
 * overflow, matching Rust's `wrapping_add`.
 */
template <typename T> [[nodiscard]] constexpr T wrapping_add(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  using U = std::make_unsigned_t<T>;
  return static_cast<T>(static_cast<U>(static_cast<U>(a) + static_cast<U>(b)));
}

/**
 * @brief Subtracts @p b from @p a with well-defined modulo-2^N wraparound
 * on overflow, matching Rust's `wrapping_sub`.
 */
template <typename T> [[nodiscard]] constexpr T wrapping_sub(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  using U = std::make_unsigned_t<T>;
  return static_cast<T>(static_cast<U>(static_cast<U>(a) - static_cast<U>(b)));
}

/**
 * @brief Multiplies @p a and @p b with well-defined modulo-2^N wraparound
 * on overflow, matching Rust's `wrapping_mul`.
 */
template <typename T> [[nodiscard]] constexpr T wrapping_mul(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  using U = std::make_unsigned_t<T>;
  return static_cast<T>(static_cast<U>(static_cast<U>(a) * static_cast<U>(b)));
}

/**
 * @brief Adds @p a and @p b, clamping to `numeric_limits<T>::max()` (or
 * `::min()` for signed underflow) instead of overflowing, matching Rust's
 * `saturating_add`.
 */
template <typename T> [[nodiscard]] constexpr T saturating_add(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  auto checked = checked_add(a, b);
  if (checked.has_value())
    return checked.value();
  if constexpr (std::is_unsigned_v<T>) {
    return std::numeric_limits<T>::max();
  } else {
    return b > 0 ? std::numeric_limits<T>::max() : std::numeric_limits<T>::min();
  }
}

/**
 * @brief Subtracts @p b from @p a, clamping to `numeric_limits<T>::min()`
 * (or `::max()` for signed overflow) instead of overflowing, matching
 * Rust's `saturating_sub`.
 */
template <typename T> [[nodiscard]] constexpr T saturating_sub(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  auto checked = checked_sub(a, b);
  if (checked.has_value())
    return checked.value();
  if constexpr (std::is_unsigned_v<T>) {
    return static_cast<T>(0);
  } else {
    return b < 0 ? std::numeric_limits<T>::max() : std::numeric_limits<T>::min();
  }
}

/**
 * @brief Multiplies @p a and @p b, clamping to
 * `numeric_limits<T>::max()`/`::min()` instead of overflowing, matching
 * Rust's `saturating_mul`.
 */
template <typename T> [[nodiscard]] constexpr T saturating_mul(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  auto checked = checked_mul(a, b);
  if (checked.has_value())
    return checked.value();
  if constexpr (std::is_unsigned_v<T>) {
    return std::numeric_limits<T>::max();
  } else {
    const bool positive_result = (a > 0) == (b > 0);
    return positive_result ? std::numeric_limits<T>::max() : std::numeric_limits<T>::min();
  }
}

/**
 * @brief Adds @p a and @p b, returning the wrapped result alongside a
 * `bool` reporting whether the addition overflowed, matching Rust's
 * `overflowing_add`.
 */
template <typename T> [[nodiscard]] constexpr overflowing_result<T> overflowing_add(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  return {wrapping_add(a, b), !checked_add(a, b).has_value()};
}

/**
 * @brief Subtracts @p b from @p a, returning the wrapped result alongside
 * a `bool` reporting whether the subtraction overflowed, matching Rust's
 * `overflowing_sub`.
 */
template <typename T> [[nodiscard]] constexpr overflowing_result<T> overflowing_sub(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  return {wrapping_sub(a, b), !checked_sub(a, b).has_value()};
}

/**
 * @brief Multiplies @p a and @p b, returning the wrapped result alongside
 * a `bool` reporting whether the multiplication overflowed, matching
 * Rust's `overflowing_mul`.
 */
template <typename T> [[nodiscard]] constexpr overflowing_result<T> overflowing_mul(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  return {wrapping_mul(a, b), !checked_mul(a, b).has_value()};
}

} // namespace reloco
