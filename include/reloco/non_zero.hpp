// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file non_zero.hpp
 * @brief Rust `NonZeroU8`/`NonZeroI32`/... equivalent: an integral value
 * statically known to never be `0`.
 *
 * A `non_zero<T>` carries the same invariant Rust's `NonZero*` family
 * encodes in its type: once constructed, its wrapped value is never `0`.
 * That invariant is checked once, at construction (`try_create`), rather
 * than re-checked on every use, so it also documents "this parameter is
 * never zero" directly in a function signature -- exactly Rust's
 * motivation for the type.
 */

#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"

#include <type_traits>

namespace reloco {

/**
 * @brief An integral value of type @c T that is statically known to never
 * be `0`.
 *
 * Only constructible through `try_create()` (fails with
 * `error::invalid_argument` for a `0` argument) or `unsafe_create()` (a
 * debug-only-checked escape hatch for a caller that has already proven the
 * value is non-zero), matching reloco's checked/fallible/unsafe tri-tier
 * convention. `get()` and the implicit conversion to `T` are always sound,
 * since a `non_zero<T>` can never hold `0` once constructed.
 */
template <typename T> class non_zero {
  static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>,
                "non_zero<T> requires an integral T, excluding bool");

public:
  using value_type = T;

  /**
   * @brief Attempts to wrap @p value. Fails with `error::invalid_argument`
   * if @p value is `0`.
   */
  [[nodiscard]] static constexpr result<non_zero> try_create(T value) noexcept {
    if (value == T(0))
      return unexpected(error::invalid_argument);
    return non_zero(value);
  }

  /**
   * @brief Wraps @p value with only a debug-only non-zero check --
   * sound exactly when the caller has already proven @p value is not `0`.
   */
  [[nodiscard]] static constexpr non_zero unsafe_create(T value) noexcept {
    RELOCO_DEBUG_ASSERT(value != T(0), "non_zero<T>: value must not be zero");
    return non_zero(value);
  }

  /**
   * @brief Returns the wrapped value.
   */
  [[nodiscard]] constexpr T get() const noexcept { return value_; }

  /**
   * @brief Implicitly converts to the wrapped value, so a `non_zero<T>`
   * can be used anywhere a `T` is expected (e.g. arithmetic, comparisons).
   */
  [[nodiscard]] constexpr operator T() const noexcept { return value_; }

  [[nodiscard]] constexpr bool operator==(const non_zero &other) const noexcept { return value_ == other.value_; }
  [[nodiscard]] constexpr bool operator!=(const non_zero &other) const noexcept { return value_ != other.value_; }
  [[nodiscard]] constexpr bool operator<(const non_zero &other) const noexcept { return value_ < other.value_; }
  [[nodiscard]] constexpr bool operator>(const non_zero &other) const noexcept { return value_ > other.value_; }
  [[nodiscard]] constexpr bool operator<=(const non_zero &other) const noexcept { return value_ <= other.value_; }
  [[nodiscard]] constexpr bool operator>=(const non_zero &other) const noexcept { return value_ >= other.value_; }

private:
  constexpr explicit non_zero(T value) noexcept : value_(value) {}

  T value_;
};

} // namespace reloco
