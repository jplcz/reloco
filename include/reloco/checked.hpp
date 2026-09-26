// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file checked.hpp
 * @brief An integral newtype whose arithmetic is always explicitly
 * fallible: every operation returns `result<checked<T>>` instead of
 * wrapping (`wrapping<T>`), clamping (`saturating<T>`), or invoking
 * undefined behavior/silently wrapping like plain `T` arithmetic.
 *
 * Unlike `wrapping<T>`/`saturating<T>` (which overload `+`/`-`/`*` to
 * always return a `T`-like value because they always have *a* defined
 * answer for overflow), `checked<T>` has no such answer -- overflow
 * really is an error here -- so its arithmetic is exposed as named
 * `try_*` methods (matching reloco's fallible-operation naming
 * convention elsewhere, see `concepts.hpp`) rather than as operator
 * overloads returning `result<T>`, which would silently defeat the
 * "you can't ignore this failure without writing code that says so"
 * property `result<T>` exists for in the first place.
 *
 * `checked<T>` is a thin, `constexpr`-friendly wrapper over the free
 * functions in `int_ops.hpp`, for callers who prefer chaining named
 * methods on a value (`a.try_add(b).and_then(...)`) over free-function
 * calls (`checked_add(a, b)`).
 */

#include "int_ops.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>

namespace reloco {

/**
 * @brief An integral newtype whose arithmetic is always explicitly
 * fallible via named `try_*` methods returning `result<checked<T>>`
 * (`try_add`/`try_sub`/`try_mul`/`try_div`/`try_rem`/`try_neg`/
 * `try_abs`), forwarding to the matching `checked_*` free function in
 * `int_ops.hpp`.
 */
template <typename T> class checked {
  static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>, "checked<T> requires a non-bool integral T");

public:
  using value_type = T;

  constexpr checked() noexcept : value_(T(0)) {}
  constexpr explicit checked(T value) noexcept : value_(value) {}

  /** @brief Returns the wrapped value. */
  [[nodiscard]] constexpr T get() const noexcept { return value_; }

  /**
   * @brief Implicitly converts to the wrapped value, so a `checked<T>`
   * can be used anywhere a `T` is expected (e.g. comparisons, formatting).
   */
  [[nodiscard]] constexpr operator T() const noexcept { return value_; }

  /** @brief Adds @p other, failing with `error::integer_overflow` on overflow. */
  [[nodiscard]] constexpr result<checked> try_add(checked other) const noexcept {
    auto added = checked_add(value_, other.value_);
    if (!added.has_value())
      return unexpected(added.error());
    return checked(added.value());
  }

  /** @brief Subtracts @p other, failing with `error::integer_overflow` on overflow/underflow. */
  [[nodiscard]] constexpr result<checked> try_sub(checked other) const noexcept {
    auto subtracted = checked_sub(value_, other.value_);
    if (!subtracted.has_value())
      return unexpected(subtracted.error());
    return checked(subtracted.value());
  }

  /** @brief Multiplies by @p other, failing with `error::integer_overflow` on overflow. */
  [[nodiscard]] constexpr result<checked> try_mul(checked other) const noexcept {
    auto multiplied = checked_mul(value_, other.value_);
    if (!multiplied.has_value())
      return unexpected(multiplied.error());
    return checked(multiplied.value());
  }

  /**
   * @brief Divides by @p other, failing with `error::division_by_zero`
   * (zero divisor) or `error::integer_overflow` (the signed
   * `min() / -1` corner case).
   */
  [[nodiscard]] constexpr result<checked> try_div(checked other) const noexcept {
    auto divided = checked_div(value_, other.value_);
    if (!divided.has_value())
      return unexpected(divided.error());
    return checked(divided.value());
  }

  /**
   * @brief Computes the remainder of dividing by @p other, failing with
   * `error::division_by_zero` (zero divisor) or `error::integer_overflow`
   * (the signed `min() % -1` corner case).
   */
  [[nodiscard]] constexpr result<checked> try_rem(checked other) const noexcept {
    auto remainder = checked_rem(value_, other.value_);
    if (!remainder.has_value())
      return unexpected(remainder.error());
    return checked(remainder.value());
  }

  /**
   * @brief Negates this value, failing with `error::integer_overflow` for
   * signed `numeric_limits<T>::min()` or any nonzero unsigned `T`.
   */
  [[nodiscard]] constexpr result<checked> try_neg() const noexcept {
    auto negated = checked_neg(value_);
    if (!negated.has_value())
      return unexpected(negated.error());
    return checked(negated.value());
  }

  /**
   * @brief Computes the absolute value (signed `T` only), failing with
   * `error::integer_overflow` for `numeric_limits<T>::min()`.
   */
  [[nodiscard]] constexpr result<checked> try_abs() const noexcept {
    auto absolute = checked_abs(value_);
    if (!absolute.has_value())
      return unexpected(absolute.error());
    return checked(absolute.value());
  }

  [[nodiscard]] constexpr bool operator==(checked other) const noexcept { return value_ == other.value_; }
  [[nodiscard]] constexpr bool operator!=(checked other) const noexcept { return value_ != other.value_; }
  [[nodiscard]] constexpr bool operator<(checked other) const noexcept { return value_ < other.value_; }
  [[nodiscard]] constexpr bool operator>(checked other) const noexcept { return value_ > other.value_; }
  [[nodiscard]] constexpr bool operator<=(checked other) const noexcept { return value_ <= other.value_; }
  [[nodiscard]] constexpr bool operator>=(checked other) const noexcept { return value_ >= other.value_; }

private:
  T value_;
};

} // namespace reloco

namespace std {

template <typename T> struct hash<reloco::checked<T>> {
  std::size_t operator()(const reloco::checked<T> &c) const noexcept { return hash<T>{}(c.get()); }
};

} // namespace std
