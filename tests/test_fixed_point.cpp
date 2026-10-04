// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/fixed_point.hpp>
#include <reloco/span.hpp>

#include <cstdint>

using reloco::fixed_point;
using reloco::span;
using reloco::taylor_eval;

namespace {

using q21_11 = fixed_point<std::uint32_t, 11>;
using q20_11_signed = fixed_point<std::int32_t, 11>;

std::uint32_t abs_diff(std::uint32_t a, std::uint32_t b) noexcept { return a > b ? a - b : b - a; }

std::int32_t abs_diff(std::int32_t a, std::int32_t b) noexcept { return a > b ? a - b : b - a; }

} // namespace

TEST(FixedPointTest, OneRawMatchesClassicUnixFixedPointConstant) {
  // Matches Linux's own FIXED_1 == 1 << FSHIFT(11) for calc_load().
  EXPECT_EQ(q21_11::one_raw, 2048U);
}

TEST(FixedPointTest, FromIntRoundTripsThroughToInt) {
  auto v = q21_11::from_int(5);
  EXPECT_EQ(v.to_int(), 5U);
  EXPECT_EQ(v.raw(), 5U * q21_11::one_raw);
}

TEST(FixedPointTest, FromRawRoundTrips) {
  auto v = q21_11::from_raw(12345);
  EXPECT_EQ(v.raw(), 12345U);
}

TEST(FixedPointTest, AddSubtract) {
  auto a = q21_11::from_int(3);
  auto b = q21_11::from_int(2);
  EXPECT_EQ((a + b).to_int(), 5U);
  EXPECT_EQ((a - b).to_int(), 1U);
}

TEST(FixedPointTest, MultiplyWholeNumbers) {
  auto a = q21_11::from_int(3);
  auto b = q21_11::from_int(4);
  EXPECT_EQ((a * b).to_int(), 12U);
}

TEST(FixedPointTest, MultiplyFractional) {
  // 0.5 * 0.5 == 0.25
  auto half = q21_11::from_raw(q21_11::one_raw / 2);
  auto quarter = half * half;
  EXPECT_EQ(quarter.raw(), q21_11::one_raw / 4);
}

TEST(FixedPointTest, DivideWholeNumbers) {
  auto a = q21_11::from_int(10);
  auto b = q21_11::from_int(4);
  auto result = a / b;
  EXPECT_EQ(result.to_int(), 2U); // truncated toward zero: 2.5 -> 2
  EXPECT_EQ(result.fractional_percent(), 50U);
}

TEST(FixedPointTest, FractionalPercentWholeNumber) { EXPECT_EQ(q21_11::from_int(7).fractional_percent(), 0U); }

TEST(FixedPointTest, FractionalPercentRoundsNearestFraction) {
  // 0.92 rounds to 92%, matching Linux's EXP_1 == 1884/2048 decay constant.
  auto v = q21_11::from_raw(1884);
  EXPECT_EQ(v.fractional_percent(), 92U);
}

TEST(FixedPointTest, PowZeroExponentIsOne) {
  auto v = q21_11::from_raw(1884);
  auto result = q21_11::pow(v, 0);
  EXPECT_EQ(result.raw(), q21_11::one_raw);
}

TEST(FixedPointTest, PowOneExponentIsBase) {
  auto v = q21_11::from_raw(1884);
  auto result = q21_11::pow(v, 1);
  EXPECT_EQ(result.raw(), v.raw());
}

TEST(FixedPointTest, PowMatchesRepeatedMultiplication) {
  auto v = q21_11::from_raw(1884); // ~0.92
  auto repeated = q21_11::from_int(1);
  for (int i = 0; i < 5; ++i)
    repeated = repeated * v;
  auto squared = q21_11::pow(v, 5);
  EXPECT_EQ(squared.raw(), repeated.raw());
}

TEST(FixedPointTest, PowDecaysTowardZeroForManySteps) {
  auto v = q21_11::from_raw(1884); // ~0.92 per step
  auto result = q21_11::pow(v, 1000);
  EXPECT_LT(result.raw(), 10U); // effectively decayed away
}

TEST(FixedPointTest, ComparisonOperators) {
  auto a = q21_11::from_int(1);
  auto b = q21_11::from_int(2);
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(b > a);
  EXPECT_TRUE(a <= a);
  EXPECT_TRUE(a >= a);
  EXPECT_TRUE(a == a);
  EXPECT_TRUE(a != b);
}

TEST(FixedPointTest, CompoundAssignmentOperators) {
  auto v = q21_11::from_int(2);
  v += q21_11::from_int(3);
  EXPECT_EQ(v.to_int(), 5U);
  v -= q21_11::from_int(1);
  EXPECT_EQ(v.to_int(), 4U);
  v *= q21_11::from_int(2);
  EXPECT_EQ(v.to_int(), 8U);
  v /= q21_11::from_int(4);
  EXPECT_EQ(v.to_int(), 2U);
}

TEST(FixedPointTest, SignedRepSupportsNegativeValues) {
  auto a = q20_11_signed::from_int(-3);
  auto b = q20_11_signed::from_int(2);
  EXPECT_EQ((a + b).to_int(), -1);
  EXPECT_LT(a, b);
}

TEST(FixedPointTest, ConstexprUsageCompiles) {
  constexpr auto a = q21_11::from_int(2);
  constexpr auto b = q21_11::from_int(3);
  constexpr auto sum = a + b;
  static_assert(sum.to_int() == 5U, "constexpr fixed_point addition must be usable at compile time");
  constexpr auto p = q21_11::pow(q21_11::from_raw(1884), 2);
  static_assert(p.raw() > 0, "constexpr fixed_point pow must be usable at compile time");
}

TEST(FixedPointTest, SqrtOfPerfectSquare) {
  auto v = q21_11::from_int(16).sqrt();
  EXPECT_EQ(v.to_int(), 4U);
}

TEST(FixedPointTest, SqrtOfNonPerfectSquareIsExactFloor) {
  // sqrt(2) ~= 1.41421356..., scaled by one_raw(2048) and floored == 2896.
  auto v = q21_11::from_int(2).sqrt();
  EXPECT_EQ(v.raw(), 2896U);
}

TEST(FixedPointTest, SqrtOfZeroIsZero) { EXPECT_EQ(q21_11::from_int(0).sqrt().raw(), 0U); }

TEST(FixedPointTest, SqrtOfNegativeIsZero) {
  auto v = q20_11_signed::from_int(-4).sqrt();
  EXPECT_EQ(v.raw(), 0);
}

TEST(FixedPointTest, SqrtIsConstexpr) {
  constexpr auto v = q21_11::from_int(25).sqrt();
  static_assert(v.to_int() == 5U, "constexpr fixed_point sqrt must be usable at compile time");
}

TEST(FixedPointTest, ExpOfZeroIsExactlyOne) {
  auto v = q21_11::from_int(0).exp();
  EXPECT_EQ(v.raw(), q21_11::one_raw);
}

TEST(FixedPointTest, ExpOfOneApproximatesE) {
  // e ~= 2.718281828..., scaled by one_raw(2048) ~= 5566.9.
  auto v = q21_11::from_int(1).exp();
  EXPECT_LE(abs_diff(v.raw(), 5567U), 4U);
}

TEST(FixedPointTest, ExpOfNegativeOneApproximatesInverseE) {
  // 1/e ~= 0.367879441..., scaled by one_raw(2048) ~= 753.5.
  auto v = q20_11_signed::from_int(-1).exp();
  EXPECT_LE(abs_diff(v.raw(), 753), 4);
}

TEST(FixedPointTest, ExpWithRangeReductionApproximatesLargerValue) {
  // e^3 ~= 20.0855..., scaled by one_raw(2048) ~= 41135.
  auto v = q21_11::from_int(3).exp();
  EXPECT_LE(abs_diff(v.raw(), 41135U), 200U);
}

TEST(FixedPointTest, ExpIsConstexpr) {
  constexpr auto v = q21_11::from_int(0).exp();
  static_assert(v.raw() == q21_11::one_raw, "constexpr fixed_point exp must be usable at compile time");
}

TEST(FixedPointTest, TaylorEvalEmptyCoefficientsIsZero) {
  q21_11 coefficients[1];
  span<const q21_11> empty(coefficients, static_cast<std::size_t>(0));
  EXPECT_EQ(taylor_eval(empty, q21_11::from_int(5)).raw(), 0U);
}

TEST(FixedPointTest, TaylorEvalConstantIgnoresX) {
  q21_11 coefficients[] = {q21_11::from_int(7)};
  auto result_a = taylor_eval(span<const q21_11>(coefficients), q21_11::from_int(0));
  auto result_b = taylor_eval(span<const q21_11>(coefficients), q21_11::from_int(100));
  EXPECT_EQ(result_a.to_int(), 7U);
  EXPECT_EQ(result_b.to_int(), 7U);
}

TEST(FixedPointTest, TaylorEvalLinear) {
  // 2 + 3*x, at x = 4 -> 14
  q21_11 coefficients[] = {q21_11::from_int(2), q21_11::from_int(3)};
  auto result = taylor_eval(span<const q21_11>(coefficients), q21_11::from_int(4));
  EXPECT_EQ(result.to_int(), 14U);
}

TEST(FixedPointTest, TaylorEvalMatchesManualHornerForCubic) {
  // 1 + 2*x + 3*x^2 + 4*x^3, at x = 2 -> 1 + 4 + 12 + 32 == 49
  q21_11 coefficients[] = {q21_11::from_int(1), q21_11::from_int(2), q21_11::from_int(3), q21_11::from_int(4)};
  auto x = q21_11::from_int(2);
  auto result = taylor_eval(span<const q21_11>(coefficients), x);
  EXPECT_EQ(result.to_int(), 49U);
}

TEST(FixedPointTest, TaylorEvalIsConstexpr) {
  constexpr q21_11 coefficients[] = {q21_11::from_int(1), q21_11::from_int(1)};
  constexpr auto result = taylor_eval(span<const q21_11>(coefficients), q21_11::from_int(3));
  static_assert(result.to_int() == 4U, "constexpr taylor_eval must be usable at compile time");
}
