// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/int_ops.hpp>

#include <cstdint>
#include <limits>

using reloco::checked_abs;
using reloco::checked_add;
using reloco::checked_cast;
using reloco::checked_div;
using reloco::checked_mul;
using reloco::checked_neg;
using reloco::checked_rem;
using reloco::checked_sub;
using reloco::error;
using reloco::overflowing_add;
using reloco::overflowing_mul;
using reloco::overflowing_sub;
using reloco::saturating_add;
using reloco::saturating_mul;
using reloco::saturating_sub;
using reloco::wrapping_add;
using reloco::wrapping_mul;
using reloco::wrapping_sub;

TEST(IntOpsTest, CheckedAddSucceedsWithinRange) {
  const auto result = checked_add<int>(2, 3);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 5);
}

TEST(IntOpsTest, CheckedAddFailsOnSignedOverflow) {
  const auto result = checked_add<int32_t>(std::numeric_limits<int32_t>::max(), 1);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedAddFailsOnSignedUnderflow) {
  const auto result = checked_add<int32_t>(std::numeric_limits<int32_t>::min(), -1);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedAddFailsOnUnsignedOverflow) {
  const auto result = checked_add<uint8_t>(255, 1);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedSubFailsOnUnsignedUnderflow) {
  const auto result = checked_sub<uint8_t>(0, 1);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedSubSucceedsWithinRange) {
  const auto result = checked_sub<int>(5, 3);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 2);
}

TEST(IntOpsTest, CheckedMulFailsOnOverflow) {
  const auto result = checked_mul<int32_t>(std::numeric_limits<int32_t>::max(), 2);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedMulSucceedsWithinRange) {
  const auto result = checked_mul<int>(6, 7);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 42);
}

TEST(IntOpsTest, WrappingAddWrapsUnsigned) {
  EXPECT_EQ(wrapping_add<uint8_t>(255, 1), 0);
}

TEST(IntOpsTest, WrappingSubWrapsUnsigned) {
  EXPECT_EQ(wrapping_sub<uint8_t>(0, 1), 255);
}

TEST(IntOpsTest, WrappingMulWraps) {
  EXPECT_EQ(wrapping_mul<uint8_t>(200, 2), static_cast<uint8_t>(400));
}

TEST(IntOpsTest, SaturatingAddClampsToMax) {
  EXPECT_EQ(saturating_add<uint8_t>(255, 10), 255);
}

TEST(IntOpsTest, SaturatingSubClampsToMin) {
  EXPECT_EQ(saturating_sub<uint8_t>(0, 10), 0);
}

TEST(IntOpsTest, SaturatingMulClampsSignedToMaxOrMin) {
  EXPECT_EQ(saturating_mul<int8_t>(100, 2), std::numeric_limits<int8_t>::max());
  EXPECT_EQ(saturating_mul<int8_t>(-100, 2), std::numeric_limits<int8_t>::min());
}

TEST(IntOpsTest, OverflowingAddReportsOverflow) {
  const auto result = overflowing_add<uint8_t>(255, 1);
  EXPECT_EQ(result.value, 0);
  EXPECT_TRUE(result.overflowed);
}

TEST(IntOpsTest, OverflowingSubReportsNoOverflowWithinRange) {
  const auto result = overflowing_sub<int>(5, 3);
  EXPECT_EQ(result.value, 2);
  EXPECT_FALSE(result.overflowed);
}

TEST(IntOpsTest, OverflowingMulReportsOverflow) {
  const auto result = overflowing_mul<int32_t>(std::numeric_limits<int32_t>::max(), 2);
  EXPECT_TRUE(result.overflowed);
}

TEST(IntOpsTest, CheckedDivSucceedsWithinRange) {
  const auto result = checked_div<int>(10, 3);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 3);
}

TEST(IntOpsTest, CheckedDivFailsOnZeroDivisor) {
  const auto result = checked_div<int>(10, 0);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::division_by_zero);
}

TEST(IntOpsTest, CheckedDivFailsOnMinDividedByNegativeOne) {
  const auto result = checked_div<int32_t>(std::numeric_limits<int32_t>::min(), -1);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedDivWorksForUnsigned) {
  const auto result = checked_div<unsigned>(10, 4);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 2);
}

TEST(IntOpsTest, CheckedRemSucceedsWithinRange) {
  const auto result = checked_rem<int>(10, 3);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 1);
}

TEST(IntOpsTest, CheckedRemFailsOnZeroDivisor) {
  const auto result = checked_rem<int>(10, 0);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::division_by_zero);
}

TEST(IntOpsTest, CheckedRemFailsOnMinModuloNegativeOne) {
  const auto result = checked_rem<int32_t>(std::numeric_limits<int32_t>::min(), -1);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedNegSucceedsForOrdinarySignedValue) {
  const auto result = checked_neg<int>(5);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), -5);
}

TEST(IntOpsTest, CheckedNegFailsForSignedMin) {
  const auto result = checked_neg<int32_t>(std::numeric_limits<int32_t>::min());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedNegSucceedsForZeroUnsigned) {
  const auto result = checked_neg<unsigned>(0);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 0U);
}

TEST(IntOpsTest, CheckedNegFailsForNonzeroUnsigned) {
  const auto result = checked_neg<unsigned>(5);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedAbsSucceedsForOrdinaryValues) {
  EXPECT_EQ(checked_abs<int>(-5).value(), 5);
  EXPECT_EQ(checked_abs<int>(5).value(), 5);
}

TEST(IntOpsTest, CheckedAbsFailsForSignedMin) {
  const auto result = checked_abs<int32_t>(std::numeric_limits<int32_t>::min());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedCastSignedToSignedNarrowingSucceeds) {
  const auto result = checked_cast<int8_t>(int32_t{100});
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 100);
}

TEST(IntOpsTest, CheckedCastSignedToSignedNarrowingFails) {
  const auto result = checked_cast<int8_t>(int32_t{200});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedCastSignedToSignedWideningAlwaysSucceeds) {
  const auto result = checked_cast<int64_t>(int8_t{-100});
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), -100);
}

TEST(IntOpsTest, CheckedCastUnsignedToUnsignedNarrowingFails) {
  const auto result = checked_cast<uint8_t>(uint32_t{300});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedCastUnsignedToUnsignedWideningAlwaysSucceeds) {
  const auto result = checked_cast<uint64_t>(uint8_t{200});
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 200U);
}

TEST(IntOpsTest, CheckedCastSignedToUnsignedFailsForNegative) {
  const auto result = checked_cast<uint32_t>(int32_t{-1});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedCastSignedToUnsignedFailsWhenTooLarge) {
  const auto result = checked_cast<uint8_t>(int32_t{300});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedCastSignedToUnsignedSucceedsWithinRange) {
  const auto result = checked_cast<uint8_t>(int32_t{200});
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 200U);
}

TEST(IntOpsTest, CheckedCastUnsignedToSignedFailsWhenTooLarge) {
  const auto result = checked_cast<int8_t>(uint32_t{200});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::integer_overflow);
}

TEST(IntOpsTest, CheckedCastUnsignedToSignedSucceedsWithinRange) {
  const auto result = checked_cast<int32_t>(uint8_t{200});
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 200);
}

TEST(IntOpsTest, CheckedCastSameTypeAlwaysSucceeds) {
  const auto result = checked_cast<int>(int{42});
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 42);
}

// Note: `checked_add`/`checked_cast`/... are marked `constexpr` (always
// legal for a function), but `result<T>` (`expected<T, error>`) is not a
// C++17 literal type (its placement-new-based internals aren't
// `constexpr`-eligible until C++20), so a call through them can't
// actually be *evaluated* as a constant expression in C++17 mode -- see
// `wrapping.hpp`/`saturating.hpp` and `docs/reference.md` for the same
// caveat.
