// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/fixed_point.hpp>

#include <cstdint>

using reloco::fixed_point;

namespace {

using q21_11 = fixed_point<std::uint32_t, 11>;
using q20_11_signed = fixed_point<std::int32_t, 11>;

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
