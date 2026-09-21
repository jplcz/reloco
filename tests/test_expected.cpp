// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/expected.hpp>

#include <string>

TEST(ExpectedTest, HoldsAndReportsAValue) {
  reloco::expected<int, std::string> result(42);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(static_cast<bool>(result));
  EXPECT_EQ(result.value(), 42);
  EXPECT_EQ(*result, 42);
}

TEST(ExpectedTest, HoldsAndReportsAnError) {
  reloco::expected<int, std::string> result(reloco::unexpected<std::string>("failed"));

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), "failed");
}

TEST(ExpectedTest, TransformsAndChainsSuccessfulValues) {
  reloco::expected<int, std::string> result(21);

  const auto doubled = result.transform([](int value) { return value * 2; });
  ASSERT_TRUE(doubled.has_value());
  EXPECT_EQ(doubled.value(), 42);

  const auto chained = result.and_then([](int value) { return reloco::expected<int, std::string>(value + 1); });
  ASSERT_TRUE(chained.has_value());
  EXPECT_EQ(chained.value(), 22);
}

TEST(ExpectedTest, TransformAndAndThenPropagateErrors) {
  reloco::expected<int, std::string> result(reloco::unexpected<std::string>("bad"));

  const auto transformed = result.transform([](int value) { return value * 2; });
  ASSERT_FALSE(transformed.has_value());
  EXPECT_EQ(transformed.error(), "bad");
}

TEST(ExpectedTest, ValueOrFallsBackOnError) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  EXPECT_EQ(ok.value_or(99), 5);
  EXPECT_EQ(err.value_or(99), 99);
}

TEST(ExpectedTest, SupportsVoidValueType) {
  reloco::expected<void, std::string> ok;
  reloco::expected<void, std::string> err(reloco::unexpected<std::string>("bad"));

  EXPECT_TRUE(ok.has_value());
  EXPECT_FALSE(err.has_value());
  EXPECT_EQ(err.error(), "bad");
}

TEST(ExpectedTest, EqualityComparesValuesAndErrors) {
  reloco::expected<int, std::string> a(1);
  reloco::expected<int, std::string> b(1);
  reloco::expected<int, std::string> c(2);

  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}
