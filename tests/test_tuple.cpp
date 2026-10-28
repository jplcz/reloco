// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <memory>
#include <reloco/tuple.hpp>
#include <string>
#include <type_traits>

namespace {

TEST(TupleTest, IsAStdTuple) {
  reloco::tuple<int, std::string> t(1, "a");
  static_assert(std::is_base_of_v<std::tuple<int, std::string>, reloco::tuple<int, std::string>>);
  std::tuple<int, std::string> &base = t;
  EXPECT_EQ(std::get<0>(base), 1);
  EXPECT_EQ(std::get<1>(t), "a"); // std::get works on the derived type
  EXPECT_EQ(t, (reloco::tuple<int, std::string>(1, "a")));
  EXPECT_LT(t, (reloco::tuple<int, std::string>(2, "a")));
  EXPECT_EQ(t.size(), 2u);
}

TEST(TupleTest, DeductionAndFactories) {
  reloco::tuple t(1, 2.5);
  static_assert(std::is_same_v<decltype(t), reloco::tuple<int, double>>);
  reloco::tuple from_std(std::make_tuple(1, 'c'));
  static_assert(std::is_same_v<decltype(from_std), reloco::tuple<int, char>>);
  reloco::tuple from_pair(std::make_pair(1, 'c'));
  static_assert(std::is_same_v<decltype(from_pair), reloco::tuple<int, char>>);
  int x = 0;
  reloco::tie(x).get<0>() = 5;
  EXPECT_EQ(x, 5);
  static_assert(std::is_same_v<decltype(reloco::make_tuple(1, std::string("s"))), reloco::tuple<int, std::string>>);
}

TEST(TupleTest, AccessAndStructuredBindings) {
  reloco::tuple<int, std::string> t(7, "seven");
  t.get<0>() = 8;
  EXPECT_EQ(t.first(), 8);
  EXPECT_EQ(t.last(), "seven");
  auto &[n, s] = t;
  n = 9;
  EXPECT_EQ(t.get<0>(), 9);
  auto [n2, s2] = std::move(t); // rvalue get() moves the elements out
  EXPECT_EQ(n2, 9);
  EXPECT_EQ(s2, "seven");

  static_assert(std::is_same_v<decltype(std::declval<reloco::tuple<std::string> &&>().get<0>()), std::string>,
                "rvalue get returns by value so it cannot dangle");
}

TEST(TupleTest, ApplyForEachMap) {
  reloco::tuple<int, int, int> t(1, 2, 3);
  EXPECT_EQ(t.apply([](int a, int b, int c) { return a + b + c; }), 6);

  int sum = 0;
  t.for_each([&sum](int v) { sum += v; });
  EXPECT_EQ(sum, 6);
  t.for_each([](int &v) { v *= 2; });
  EXPECT_EQ(t.get<2>(), 6);

  auto mapped = t.map([](int v) { return std::to_string(v); });
  static_assert(std::is_same_v<decltype(mapped), reloco::tuple<std::string, std::string, std::string>>);
  EXPECT_EQ(mapped.get<1>(), "4");
  const auto &ct = t;
  auto mixed = ct.map([](const int &v) { return v + 0.5; });
  EXPECT_EQ(mixed.first(), 2.5);
}

TEST(TupleTest, RvalueMapMovesElements) {
  reloco::tuple<std::unique_ptr<int>, std::unique_ptr<int>> t(std::make_unique<int>(1), std::make_unique<int>(2));
  auto m = std::move(t).map([](std::unique_ptr<int> p) { return *p; });
  EXPECT_EQ(m, (reloco::tuple<int, int>(1, 2)));
}

TEST(TupleTest, ZipAppendPrependConcat) {
  reloco::tuple<int, char> a(1, 'a');
  reloco::tuple<std::string, double> b("x", 2.0);
  auto z = a.zip(b);
  static_assert(
      std::is_same_v<decltype(z), reloco::tuple<reloco::tuple<int, std::string>, reloco::tuple<char, double>>>);
  EXPECT_EQ(z.get<0>().get<1>(), "x");
  EXPECT_EQ(z.get<1>().get<0>(), 'a');

  auto appended = a.append(std::string("end"));
  static_assert(std::is_same_v<decltype(appended), reloco::tuple<int, char, std::string>>);
  EXPECT_EQ(appended.last(), "end");
  auto prepended = std::move(b).prepend(1.5f);
  static_assert(std::is_same_v<decltype(prepended), reloco::tuple<float, std::string, double>>);
  EXPECT_EQ(prepended.first(), 1.5f);

  auto joined = a.concat(reloco::tuple<long>(5L));
  EXPECT_EQ(joined, (reloco::tuple<int, char, long>(1, 'a', 5L)));
  auto empty = reloco::tuple<>().concat(a);
  EXPECT_EQ(empty, a);
}

TEST(TupleTest, MoveOnlyConcat) {
  reloco::tuple<std::unique_ptr<int>> a(std::make_unique<int>(1));
  auto c = std::move(a).append(std::make_unique<int>(2));
  EXPECT_EQ(*c.get<0>(), 1);
  EXPECT_EQ(*c.get<1>(), 2);
}

TEST(TupleTest, VisitAtRuntimeIndex) {
  reloco::tuple<int, short, long> t(1, 2, 3);
  auto r = t.visit_at(1, [](auto &v) { return static_cast<long>(v) * 10; });
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 20);
  auto bad = t.visit_at(3, [](auto &v) { return static_cast<long>(v); });
  ASSERT_FALSE(bad.has_value());
  EXPECT_EQ(bad.error(), reloco::error::out_of_bounds);

  const auto &ct = t;
  auto third = ct.visit_at(2, [](const auto &v) { return static_cast<int>(v); });
  ASSERT_TRUE(third.has_value());
  EXPECT_EQ(*third, 3);
}

TEST(TupleTest, IntoStdAndRelocatability) {
  reloco::tuple<int, std::string> t(1, "s");
  std::tuple<int, std::string> plain = std::move(t).into_std();
  EXPECT_EQ(std::get<1>(plain), "s");
  static_assert(reloco::is_trivially_relocatable_v<reloco::tuple<int, double>>);
  static_assert(std::tuple_size_v<reloco::tuple<int, int, int>> == 3);
  static_assert(std::is_same_v<std::tuple_element_t<1, reloco::tuple<int, char>>, char>);
}

} // namespace
