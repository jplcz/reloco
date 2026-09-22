// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/wrapping.hpp>

#include <cstdint>
#include <limits>
#include <unordered_set>

using reloco::wrapping;

TEST(WrappingTest, DefaultConstructedIsZero) {
  constexpr wrapping<int> w;
  static_assert(w.get() == 0, "wrapping<T>::get() must be usable in a constant expression");
}

TEST(WrappingTest, ConstructsAndGets) {
  constexpr wrapping<int> w(5);
  EXPECT_EQ(w.get(), 5);
}

TEST(WrappingTest, ImplicitConversionToUnderlyingType) {
  const wrapping<int> w(7);
  const int value = w;
  EXPECT_EQ(value, 7);
}

TEST(WrappingTest, AdditionWrapsOnUnsignedOverflow) {
  const wrapping<uint8_t> a(250);
  const wrapping<uint8_t> b(10);
  EXPECT_EQ((a + b).get(), static_cast<uint8_t>(4));
}

TEST(WrappingTest, AdditionWrapsOnSignedOverflow) {
  const wrapping<int8_t> a(std::numeric_limits<int8_t>::max());
  const wrapping<int8_t> b(1);
  EXPECT_EQ((a + b).get(), std::numeric_limits<int8_t>::min());
}

TEST(WrappingTest, SubtractionWrapsOnUnsignedUnderflow) {
  const wrapping<uint8_t> a(1);
  const wrapping<uint8_t> b(2);
  EXPECT_EQ((a - b).get(), static_cast<uint8_t>(255));
}

TEST(WrappingTest, MultiplicationWraps) {
  const wrapping<uint8_t> a(100);
  const wrapping<uint8_t> b(3);
  EXPECT_EQ((a * b).get(), static_cast<uint8_t>((100 * 3) % 256));
}

TEST(WrappingTest, CompoundAssignmentOperators) {
  wrapping<uint8_t> w(250);
  w += wrapping<uint8_t>(10);
  EXPECT_EQ(w.get(), static_cast<uint8_t>(4));

  w -= wrapping<uint8_t>(10);
  EXPECT_EQ(w.get(), static_cast<uint8_t>(250));

  w *= wrapping<uint8_t>(2);
  EXPECT_EQ(w.get(), static_cast<uint8_t>(244));
}

TEST(WrappingTest, UnaryNegationWraps) {
  const wrapping<int8_t> min_val(std::numeric_limits<int8_t>::min());
  EXPECT_EQ((-min_val).get(), std::numeric_limits<int8_t>::min());

  const wrapping<int> a(5);
  EXPECT_EQ((-a).get(), -5);
}

TEST(WrappingTest, PreAndPostIncrementWrap) {
  wrapping<uint8_t> w(255);
  EXPECT_EQ((++w).get(), static_cast<uint8_t>(0));

  wrapping<uint8_t> w2(255);
  EXPECT_EQ((w2++).get(), static_cast<uint8_t>(255));
  EXPECT_EQ(w2.get(), static_cast<uint8_t>(0));
}

TEST(WrappingTest, PreAndPostDecrementWrap) {
  wrapping<uint8_t> w(0);
  EXPECT_EQ((--w).get(), static_cast<uint8_t>(255));

  wrapping<uint8_t> w2(0);
  EXPECT_EQ((w2--).get(), static_cast<uint8_t>(0));
  EXPECT_EQ(w2.get(), static_cast<uint8_t>(255));
}

TEST(WrappingTest, Comparisons) {
  const wrapping<int> a(3);
  const wrapping<int> b(3);
  const wrapping<int> c(5);

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
  EXPECT_TRUE(a != c);
  EXPECT_TRUE(a < c);
  EXPECT_TRUE(c > a);
  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(a >= b);
}

TEST(WrappingTest, IsHashable) {
  std::unordered_set<wrapping<int>, std::hash<wrapping<int>>> set;
  set.insert(wrapping<int>(1));
  set.insert(wrapping<int>(1));
  set.insert(wrapping<int>(2));
  EXPECT_EQ(set.size(), 2u);
}

TEST(WrappingTest, IsConstexprConstructible) {
  constexpr wrapping<int> a(2);
  constexpr wrapping<int> b(3);
  static_assert((a + b).get() == 5, "wrapping<T> arithmetic must be usable in a constant expression");
}
