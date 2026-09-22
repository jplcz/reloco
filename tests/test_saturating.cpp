// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/saturating.hpp>

#include <cstdint>
#include <limits>
#include <unordered_set>

using reloco::saturating;

TEST(SaturatingTest, DefaultConstructedIsZero) {
  constexpr saturating<int> s;
  static_assert(s.get() == 0, "saturating<T>::get() must be usable in a constant expression");
}

TEST(SaturatingTest, ConstructsAndGets) {
  constexpr saturating<int> s(5);
  EXPECT_EQ(s.get(), 5);
}

TEST(SaturatingTest, ImplicitConversionToUnderlyingType) {
  const saturating<int> s(7);
  const int value = s;
  EXPECT_EQ(value, 7);
}

TEST(SaturatingTest, AdditionClampsToMaxOnUnsignedOverflow) {
  const saturating<uint8_t> a(250);
  const saturating<uint8_t> b(10);
  EXPECT_EQ((a + b).get(), std::numeric_limits<uint8_t>::max());
}

TEST(SaturatingTest, AdditionClampsToMaxOnSignedOverflow) {
  const saturating<int8_t> a(std::numeric_limits<int8_t>::max());
  const saturating<int8_t> b(1);
  EXPECT_EQ((a + b).get(), std::numeric_limits<int8_t>::max());
}

TEST(SaturatingTest, SubtractionClampsToMinOnUnsignedUnderflow) {
  const saturating<uint8_t> a(1);
  const saturating<uint8_t> b(2);
  EXPECT_EQ((a - b).get(), static_cast<uint8_t>(0));
}

TEST(SaturatingTest, SubtractionClampsToMinOnSignedUnderflow) {
  const saturating<int8_t> a(std::numeric_limits<int8_t>::min());
  const saturating<int8_t> b(1);
  EXPECT_EQ((a - b).get(), std::numeric_limits<int8_t>::min());
}

TEST(SaturatingTest, MultiplicationClampsOnOverflow) {
  const saturating<uint8_t> a(100);
  const saturating<uint8_t> b(3);
  EXPECT_EQ((a * b).get(), std::numeric_limits<uint8_t>::max());
}

TEST(SaturatingTest, ArithmeticWithinRangeIsExact) {
  const saturating<int> a(10);
  const saturating<int> b(5);
  EXPECT_EQ((a + b).get(), 15);
  EXPECT_EQ((a - b).get(), 5);
  EXPECT_EQ((a * b).get(), 50);
}

TEST(SaturatingTest, CompoundAssignmentOperators) {
  saturating<uint8_t> s(250);
  s += saturating<uint8_t>(10);
  EXPECT_EQ(s.get(), std::numeric_limits<uint8_t>::max());

  s -= saturating<uint8_t>(255);
  EXPECT_EQ(s.get(), static_cast<uint8_t>(0));

  s += saturating<uint8_t>(100);
  s *= saturating<uint8_t>(5);
  EXPECT_EQ(s.get(), std::numeric_limits<uint8_t>::max());
}

TEST(SaturatingTest, UnaryNegationClamps) {
  const saturating<int8_t> min_val(std::numeric_limits<int8_t>::min());
  EXPECT_EQ((-min_val).get(), std::numeric_limits<int8_t>::max());

  const saturating<uint8_t> u(5);
  EXPECT_EQ((-u).get(), static_cast<uint8_t>(0));

  const saturating<int> a(5);
  EXPECT_EQ((-a).get(), -5);
}

TEST(SaturatingTest, PreAndPostIncrementClamp) {
  saturating<uint8_t> s(std::numeric_limits<uint8_t>::max());
  EXPECT_EQ((++s).get(), std::numeric_limits<uint8_t>::max());

  saturating<uint8_t> s2(std::numeric_limits<uint8_t>::max());
  EXPECT_EQ((s2++).get(), std::numeric_limits<uint8_t>::max());
  EXPECT_EQ(s2.get(), std::numeric_limits<uint8_t>::max());
}

TEST(SaturatingTest, PreAndPostDecrementClamp) {
  saturating<uint8_t> s(0);
  EXPECT_EQ((--s).get(), static_cast<uint8_t>(0));

  saturating<uint8_t> s2(0);
  EXPECT_EQ((s2--).get(), static_cast<uint8_t>(0));
  EXPECT_EQ(s2.get(), static_cast<uint8_t>(0));
}

TEST(SaturatingTest, Comparisons) {
  const saturating<int> a(3);
  const saturating<int> b(3);
  const saturating<int> c(5);

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
  EXPECT_TRUE(a != c);
  EXPECT_TRUE(a < c);
  EXPECT_TRUE(c > a);
  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(a >= b);
}

TEST(SaturatingTest, IsHashable) {
  std::unordered_set<saturating<int>, std::hash<saturating<int>>> set;
  set.insert(saturating<int>(1));
  set.insert(saturating<int>(1));
  set.insert(saturating<int>(2));
  EXPECT_EQ(set.size(), 2u);
}

TEST(SaturatingTest, IsConstexprConstructible) {
  constexpr saturating<int> a(2);
  static_assert(a.get() == 2, "saturating<T>::get() must be usable in a constant expression");
}
