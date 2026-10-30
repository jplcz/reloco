// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/relocatable.hpp>
#include <reloco/set.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

std::size_t max_height(std::size_t n) {
  // A red-black tree holds at most 2 * log2(n + 1) levels.
  std::size_t log2 = 0;
  for (std::size_t v = n + 1; v > 1; v >>= 1)
    ++log2;
  return 2 * (log2 + 1);
}

template <typename S, typename Ref> void expect_same(const S &set, const Ref &ref) {
  ASSERT_EQ(set.size(), ref.size());
  auto it = set.begin();
  for (const auto &v : ref) {
    ASSERT_NE(it, set.end());
    EXPECT_EQ(*it, v);
    ++it;
  }
  EXPECT_EQ(it, set.end());
}

} // namespace

TEST(SetTest, SortedAndDuplicates) {
  auto res = reloco::set<int>::try_create();
  ASSERT_TRUE(res.has_value());
  auto &set = *res;

  for (int v : {30, 10, 20, 5})
    ASSERT_TRUE(set.try_insert(v).has_value());
  auto dup = set.try_insert(10);
  ASSERT_FALSE(dup.has_value());
  EXPECT_EQ(dup.error(), reloco::error::already_exists);

  expect_same(set, std::set<int>{5, 10, 20, 30});
  EXPECT_TRUE(set.contains(20));
  EXPECT_FALSE(set.contains(21));
  auto first = set.try_first();
  auto last = set.try_last();
  ASSERT_TRUE(first.has_value() && last.has_value());
  EXPECT_EQ(first->get(), 5);
  EXPECT_EQ(last->get(), 30);
}

TEST(SetTest, StaysBalancedOnSortedInput) {
  auto res = reloco::set<int>::try_create();
  ASSERT_TRUE(res.has_value());
  auto &set = *res;

  constexpr int n = 20000;
  for (int i = 0; i < n; ++i)
    ASSERT_TRUE(set.try_insert(i).has_value());
  EXPECT_EQ(set.size(), static_cast<std::size_t>(n));
  EXPECT_LE(set.height(), max_height(n));

  // erase every other element in ascending order: the worst case for an unbalanced tree
  for (int i = 0; i < n; i += 2)
    ASSERT_TRUE(set.try_remove(i).has_value());
  EXPECT_EQ(set.size(), static_cast<std::size_t>(n / 2));
  EXPECT_LE(set.height(), max_height(n / 2));
}

TEST(SetTest, RandomizedAgainstStdSet) {
  std::mt19937 rng(12345);
  auto res = reloco::set<int>::try_create();
  ASSERT_TRUE(res.has_value());
  auto &set = *res;
  std::set<int> ref;

  for (int step = 0; step < 30000; ++step) {
    const int key = static_cast<int>(rng() % 512);
    if (rng() % 3 != 0) {
      EXPECT_EQ(set.try_insert(key).has_value(), ref.insert(key).second);
    } else {
      EXPECT_EQ(set.try_remove(key).has_value(), ref.erase(key) == 1);
    }
    // the cached first/last must track every insert and erase
    auto first = set.try_first();
    auto last = set.try_last();
    if (ref.empty()) {
      EXPECT_FALSE(first.has_value());
      EXPECT_FALSE(last.has_value());
    } else {
      ASSERT_TRUE(first.has_value() && last.has_value());
      EXPECT_EQ(first->get(), *ref.begin());
      EXPECT_EQ(last->get(), *ref.rbegin());
      EXPECT_EQ(*set.begin(), *ref.begin());
    }
    if (step % 1500 == 0) {
      expect_same(set, ref);
      EXPECT_LE(set.height(), max_height(set.size()));
    }
  }
  expect_same(set, ref);

  while (!ref.empty()) {
    ASSERT_TRUE(set.try_pop_first().has_value());
    ref.erase(ref.begin());
  }
  EXPECT_TRUE(set.empty());
  EXPECT_EQ(set.height(), 0u);
}

TEST(SetTest, ReferencesStayValidAcrossOtherChanges) {
  auto res = reloco::set<std::string>::try_create();
  ASSERT_TRUE(res.has_value());
  auto &set = *res;

  auto kept_res = set.try_insert("kept");
  ASSERT_TRUE(kept_res.has_value());
  const std::string &kept = kept_res->get();
  for (int i = 0; i < 1000; ++i)
    ASSERT_TRUE(set.try_insert("k" + std::to_string(i)).has_value());
  for (int i = 0; i < 1000; i += 2)
    ASSERT_TRUE(set.try_remove("k" + std::to_string(i)).has_value());
  EXPECT_EQ(kept, "kept");
}

TEST(SetTest, RetainAppendCloneAndPredicates) {
  auto a_res = reloco::set<int>::try_create();
  auto b_res = reloco::set<int>::try_create();
  ASSERT_TRUE(a_res.has_value() && b_res.has_value());
  auto &a = *a_res;
  auto &b = *b_res;
  for (int i = 0; i < 100; ++i)
    ASSERT_TRUE(a.try_insert(i).has_value());
  for (int i = 50; i < 150; ++i)
    ASSERT_TRUE(b.try_insert(i).has_value());

  auto clone = a.try_clone();
  ASSERT_TRUE(clone.has_value());
  EXPECT_EQ(clone->size(), 100u);
  EXPECT_TRUE(clone->is_subset(a));
  EXPECT_TRUE(a.is_superset(*clone));

  a.append(b);
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(a.size(), 150u);
  EXPECT_LE(a.height(), max_height(150));

  a.retain([](const int &v) { return v % 3 == 0; });
  EXPECT_EQ(a.size(), 50u);
  EXPECT_LE(a.height(), max_height(50));
  for (const int v : a)
    EXPECT_EQ(v % 3, 0);
  EXPECT_TRUE(clone->is_disjoint(b));
}

static_assert(reloco::is_trivially_relocatable_v<reloco::set<int>>);

TEST(SetTest, CloneKeepsShapeAndStaysBalancedWhenMutated) {
  auto res = reloco::set<int>::try_create();
  ASSERT_TRUE(res.has_value());
  auto &set = *res;
  for (int i = 0; i < 5000; ++i)
    ASSERT_TRUE(set.try_insert(i).has_value());
  for (int i = 0; i < 5000; i += 3)
    ASSERT_TRUE(set.try_remove(i).has_value());

  auto clone = set.try_clone();
  ASSERT_TRUE(clone.has_value());
  EXPECT_EQ(clone->size(), set.size());
  EXPECT_EQ(clone->height(), set.height());
  expect_same(*clone, std::set<int>(set.begin(), set.end()));

  // The copied colours must be a valid red-black coloring: keep mutating the clone and check the height bound.
  for (int i = 5000; i < 9000; ++i)
    ASSERT_TRUE(clone->try_insert(i).has_value());
  for (int i = 1; i < 5000; i += 2)
    (void)clone->try_remove(i);
  EXPECT_LE(clone->height(), max_height(clone->size()));
  EXPECT_EQ(set.size(), 5000u - 1667u);

  auto empty = reloco::set<int>::try_create();
  ASSERT_TRUE(empty.has_value());
  auto empty_clone = empty->try_clone();
  ASSERT_TRUE(empty_clone.has_value());
  EXPECT_TRUE(empty_clone->empty());
}
