// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/packed_bits.hpp"
#include <cstdint>
#include <gtest/gtest.h>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco::testing {

// --- Define a mock hardware descriptor layout for testing ---
using enable_flag = bitfield<0, 1>;         // Unsigned: 1 bit  (0..1)
using priority = bitfield<1, 3>;            // Unsigned: 3 bits (0..7)
using temperature = signed_bitfield<4, 8>;  // Signed:   8 bits (-128..127)
using pfn_high = bitfield<12, 52>;          // Unsigned: 52 bits
using full_width = bitfield<0, 64>;         // Unsigned: 64 bits (Full size test)
using signed_full = signed_bitfield<0, 64>; // Signed:   64 bits (Full size test)

TEST(PackedBitsTest, InitializationAndClear) {
  packed_bits<uint32_t> empty;
  EXPECT_EQ(empty.value(), 0u);

  packed_bits<uint64_t> initialized(0xDEADBEEF0000FFFFull);
  EXPECT_EQ(initialized.value(), 0xDEADBEEF0000FFFFull);

  initialized.clear();
  EXPECT_EQ(initialized.value(), 0u);
}

TEST(PackedBitsTest, UnsignedBitfields) {
  packed_bits<uint64_t> desc;

  desc.set<enable_flag>(1);
  EXPECT_EQ(desc.get<enable_flag>(), 1u);
  EXPECT_EQ(desc.value(), 1u); // Bit 0 is 1

  desc.set<priority>(5);
  EXPECT_EQ(desc.get<priority>(), 5u);
  // 5 is binary 101, shifted left by 1 is 1010 (10). 10 + 1 = 11
  EXPECT_EQ(desc.value(), 11u);

  // Overwrite priority
  desc.set<priority>(2);
  EXPECT_EQ(desc.get<priority>(), 2u);
  EXPECT_EQ(desc.get<enable_flag>(), 1u); // Ensure enable flag is intact
}

TEST(PackedBitsTest, SignedBitfieldsPositive) {
  packed_bits<uint64_t> desc;

  desc.set<temperature>(42);

  // get() should return a signed integer type (int64_t for uint64_t backing)
  auto temp = desc.get<temperature>();
  static_assert(std::is_signed_v<decltype(temp)>, "Signed bitfield must return a signed type");

  EXPECT_EQ(temp, 42);
}

TEST(PackedBitsTest, SignedBitfieldsNegative) {
  packed_bits<uint64_t> desc;

  desc.set<temperature>(-5);
  EXPECT_EQ(desc.get<temperature>(), -5);

  desc.set<temperature>(-128); // Min 8-bit signed value
  EXPECT_EQ(desc.get<temperature>(), -128);

  desc.set<temperature>(127); // Max 8-bit signed value
  EXPECT_EQ(desc.get<temperature>(), 127);
}

TEST(PackedBitsTest, MultipleFieldsDoNotOverlap) {
  packed_bits<uint64_t> desc;

  desc.set<enable_flag>(1);
  desc.set<priority>(7);
  desc.set<temperature>(-50);
  desc.set<pfn_high>(0x000F'FFFF'FFFF'FFFFull);

  // Ensure writing the highest field didn't corrupt the lowest
  EXPECT_EQ(desc.get<enable_flag>(), 1u);
  EXPECT_EQ(desc.get<priority>(), 7u);
  EXPECT_EQ(desc.get<temperature>(), -50);
  EXPECT_EQ(desc.get<pfn_high>(), 0x000F'FFFF'FFFF'FFFFull);
}

TEST(PackedBitsTest, FullWidthFieldBypassesUndefinedBehavior) {
  packed_bits<uint64_t> desc;

  desc.set<full_width>(0x123456789ABCDEF0ull);
  EXPECT_EQ(desc.get<full_width>(), 0x123456789ABCDEF0ull);

  desc.set<signed_full>(-42ll);
  EXPECT_EQ(desc.get<signed_full>(), -42ll);
}

// ----------------------------------------------------------------------------
// Death Tests (run only if debug asserts are enabled)
// ----------------------------------------------------------------------------
#if !defined(NDEBUG) || defined(RELOCO_ENABLE_ASSERTS)

// Note: Google Test expects a regex for the death test message.
// Adjust "truncated" to match whatever your RELOCO_DEBUG_ASSERT outputs.

TEST(PackedBitsDeathTest, UnsignedTruncationTraps) {
  packed_bits<uint64_t> desc;
  // priority is 3 bits (max 7). 8 should cause a truncation assert.
  EXPECT_DEATH({ desc.set<priority>(8); }, "");
}

TEST(PackedBitsDeathTest, UnsignedNegativeTraps) {
  packed_bits<uint64_t> desc;
  // Cannot insert negative value into unsigned bitfield
  EXPECT_DEATH({ desc.set<priority>(-1); }, "");
}

TEST(PackedBitsDeathTest, SignedTruncationTrapsPositive) {
  packed_bits<uint64_t> desc;
  // temperature is 8 bits signed (-128 to 127). 128 exceeds capacity.
  EXPECT_DEATH({ desc.set<temperature>(128); }, "");
}

TEST(PackedBitsDeathTest, SignedTruncationTrapsNegative) {
  packed_bits<uint64_t> desc;
  // temperature is 8 bits signed (-128 to 127). -129 exceeds capacity.
  EXPECT_DEATH({ desc.set<temperature>(-129); }, "");
}

#endif

TEST(PackedBitsTest, TrySetUnsignedLimits) {
  packed_bits<uint64_t> desc;

  // priority is 3 bits (max 7)
  EXPECT_TRUE(desc.try_set<priority>(7).has_value());
  EXPECT_EQ(desc.get<priority>(), 7u);

  // 8 exceeds 3 bits
  auto res_too_large = desc.try_set<priority>(8);
  EXPECT_FALSE(res_too_large.has_value());
  EXPECT_EQ(res_too_large.error(), error::out_of_range);

  // Cannot pass negative to unsigned
  auto res_negative = desc.try_set<priority>(-1);
  EXPECT_FALSE(res_negative.has_value());
  EXPECT_EQ(res_negative.error(), error::out_of_range);

  // Ensure the original value was not modified by the failed attempts
  EXPECT_EQ(desc.get<priority>(), 7u);
}

TEST(PackedBitsTest, TrySetSignedLimits) {
  packed_bits<uint64_t> desc;

  // temperature is 8 bits signed (-128 to 127)
  EXPECT_TRUE(desc.try_set<temperature>(127).has_value());
  EXPECT_TRUE(desc.try_set<temperature>(-128).has_value());

  // 128 exceeds positive capacity
  auto res_pos = desc.try_set<temperature>(128);
  EXPECT_FALSE(res_pos.has_value());
  EXPECT_EQ(res_pos.error(), error::out_of_range);

  // -129 exceeds negative capacity
  auto res_neg = desc.try_set<temperature>(-129);
  EXPECT_FALSE(res_neg.has_value());
  EXPECT_EQ(res_neg.error(), error::out_of_range);
}

TEST(PackedBitsTest, SaturatingSetUnsigned) {
  packed_bits<uint64_t> desc;
  // priority is 3 bits (max value 7)

  desc.saturating_set<priority>(5);
  EXPECT_EQ(desc.get<priority>(), 5u);

  // Exceeds max -> saturates to 7
  desc.saturating_set<priority>(200);
  EXPECT_EQ(desc.get<priority>(), 7u);

  // Negative input -> saturates to 0
  desc.saturating_set<priority>(-50);
  EXPECT_EQ(desc.get<priority>(), 0u);
}

TEST(PackedBitsTest, SaturatingSetSigned) {
  packed_bits<uint64_t> desc;
  // temperature is 8 bits signed (-128 to 127)

  // Exceeds positive max -> saturates to 127
  desc.saturating_set<temperature>(500);
  EXPECT_EQ(desc.get<temperature>(), 127);

  // Exceeds negative min -> saturates to -128
  desc.saturating_set<temperature>(-500);
  EXPECT_EQ(desc.get<temperature>(), -128);
}

TEST(PackedBitsTest, TruncatingSetBehavior) {
  packed_bits<uint64_t> desc;
  // priority is 3 bits (mask = 0b111 = 7)

  // 0x1A (binary 11010) masked by 7 is binary 010 (2)
  desc.truncating_set<priority>(0x1A);
  EXPECT_EQ(desc.get<priority>(), 2u);
}

} // namespace reloco::testing

RELOCO_END_UNSAFE_BUFFER_USAGE
