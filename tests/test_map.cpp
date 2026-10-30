// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/map.hpp>
#include <reloco/relocatable.hpp>

#include <cstddef>
#include <map>
#include <random>
#include <string>
#include <utility>

TEST(MapTest, InsertLookupRemove) {
  auto res = reloco::map<int, std::string>::try_create();
  ASSERT_TRUE(res.has_value());
  auto &map = *res;

  ASSERT_TRUE(map.try_insert(2, "two").has_value());
  ASSERT_TRUE(map.try_insert(1, "one").has_value());
  auto dup = map.try_insert(2, "again");
  ASSERT_FALSE(dup.has_value());
  EXPECT_EQ(dup.error(), reloco::error::already_exists);

  auto one = map.try_at(1);
  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(one->get(), "one");
  one->get() = "uno";
  auto const_one = std::as_const(map).try_at(1);
  ASSERT_TRUE(const_one.has_value());
  EXPECT_EQ(const_one->get(), "uno");
  EXPECT_EQ(map.try_at(3).error(), reloco::error::not_found);

  auto first = map.try_first_key_value();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->first.get(), 1);
  auto last = map.try_last_key_value();
  ASSERT_TRUE(last.has_value());
  EXPECT_EQ(last->first.get(), 2);

  ASSERT_TRUE(map.try_remove(1).has_value());
  EXPECT_FALSE(map.contains(1));
  EXPECT_EQ(map.size(), 1u);
}

TEST(MapTest, BalancedAndMatchesStdMap) {
  std::mt19937 rng(777);
  auto res = reloco::map<int, int>::try_create();
  ASSERT_TRUE(res.has_value());
  auto &map = *res;
  std::map<int, int> ref;

  // sorted load first (degenerate for an unbalanced tree)
  for (int i = 0; i < 10000; ++i) {
    ASSERT_TRUE(map.try_insert(i, i * 2).has_value());
    ref.emplace(i, i * 2);
  }
  EXPECT_LE(map.height(), 2u * 14u);

  for (int step = 0; step < 20000; ++step) {
    const int key = static_cast<int>(rng() % 12000);
    if (rng() % 2) {
      EXPECT_EQ(map.try_insert(key, step).has_value(), ref.emplace(key, step).second);
    } else {
      EXPECT_EQ(map.try_remove(key).has_value(), ref.erase(key) == 1);
    }
  }
  ASSERT_EQ(map.size(), ref.size());
  auto it = map.begin();
  for (const auto &[k, v] : ref) {
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->first, k);
    EXPECT_EQ(it->second, v);
    ++it;
  }
  EXPECT_LE(map.height(), 2u * 15u);
}

TEST(MapTest, EntryRetainAppendClone) {
  auto a_res = reloco::map<int, int>::try_create();
  auto b_res = reloco::map<int, int>::try_create();
  ASSERT_TRUE(a_res.has_value() && b_res.has_value());
  auto &a = *a_res;
  auto &b = *b_res;

  ASSERT_TRUE(a.try_entry_or_insert(1, 10).has_value());
  auto existing = a.try_entry_or_insert(1, 99);
  ASSERT_TRUE(existing.has_value());
  EXPECT_EQ(existing->get(), 10);
  ASSERT_TRUE(a.try_entry_and_modify(1, [](int &v) { v += 1; }).has_value());
  auto eleven = a.try_at(1);
  ASSERT_TRUE(eleven.has_value());
  EXPECT_EQ(eleven->get(), 11);

  ASSERT_TRUE(b.try_insert(1, 500).has_value());
  ASSERT_TRUE(b.try_insert(2, 600).has_value());
  a.append(b);
  EXPECT_TRUE(b.empty());
  auto replaced = a.try_at(1);
  ASSERT_TRUE(replaced.has_value());
  EXPECT_EQ(replaced->get(), 500);

  auto clone = a.try_clone();
  ASSERT_TRUE(clone.has_value());
  a.retain([](const int &k, int &) { return k == 2; });
  EXPECT_EQ(a.size(), 1u);
  EXPECT_EQ(clone->size(), 2u);
}

static_assert(reloco::is_trivially_relocatable_v<reloco::map<int, int>>);
