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
 *
 * ## `sqrt()`, `exp()`, and `taylor_eval`: approximate, still floating-
 * point-free
 *
 * `sqrt()` is exact integer square-root math (the classic binary
 * "digit-by-digit" algorithm over the widened raw bit pattern, see
 * `detail::isqrt_u64` below) -- no series, no approximation error beyond
 * `Rep`'s own fixed-point quantization.
 *
 * `exp()` (`e^x`) has no closed-form integer algorithm the way `sqrt()`
 * does, so it falls back to a Taylor series (`e^x = sum x^n/n!`),
 * evaluated by the free function template `taylor_eval(coefficients, x)`
 * below -- a small, generically-useful Horner's-method evaluator over
 * *any* caller-supplied `span` of `fixed_point` coefficients (not tied to
 * `exp()`'s own `1/n!` table at all; a caller with its own precomputed
 * series -- `sin`/`cos`/`log1p`/a curve fit, ... -- can reuse it
 * directly). `exp()` itself additionally range-reduces its argument first
 * (`e^x = (e^(x/2^k))^(2^k)`, choosing `k` so `|x/2^k| <= 1` keeps the
 * series both accurate and far less likely to overflow the widened
 * intermediate any single term needs), the same "scaling and squaring"
 * trick real floating-point `expm1`/matrix-exponential implementations
 * use. Both use a fixed (not adaptive/error-bounded) iteration count, and
 * inherit every other operator's silent-overflow caveat above -- treat
 * them as a convenient approximation for e.g. a decay-curve fit, not a
 * numerically-rigorous `<cmath>` replacement.
 */

#include "array.hpp"
#include "span.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace reloco {

template <typename Rep, unsigned FracBits> class fixed_point;

/**
 * @brief Evaluates `coefficients[0] + coefficients[1]*x + coefficients[2]*x^2 + ...`
 * (a truncated Taylor/Maclaurin series, or any other polynomial given as
 * an explicit coefficient list) at @p x, via Horner's method
 * (`coefficients.size() - 1` multiply-adds, not a separate `pow` per
 * term). @p coefficients is caller-owned, non-owning storage (any
 * contiguous range `span` can borrow from -- a `reloco::array`, a plain
 * C array, ...), so the degree and the coefficients themselves are
 * entirely up to the caller; an empty @p coefficients evaluates to `0`.
 * See the @file-level docs above for `exp()`'s own use of this as its
 * `1/n!` series evaluator.
 */
template <typename Rep, unsigned FracBits>
[[nodiscard]] constexpr fixed_point<Rep, FracBits> taylor_eval(span<const fixed_point<Rep, FracBits>> coefficients,
                                                                fixed_point<Rep, FracBits> x) noexcept;

namespace detail {

/**
 * @brief Exact integer square root of @p n (`floor(sqrt(n))`), via the
 * classic binary "digit-by-digit" algorithm -- `O(log n)` iterations,
 * portable (no compiler-specific intrinsics), usable in a `constexpr`
 * context.
 */
[[nodiscard]] constexpr std::uint64_t isqrt_u64(std::uint64_t n) noexcept {
  std::uint64_t result = 0;
  std::uint64_t bit = std::uint64_t{1} << 62; // the highest even power of 2 (so `bit` is always a power of 4) that fits in 64 bits
  while (bit > n)
    bit >>= 2;
  while (bit != 0) {
    if (n >= result + bit) {
      n -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return result;
}

} // namespace detail

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

  /**
   * @brief `floor(sqrt(*this))`, exact aside from `Rep`'s own fixed-point
   * quantization (not a series/approximation -- see the @file-level docs
   * above). Defined as `0` for a negative value (signed `Rep` only; no
   * complex-number support) instead of invoking undefined behavior.
   */
  [[nodiscard]] constexpr fixed_point sqrt() const noexcept {
    if constexpr (std::is_signed_v<Rep>) {
      if (raw_ <= 0)
        return fixed_point();
    } else {
      if (raw_ == 0)
        return fixed_point();
    }
    // sqrt(raw/one) == sqrt(raw*one)/one -- scaling the radicand by `one_raw` first keeps the
    // result in the same Q-format `*this` is already in.
    auto scaled = static_cast<std::uint64_t>(static_cast<wide>(raw_) * static_cast<wide>(one_raw));
    return from_raw(static_cast<Rep>(detail::isqrt_u64(scaled)));
  }

  /**
   * @brief `e` raised to `*this`, via a range-reduced Taylor series (see
   * the @file-level docs above for the full "scaling and squaring" +
   * `taylor_eval` algorithm, and its accuracy/overflow caveats).
   */
  [[nodiscard]] constexpr fixed_point exp() const noexcept {
    wide raw_w = static_cast<wide>(raw_);
    wide magnitude = raw_w < 0 ? static_cast<wide>(-raw_w) : raw_w;
    unsigned reduction_steps = 0;
    while (magnitude > static_cast<wide>(one_raw) && reduction_steps < 48) {
      magnitude /= 2;
      ++reduction_steps;
    }
    wide divisor = wide{1} << reduction_steps;
    fixed_point reduced = from_raw(static_cast<Rep>(raw_w / divisor));

    constexpr std::size_t taylor_terms = 16;
    array<fixed_point, taylor_terms> coefficients;
    coefficients[0] = from_int(1);
    for (std::size_t n = 1; n < taylor_terms; ++n)
      coefficients[n] = coefficients[n - 1] / from_int(static_cast<unsigned>(n));

    fixed_point result = taylor_eval(span<const fixed_point>(coefficients.data(), taylor_terms), reduced);
    for (unsigned i = 0; i < reduction_steps; ++i)
      result = result * result;
    return result;
  }

private:
  Rep raw_ = Rep{0};
};

/**
 * @brief Out-of-line definition of the `taylor_eval` declaration above
 * (needed before `fixed_point` itself, since `fixed_point::exp()` calls
 * it). See that declaration's own docs for the full contract.
 */
template <typename Rep, unsigned FracBits>
[[nodiscard]] constexpr fixed_point<Rep, FracBits> taylor_eval(span<const fixed_point<Rep, FracBits>> coefficients,
                                                                fixed_point<Rep, FracBits> x) noexcept {
  using fp = fixed_point<Rep, FracBits>;
  if (coefficients.empty())
    return fp();
  fp result = coefficients[coefficients.size() - 1];
  for (std::size_t i = coefficients.size() - 1; i > 0; --i)
    result = result * x + coefficients[i - 1];
  return result;
}

} // namespace reloco
