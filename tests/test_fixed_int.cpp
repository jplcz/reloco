// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/fixed_int.hpp>
#include <reloco/fixed_point.hpp>

#include <cstdint>
#include <type_traits>

using reloco::fixed_int;
using reloco::fixed_point;
using reloco::fixed_uint;
using reloco::detail::is_fixed_point_rep_v;
using reloco::detail::is_signed_rep_v;
using reloco::detail::next_wider_t;
using reloco::detail::wide_int;

namespace {

using i128_fallback = wide_int<128, true>;
using u128_fallback = wide_int<128, false>;
using i256 = fixed_int<256, true>;
using u256 = fixed_uint<256>;

} // namespace

// --- Native-width aliasing -------------------------------------------------

TEST(FixedIntTest, NativeWidthsAliasNativeTypes) {
  static_assert(std::is_same_v<fixed_int<8, true>, std::int8_t>);
  static_assert(std::is_same_v<fixed_int<8, false>, std::uint8_t>);
  static_assert(std::is_same_v<fixed_int<16, true>, std::int16_t>);
  static_assert(std::is_same_v<fixed_int<16, false>, std::uint16_t>);
  static_assert(std::is_same_v<fixed_int<32, true>, std::int32_t>);
  static_assert(std::is_same_v<fixed_int<32, false>, std::uint32_t>);
  static_assert(std::is_same_v<fixed_int<64, true>, std::int64_t>);
  static_assert(std::is_same_v<fixed_int<64, false>, std::uint64_t>);
  SUCCEED();
}

#if RELOCO_HAS_INT128
TEST(FixedIntTest, 128BitAliasesCompilerExtensionWhenAvailable) {
  RELOCO_BEGIN_SUPPRESS_PEDANTIC_INT128
  static_assert(std::is_same_v<fixed_int<128, true>, __int128>);
  static_assert(std::is_same_v<fixed_int<128, false>, unsigned __int128>);
  RELOCO_END_SUPPRESS_PEDANTIC_INT128
  SUCCEED();
}
#endif

TEST(FixedIntTest, AboveNativeWidthsFallBackToWideInt) {
  static_assert(std::is_same_v<i256, wide_int<256, true>>);
  static_assert(std::is_same_v<u256, wide_int<256, false>>);
  SUCCEED();
}

// --- wide_int construction / round-trip conversions ------------------------

TEST(WideIntTest, DefaultConstructsToZero) {
  i128_fallback v;
  EXPECT_EQ(static_cast<std::int64_t>(v), 0);
}

TEST(WideIntTest, ConstructsFromPositiveNativeInt) {
  i128_fallback v(42);
  EXPECT_EQ(static_cast<std::int64_t>(v), 42);
}

TEST(WideIntTest, ConstructsFromNegativeNativeInt) {
  i128_fallback v(-7);
  EXPECT_EQ(static_cast<std::int64_t>(v), -7);
  EXPECT_TRUE(v.is_negative());
}

TEST(WideIntTest, ConstructsFromNarrowSignedTypesSignExtend) {
  i128_fallback from_i8(std::int8_t{-1});
  EXPECT_EQ(static_cast<std::int64_t>(from_i8), -1);
  EXPECT_TRUE(from_i8.is_negative());

  i128_fallback from_i16(std::int16_t{-1});
  EXPECT_EQ(static_cast<std::int64_t>(from_i16), -1);
}

TEST(WideIntTest, ConstructsFromUnsignedTypesZeroExtend) {
  u128_fallback v(std::uint8_t{0xFFU});
  EXPECT_EQ(static_cast<std::uint64_t>(v), 0xFFULL);
  EXPECT_FALSE(v.is_negative());
}

TEST(WideIntTest, WidensFromNarrowerWideInt) {
  wide_int<64, true> narrow(-5);
  i128_fallback wide(narrow);
  EXPECT_EQ(static_cast<std::int64_t>(wide), -5);
}

TEST(WideIntTest, TruncatesToNarrowerWideInt) {
  i256 wide = (i256(1) << 150U) + i256(5);
  auto narrow = static_cast<i128_fallback>(wide);
  EXPECT_EQ(static_cast<std::int64_t>(narrow), 5);
}

TEST(WideIntTest, TruncatesToNarrowerNativeInt) {
  i128_fallback wide(0x1FF);
  EXPECT_EQ(static_cast<std::uint8_t>(wide), 0xFFU);
}

// --- Arithmetic --------------------------------------------------------

TEST(WideIntTest, Addition) {
  i128_fallback a(100);
  i128_fallback b(23);
  EXPECT_EQ(static_cast<std::int64_t>(a + b), 123);
}

TEST(WideIntTest, AdditionCarriesAcrossLimbs) {
  u128_fallback a(0xFFFF'FFFFU);
  u128_fallback b(1);
  auto sum = a + b;
  EXPECT_EQ(sum.limb(0), 0U);
  EXPECT_EQ(sum.limb(1), 1U);
}

TEST(WideIntTest, Subtraction) {
  i128_fallback a(100);
  i128_fallback b(123);
  EXPECT_EQ(static_cast<std::int64_t>(a - b), -23);
}

TEST(WideIntTest, UnaryNegation) {
  i128_fallback a(42);
  EXPECT_EQ(static_cast<std::int64_t>(-a), -42);
  i128_fallback b(-42);
  EXPECT_EQ(static_cast<std::int64_t>(-b), 42);
}

TEST(WideIntTest, Multiplication) {
  i128_fallback a(12345);
  i128_fallback b(6789);
  EXPECT_EQ(static_cast<std::int64_t>(a * b), 12345LL * 6789LL);
}

TEST(WideIntTest, MultiplicationAcrossLimbs) {
  u128_fallback a(0x1'0000'0000ULL); // 2^32
  u128_fallback b(0x1'0000'0000ULL); // 2^32
  auto product = a * b;              // 2^64
  EXPECT_EQ(product.limb(0), 0U);
  EXPECT_EQ(product.limb(1), 0U);
  EXPECT_EQ(product.limb(2), 1U);
}

TEST(WideIntTest, SignedDivisionTruncatesTowardZero) {
  i128_fallback a(7);
  i128_fallback b(2);
  EXPECT_EQ(static_cast<std::int64_t>(a / b), 3);
  i128_fallback neg_a(-7);
  EXPECT_EQ(static_cast<std::int64_t>(neg_a / b), -3);
}

TEST(WideIntTest, SignedModuloFollowsDividendSign) {
  i128_fallback a(7);
  i128_fallback b(2);
  EXPECT_EQ(static_cast<std::int64_t>(a % b), 1);
  i128_fallback neg_a(-7);
  EXPECT_EQ(static_cast<std::int64_t>(neg_a % b), -1);
}

TEST(WideIntTest, UnsignedDivisionModulo) {
  u128_fallback a(100);
  u128_fallback b(7);
  EXPECT_EQ(static_cast<std::uint64_t>(a / b), 14ULL);
  EXPECT_EQ(static_cast<std::uint64_t>(a % b), 2ULL);
}

TEST(WideIntTest, DivisionAcrossLimbs) {
  u256 a(0);
  // Build 2^200 via repeated shifting, then divide by 2^100 to get 2^100 back.
  u256 big = u256(1) << 200U;
  u256 divisor = u256(1) << 100U;
  EXPECT_EQ(big / divisor, divisor);
  (void)a;
}

// --- Bitwise / shifts -------------------------------------------------

TEST(WideIntTest, BitwiseAndOrXorNot) {
  u128_fallback a(0xF0U);
  u128_fallback b(0x0FU);
  EXPECT_EQ(static_cast<std::uint64_t>(a & b), 0ULL);
  EXPECT_EQ(static_cast<std::uint64_t>(a | b), 0xFFULL);
  EXPECT_EQ(static_cast<std::uint64_t>(a ^ b), 0xFFULL);
  EXPECT_EQ(static_cast<std::uint8_t>(~u128_fallback(0)), 0xFFU);
}

TEST(WideIntTest, ShiftLeftAcrossLimbBoundary) {
  u128_fallback a(1);
  auto shifted = a << 32U;
  EXPECT_EQ(shifted.limb(0), 0U);
  EXPECT_EQ(shifted.limb(1), 1U);
}

TEST(WideIntTest, ShiftLeftWithinLimbCarriesBits) {
  u128_fallback a(0x8000'0000U); // top bit of limb 0
  auto shifted = a << 1U;
  EXPECT_EQ(shifted.limb(0), 0U);
  EXPECT_EQ(shifted.limb(1), 1U);
}

TEST(WideIntTest, ShiftRightLogicalForUnsigned) {
  u128_fallback a(1);
  auto shifted = a << 64U;
  auto back = shifted >> 64U;
  EXPECT_EQ(static_cast<std::uint64_t>(back), 1ULL);
}

TEST(WideIntTest, ShiftRightArithmeticSignExtendsForSigned) {
  i128_fallback a(-8);
  auto shifted = a >> 1U;
  EXPECT_EQ(static_cast<std::int64_t>(shifted), -4);
  EXPECT_TRUE(shifted.is_negative());
}

// --- Comparisons --------------------------------------------------------

TEST(WideIntTest, UnsignedComparisons) {
  u128_fallback a(5);
  u128_fallback b(10);
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(b > a);
  EXPECT_TRUE(a <= a);
  EXPECT_TRUE(a >= a);
  EXPECT_EQ(a, a);
  EXPECT_NE(a, b);
}

TEST(WideIntTest, SignedComparisonsRespectSign) {
  i128_fallback neg(-5);
  i128_fallback pos(5);
  EXPECT_TRUE(neg < pos);
  EXPECT_TRUE(pos > neg);
  i128_fallback more_neg(-100);
  EXPECT_TRUE(more_neg < neg);
}

// --- Compound assignment -------------------------------------------------

TEST(WideIntTest, CompoundAssignmentOperators) {
  i128_fallback v(10);
  v += i128_fallback(5);
  EXPECT_EQ(static_cast<std::int64_t>(v), 15);
  v -= i128_fallback(3);
  EXPECT_EQ(static_cast<std::int64_t>(v), 12);
  v *= i128_fallback(2);
  EXPECT_EQ(static_cast<std::int64_t>(v), 24);
  v /= i128_fallback(4);
  EXPECT_EQ(static_cast<std::int64_t>(v), 6);
  v <<= 2U;
  EXPECT_EQ(static_cast<std::int64_t>(v), 24);
  v >>= 1U;
  EXPECT_EQ(static_cast<std::int64_t>(v), 12);
}

// --- constexpr usability --------------------------------------------------

TEST(WideIntTest, ConstexprUsable) {
  constexpr i128_fallback a(10);
  constexpr i128_fallback b(3);
  constexpr auto sum = a + b;
  constexpr auto product = a * b;
  constexpr auto quotient = a / b;
  static_assert(static_cast<std::int64_t>(sum) == 13);
  static_assert(static_cast<std::int64_t>(product) == 30);
  static_assert(static_cast<std::int64_t>(quotient) == 3);
  SUCCEED();
}

// --- Traits consumed by fixed_point.hpp -----------------------------------

TEST(FixedIntTraitsTest, IsFixedPointRepRecognizesEveryRepFlavor) {
  static_assert(is_fixed_point_rep_v<std::int32_t>);
  static_assert(is_fixed_point_rep_v<std::uint64_t>);
  static_assert(is_fixed_point_rep_v<i256>);
  static_assert(!is_fixed_point_rep_v<bool>);
  static_assert(!is_fixed_point_rep_v<float>);
  SUCCEED();
}

TEST(FixedIntTraitsTest, IsSignedRepTracksSignedness) {
  static_assert(is_signed_rep_v<std::int32_t>);
  static_assert(!is_signed_rep_v<std::uint32_t>);
  static_assert(is_signed_rep_v<i256>);
  static_assert(!is_signed_rep_v<u256>);
  SUCCEED();
}

TEST(FixedIntTraitsTest, NextWiderChainsIndefinitely) {
  static_assert(std::is_same_v<next_wider_t<std::int8_t>, std::int16_t>);
  static_assert(std::is_same_v<next_wider_t<std::int32_t>, std::int64_t>);
  static_assert(std::is_same_v<next_wider_t<i256>, wide_int<512, true>>);
  SUCCEED();
}

// --- Integration with fixed_point ------------------------------------------

TEST(FixedIntIntegrationTest, FixedPointOverWideIntRep) {
  using big_fp = fixed_point<i256, 64>;
  auto a = big_fp::from_int(1000);
  auto b = big_fp::from_int(3);
  auto sum = a + b;
  EXPECT_EQ(sum.to_int(), i256(1003));

  auto product = a * b;
  EXPECT_EQ(product.to_int(), i256(3000));

  auto quotient = big_fp::from_int(10) / big_fp::from_int(4);
  EXPECT_EQ(quotient.to_int(), i256(2)); // truncated toward zero

  auto squared = big_fp::pow(big_fp::from_int(2), 10);
  EXPECT_EQ(squared.to_int(), i256(1024));
}

TEST(FixedIntIntegrationTest, SqrtOverWideIntRep) {
  using big_fp = fixed_point<u256, 32>;
  auto sixteen = big_fp::from_int(16);
  EXPECT_EQ(sixteen.sqrt().to_int(), u256(4));
}
