// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/duration.hpp>

TEST(DurationTest, FromSecsRoundTrips) {
  auto d = reloco::duration::from_secs(5);
  EXPECT_EQ(d.as_secs(), 5u);
  EXPECT_EQ(d.subsec_nanos(), 0u);
}

TEST(DurationTest, FromMillisSplitsSecsAndNanos) {
  auto d = reloco::duration::from_millis(1'500);
  EXPECT_EQ(d.as_secs(), 1u);
  EXPECT_EQ(d.subsec_nanos(), 500'000'000u);
  EXPECT_EQ(d.subsec_millis(), 500u);
  EXPECT_EQ(d.as_millis(), 1'500u);
}

TEST(DurationTest, FromMicrosSplitsSecsAndNanos) {
  auto d = reloco::duration::from_micros(2'250'000);
  EXPECT_EQ(d.as_secs(), 2u);
  EXPECT_EQ(d.subsec_nanos(), 250'000'000u);
  EXPECT_EQ(d.subsec_micros(), 250'000u);
  EXPECT_EQ(d.as_micros(), 2'250'000u);
}

TEST(DurationTest, FromNanosSplitsSecsAndNanos) {
  auto d = reloco::duration::from_nanos(3'000'000'123);
  EXPECT_EQ(d.as_secs(), 3u);
  EXPECT_EQ(d.subsec_nanos(), 123u);
  EXPECT_EQ(d.as_nanos(), 3'000'000'123u);
}

TEST(DurationTest, IsZero) {
  EXPECT_TRUE(reloco::duration().is_zero());
  EXPECT_TRUE(reloco::duration::from_secs(0).is_zero());
  EXPECT_FALSE(reloco::duration::from_nanos(1).is_zero());
}

TEST(DurationTest, ComparisonOperators) {
  auto a = reloco::duration::from_millis(100);
  auto b = reloco::duration::from_millis(200);
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(b > a);
  EXPECT_TRUE(b >= a);
  EXPECT_TRUE(a == reloco::duration::from_millis(100));
  EXPECT_TRUE(a != b);
  EXPECT_FALSE(a > b);
}

TEST(DurationTest, AdditionCarriesNanosOverflowIntoSecs) {
  auto a = reloco::duration::from_millis(700);
  auto b = reloco::duration::from_millis(500);
  auto c = a + b;
  EXPECT_EQ(c.as_secs(), 1u);
  EXPECT_EQ(c.subsec_nanos(), 200'000'000u);
}

TEST(DurationTest, ConstexprUsable) {
  constexpr auto d = reloco::duration::from_secs(1);
  static_assert(d.as_secs() == 1, "constexpr from_secs");
  SUCCEED();
}

#if RELOCO_DETAIL_DURATION_HAS_TIMESPEC

TEST(DurationTest, ConvertsToTimespec) {
  auto d = reloco::duration::from_nanos(2'000'000'042);
  auto ts = reloco::duration_cast<struct timespec>(d);
  EXPECT_EQ(ts.tv_sec, 2);
  EXPECT_EQ(ts.tv_nsec, 42);
}

#endif

#if RELOCO_DETAIL_DURATION_HAS_TIMEVAL

TEST(DurationTest, ConvertsToTimevalTruncatingSubMicros) {
  auto d = reloco::duration::from_nanos(3'000'042'999);
  auto tv = reloco::duration_cast<struct timeval>(d);
  EXPECT_EQ(tv.tv_sec, 3);
  EXPECT_EQ(tv.tv_usec, 42);
}

#endif
