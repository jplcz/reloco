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
 * - `checked_div/rem(a, b)` return `result<T>`, failing with
 *   `error::division_by_zero` for `b == 0` and `error::integer_overflow`
 *   for the one signed corner case that overflows
 *   (`numeric_limits<T>::min() / T{-1}`), matching Rust's `checked_div`/
 *   `checked_rem`.
 * - `checked_neg(a)` returns `result<T>`, failing with
 *   `error::integer_overflow` for signed `numeric_limits<T>::min()` (whose
 *   negation doesn't fit in `T`) and for any nonzero unsigned `T` (which
 *   has no representable negative value), matching Rust's `checked_neg`.
 * - `checked_abs(a)` (signed types only) returns `result<T>`, failing with
 *   `error::integer_overflow` for `numeric_limits<T>::min()`, matching
 *   Rust's `checked_abs`.
 * - `checked_cast<To>(from)` converts an integral value to another
 *   integral type, failing with `error::integer_overflow` if @p from
 *   doesn't fit in `To`, matching Rust's `TryFrom`/`TryInto` for integers.
 *
 * All of the above are implemented with portable, standard-conforming
 * range/division checks (no `__builtin_*_overflow`/compiler intrinsics),
 * so they work identically across every compiler reloco supports,
 * including MSVC.
 */

#include "error.hpp"
#include "expected.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace reloco {

template <typename T> struct overflowing_result {
  T value;
  bool overflowed;
};

namespace detail {

template <typename T> constexpr void assert_supported_int() noexcept {
  static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>, "int_ops requires a non-bool integral type");
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

/**
 * @brief Divides @p a by @p b, failing with `error::division_by_zero` for
 * `b == 0` and `error::integer_overflow` for the one signed corner case
 * that overflows (`numeric_limits<T>::min() / T{-1}`), matching Rust's
 * `checked_div`.
 */
template <typename T> [[nodiscard]] constexpr result<T> checked_div(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  if (b == 0)
    return unexpected(error::division_by_zero);
  if constexpr (std::is_signed_v<T>) {
    if (a == std::numeric_limits<T>::min() && b == static_cast<T>(-1))
      return unexpected(error::integer_overflow);
  }
  return static_cast<T>(a / b);
}

/**
 * @brief Computes the remainder of @p a divided by @p b, failing with
 * `error::division_by_zero` for `b == 0` and `error::integer_overflow` for
 * the one signed corner case that overflows
 * (`numeric_limits<T>::min() % T{-1}`), matching Rust's `checked_rem`.
 */
template <typename T> [[nodiscard]] constexpr result<T> checked_rem(T a, T b) noexcept {
  detail::assert_supported_int<T>();
  if (b == 0)
    return unexpected(error::division_by_zero);
  if constexpr (std::is_signed_v<T>) {
    if (a == std::numeric_limits<T>::min() && b == static_cast<T>(-1))
      return unexpected(error::integer_overflow);
  }
  return static_cast<T>(a % b);
}

/**
 * @brief Negates @p a, failing with `error::integer_overflow` for signed
 * `numeric_limits<T>::min()` (whose negation doesn't fit in `T`) and for
 * any nonzero unsigned `T` (which has no representable negative value),
 * matching Rust's `checked_neg`.
 */
template <typename T> [[nodiscard]] constexpr result<T> checked_neg(T a) noexcept {
  detail::assert_supported_int<T>();
  if constexpr (std::is_unsigned_v<T>) {
    if (a != 0)
      return unexpected(error::integer_overflow);
    return static_cast<T>(0);
  } else {
    if (a == std::numeric_limits<T>::min())
      return unexpected(error::integer_overflow);
    return static_cast<T>(-a);
  }
}

/**
 * @brief Computes the absolute value of @p a, failing with
 * `error::integer_overflow` for `numeric_limits<T>::min()` (whose
 * absolute value doesn't fit in `T`), matching Rust's `checked_abs`.
 *
 * Only defined for signed `T`, matching Rust (which has no `checked_abs`
 * for unsigned integers, since an unsigned value's absolute value is
 * always itself).
 */
template <typename T> [[nodiscard]] constexpr result<T> checked_abs(T a) noexcept {
  detail::assert_supported_int<T>();
  static_assert(std::is_signed_v<T>, "checked_abs requires a signed integer type");
  if (a == std::numeric_limits<T>::min())
    return unexpected(error::integer_overflow);
  return static_cast<T>(a < 0 ? -a : a);
}

/**
 * @brief Converts @p from to `To`, failing with `error::integer_overflow`
 * if the value doesn't fit in `To`, matching Rust's `TryFrom`/`TryInto`
 * for integers.
 *
 * Compares via `intmax_t`/`uintmax_t` (rather than mixing signed/unsigned
 * comparisons of `From`/`To` directly, which is exactly the kind of
 * implicit conversion `-Wsign-conversion` warns about and can silently
 * misbehave) so the check is correct across every signed/unsigned and
 * differing-width `From`/`To` combination.
 */
template <typename To, typename From> [[nodiscard]] constexpr result<To> checked_cast(From from) noexcept {
  detail::assert_supported_int<To>();
  detail::assert_supported_int<From>();
  if constexpr (std::is_same_v<To, From>) {
    return from;
  } else if constexpr (std::is_signed_v<From> && std::is_signed_v<To>) {
    // Both signed: `intmax_t` is guaranteed at least as wide as any
    // standard signed integer type, so widening either side to it (rather
    // than casting one of `To`'s limits down into `From`, which can
    // itself overflow when `To` is wider than `From`) never loses range.
    if (static_cast<intmax_t>(from) < static_cast<intmax_t>(std::numeric_limits<To>::min()) ||
        static_cast<intmax_t>(from) > static_cast<intmax_t>(std::numeric_limits<To>::max()))
      return unexpected(error::integer_overflow);
    return static_cast<To>(from);
  } else if constexpr (!std::is_signed_v<From> && !std::is_signed_v<To>) {
    // Both unsigned: same reasoning as above, but in `uintmax_t`.
    if (static_cast<uintmax_t>(from) > static_cast<uintmax_t>(std::numeric_limits<To>::max()))
      return unexpected(error::integer_overflow);
    return static_cast<To>(from);
  } else if constexpr (std::is_signed_v<From> && !std::is_signed_v<To>) {
    // Signed source, unsigned destination: negative values never fit, and
    // large positive values must still fit within `To`'s range.
    if (from < 0 || static_cast<uintmax_t>(from) > static_cast<uintmax_t>(std::numeric_limits<To>::max()))
      return unexpected(error::integer_overflow);
    return static_cast<To>(from);
  } else {
    // Unsigned source, signed destination: `To`'s max is always
    // non-negative, so comparing in `uintmax_t` (where it fits without
    // truncation regardless of how `To` compares in width to `From`)
    // covers every combination correctly.
    if (static_cast<uintmax_t>(from) > static_cast<uintmax_t>(std::numeric_limits<To>::max()))
      return unexpected(error::integer_overflow);
    return static_cast<To>(from);
  }
}

} // namespace reloco
