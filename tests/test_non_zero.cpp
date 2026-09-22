// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/non_zero.hpp>

#include <cstdint>
#include <type_traits>

using reloco::error;
using reloco::non_zero;

TEST(NonZeroTest, TryCreateSucceedsForNonZeroValue) {
  const auto result = non_zero<int>::try_create(42);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().get(), 42);
}

TEST(NonZeroTest, TryCreateFailsForZero) {
  const auto result = non_zero<int>::try_create(0);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error::invalid_argument);
}

TEST(NonZeroTest, UnsafeCreateWrapsTheValue) {
  const auto nz = non_zero<int>::unsafe_create(7);
  EXPECT_EQ(nz.get(), 7);
}

TEST(NonZeroTest, ImplicitConversionToUnderlyingType) {
  const auto nz = non_zero<int>::unsafe_create(5);
  const int value = nz;
  EXPECT_EQ(value, 5);
  EXPECT_EQ(nz + 1, 6);
}

TEST(NonZeroTest, Comparisons) {
  const auto a = non_zero<int>::unsafe_create(3);
  const auto b = non_zero<int>::unsafe_create(3);
  const auto c = non_zero<int>::unsafe_create(5);

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
  EXPECT_TRUE(a != c);
  EXPECT_TRUE(a < c);
  EXPECT_TRUE(c > a);
  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(a >= b);
}

TEST(NonZeroTest, WorksWithDifferentIntegralTypes) {
  EXPECT_TRUE(non_zero<uint8_t>::try_create(1).has_value());
  EXPECT_FALSE(non_zero<uint8_t>::try_create(0).has_value());

  EXPECT_TRUE(non_zero<int64_t>::try_create(-1).has_value());
  EXPECT_FALSE(non_zero<int64_t>::try_create(0).has_value());
}

TEST(NonZeroTest, IsConstexprConstructible) {
  constexpr auto nz = non_zero<int>::unsafe_create(9);
  static_assert(nz.get() == 9, "non_zero<T>::get() must be usable in a constant expression");
}
