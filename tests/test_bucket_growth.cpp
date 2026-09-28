// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/bucket_growth.hpp>

TEST(BucketGrowthTest, LinearIgnoresCurrentBucketsAndClamps) {
  reloco::bucket_growth::linear strategy;
  // Tight fit: ceil(10 / 4) == 3.
  EXPECT_EQ(strategy.next_bucket_count(1000, 10, 4, 1, 1000), 3);
  // current_buckets is ignored entirely.
  EXPECT_EQ(strategy.next_bucket_count(0, 10, 4, 1, 1000), 3);
  // Clamped up to min_buckets.
  EXPECT_EQ(strategy.next_bucket_count(0, 1, 1, 8, 64), 8);
  // Clamped down to max_buckets.
  EXPECT_EQ(strategy.next_bucket_count(0, 1000, 1, 1, 16), 16);
}

TEST(BucketGrowthTest, DoublingThenRatioDoublesBelowKnee) {
  reloco::bucket_growth::doubling_then_ratio strategy{/*knee_buckets=*/1024, /*ratio_numerator=*/5,
                                                       /*ratio_denominator=*/4};
  // Starting from 0 (empty table): 1 -> 2 -> 4 -> 8 -> 16 to reach >= 10.
  EXPECT_EQ(strategy.next_bucket_count(0, 10, 1, 1, 1'000'000), 16);
  // Already big enough: no growth needed.
  EXPECT_EQ(strategy.next_bucket_count(32, 10, 1, 1, 1'000'000), 32);
  // Clamped to max_buckets even if the curve would go higher.
  EXPECT_EQ(strategy.next_bucket_count(0, 1000, 1, 1, 20), 20);
}

TEST(BucketGrowthTest, DoublingThenRatioSlowsDownAboveKnee) {
  reloco::bucket_growth::doubling_then_ratio strategy{/*knee_buckets=*/8, /*ratio_numerator=*/5,
                                                       /*ratio_denominator=*/4};
  // current_buckets (8) is already at the knee: growth should use the 5/4
  // ratio, not doubling, to reach a target just above it.
  std::size_t result = strategy.next_bucket_count(8, 9, 1, 1, 1'000'000);
  EXPECT_GE(result, 9u);
  EXPECT_LT(result, 16u); // must NOT have doubled to 16
}

TEST(BucketGrowthTest, FixedRatioGrowsByConfiguredRatioOnly) {
  reloco::bucket_growth::fixed_ratio strategy{/*ratio_numerator=*/3, /*ratio_denominator=*/2};
  // 4 -> 6 -> 9 -> 14 (ceil(4*3/2)=6, ceil(6*3/2)=9, ceil(9*3/2)=14) to reach >= 10.
  EXPECT_EQ(strategy.next_bucket_count(4, 10, 1, 1, 1'000'000), 14);
}

TEST(BucketGrowthTest, SqrtCurveGrowsSlowerThanLinear) {
  reloco::bucket_growth::sqrt_curve strategy;
  reloco::bucket_growth::linear linear_strategy;

  std::size_t sqrt_result = strategy.next_bucket_count(0, 10000, 1, 1, 1'000'000);
  std::size_t linear_result = linear_strategy.next_bucket_count(0, 10000, 1, 1, 1'000'000);
  EXPECT_EQ(sqrt_result, 100u); // floor(sqrt(10000)) == 100
  EXPECT_LT(sqrt_result, linear_result);
}

TEST(BucketGrowthTest, PowerOfTwoRoundsUpAndClamps) {
  reloco::bucket_growth::power_of_two strategy;
  // ceil(10 / 1) == 10, next power of two >= 10 is 16.
  EXPECT_EQ(strategy.next_bucket_count(0, 10, 1, 1, 1'000'000), 16u);
  // Already a power of two: stays put.
  EXPECT_EQ(strategy.next_bucket_count(0, 8, 1, 1, 1'000'000), 8u);
  // current_buckets is ignored entirely.
  EXPECT_EQ(strategy.next_bucket_count(1'000'000, 10, 1, 1, 1'000'000), 16u);
  // Clamped down to max_buckets even if the next power of two is bigger.
  EXPECT_EQ(strategy.next_bucket_count(0, 1000, 1, 1, 20), 20u);
  // n <= 1 rounds up to 1.
  EXPECT_EQ(strategy.next_bucket_count(0, 0, 1, 1, 1'000'000), 1u);
}

namespace {
bool is_prime_reference(std::size_t n) {
  if (n < 2)
    return false;
  for (std::size_t i = 2; i * i <= n; ++i) {
    if (n % i == 0)
      return false;
  }
  return true;
}
} // namespace

TEST(BucketGrowthTest, PrimeGrowthRoundsUpToAnActualPrimeAndClamps) {
  reloco::bucket_growth::prime_growth strategy;
  for (std::size_t target : {1u, 2u, 4u, 10u, 100u, 997u, 1000u, 7919u}) {
    std::size_t result = strategy.next_bucket_count(0, target, 1, 1, 1'000'000);
    EXPECT_TRUE(is_prime_reference(result)) << "result=" << result << " target=" << target;
    EXPECT_GE(result, target);
  }
  // current_buckets is ignored entirely.
  EXPECT_EQ(strategy.next_bucket_count(0, 10, 1, 1, 1'000'000), strategy.next_bucket_count(999, 10, 1, 1, 1'000'000));
  // Clamped down to max_buckets even if the next prime is bigger.
  EXPECT_EQ(strategy.next_bucket_count(0, 1000, 1, 1, 20), 20u);
}

TEST(BucketGrowthTest, ChunkedRoundsUpToNextMultipleOfChunkSizeAndClamps) {
  reloco::bucket_growth::chunked strategy{/*chunk_size=*/64};
  // ceil(100 / 1) == 100, next multiple of 64 >= 100 is 128.
  EXPECT_EQ(strategy.next_bucket_count(0, 100, 1, 1, 1'000'000), 128u);
  // Exact multiple stays put.
  EXPECT_EQ(strategy.next_bucket_count(0, 128, 1, 1, 1'000'000), 128u);
  // current_buckets is ignored entirely.
  EXPECT_EQ(strategy.next_bucket_count(1'000'000, 100, 1, 1, 1'000'000), 128u);
  // Clamped down to max_buckets even if the next multiple is bigger.
  EXPECT_EQ(strategy.next_bucket_count(0, 100, 1, 1, 100), 100u);
  // elements_per_bucket: e.g. 100 elements at 4/bucket -> ceil(100/4)=25 -> next multiple of 4 is 28.
  reloco::bucket_growth::chunked small_chunk{/*chunk_size=*/4};
  EXPECT_EQ(small_chunk.next_bucket_count(0, 100, 4, 1, 1'000'000), 28u);
}

TEST(BucketGrowthTest, AllStrategiesShareTheSameInterfaceShape) {
  // Every strategy takes the same 5 arguments and returns std::size_t,
  // so they are interchangeable behind a template callsite.
  auto run_all = [](auto &&strategy) -> std::size_t {
    return strategy.next_bucket_count(4, 10, 2, 1, 1000);
  };
  EXPECT_GT(run_all(reloco::bucket_growth::linear{}), 0u);
  EXPECT_GT(run_all(reloco::bucket_growth::doubling_then_ratio{}), 0u);
  EXPECT_GT(run_all(reloco::bucket_growth::fixed_ratio{}), 0u);
  EXPECT_GT(run_all(reloco::bucket_growth::power_of_two{}), 0u);
  EXPECT_GT(run_all(reloco::bucket_growth::prime_growth{}), 0u);
  EXPECT_GT(run_all(reloco::bucket_growth::chunked{}), 0u);
  EXPECT_GT(run_all(reloco::bucket_growth::sqrt_curve{}), 0u);
}
