// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/iterator.hpp"
#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <vector>

using namespace reloco;

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

TEST(IteratorTest, BasicRangeForOverBorrowedItems) {
  std::vector<int> v{1, 2, 3, 4, 5};
  int sum = 0;
  for (auto &x : reloco::iter(v)) {
    sum += x;
    x *= 10; // proves the range-for binding is a real, mutable reference
  }
  EXPECT_EQ(sum, 15);
  EXPECT_EQ(v, (std::vector<int>{10, 20, 30, 40, 50}));
}

TEST(IteratorTest, ConstRangeYieldsConstReferences) {
  const std::vector<int> v{1, 2, 3};
  int sum = 0;
  for (const auto &x : reloco::iter(v))
    sum += x;
  EXPECT_EQ(sum, 6);
}

TEST(IteratorTest, NextDrainsThenReturnsEmpty) {
  std::vector<int> v{1, 2};
  auto it = reloco::iter(v);
  auto a = it.next();
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->get(), 1);
  auto b = it.next();
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->get(), 2);
  EXPECT_FALSE(it.next().has_value());
  EXPECT_FALSE(it.next().has_value()); // stays empty (fused behavior)
}

TEST(IteratorTest, MapProducesOwnedValues) {
  std::vector<int> v{1, 2, 3};
  auto squared = reloco::iter(v).map([](int &x) { return x * x; });
  std::vector<int> out;
  for (auto x : squared)
    out.push_back(x);
  EXPECT_EQ(out, (std::vector<int>{1, 4, 9}));
}

TEST(IteratorTest, FilterKeepsOnlyMatching) {
  std::vector<int> v{1, 2, 3, 4, 5, 6};
  std::vector<int> out;
  for (auto &x : reloco::iter(v).filter([](int &x) { return x % 2 == 0; }))
    out.push_back(x);
  EXPECT_EQ(out, (std::vector<int>{2, 4, 6}));
}

TEST(IteratorTest, EnumeratePairsIndexAndItem) {
  std::vector<std::string> v{"a", "b", "c"};
  auto e = reloco::iter(v).enumerate();
  std::size_t expected_index = 0;
  while (auto item = e.next()) {
    EXPECT_EQ(item->first, expected_index);
    EXPECT_EQ(item->second.get(), v[expected_index]);
    ++expected_index;
  }
  EXPECT_EQ(expected_index, 3u);
}

TEST(IteratorTest, TakeLimitsCount) {
  std::vector<int> v{1, 2, 3, 4, 5};
  EXPECT_EQ(reloco::iter(v).take(3).count(), 3u);
  EXPECT_EQ(reloco::iter(v).take(100).count(), 5u); // fewer items than requested
  EXPECT_EQ(reloco::iter(v).take(0).count(), 0u);
}

TEST(IteratorTest, SkipDiscardsPrefix) {
  std::vector<int> v{1, 2, 3, 4, 5};
  auto s = reloco::iter(v).skip(2);
  auto first = s.next();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->get(), 3);
  EXPECT_EQ(reloco::iter(v).skip(100).count(), 0u); // skipping past the end is not an error
}

TEST(IteratorTest, ZipStopsAtShorterSide) {
  std::vector<int> nums{1, 2, 3, 4};
  std::vector<std::string> names{"a", "b"};
  std::vector<std::pair<int, std::string>> out;
  for (auto pair : reloco::iter(nums).zip(reloco::iter(names)))
    out.emplace_back(pair.first.get(), pair.second.get());
  EXPECT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0], (std::pair<int, std::string>{1, "a"}));
  EXPECT_EQ(out[1], (std::pair<int, std::string>{2, "b"}));
}

TEST(IteratorTest, ChainConcatenatesBothSides) {
  std::vector<int> a{1, 2};
  std::vector<int> b{3, 4, 5};
  std::vector<int> out;
  for (auto &x : reloco::iter(a).chain(reloco::iter(b)))
    out.push_back(x);
  EXPECT_EQ(out, (std::vector<int>{1, 2, 3, 4, 5}));
}

TEST(IteratorTest, FuseKeepsReturningEmptyAfterExhaustion) {
  std::vector<int> v{1};
  auto f = reloco::iter(v).fuse();
  EXPECT_TRUE(f.next().has_value());
  EXPECT_FALSE(f.next().has_value());
  EXPECT_FALSE(f.next().has_value());
}

TEST(IteratorTest, ForEachVisitsEveryItem) {
  std::vector<int> v{1, 2, 3};
  int sum = 0;
  reloco::iter(v).for_each([&](int &x) { sum += x; });
  EXPECT_EQ(sum, 6);
}

TEST(IteratorTest, FoldAccumulates) {
  std::vector<int> v{1, 2, 3, 4};
  auto sum = reloco::iter(v).fold(0, [](int acc, int &x) { return acc + x; });
  EXPECT_EQ(sum, 10);
}

TEST(IteratorTest, CountReturnsRemainingItemCount) {
  std::vector<int> v{1, 2, 3};
  EXPECT_EQ(reloco::iter(v).count(), 3u);
}

TEST(IteratorTest, NthSkipsAndReturns) {
  std::vector<int> v{10, 20, 30, 40};
  auto item = reloco::iter(v).nth(2);
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(item->get(), 30);
  EXPECT_FALSE(reloco::iter(v).nth(100).has_value());
}

TEST(IteratorTest, AllAndAny) {
  std::vector<int> v{2, 4, 6};
  EXPECT_TRUE(reloco::iter(v).all([](int &x) { return x % 2 == 0; }));
  EXPECT_FALSE(reloco::iter(v).any([](int &x) { return x % 2 != 0; }));
  std::vector<int> empty;
  EXPECT_TRUE(reloco::iter(empty).all([](int &) { return false; })); // vacuously true
  EXPECT_FALSE(reloco::iter(empty).any([](int &) { return true; }));
}

TEST(IteratorTest, FindReturnsFirstMatch) {
  std::vector<int> v{1, 2, 3, 4, 5};
  auto found = reloco::iter(v).find([](int &x) { return x > 3; });
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 4);
  EXPECT_FALSE(reloco::iter(v).find([](int &x) { return x > 100; }).has_value());
}

TEST(IteratorTest, ChainedAdaptorsComposeInOneRangeFor) {
  std::vector<int> v{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  std::size_t total = 0;
  for (auto pair : reloco::iter(v)
                        .filter([](int &x) { return x % 2 == 0; })
                        .map([](int &x) { return x * x; })
                        .take(2)
                        .enumerate())
    total += pair.first + static_cast<std::size_t>(pair.second);
  // even -> {2,4,6,8,10}; squared -> {4,16,36,64,100}; take(2) -> {4,16};
  // enumerate -> (0,4),(1,16); total = 0+4+1+16 = 21.
  EXPECT_EQ(total, 21u);
}

TEST(IteratorTest, RangeForOverTemporaryAdaptorChainWorks) {
  // The range-expression is lifetime-extended for the loop's duration, so
  // this must compile and run even though every adaptor chained onto
  // `reloco::iter(empty_vec)` here is itself a temporary (only the
  // underlying `std::vector` must be a named lvalue -- see the deleted
  // rvalue `iter()` overload).
  std::vector<int> empty_vec;
  std::vector<int> out;
  for (auto x : reloco::iter(empty_vec).map([](int &x) { return x; }))
    out.push_back(x);
  EXPECT_TRUE(out.empty());

  std::vector<int> v{1, 2, 3};
  std::vector<int> collected;
  for (auto &x : reloco::iter(v).filter([](int &x) { return x != 2; }))
    collected.push_back(x);
  EXPECT_EQ(collected, (std::vector<int>{1, 3}));
}

namespace {

// void_t-based detection idiom (see tests/test_intrusive_hash_table.cpp for
// the same pattern) -- `std::is_invocable_v<decltype(&T::method), ...>`
// can't be used here since these methods aren't overloaded, but the
// rvalue-qualified overloads are still only reachable through a direct
// call-expression check.
template <typename T, typename = void> struct can_call_begin_on : std::false_type {};
template <typename T> struct can_call_begin_on<T, std::void_t<decltype(std::declval<T>().begin())>> : std::true_type {
};

template <typename T, typename = void> struct can_call_next_on : std::false_type {};
template <typename T> struct can_call_next_on<T, std::void_t<decltype(std::declval<T>().next())>> : std::true_type {};

template <typename Range, typename = void> struct can_call_iter_on : std::false_type {};
template <typename Range>
struct can_call_iter_on<Range, std::void_t<decltype(reloco::iter(std::declval<Range>()))>> : std::true_type {};

} // namespace

TEST(IteratorTest, RvalueHardeningIsCompileTimeRejected) {
  using range_type = decltype(reloco::iter(std::declval<std::vector<int> &>()));

  static_assert(can_call_begin_on<range_type &>::value, "lvalue begin() must remain callable");
  static_assert(!can_call_begin_on<range_type>::value, "rvalue-this begin() must be rejected");

  static_assert(can_call_next_on<range_type &>::value, "lvalue next() must remain callable");
  static_assert(!can_call_next_on<range_type>::value, "rvalue-this next() must be rejected");

  static_assert(can_call_iter_on<std::vector<int> &>::value, "reloco::iter() on an lvalue range must be callable");
  static_assert(!can_call_iter_on<std::vector<int>>::value, "reloco::iter() on a temporary range must be rejected");
}

RELOCO_END_UNSAFE_BUFFER_USAGE
