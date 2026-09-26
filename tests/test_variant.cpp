// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/error.hpp"
#include "reloco/lifetime.hpp"
#include "reloco/relocatable.hpp"
#include "reloco/variant.hpp"
#include <gtest/gtest.h>

#include <string>

using namespace reloco;

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

TEST(VariantTest, IsBehavesLikeHoldsAlternative) {
  variant<int, std::string> v(42);

  EXPECT_TRUE(v.is<int>());
  EXPECT_FALSE(v.is<std::string>());

  v = std::string("hello");
  EXPECT_FALSE(v.is<int>());
  EXPECT_TRUE(v.is<std::string>());
}

TEST(VariantTest, AsReturnsPresentOptionalOnMatch) {
  variant<int, std::string> v(42);

  auto as_int = v.as<int>();
  ASSERT_TRUE(as_int.has_value());
  EXPECT_EQ(as_int.value().get(), 42);

  auto as_string = v.as<std::string>();
  EXPECT_FALSE(as_string.has_value());
}

TEST(VariantTest, AsMutableReferenceCanModifyActiveAlternative) {
  variant<int, std::string> v(1);

  auto as_int = v.as<int>();
  ASSERT_TRUE(as_int.has_value());
  as_int.value().get() = 99;

  EXPECT_EQ(std::get<int>(v), 99);
}

TEST(VariantTest, AsConstReturnsConstReference) {
  const variant<int, std::string> v(std::string("const"));

  auto as_string = v.as<std::string>();
  ASSERT_TRUE(as_string.has_value());
  EXPECT_EQ(as_string.value().get(), "const");
}

TEST(VariantTest, MatchDispatchesToActiveAlternative) {
  variant<int, std::string> v(5);

  int result = v.match([](int i) { return i * 2; }, [](const std::string &s) { return static_cast<int>(s.size()); });
  EXPECT_EQ(result, 10);

  v = std::string("abcd");
  result = v.match([](int i) { return i * 2; }, [](const std::string &s) { return static_cast<int>(s.size()); });
  EXPECT_EQ(result, 4);
}

TEST(VariantTest, MatchOnConstAndRvalue) {
  const variant<int, std::string> cv(7);
  EXPECT_EQ(cv.match([](int i) { return i; }, [](const std::string &) { return -1; }), 7);

  variant<int, std::string> rv(3);
  EXPECT_EQ(std::move(rv).match([](int i) { return i; }, [](const std::string &) { return -1; }), 3);
}

TEST(VariantTest, MatchCanMutateThroughReference) {
  variant<int, std::string> v(1);

  v.match([](int &i) { i += 41; }, [](std::string &) {});

  EXPECT_EQ(std::get<int>(v), 42);
}

TEST(VariantTest, InteroperatesWithStdVariantFreeFunctions) {
  variant<int, std::string> v(std::string("x"));

  EXPECT_EQ(v.index(), 1u);
  EXPECT_TRUE(std::holds_alternative<std::string>(v));
  EXPECT_EQ(std::get<std::string>(v), "x");

  std::variant<int, std::string> &base_ref = v;
  EXPECT_EQ(std::get<std::string>(base_ref), "x");
}

TEST(VariantTest, IsTriviallyRelocatableFollowsAlternatives) {
  static_assert(is_trivially_relocatable<variant<int, double>>::value,
                "variant<int, double> should be trivially relocatable");
}

TEST(VariantTest, OverloadedHelperBuildsVisitor) {
  std::variant<int, std::string> raw(3);
  int result = std::visit(overloaded{[](int i) { return i; }, [](const std::string &s) { return (int)s.size(); }}, raw);
  EXPECT_EQ(result, 3);
}

TEST(VariantTest, IsWithPredicateChecksTypeAndValue) {
  variant<int, std::string> v(4);

  EXPECT_TRUE(v.is<int>([](int i) { return i == 4; }));
  EXPECT_FALSE(v.is<int>([](int i) { return i == 5; }));

  bool invoked = false;
  EXPECT_FALSE(v.is<std::string>([&invoked](const std::string &) {
    invoked = true;
    return true;
  }));
  EXPECT_FALSE(invoked);
}

TEST(VariantTest, GetIsCheckedAccessor) {
  variant<int, std::string> v(7);

  EXPECT_EQ(v.get<int>(), 7);
  EXPECT_EQ(std::as_const(v).get<int>(), 7);

  v.get<int>() = 8;
  EXPECT_EQ(std::get<int>(v), 8);

  variant<int, std::string> rv(std::string("moved"));
  EXPECT_EQ(std::move(rv).get<std::string>(), "moved");
}

TEST(VariantTest, TryGetReturnsResultOnMatch) {
  variant<int, std::string> v(42);

  auto ok = v.try_get<int>();
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok.value().get(), 42);

  auto err = v.try_get<std::string>();
  ASSERT_FALSE(err.has_value());
  EXPECT_EQ(err.error(), error::not_found);
}

TEST(VariantTest, TryGetConstReturnsConstReference) {
  const variant<int, std::string> v(std::string("const"));

  auto ok = v.try_get<std::string>();
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok.value().get(), "const");

  EXPECT_FALSE(v.try_get<int>().has_value());
}

TEST(VariantTest, TryGetMutableReferenceCanModifyActiveAlternative) {
  variant<int, std::string> v(1);

  auto ok = v.try_get<int>();
  ASSERT_TRUE(ok.has_value());
  ok.value().get() = 100;

  EXPECT_EQ(std::get<int>(v), 100);
}

TEST(VariantTest, UnsafeGetReturnsActiveAlternative) {
  variant<int, std::string> v(9);

  EXPECT_EQ(v.unsafe_get<int>(), 9);
  EXPECT_EQ(std::as_const(v).unsafe_get<int>(), 9);

  v.unsafe_get<int>() = 11;
  EXPECT_EQ(std::get<int>(v), 11);
}

RELOCO_END_UNSAFE_BUFFER_USAGE
