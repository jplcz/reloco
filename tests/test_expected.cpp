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

TEST(ExpectedTest, MapIsAnAliasForTransform) {
  reloco::expected<int, std::string> ok(21);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  const auto mapped_ok = ok.map([](int value) { return value * 2; });
  ASSERT_TRUE(mapped_ok.has_value());
  EXPECT_EQ(mapped_ok.value(), 42);

  const auto mapped_err = err.map([](int value) { return value * 2; });
  ASSERT_FALSE(mapped_err.has_value());
  EXPECT_EQ(mapped_err.error(), "bad");
}

TEST(ExpectedTest, MapErrTransformsOnlyTheError) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  const auto mapped_ok = ok.map_err([](const std::string &e) { return e.size(); });
  ASSERT_TRUE(mapped_ok.has_value());
  EXPECT_EQ(mapped_ok.value(), 5);

  const auto mapped_err = err.map_err([](const std::string &e) { return e.size(); });
  ASSERT_FALSE(mapped_err.has_value());
  EXPECT_EQ(mapped_err.error(), 3u);
}

TEST(ExpectedTest, OrElseRecoversFromAnError) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  const auto recovered_ok = ok.or_else([](const std::string &) { return reloco::expected<int, std::string>(0); });
  EXPECT_EQ(recovered_ok.value(), 5);

  const auto recovered_err = err.or_else([](const std::string &) { return reloco::expected<int, std::string>(0); });
  EXPECT_EQ(recovered_err.value(), 0);
}

TEST(ExpectedTest, UnwrapOrElseInvokesFallbackOnlyOnError) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  EXPECT_EQ(ok.unwrap_or_else([](const std::string &) { return 99; }), 5);
  EXPECT_EQ(err.unwrap_or_else([](const std::string &e) { return static_cast<int>(e.size()); }), 3);
}

TEST(ExpectedTest, VoidSpecializationSupportsAndThenMapErrAndOrElse) {
  reloco::expected<void, std::string> ok;
  reloco::expected<void, std::string> err(reloco::unexpected<std::string>("bad"));

  const auto chained_ok = ok.and_then([]() { return reloco::expected<void, std::string>(); });
  EXPECT_TRUE(chained_ok.has_value());

  const auto chained_err = err.and_then([]() { return reloco::expected<void, std::string>(); });
  ASSERT_FALSE(chained_err.has_value());
  EXPECT_EQ(chained_err.error(), "bad");

  const auto mapped_err = err.map_err([](const std::string &e) { return e.size(); });
  ASSERT_FALSE(mapped_err.has_value());
  EXPECT_EQ(mapped_err.error(), 3u);

  const auto recovered = err.or_else([](const std::string &) { return reloco::expected<void, std::string>(); });
  EXPECT_TRUE(recovered.has_value());
}

TEST(ExpectedTest, IsOkAndIsErrReportHeldAlternative) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  EXPECT_TRUE(ok.is_ok());
  EXPECT_FALSE(ok.is_err());
  EXPECT_FALSE(err.is_ok());
  EXPECT_TRUE(err.is_err());
}

TEST(ExpectedTest, UnwrapAndUnwrapErrAliasValueAndError) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  EXPECT_EQ(ok.unwrap(), 5);
  EXPECT_EQ(err.unwrap_err(), "bad");
}

TEST(ExpectedTest, ExpectAndExpectErrReturnUnderlyingValues) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  EXPECT_EQ(ok.expect("should hold a value"), 5);
  EXPECT_EQ(err.expect_err("should hold an error"), "bad");
}

TEST(ExpectedTest, UnwrapOrFallsBackOnError) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  EXPECT_EQ(ok.unwrap_or(99), 5);
  EXPECT_EQ(err.unwrap_or(99), 99);
}

TEST(ExpectedTest, UnwrapOrDefaultFallsBackToDefaultConstructedValue) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  EXPECT_EQ(ok.unwrap_or_default(), 5);
  EXPECT_EQ(err.unwrap_or_default(), 0);
}

TEST(ExpectedTest, MapOrAppliesFunctionOrReturnsDefault) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  EXPECT_EQ(ok.map_or(0, [](int v) { return v * 2; }), 10);
  EXPECT_EQ(err.map_or(0, [](int v) { return v * 2; }), 0);
}

TEST(ExpectedTest, MapOrElseAppliesFunctionOrDefaultFunction) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  EXPECT_EQ(
      ok.map_or_else([](const std::string &e) { return static_cast<int>(e.size()); }, [](int v) { return v * 2; }),
      10);
  EXPECT_EQ(
      err.map_or_else([](const std::string &e) { return static_cast<int>(e.size()); }, [](int v) { return v * 2; }),
      3);
}

TEST(ExpectedTest, IsOkAndIsErrAndOnlyInvokePredicateOnMatchingAlternative) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  EXPECT_TRUE(ok.is_ok_and([](int v) { return v == 5; }));
  EXPECT_FALSE(ok.is_ok_and([](int v) { return v == 6; }));
  EXPECT_FALSE(ok.is_err_and([](const std::string &) { return true; }));

  EXPECT_TRUE(err.is_err_and([](const std::string &e) { return e == "bad"; }));
  EXPECT_FALSE(err.is_err_and([](const std::string &e) { return e == "other"; }));
  EXPECT_FALSE(err.is_ok_and([](int) { return true; }));
}

TEST(ExpectedTest, InspectAndInspectErrInvokeSideEffectWithoutConsuming) {
  reloco::expected<int, std::string> ok(5);
  reloco::expected<int, std::string> err(reloco::unexpected<std::string>("bad"));

  int seen_value = 0;
  ok.inspect([&](int v) { seen_value = v; });
  EXPECT_EQ(seen_value, 5);
  ok.inspect_err([&](const std::string &) { FAIL() << "should not be invoked on success"; });

  std::string seen_error;
  err.inspect_err([&](const std::string &e) { seen_error = e; });
  EXPECT_EQ(seen_error, "bad");
  err.inspect([&](int) { FAIL() << "should not be invoked on failure"; });

  // *this is untouched by either call.
  EXPECT_TRUE(ok.has_value());
  EXPECT_FALSE(err.has_value());
}

TEST(ExpectedTest, VoidSpecializationSupportsRustResultExtensions) {
  reloco::expected<void, std::string> ok;
  reloco::expected<void, std::string> err(reloco::unexpected<std::string>("bad"));

  EXPECT_TRUE(ok.is_ok());
  EXPECT_FALSE(err.is_ok());
  EXPECT_TRUE(err.is_err());

  ok.unwrap();
  EXPECT_EQ(err.unwrap_err(), "bad");
  ok.expect("should hold a value");
  EXPECT_EQ(err.expect_err("should hold an error"), "bad");

  EXPECT_EQ(ok.map_or(-1, [] { return 1; }), 1);
  EXPECT_EQ(err.map_or(-1, [] { return 1; }), -1);

  EXPECT_EQ(ok.map_or_else([](const std::string &e) { return static_cast<int>(e.size()); }, [] { return 1; }), 1);
  EXPECT_EQ(err.map_or_else([](const std::string &e) { return static_cast<int>(e.size()); }, [] { return 1; }), 3);

  EXPECT_TRUE(ok.is_ok_and([] { return true; }));
  EXPECT_FALSE(err.is_ok_and([] { return true; }));
  EXPECT_TRUE(err.is_err_and([](const std::string &e) { return e == "bad"; }));
  EXPECT_FALSE(ok.is_err_and([](const std::string &) { return true; }));

  bool invoked = false;
  ok.inspect([&] { invoked = true; });
  EXPECT_TRUE(invoked);
  ok.inspect_err([](const std::string &) { FAIL() << "should not be invoked on success"; });

  std::string seen_error;
  err.inspect_err([&](const std::string &e) { seen_error = e; });
  EXPECT_EQ(seen_error, "bad");
  err.inspect([] { FAIL() << "should not be invoked on failure"; });
}

