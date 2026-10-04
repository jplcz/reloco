// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file fixed_int.hpp
 * @brief `fixed_int<N, Signed = true>`: an arbitrary fixed-precision
 * integer, `N` bits wide (`N` must be a power of two, at least 8),
 * feedable to `fixed_point<Rep, FracBits>` (`fixed_point.hpp`) as `Rep`
 * once `N` outgrows whatever native integer width the target compiler
 * offers.
 *
 * ## "Defer to the compiler" up to 128 bits
 *
 * For every `N` a mainstream compiler can plausibly represent natively
 * (8/16/32/64 bits always; 128 bits when the `__int128`/
 * `unsigned __int128` compiler extension is available, detected via the
 * portable `__SIZEOF_INT128__` feature-test macro), `fixed_int<N,
 * Signed>` is a plain type alias for that native type -- no wrapper, no
 * overhead, exactly the integer the compiler/ABI already knows how to
 * add/multiply/divide in one or two instructions. Only once `N` exceeds
 * what the compiler natively offers (`N >= 256`, or `N == 128` on a
 * target without the `__int128` extension) does `fixed_int<N, Signed>`
 * fall back to `detail::wide_int<N, Signed>` below, a portable,
 * software, two's-complement multi-limb integer implementing the same
 * `+`/`-`/`*`/`/`/`%`/comparisons/shifts/bitwise-ops surface a native
 * integer has -- built from 32-bit limbs, schoolbook
 * multiply/shift-subtract long division, `O(N)`/`O(N^2)` rather than the
 * single-instruction native case, but otherwise a drop-in `Rep` for
 * `fixed_point`.
 *
 * ## Why `fixed_point` needs this at all
 *
 * `fixed_point<Rep, FracBits>::operator*`/`operator/` need a *wider*
 * intermediate than `Rep` itself to avoid losing precision (the classic
 * "multiply two N-bit numbers, the exact product needs 2N bits" problem)
 * -- previously hardcoded to `Rep` capped at 32 bits widening to a fixed
 * `std::int64_t`/`std::uint64_t`. `next_wider_t<Rep>` below generalizes
 * that one hardcoded step into an open-ended chain (`8 -> 16 -> 32 -> 64
 * -> 128 -> 256 -> 512 -> ...`), so `fixed_point` itself places no upper
 * bound on `Rep`'s width at all: pick whatever `fixed_int<N, Signed>`
 * precision an application needs, and `fixed_point`'s own widened
 * multiply/divide intermediate (`fixed_int<2*N, Signed>`) follows
 * automatically, recursively falling back to `wide_int` only once (and
 * exactly as far as) the native chain runs out.
 */

#include "array.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(__SIZEOF_INT128__)
#define RELOCO_HAS_INT128 1
#else
#define RELOCO_HAS_INT128 0
#endif

namespace reloco {

namespace detail {

#if RELOCO_HAS_INT128
using int128_t = __int128;
using uint128_t = unsigned __int128;
#endif

template <typename T> struct is_builtin_int128 : std::false_type {};
#if RELOCO_HAS_INT128
template <> struct is_builtin_int128<int128_t> : std::true_type {};
template <> struct is_builtin_int128<uint128_t> : std::true_type {};
#endif
template <typename T> inline constexpr bool is_builtin_int128_v = is_builtin_int128<T>::value;

/** @brief `T`'s unsigned counterpart -- `std::make_unsigned_t<T>` for an ordinary integral `T`, specialized for the
 * `__int128`/`unsigned __int128` compiler extension (which standard library type traits aren't guaranteed to
 * support). */
template <typename T> struct unsigned_counterpart {
  using type = std::make_unsigned_t<T>;
};
#if RELOCO_HAS_INT128
template <> struct unsigned_counterpart<int128_t> {
  using type = uint128_t;
};
template <> struct unsigned_counterpart<uint128_t> {
  using type = uint128_t;
};
#endif
template <typename T> using unsigned_counterpart_t = typename unsigned_counterpart<T>::type;

/** @brief Whether `T{-1} < T{0}` -- a portable, `std::is_signed`-independent way to ask "is this arithmetic type
 * signed", usable for `__int128`/`unsigned __int128` too (which `std::is_signed` isn't guaranteed to support). */
template <typename T> [[nodiscard]] constexpr bool arithmetic_is_signed() noexcept { return T(-1) < T(0); }

template <std::size_t N, bool Signed> class wide_int;

template <typename T> struct is_wide_int : std::false_type {};
template <std::size_t N, bool Signed> struct is_wide_int<wide_int<N, Signed>> : std::true_type {};
template <typename T> inline constexpr bool is_wide_int_v = is_wide_int<T>::value;

/** @brief Whether `T` is something `fixed_point<Rep, FracBits>` can use as `Rep`: a native (non-`bool`) integral
 * type, the `__int128`/`unsigned __int128` compiler extension, or a `wide_int<N, Signed>`. */
template <typename T>
inline constexpr bool is_fixed_point_rep_v =
    (std::is_integral_v<T> && !std::is_same_v<T, bool>) || is_builtin_int128_v<T> || is_wide_int_v<T>;

/** @brief Whether `Rep` (one of the types `is_fixed_point_rep_v` accepts) represents signed values. */
template <typename T> struct is_signed_rep : std::bool_constant<std::is_signed_v<T>> {};
#if RELOCO_HAS_INT128
template <> struct is_signed_rep<int128_t> : std::true_type {};
template <> struct is_signed_rep<uint128_t> : std::false_type {};
#endif
template <std::size_t N, bool Signed> struct is_signed_rep<wide_int<N, Signed>> : std::bool_constant<Signed> {};
template <typename T> inline constexpr bool is_signed_rep_v = is_signed_rep<T>::value;

/**
 * @brief The next-wider representation above `Rep` -- what `fixed_point<Rep, FracBits>` uses as its own widened
 * multiply/divide intermediate. Native up the chain to 64 bits, then to the `__int128`/`unsigned __int128` compiler
 * extension if available (`wide_int<128, Signed>` otherwise), then `wide_int<2N, Signed>` from there on,
 * indefinitely.
 */
template <typename Rep> struct next_wider;
template <> struct next_wider<std::int8_t> {
  using type = std::int16_t;
};
template <> struct next_wider<std::uint8_t> {
  using type = std::uint16_t;
};
template <> struct next_wider<std::int16_t> {
  using type = std::int32_t;
};
template <> struct next_wider<std::uint16_t> {
  using type = std::uint32_t;
};
template <> struct next_wider<std::int32_t> {
  using type = std::int64_t;
};
template <> struct next_wider<std::uint32_t> {
  using type = std::uint64_t;
};
#if RELOCO_HAS_INT128
template <> struct next_wider<std::int64_t> {
  using type = int128_t;
};
template <> struct next_wider<std::uint64_t> {
  using type = uint128_t;
};
template <> struct next_wider<int128_t> {
  using type = wide_int<256, true>;
};
template <> struct next_wider<uint128_t> {
  using type = wide_int<256, false>;
};
#else
template <> struct next_wider<std::int64_t> {
  using type = wide_int<128, true>;
};
template <> struct next_wider<std::uint64_t> {
  using type = wide_int<128, false>;
};
#endif
template <std::size_t N, bool Signed> struct next_wider<wide_int<N, Signed>> {
  using type = wide_int<2 * N, Signed>;
};
template <typename Rep> using next_wider_t = typename next_wider<Rep>::type;

/**
 * @brief A portable, software, two's-complement, `N`-bit multi-limb integer (32-bit limbs, little-endian) --
 * `fixed_int<N, Signed>`'s fallback representation once `N` exceeds what the target compiler offers natively. See
 * the @file-level docs above for the full rationale.
 *
 * Every operator below is plain, unchecked, wraparound (modulo-2^N) arithmetic, exactly matching a native
 * integer's own existing overflow behavior (well-defined modulo wraparound for an unsigned `N`, silent truncation
 * standing in for a signed native type's undefined-behavior overflow) -- the same convention
 * `fixed_point<Rep, FracBits>` itself documents for its own operators.
 */
template <std::size_t N, bool Signed> class wide_int {
  static_assert(N % 32 == 0 && N >= 64, "wide_int<N, Signed> requires N to be a multiple of 32, at least 64 bits");

  static constexpr std::size_t limb_count = N / 32;

public:
  static constexpr std::size_t bit_width = N;
  static constexpr bool is_signed = Signed;

  constexpr wide_int() noexcept = default;

  /** @brief Sign/zero-extends (per `std::is_signed_v<T>`, not this `wide_int`'s own `Signed`) a native integral or
   * `__int128`/`unsigned __int128` value into this `wide_int`'s width. */
  template <typename T, typename = std::enable_if_t<(std::is_integral_v<T> && !std::is_same_v<T, bool>) ||
                                                      is_builtin_int128_v<T>>>
  constexpr wide_int(T value) noexcept { // NOLINT(*-explicit-constructor) -- deliberately implicit, matching native integer promotion
    if constexpr (sizeof(T) < sizeof(std::int32_t)) {
      // Widen anything narrower than a limb via ordinary (correctly sign-extending) conversion first, so the
      // limb-filling loop below only ever has to deal with whole 32-bit-or-wider chunks.
      using W = std::conditional_t<arithmetic_is_signed<T>(), std::int32_t, std::uint32_t>;
      assign_from(static_cast<W>(value));
    } else {
      assign_from(value);
    }
  }

  /** @brief Sign/zero-extends (per @p S) a narrower-or-equal-width `wide_int<M, S>` into this `wide_int`'s width. */
  template <std::size_t M, bool S, typename = std::enable_if_t<(M <= N)>>
  constexpr wide_int(const wide_int<M, S> &other) noexcept { // NOLINT(*-explicit-constructor)
    constexpr std::size_t other_limbs = M / 32;
    const std::uint32_t fill = (S && other.is_negative()) ? 0xFFFFFFFFU : 0U;
    for (std::size_t i = 0; i < limb_count; ++i)
      limbs_[i] = (i < other_limbs) ? other.limb(i) : fill;
  }

  /** @brief The @p index -th 32-bit limb (`0` is least-significant), or `0` for an out-of-range @p index. */
  [[nodiscard]] constexpr std::uint32_t limb(std::size_t index) const noexcept {
    return index < limb_count ? limbs_[index] : 0U;
  }

  /** @brief Whether this value's sign bit is set (always `false` for `Signed == false`). */
  [[nodiscard]] constexpr bool is_negative() const noexcept { return Signed && (limbs_[limb_count - 1] >> 31) != 0; }

  /** @brief Truncating, explicit conversion to a native integral or `__int128`/`unsigned __int128` type. */
  template <typename T, typename = std::enable_if_t<(std::is_integral_v<T> && !std::is_same_v<T, bool>) ||
                                                      is_builtin_int128_v<T>>>
  explicit constexpr operator T() const noexcept {
    if constexpr (sizeof(T) < sizeof(std::int32_t)) {
      return static_cast<T>(limbs_[0]);
    } else {
      using UT = unsigned_counterpart_t<T>;
      constexpr std::size_t t_limbs = sizeof(T) * 8 / 32;
      UT acc = 0;
      for (std::size_t i = 0; i < t_limbs && i < limb_count; ++i)
        acc |= static_cast<UT>(limbs_[i]) << (i * 32);
      return static_cast<T>(acc);
    }
  }

  /** @brief Truncating, explicit conversion to a narrower-or-equal-width `wide_int<M, S>`. */
  template <std::size_t M, bool S, typename = std::enable_if_t<(M <= N)>>
  explicit constexpr operator wide_int<M, S>() const noexcept {
    wide_int<M, S> result;
    for (std::size_t i = 0; i < M / 32; ++i)
      result.set_limb(i, limbs_[i]);
    return result;
  }

  constexpr void set_limb(std::size_t index, std::uint32_t value) noexcept {
    if (index < limb_count)
      limbs_[index] = value;
  }

  [[nodiscard]] friend constexpr wide_int operator+(wide_int a, wide_int b) noexcept {
    wide_int result;
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < limb_count; ++i) {
      std::uint64_t sum = static_cast<std::uint64_t>(a.limbs_[i]) + b.limbs_[i] + carry;
      result.limbs_[i] = static_cast<std::uint32_t>(sum);
      carry = sum >> 32;
    }
    return result;
  }

  [[nodiscard]] friend constexpr wide_int operator-(wide_int v) noexcept {
    wide_int inv;
    for (std::size_t i = 0; i < limb_count; ++i)
      inv.limbs_[i] = ~v.limbs_[i];
    return inv + wide_int(1);
  }

  [[nodiscard]] friend constexpr wide_int operator-(wide_int a, wide_int b) noexcept { return a + (-b); }

  [[nodiscard]] friend constexpr wide_int operator*(wide_int a, wide_int b) noexcept {
    wide_int result;
    for (std::size_t i = 0; i < limb_count; ++i) {
      std::uint64_t carry = 0;
      for (std::size_t j = 0; j + i < limb_count; ++j) {
        std::uint64_t prod = static_cast<std::uint64_t>(a.limbs_[i]) * b.limbs_[j] + result.limbs_[i + j] + carry;
        result.limbs_[i + j] = static_cast<std::uint32_t>(prod);
        carry = prod >> 32;
      }
      // Any carry beyond limb_count - i is discarded -- truncating mod 2^N, matching a native integer's own
      // wraparound-on-overflow multiply.
    }
    return result;
  }

  [[nodiscard]] friend constexpr wide_int operator/(wide_int a, wide_int b) noexcept {
    if constexpr (Signed) {
      bool neg = a.is_negative() != b.is_negative();
      wide_int quotient;
      wide_int remainder;
      unsigned_divmod(a.is_negative() ? -a : a, b.is_negative() ? -b : b, quotient, remainder);
      return neg ? -quotient : quotient;
    } else {
      wide_int quotient;
      wide_int remainder;
      unsigned_divmod(a, b, quotient, remainder);
      return quotient;
    }
  }

  [[nodiscard]] friend constexpr wide_int operator%(wide_int a, wide_int b) noexcept {
    wide_int quotient;
    wide_int remainder;
    if constexpr (Signed) {
      unsigned_divmod(a.is_negative() ? -a : a, b.is_negative() ? -b : b, quotient, remainder);
      return a.is_negative() ? -remainder : remainder; // remainder follows the dividend's sign, matching `%`
    } else {
      unsigned_divmod(a, b, quotient, remainder);
      return remainder;
    }
  }

  [[nodiscard]] friend constexpr wide_int operator&(wide_int a, wide_int b) noexcept {
    wide_int result;
    for (std::size_t i = 0; i < limb_count; ++i)
      result.limbs_[i] = a.limbs_[i] & b.limbs_[i];
    return result;
  }

  [[nodiscard]] friend constexpr wide_int operator|(wide_int a, wide_int b) noexcept {
    wide_int result;
    for (std::size_t i = 0; i < limb_count; ++i)
      result.limbs_[i] = a.limbs_[i] | b.limbs_[i];
    return result;
  }

  [[nodiscard]] friend constexpr wide_int operator^(wide_int a, wide_int b) noexcept {
    wide_int result;
    for (std::size_t i = 0; i < limb_count; ++i)
      result.limbs_[i] = a.limbs_[i] ^ b.limbs_[i];
    return result;
  }

  [[nodiscard]] friend constexpr wide_int operator~(wide_int v) noexcept {
    wide_int result;
    for (std::size_t i = 0; i < limb_count; ++i)
      result.limbs_[i] = ~v.limbs_[i];
    return result;
  }

  [[nodiscard]] friend constexpr wide_int operator<<(wide_int v, unsigned shift) noexcept {
    wide_int result;
    if (shift >= N)
      return result;
    auto limb_shift = static_cast<std::ptrdiff_t>(shift / 32);
    unsigned bit_shift = shift % 32;
    for (std::size_t i = 0; i < limb_count; ++i) {
      auto src_idx = static_cast<std::ptrdiff_t>(i) - limb_shift;
      std::uint32_t cur = (src_idx >= 0) ? v.limbs_[static_cast<std::size_t>(src_idx)] : 0U;
      std::uint32_t prev = (src_idx - 1 >= 0) ? v.limbs_[static_cast<std::size_t>(src_idx - 1)] : 0U;
      result.limbs_[i] =
          (bit_shift == 0) ? cur
                           : static_cast<std::uint32_t>((static_cast<std::uint64_t>(cur) << bit_shift) |
                                                         (static_cast<std::uint64_t>(prev) >> (32 - bit_shift)));
    }
    return result;
  }

  [[nodiscard]] friend constexpr wide_int operator>>(wide_int v, unsigned shift) noexcept {
    wide_int result;
    const std::uint32_t fill = (Signed && v.is_negative()) ? 0xFFFFFFFFU : 0U;
    if (shift >= N) {
      for (std::size_t i = 0; i < limb_count; ++i)
        result.limbs_[i] = fill;
      return result;
    }
    std::size_t limb_shift = shift / 32;
    unsigned bit_shift = shift % 32;
    for (std::size_t i = 0; i < limb_count; ++i) {
      std::size_t src_idx = i + limb_shift;
      std::uint32_t cur = (src_idx < limb_count) ? v.limbs_[src_idx] : fill;
      std::uint32_t next = (src_idx + 1 < limb_count) ? v.limbs_[src_idx + 1] : fill;
      result.limbs_[i] =
          (bit_shift == 0) ? cur
                           : static_cast<std::uint32_t>((cur >> bit_shift) |
                                                         (static_cast<std::uint64_t>(next) << (32 - bit_shift)));
    }
    return result;
  }

  constexpr wide_int &operator+=(wide_int other) noexcept { return *this = *this + other; }
  constexpr wide_int &operator-=(wide_int other) noexcept { return *this = *this - other; }
  constexpr wide_int &operator*=(wide_int other) noexcept { return *this = *this * other; }
  constexpr wide_int &operator/=(wide_int other) noexcept { return *this = *this / other; }
  constexpr wide_int &operator%=(wide_int other) noexcept { return *this = *this % other; }
  constexpr wide_int &operator&=(wide_int other) noexcept { return *this = *this & other; }
  constexpr wide_int &operator|=(wide_int other) noexcept { return *this = *this | other; }
  constexpr wide_int &operator^=(wide_int other) noexcept { return *this = *this ^ other; }
  constexpr wide_int &operator<<=(unsigned shift) noexcept { return *this = *this << shift; }
  constexpr wide_int &operator>>=(unsigned shift) noexcept { return *this = *this >> shift; }

  [[nodiscard]] friend constexpr bool operator==(const wide_int &a, const wide_int &b) noexcept {
    for (std::size_t i = 0; i < limb_count; ++i)
      if (a.limbs_[i] != b.limbs_[i])
        return false;
    return true;
  }

  [[nodiscard]] friend constexpr bool operator!=(const wide_int &a, const wide_int &b) noexcept { return !(a == b); }

  [[nodiscard]] friend constexpr bool operator<(const wide_int &a, const wide_int &b) noexcept {
    if constexpr (Signed) {
      bool a_neg = a.is_negative();
      bool b_neg = b.is_negative();
      if (a_neg != b_neg)
        return a_neg;
      return unsigned_less(a, b);
    } else {
      return unsigned_less(a, b);
    }
  }

  [[nodiscard]] friend constexpr bool operator>(const wide_int &a, const wide_int &b) noexcept { return b < a; }
  [[nodiscard]] friend constexpr bool operator<=(const wide_int &a, const wide_int &b) noexcept { return !(b < a); }
  [[nodiscard]] friend constexpr bool operator>=(const wide_int &a, const wide_int &b) noexcept { return !(a < b); }

private:
  template <typename U> constexpr void assign_from(U value) noexcept {
    constexpr std::size_t u_bits = sizeof(U) * 8;
    constexpr std::size_t u_limbs = u_bits / 32;
    using UU = unsigned_counterpart_t<U>;
    auto uvalue = static_cast<UU>(value);
    const std::uint32_t fill = (arithmetic_is_signed<U>() && value < U(0)) ? 0xFFFFFFFFU : 0U;
    for (std::size_t i = 0; i < limb_count; ++i)
      limbs_[i] = (i < u_limbs) ? static_cast<std::uint32_t>(uvalue >> (i * 32)) : fill;
  }

  [[nodiscard]] static constexpr bool unsigned_less(const wide_int &a, const wide_int &b) noexcept {
    for (std::size_t i = limb_count; i-- > 0;) {
      if (a.limbs_[i] != b.limbs_[i])
        return a.limbs_[i] < b.limbs_[i];
    }
    return false;
  }

  [[nodiscard]] static constexpr bool get_bit(const wide_int &v, std::size_t bit) noexcept {
    return ((v.limbs_[bit / 32] >> (bit % 32)) & 1U) != 0;
  }

  static constexpr void set_bit(wide_int &v, std::size_t bit) noexcept { v.limbs_[bit / 32] |= (1U << (bit % 32)); }

  /** @brief Unsigned (magnitude-only) long division via the classic shift-subtract bit-by-bit algorithm -- `O(N)`
   * iterations, each `O(limb_count)`. Dividing by zero yields an unspecified (not undefined, not trapping) result:
   * a software division has no hardware trap to defer to, unlike a native integer's own division-by-zero UB. */
  static constexpr void unsigned_divmod(const wide_int &dividend, const wide_int &divisor, wide_int &quotient,
                                         wide_int &remainder) noexcept {
    quotient = wide_int();
    remainder = wide_int();
    for (std::size_t i = N; i-- > 0;) {
      remainder = remainder << 1U;
      if (get_bit(dividend, i))
        set_bit(remainder, 0);
      if (!unsigned_less(remainder, divisor)) {
        remainder = remainder - divisor;
        set_bit(quotient, i);
      }
    }
  }

  array<std::uint32_t, limb_count> limbs_{};
};

} // namespace detail

/**
 * @brief An arbitrary fixed-precision integer, @p N bits wide (@p N must be a power of two, at least 8 bits). A
 * plain alias for a native integer type (or the `__int128`/`unsigned __int128` compiler extension) wherever the
 * target compiler offers one; `detail::wide_int<N, Signed>` otherwise. See the @file-level docs above.
 */
template <std::size_t N, bool Signed = true> struct fixed_int_t {
  static_assert(N >= 8 && (N & (N - 1)) == 0, "fixed_int<N, Signed>: N must be a power of two, at least 8 bits");

private:
  template <std::size_t M, bool S> struct select {
    using type = detail::wide_int<M, S>;
  };
  template <bool S> struct select<8, S> {
    using type = std::conditional_t<S, std::int8_t, std::uint8_t>;
  };
  template <bool S> struct select<16, S> {
    using type = std::conditional_t<S, std::int16_t, std::uint16_t>;
  };
  template <bool S> struct select<32, S> {
    using type = std::conditional_t<S, std::int32_t, std::uint32_t>;
  };
  template <bool S> struct select<64, S> {
    using type = std::conditional_t<S, std::int64_t, std::uint64_t>;
  };
#if RELOCO_HAS_INT128
  template <bool S> struct select<128, S> {
    using type = std::conditional_t<S, detail::int128_t, detail::uint128_t>;
  };
#endif

public:
  using type = typename select<N, Signed>::type;
};

template <std::size_t N, bool Signed = true> using fixed_int = typename fixed_int_t<N, Signed>::type;

/** @brief `fixed_int<N, false>`, spelled out -- an unsigned arbitrary fixed-precision integer. */
template <std::size_t N> using fixed_uint = fixed_int<N, false>;

} // namespace reloco
