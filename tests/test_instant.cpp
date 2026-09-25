// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/instant.hpp>
#include <reloco/park.hpp>

TEST(InstantTest, DefaultConstructedInstantsCompareEqual) {
  reloco::instant a;
  reloco::instant b;
  EXPECT_EQ(a, b);
  EXPECT_FALSE(a < b);
  EXPECT_FALSE(a > b);
  EXPECT_LE(a, b);
  EXPECT_GE(a, b);
}

TEST(InstantTest, DurationSinceZeroForEqualInstants) {
  reloco::instant a;
  EXPECT_EQ(a.duration_since(a), reloco::duration());
  EXPECT_TRUE(a.checked_duration_since(a).has_value());
  EXPECT_EQ(a.checked_duration_since(a).value(), reloco::duration());
}

TEST(InstantTest, AddDurationAdvancesAndCompareOrders) {
  reloco::instant a;
  reloco::instant b = a + reloco::duration::from_secs(5);
  EXPECT_GT(b, a);
  EXPECT_LT(a, b);
  EXPECT_NE(a, b);
  EXPECT_EQ(b.duration_since(a), reloco::duration::from_secs(5));
  EXPECT_EQ(b - a, reloco::duration::from_secs(5));
}

TEST(InstantTest, SubDurationMovesBackward) {
  reloco::instant a;
  reloco::instant b = a + reloco::duration::from_secs(10);
  reloco::instant c = b - reloco::duration::from_secs(4);
  EXPECT_EQ(c.duration_since(a), reloco::duration::from_secs(6));
}

TEST(InstantTest, DurationSinceSaturatesToZeroWhenEarlierIsLater) {
  reloco::instant a;
  reloco::instant b = a + reloco::duration::from_secs(1);
  // a.duration_since(b): b is later than a, so this "goes backward".
  EXPECT_EQ(a.duration_since(b), reloco::duration());
  EXPECT_EQ(a.saturating_duration_since(b), reloco::duration());
}

TEST(InstantTest, CheckedDurationSinceFailsWhenEarlierIsLater) {
  reloco::instant a;
  reloco::instant b = a + reloco::duration::from_secs(1);
  auto result = a.checked_duration_since(b);
  EXPECT_FALSE(result.has_value());
}

TEST(InstantTest, CheckedAddAlwaysSucceeds) {
  reloco::instant a;
  auto added = a.checked_add(reloco::duration::from_secs(3));
  ASSERT_TRUE(added.has_value());
  EXPECT_EQ(added.value().duration_since(a), reloco::duration::from_secs(3));
}

TEST(InstantTest, CheckedSubSucceedsWithinRangeAndFailsPastEpoch) {
  reloco::instant a;
  reloco::instant b = a + reloco::duration::from_secs(5);
  auto ok = b.checked_sub(reloco::duration::from_secs(2));
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok.value().duration_since(a), reloco::duration::from_secs(3));

  auto underflow = a.checked_sub(reloco::duration::from_secs(1));
  EXPECT_FALSE(underflow.has_value());
}

TEST(InstantTest, SubOperatorBetweenInstantsSaturatesLikeDurationSince) {
  reloco::instant a;
  reloco::instant b = a + reloco::duration::from_secs(2);
  EXPECT_EQ(b - a, reloco::duration::from_secs(2));
  EXPECT_EQ(a - b, reloco::duration());
}

TEST(InstantTest, SubtractionBorrowsAcrossSecondBoundary) {
  reloco::instant a;
  reloco::instant b = a + reloco::duration::from_nanos(500'000'000);
  reloco::instant c = b + reloco::duration::from_secs(1);
  // c - a should be 1.5s, and c - b should be exactly 1s, exercising the
  // sub-second borrow path in the underlying duration subtraction.
  EXPECT_EQ(c.duration_since(a), reloco::duration::from_millis(1'500));
  EXPECT_EQ(c.duration_since(b), reloco::duration::from_secs(1));
}

#if RELOCO_DETAIL_INSTANT_HAS_NOW

TEST(InstantTest, NowIsMonotonicallyNonDecreasing) {
  reloco::instant a = reloco::instant::now();
  reloco::instant b = reloco::instant::now();
  EXPECT_GE(b, a);
}

TEST(InstantTest, ElapsedGrowsAfterSleeping) {
  reloco::instant start = reloco::instant::now();
  reloco::this_thread::sleep_for(reloco::duration::from_millis(10));
  reloco::duration elapsed = start.elapsed();
  EXPECT_GE(elapsed, reloco::duration::from_millis(5));
}

#endif // RELOCO_DETAIL_INSTANT_HAS_NOW
