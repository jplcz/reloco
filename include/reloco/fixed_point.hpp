// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file fixed_point.hpp
 * @brief `fixed_point<Rep, FracBits>`: a `Q(bits(Rep)-FracBits).FracBits`
 * binary fixed-point number, for code that needs fractional quantities
 * (decay factors, ratios, load averages, ...) without ever touching a
 * floating-point unit -- exactly the same freestanding/no-FPU motivation
 * `duration.hpp`'s own file-level docs give for avoiding
 * `std::chrono::duration`'s floating-point-period pitfalls.
 *
 * `Rep` is a plain integral storage type (`std::uint32_t`, `std::int32_t`,
 * ...; `sizeof(Rep) <= sizeof(std::uint32_t)` is required, so every
 * widening intermediate below fits in a 64-bit accumulator); `FracBits`
 * is how many of `Rep`'s low bits represent the fractional part. A
 * `fixed_point<std::uint32_t, 11>` matches the classic Unix/Linux
 * `calc_load()` `SHIFT_FIXED`/`FIXED_1` convention (`Q21.11`) almost
 * exactly, and is in fact the type used for that purpose by
 * `structo::load_average` (see `structo/load_average.hpp`), the original
 * motivation for this header: computing an exponentially-decayed moving
 * average (`load_n = load_0 * p^n + active*(1-p^n)`) needs fixed-point
 * multiply/divide *and* `pow` (`p^n` for a geometric decay applied over
 * `n` elapsed sampling quanta, via exponentiation by squaring so a long
 * gap between samples costs `O(log n)`, not `O(n)`), which is exactly
 * this header's surface.
 *
 * Every operator here is plain, unchecked arithmetic over the scaled
 * representation -- silent wraparound/truncation on overflow, and
 * dividing by a zero `fixed_point` is undefined behavior, exactly
 * matching plain integer `+`/`-`/`*`/`/`'s own existing failure modes
 * (see `int_ops.hpp` for explicit `checked_*`/`wrapping_*`/
 * `saturating_*` alternatives for *integers*; this header intentionally
 * does not duplicate that machinery for the scaled fixed-point domain,
 * matching `wrapping<T>`/`saturating<T>`'s own choice not to overload
 * `/`/`%` at all).
 */

#include <cstdint>
#include <type_traits>

namespace reloco {

/**
 * @brief A `Q(bits(Rep)-FracBits).FracBits` binary fixed-point number.
 * See the @file-level docs above for the full rationale.
 */
template <typename Rep, unsigned FracBits> class fixed_point {
  static_assert(std::is_integral_v<Rep> && !std::is_same_v<Rep, bool>, "fixed_point<Rep, FracBits> requires a non-bool integral Rep");
  static_assert(sizeof(Rep) <= sizeof(std::uint32_t), "fixed_point<Rep, FracBits> requires Rep no wider than 32 bits, so every "
                                                       "widening intermediate fits in a 64-bit accumulator");
  static_assert(FracBits < sizeof(Rep) * 8, "fixed_point<Rep, FracBits>: FracBits must leave at least one integer bit in Rep");

  using wide = std::conditional_t<std::is_signed_v<Rep>, std::int64_t, std::uint64_t>;

public:
  using rep_type = Rep;
  static constexpr unsigned frac_bits = FracBits;

  /** @brief The raw bit pattern representing the value `1`. */
  static constexpr Rep one_raw = static_cast<Rep>(Rep{1} << FracBits);

  constexpr fixed_point() noexcept = default;

  /** @brief Constructs directly from an already-scaled raw bit pattern (`value * one_raw`, pre-computed). */
  [[nodiscard]] static constexpr fixed_point from_raw(Rep raw) noexcept {
    fixed_point fp;
    fp.raw_ = raw;
    return fp;
  }

  /** @brief Constructs from a plain integer, matching `value.0` -- silently wraps if `value * one_raw` overflows `Rep`,
   * exactly like plain `Rep` multiplication would. */
  template <typename T> [[nodiscard]] static constexpr fixed_point from_int(T value) noexcept {
    static_assert(std::is_integral_v<T>, "fixed_point::from_int requires an integral T");
    return from_raw(static_cast<Rep>(static_cast<Rep>(value) * one_raw));
  }

  /** @brief The raw, scaled bit pattern (`get() == as_int() * one_raw` plus whatever fraction is represented). */
  [[nodiscard]] constexpr Rep raw() const noexcept { return raw_; }

  /** @brief The integer part, truncated toward zero (plain integer division, never UB). */
  [[nodiscard]] constexpr Rep to_int() const noexcept { return static_cast<Rep>(raw_ / one_raw); }

  /**
   * @brief The fractional part, rounded to the nearest whole percent
   * (`0..=100`; `100` is reachable only via rounding a fraction within
   * half a percent of `1`) -- a floating-point-free way to render a
   * `fixed_point` the way `/proc/loadavg` renders its own fixed-point
   * averages (`"<to_int()>.<fractional_percent() zero-padded to 2 digits>"`).
   */
  [[nodiscard]] constexpr unsigned fractional_percent() const noexcept {
    Rep frac_raw = static_cast<Rep>(raw_ - static_cast<Rep>(to_int() * one_raw));
    if constexpr (std::is_signed_v<Rep>) {
      if (frac_raw < 0)
        frac_raw = static_cast<Rep>(-frac_raw);
    }
    wide scaled = static_cast<wide>(frac_raw) * 100 + static_cast<wide>(one_raw) / 2;
    return static_cast<unsigned>(scaled / static_cast<wide>(one_raw));
  }

  constexpr fixed_point &operator+=(fixed_point other) noexcept {
    raw_ = static_cast<Rep>(raw_ + other.raw_);
    return *this;
  }

  constexpr fixed_point &operator-=(fixed_point other) noexcept {
    raw_ = static_cast<Rep>(raw_ - other.raw_);
    return *this;
  }

  constexpr fixed_point &operator*=(fixed_point other) noexcept {
    *this = *this * other;
    return *this;
  }

  constexpr fixed_point &operator/=(fixed_point other) noexcept {
    *this = *this / other;
    return *this;
  }

  [[nodiscard]] friend constexpr fixed_point operator+(fixed_point a, fixed_point b) noexcept {
    return from_raw(static_cast<Rep>(a.raw_ + b.raw_));
  }

  [[nodiscard]] friend constexpr fixed_point operator-(fixed_point a, fixed_point b) noexcept {
    return from_raw(static_cast<Rep>(a.raw_ - b.raw_));
  }

  /** @brief @warning Silently truncates/wraps on overflow of the widened intermediate product, exactly like plain
   * integer multiplication would -- see the @file-level docs above. */
  [[nodiscard]] friend constexpr fixed_point operator*(fixed_point a, fixed_point b) noexcept {
    wide product = static_cast<wide>(a.raw_) * static_cast<wide>(b.raw_);
    return from_raw(static_cast<Rep>(product / static_cast<wide>(one_raw)));
  }

  /** @brief @warning Dividing by a zero `fixed_point` is undefined behavior, exactly like plain integer division by
   * zero -- see the @file-level docs above. */
  [[nodiscard]] friend constexpr fixed_point operator/(fixed_point a, fixed_point b) noexcept {
    wide numerator = static_cast<wide>(a.raw_) * static_cast<wide>(one_raw);
    return from_raw(static_cast<Rep>(numerator / static_cast<wide>(b.raw_)));
  }

  [[nodiscard]] friend constexpr bool operator==(fixed_point a, fixed_point b) noexcept { return a.raw_ == b.raw_; }
  [[nodiscard]] friend constexpr bool operator!=(fixed_point a, fixed_point b) noexcept { return a.raw_ != b.raw_; }
  [[nodiscard]] friend constexpr bool operator<(fixed_point a, fixed_point b) noexcept { return a.raw_ < b.raw_; }
  [[nodiscard]] friend constexpr bool operator>(fixed_point a, fixed_point b) noexcept { return a.raw_ > b.raw_; }
  [[nodiscard]] friend constexpr bool operator<=(fixed_point a, fixed_point b) noexcept { return a.raw_ <= b.raw_; }
  [[nodiscard]] friend constexpr bool operator>=(fixed_point a, fixed_point b) noexcept { return a.raw_ >= b.raw_; }

  /**
   * @brief `base` raised to the @p exponent -th power, via exponentiation
   * by squaring (`O(log exponent)` multiplications, not `O(exponent)`) --
   * the operation a geometric decay/compounding computation (an
   * exponential moving average fast-forwarded over many elapsed sampling
   * quanta at once, compound interest, ...) needs. Matches the same
   * silent-overflow caveat `operator*` documents above, compounded once
   * per squaring step.
   */
  [[nodiscard]] static constexpr fixed_point pow(fixed_point base, std::uint64_t exponent) noexcept {
    fixed_point result = from_int(1);
    fixed_point b = base;
    while (exponent != 0) {
      if ((exponent & 1U) != 0)
        result = result * b;
      b = b * b;
      exponent >>= 1U;
    }
    return result;
  }

private:
  Rep raw_ = Rep{0};
};

} // namespace reloco
