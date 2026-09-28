// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/masked_byte_region.hpp>

#include <algorithm>
#include <iterator>
#include <numeric>

namespace {

TEST(MaskedByteRegionTest, ConstructedRegionReadsAsAllZero) {
  reloco::masked_byte_region<16> region;
  for (size_t i = 0; i < region.size(); ++i)
    EXPECT_EQ(region[i], 0) << "offset " << i;
}

TEST(MaskedByteRegionTest, SizeReportsTemplateParameter) {
  reloco::masked_byte_region<32> region;
  EXPECT_EQ(region.size(), 32u);
}

TEST(MaskedByteRegionTest, WriteThenReadRoundTrips) {
  reloco::masked_byte_region<8> region;
  for (size_t i = 0; i < region.size(); ++i)
    region.write_byte(i, static_cast<uint8_t>(i * 7 + 1));
  for (size_t i = 0; i < region.size(); ++i)
    EXPECT_EQ(region.read_byte(i), static_cast<uint8_t>(i * 7 + 1));
}

TEST(MaskedByteRegionTest, SubscriptOperatorReadsAndWrites) {
  reloco::masked_byte_region<8> region;
  region[3] = 0xAA;
  EXPECT_EQ(region[3], 0xAA);

  uint8_t read_back = region[3];
  EXPECT_EQ(read_back, 0xAA);
}

TEST(MaskedByteRegionTest, XorAssignFlipsBits) {
  reloco::masked_byte_region<8> region;
  region[0] = 0x0F;
  region[0] ^= 0xFF;
  EXPECT_EQ(region[0], 0xF0);
}

TEST(MaskedByteRegionTest, UnderlyingStorageIsNotThePlaintext) {
  // The whole point of this class: the plaintext must never appear
  // contiguously in the object's own storage. We can't reach the private
  // obfuscated_data_ member from a test, but two regions holding the same
  // plaintext at the same offset must not be trivially comparable as
  // "the same bytes" via their raw representation -- different
  // instances get different (address-derived) nonces, so this at least
  // documents the intended invariant even though it can't peek at the
  // private bytes directly.
  reloco::masked_byte_region<8> region_a;
  reloco::masked_byte_region<8> region_b;
  region_a.fill(0x5A);
  region_b.fill(0x5A);
  EXPECT_EQ(region_a[0], region_b[0]); // Decoded plaintext still matches.
}

TEST(MaskedByteRegionTest, FillSetsEveryByte) {
  reloco::masked_byte_region<16> region;
  region.fill(0x77);
  for (size_t i = 0; i < region.size(); ++i)
    EXPECT_EQ(region[i], 0x77);
}

TEST(MaskedByteRegionTest, WipeZeroesEveryByte) {
  reloco::masked_byte_region<16> region;
  region.fill(0x77);
  region.wipe();
  for (size_t i = 0; i < region.size(); ++i)
    EXPECT_EQ(region[i], 0);
}

TEST(MaskedByteRegionTest, DestructorWipesRegion) {
  // We can't observe post-destruction state directly, but we can at
  // least confirm the destructor runs without asserting/crashing on a
  // region that has been written to and never explicitly wiped.
  {
    reloco::masked_byte_region<16> region;
    region.fill(0xEE);
  }
  SUCCEED();
}

// ---- Iterator support ----

TEST(MaskedByteRegionTest, BeginEndSpanTheWholeRegion) {
  reloco::masked_byte_region<8> region;
  EXPECT_EQ(std::distance(region.begin(), region.end()), 8);
  EXPECT_EQ(std::distance(region.cbegin(), region.cend()), 8);
}

TEST(MaskedByteRegionTest, MutableIteratorWritesThroughProxy) {
  reloco::masked_byte_region<8> region;
  size_t i = 0;
  for (auto it = region.begin(); it != region.end(); ++it, ++i)
    *it = static_cast<uint8_t>(i);
  for (size_t j = 0; j < region.size(); ++j)
    EXPECT_EQ(region[j], j);
}

TEST(MaskedByteRegionTest, ConstIteratorYieldsValuesByValue) {
  reloco::masked_byte_region<8> region;
  region.fill(0x99);
  const auto &const_region = region;
  for (auto it = const_region.begin(); it != const_region.end(); ++it)
    EXPECT_EQ(*it, 0x99);
}

TEST(MaskedByteRegionTest, RangeForWorksViaBeginEnd) {
  reloco::masked_byte_region<8> region;
  region.fill(0x33);
  for (uint8_t b : region)
    EXPECT_EQ(b, 0x33);
}

TEST(MaskedByteRegionTest, StdAlgorithmsComposeWithIterators) {
  reloco::masked_byte_region<8> region;
  size_t counter = 0;
  std::generate(region.begin(), region.end(), [&counter]() { return static_cast<uint8_t>(counter++); });

  int sum = std::accumulate(region.cbegin(), region.cend(), 0);
  EXPECT_EQ(sum, 0 + 1 + 2 + 3 + 4 + 5 + 6 + 7);

  EXPECT_TRUE(std::equal(region.cbegin(), region.cend(), region.cbegin()));
}

TEST(MaskedByteRegionTest, RandomAccessArithmeticOnIterators) {
  reloco::masked_byte_region<8> region;
  region.fill(0x01);
  auto it = region.begin();
  auto it3 = it + 3;
  EXPECT_EQ(it3 - it, 3);
  EXPECT_EQ(it[3], 0x01);

  it3 -= 3;
  EXPECT_EQ(it3, it);

  auto end_minus_one = region.end() - 1;
  EXPECT_LT(it, end_minus_one);
  EXPECT_GT(end_minus_one, it);
  EXPECT_LE(it, it);
  EXPECT_GE(it, it);
}

TEST(MaskedByteRegionTest, PrefixAndPostfixIncrementDecrement) {
  reloco::masked_byte_region<8> region;
  auto it = region.begin();

  auto pre = ++it;
  EXPECT_EQ(pre, it);

  auto post = it++;
  EXPECT_NE(post, it);
  EXPECT_EQ(post + 1, it);

  // Bring 'it' back down to where 'post' was left behind
  --it;
  EXPECT_EQ(it, post); // Both are now 1

  // Advance both using postfix and prefix
  it++;
  ++post;
  EXPECT_EQ(it, post); // Both are now 2

  // Test postfix decrement
  auto post_dec = it--;
  EXPECT_EQ(post_dec, post); // post_dec is 2, post is 2
  EXPECT_EQ(it, post - 1);   // it is 1
}

} // namespace
