// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Exercises fault_injection_patterns.hpp's macros -- see
// test_fault_injection.cpp's own file-level comment for why
// RELOCO_ENABLE_FAULT_INJECTION is defined locally here.
#define RELOCO_ENABLE_FAULT_INJECTION

#include <gtest/gtest.h>
#include <reloco/fault_injection_patterns.hpp>

#include <cstdint>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

RELOCO_FAULT_TAG(mutate_pattern_point);
RELOCO_FAULT_TAG(set_pattern_point);
RELOCO_FAULT_TAG(spy_pattern_point);
RELOCO_FAULT_TAG(fire_n_pattern_point);
RELOCO_FAULT_TAG(fire_once_pattern_point);
RELOCO_FAULT_TAG(when_pattern_point);
RELOCO_FAULT_TAG(skip_n_pattern_point);
RELOCO_FAULT_TAG(nth_pattern_point);
RELOCO_FAULT_TAG(every_n_pattern_point);
RELOCO_FAULT_TAG(toggle_pattern_point);
RELOCO_FAULT_TAG(increment_pattern_point);
RELOCO_FAULT_TAG(xor_pattern_point);

} // namespace

TEST(FaultInjectionPatternsTest, MutateRunsArbitraryBodyOnExposedValue) {
  RELOCO_FAULT_MUTATE(fi, mutate_pattern_point, int, reloco_fault_value += 1000);

  int x = 5;
  RELOCO_FAULT_POINT_ARGS(mutate_pattern_point, x);
  EXPECT_EQ(1005, x);
}

TEST(FaultInjectionPatternsTest, SetOverwritesUnconditionally) {
  RELOCO_FAULT_SET(fi, set_pattern_point, std::uint64_t, 0xDEADBEEFu);

  std::uint64_t w = 42;
  RELOCO_FAULT_POINT_ARGS(set_pattern_point, w);
  EXPECT_EQ(0xDEADBEEFu, w);
}

TEST(FaultInjectionPatternsTest, SpyCountsFiringsWithoutMutating) {
  int hits = 0;
  RELOCO_FAULT_SPY(fi, spy_pattern_point, hits);

  RELOCO_FAULT_POINT(spy_pattern_point);
  RELOCO_FAULT_POINT(spy_pattern_point);
  RELOCO_FAULT_POINT(spy_pattern_point);
  EXPECT_EQ(3, hits);
}

TEST(FaultInjectionPatternsTest, FireNOnlyFiresFirstCountTimes) {
  int fires = 0;
  RELOCO_FAULT_FIRE_N(fi, fire_n_pattern_point, 2, [&](int &x) {
    ++fires;
    x = -1;
  });

  int a = 1;
  RELOCO_FAULT_POINT_ARGS(fire_n_pattern_point, a);
  EXPECT_EQ(-1, a);

  int b = 2;
  RELOCO_FAULT_POINT_ARGS(fire_n_pattern_point, b);
  EXPECT_EQ(-1, b);

  int c = 3;
  RELOCO_FAULT_POINT_ARGS(fire_n_pattern_point, c);
  EXPECT_EQ(3, c); // third hit: countdown exhausted, hook no longer fires

  EXPECT_EQ(2, fires);
  EXPECT_EQ(0u, fi_countdown.remaining());
}

TEST(FaultInjectionPatternsTest, FireOnceFiresExactlyOnce) {
  int fires = 0;
  RELOCO_FAULT_FIRE_ONCE(fi, fire_once_pattern_point, [&] { ++fires; });

  RELOCO_FAULT_POINT(fire_once_pattern_point);
  RELOCO_FAULT_POINT(fire_once_pattern_point);
  RELOCO_FAULT_POINT(fire_once_pattern_point);
  EXPECT_EQ(1, fires);
}

TEST(FaultInjectionPatternsTest, WhenOnlyFiresIfPredicateHolds) {
  RELOCO_FAULT_WHEN(fi, when_pattern_point, [](int &idx) { return idx > 100; }, [](int &idx) { idx = -1; });

  int low = 5;
  RELOCO_FAULT_POINT_ARGS(when_pattern_point, low);
  EXPECT_EQ(5, low); // predicate false: hook did not fire

  int high = 200;
  RELOCO_FAULT_POINT_ARGS(when_pattern_point, high);
  EXPECT_EQ(-1, high); // predicate true: hook fired
}

TEST(FaultInjectionPatternsTest, SkipNSkipsFirstCountThenFiresEveryTimeAfter) {
  RELOCO_FAULT_SKIP_N(fi, skip_n_pattern_point, 2, [](int &v) { v = -1; });

  int a = 1;
  RELOCO_FAULT_POINT_ARGS(skip_n_pattern_point, a);
  EXPECT_EQ(1, a); // skipped

  int b = 2;
  RELOCO_FAULT_POINT_ARGS(skip_n_pattern_point, b);
  EXPECT_EQ(2, b); // skipped

  int c = 3;
  RELOCO_FAULT_POINT_ARGS(skip_n_pattern_point, c);
  EXPECT_EQ(-1, c); // 3rd hit: warm-up done, fires

  int d = 4;
  RELOCO_FAULT_POINT_ARGS(skip_n_pattern_point, d);
  EXPECT_EQ(-1, d); // keeps firing afterwards

  EXPECT_EQ(0u, fi_skip.remaining_skip());
}

TEST(FaultInjectionPatternsTest, NthFiresOnlyOnExactHit) {
  RELOCO_FAULT_NTH(fi, nth_pattern_point, 2, [](int &v) { v = -1; });

  int a = 1;
  RELOCO_FAULT_POINT_ARGS(nth_pattern_point, a);
  EXPECT_EQ(1, a);

  int b = 2;
  RELOCO_FAULT_POINT_ARGS(nth_pattern_point, b);
  EXPECT_EQ(-1, b); // exactly the 2nd hit

  int c = 3;
  RELOCO_FAULT_POINT_ARGS(nth_pattern_point, c);
  EXPECT_EQ(3, c); // 3rd hit: never fires again

  EXPECT_EQ(3u, fi_nth.call_count());
}

TEST(FaultInjectionPatternsTest, EveryNFiresPeriodically) {
  RELOCO_FAULT_EVERY_N(fi, every_n_pattern_point, 2, [](int &v) { v = -1; });

  int values[4] = {1, 2, 3, 4};
  for (int &v : values)
    RELOCO_FAULT_POINT_ARGS(every_n_pattern_point, v);

  EXPECT_EQ(1, values[0]);
  EXPECT_EQ(-1, values[1]); // 2nd hit
  EXPECT_EQ(3, values[2]);
  EXPECT_EQ(-1, values[3]); // 4th hit

  EXPECT_EQ(4u, fi_period.call_count());
}

TEST(FaultInjectionPatternsTest, ToggleFlipsExposedValue) {
  RELOCO_FAULT_TOGGLE(fi, toggle_pattern_point, bool);

  bool flag = false;
  RELOCO_FAULT_POINT_ARGS(toggle_pattern_point, flag);
  EXPECT_TRUE(flag);
  RELOCO_FAULT_POINT_ARGS(toggle_pattern_point, flag);
  EXPECT_FALSE(flag);
}

TEST(FaultInjectionPatternsTest, IncrementNudgesExposedValue) {
  RELOCO_FAULT_INCREMENT(fi, increment_pattern_point, int, -1);

  int idx = 10;
  RELOCO_FAULT_POINT_ARGS(increment_pattern_point, idx);
  EXPECT_EQ(9, idx);
  RELOCO_FAULT_POINT_ARGS(increment_pattern_point, idx);
  EXPECT_EQ(8, idx);
}

TEST(FaultInjectionPatternsTest, XorFlipsMaskedBits) {
  RELOCO_FAULT_XOR(fi, xor_pattern_point, std::uint64_t, std::uint64_t{1} << 63);

  std::uint64_t v = 0;
  RELOCO_FAULT_POINT_ARGS(xor_pattern_point, v);
  EXPECT_EQ(std::uint64_t{1} << 63, v);
  RELOCO_FAULT_POINT_ARGS(xor_pattern_point, v);
  EXPECT_EQ(0u, v); // flipping the same bit again restores it
}

RELOCO_END_UNSAFE_BUFFER_USAGE
