// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/checked.hpp>

#include <cstdint>
#include <functional>
#include <limits>

using reloco::checked;
using reloco::error;

TEST(CheckedTest, DefaultConstructedIsZero) {
  EXPECT_EQ(checked<int>().get(), 0);
}

TEST(CheckedTest, GetReturnsWrappedValue) {
  EXPECT_EQ(checked<int>(42).get(), 42);
}

TEST(CheckedTest, ImplicitlyConvertsToT) {
  const checked<int> value(7);
  const int plain = value;
  EXPECT_EQ(plain, 7);
}

TEST(CheckedTest, TryAddSucceedsWithinRange) {
  const auto result = checked<int>(2).try_add(checked<int>(3));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().get(), 5);
}

TEST(CheckedTest, TryAddFailsOnOverflow) {
  const auto result = checked<int32_t>(std::numeric_limits<int32_t>::max()).try_add(checked<int32_t>(1));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(CheckedTest, TrySubSucceedsWithinRange) {
  const auto result = checked<int>(5).try_sub(checked<int>(3));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().get(), 2);
}

TEST(CheckedTest, TrySubFailsOnUnsignedUnderflow) {
  const auto result = checked<uint8_t>(0).try_sub(checked<uint8_t>(1));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(CheckedTest, TryMulSucceedsWithinRange) {
  const auto result = checked<int>(6).try_mul(checked<int>(7));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().get(), 42);
}

TEST(CheckedTest, TryMulFailsOnOverflow) {
  const auto result = checked<int32_t>(std::numeric_limits<int32_t>::max()).try_mul(checked<int32_t>(2));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(CheckedTest, TryDivSucceedsWithinRange) {
  const auto result = checked<int>(10).try_div(checked<int>(3));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().get(), 3);
}

TEST(CheckedTest, TryDivFailsOnZeroDivisor) {
  const auto result = checked<int>(10).try_div(checked<int>(0));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::division_by_zero);
}

TEST(CheckedTest, TryDivFailsOnMinDividedByNegativeOne) {
  const auto result =
      checked<int32_t>(std::numeric_limits<int32_t>::min()).try_div(checked<int32_t>(-1));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(CheckedTest, TryRemSucceedsWithinRange) {
  const auto result = checked<int>(10).try_rem(checked<int>(3));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().get(), 1);
}

TEST(CheckedTest, TryRemFailsOnZeroDivisor) {
  const auto result = checked<int>(10).try_rem(checked<int>(0));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::division_by_zero);
}

TEST(CheckedTest, TryNegSucceedsForOrdinaryValue) {
  const auto result = checked<int>(5).try_neg();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().get(), -5);
}

TEST(CheckedTest, TryNegFailsForSignedMin) {
  const auto result = checked<int32_t>(std::numeric_limits<int32_t>::min()).try_neg();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(CheckedTest, TryNegFailsForNonzeroUnsigned) {
  const auto result = checked<unsigned>(5).try_neg();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(CheckedTest, TryAbsSucceedsForOrdinaryValues) {
  EXPECT_EQ(checked<int>(-5).try_abs().value().get(), 5);
  EXPECT_EQ(checked<int>(5).try_abs().value().get(), 5);
}

TEST(CheckedTest, TryAbsFailsForSignedMin) {
  const auto result = checked<int32_t>(std::numeric_limits<int32_t>::min()).try_abs();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(CheckedTest, ChainedTryOperationsPropagateFailure) {
  const auto result = checked<int32_t>(std::numeric_limits<int32_t>::max())
                           .try_add(checked<int32_t>(1))
                           .and_then([](checked<int32_t> value) { return value.try_mul(checked<int32_t>(2)); });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(CheckedTest, ComparisonOperators) {
  EXPECT_TRUE(checked<int>(3) == checked<int>(3));
  EXPECT_TRUE(checked<int>(3) != checked<int>(4));
  EXPECT_TRUE(checked<int>(3) < checked<int>(4));
  EXPECT_TRUE(checked<int>(4) > checked<int>(3));
  EXPECT_TRUE(checked<int>(3) <= checked<int>(3));
  EXPECT_TRUE(checked<int>(3) >= checked<int>(3));
}

TEST(CheckedTest, HashSpecializationMatchesUnderlyingValue) {
  EXPECT_EQ(std::hash<checked<int>>{}(checked<int>(42)), std::hash<int>{}(42));
}

TEST(CheckedTest, GetIsConstexprEvaluable) {
  constexpr checked<int> value(42);
  static_assert(value.get() == 42);
}
