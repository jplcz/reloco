// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/tree_set.hpp>

#include <cstddef>
#include <utility>

TEST(TreeSetTest, DefaultConstruction) {
  auto set_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());

  const auto &set = *set_res;
  EXPECT_EQ(set.size(), 0);
  EXPECT_TRUE(set.empty());
  EXPECT_EQ(set.begin(), set.end());
}

TEST(TreeSetTest, MaintainsSortedOrder) {
  auto set_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  ASSERT_TRUE(set.try_insert(30).has_value());
  ASSERT_TRUE(set.try_insert(10).has_value());
  ASSERT_TRUE(set.try_insert(20).has_value());
  ASSERT_TRUE(set.try_insert(5).has_value());

  EXPECT_EQ(set.size(), 4);

  int expected[] = {5, 10, 20, 30};
  std::size_t idx = 0;
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  for (auto it = set.begin(); it != set.end(); ++it, ++idx) {
    EXPECT_EQ(*it, expected[idx]);
  }
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(TreeSetTest, EnforcesUniqueness) {
  auto set_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  auto ins1 = set.try_insert(42);
  ASSERT_TRUE(ins1.has_value());
  EXPECT_EQ(ins1->get(), 42);
  EXPECT_EQ(set.size(), 1);

  auto ins2 = set.try_insert(42);
  ASSERT_FALSE(ins2.has_value());
  EXPECT_EQ(ins2.error(), reloco::error::already_exists);
  EXPECT_EQ(set.size(), 1);
}

TEST(TreeSetTest, ContainsAndTryFind) {
  auto set_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  ASSERT_TRUE(set.try_insert(100));
  ASSERT_TRUE(set.try_insert(200));
  ASSERT_TRUE(set.try_insert(300));

  EXPECT_TRUE(set.contains(100));
  EXPECT_TRUE(set.contains(200));
  EXPECT_TRUE(set.contains(300));
  EXPECT_FALSE(set.contains(150));
  EXPECT_FALSE(set.contains(999));

  auto found = set.try_find(200);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 200);

  auto not_found = set.try_find(999);
  ASSERT_FALSE(not_found.has_value());
  EXPECT_EQ(not_found.error(), reloco::error::not_found);
}

TEST(TreeSetTest, RemoveShrinksSize) {
  auto set_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  ASSERT_TRUE(set.try_insert(1));
  ASSERT_TRUE(set.try_insert(2));
  ASSERT_TRUE(set.try_insert(3));

  ASSERT_TRUE(set.try_remove(2).has_value());
  EXPECT_EQ(set.size(), 2);
  EXPECT_FALSE(set.contains(2));

  auto missing = set.try_remove(2);
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), reloco::error::not_found);
}

TEST(TreeSetTest, ClearResetsSize) {
  auto set_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  ASSERT_TRUE(set.try_insert(1));
  ASSERT_TRUE(set.try_insert(2));
  EXPECT_EQ(set.size(), 2);

  set.clear();
  EXPECT_EQ(set.size(), 0);
  EXPECT_TRUE(set.empty());
}

TEST(TreeSetTest, DeepClone) {
  auto set_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  ASSERT_TRUE(set.try_insert(10));
  ASSERT_TRUE(set.try_insert(20));

  auto clone_res = set.try_clone();
  ASSERT_TRUE(clone_res.has_value());
  auto &clone = *clone_res;

  EXPECT_EQ(clone.size(), 2);
  EXPECT_TRUE(clone.contains(10));
  EXPECT_TRUE(clone.contains(20));

  ASSERT_TRUE(set.try_insert(30));
  EXPECT_EQ(set.size(), 3);
  EXPECT_EQ(clone.size(), 2);
  EXPECT_FALSE(clone.contains(30));
}

TEST(TreeSetTest, MoveConstruction) {
  auto set_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  ASSERT_TRUE(set.try_insert(1));
  ASSERT_TRUE(set.try_insert(2));

  auto moved = std::move(set);
  EXPECT_EQ(moved.size(), 2);
  EXPECT_TRUE(moved.contains(1));
  EXPECT_TRUE(moved.contains(2));
}

TEST(TreeSetTest, MutableContainerRefIntegration) {
  auto set_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  reloco::mutable_container_ref<int, int> cref(set);

  EXPECT_TRUE(cref.empty());
  EXPECT_TRUE(cref.is_associative());

  ASSERT_TRUE(cref.try_insert_at(50, 50).has_value());
  ASSERT_TRUE(cref.try_insert_at(25, 25).has_value());
  EXPECT_EQ(cref.size(), 2);

  auto val = cref.try_at(25);
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(val->get(), 25);

  ASSERT_TRUE(cref.try_erase(25).has_value());
  EXPECT_EQ(cref.size(), 1);
  EXPECT_FALSE(set.contains(25));
}

TEST(TreeSetTest, IsTriviallyRelocatable) {
  // std::less<int> is stateless/trivially copyable, so tree_set<int> is
  // trivially relocatable end-to-end.
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<reloco::tree_set<int>>);
}

TEST(TreeSetTest, TryFirstAndTryLast) {
  auto set_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  auto empty_first = set.try_first();
  ASSERT_FALSE(empty_first.has_value());
  EXPECT_EQ(empty_first.error(), reloco::error::container_empty);
  auto empty_last = set.try_last();
  ASSERT_FALSE(empty_last.has_value());
  EXPECT_EQ(empty_last.error(), reloco::error::container_empty);

  ASSERT_TRUE(set.try_insert(30));
  ASSERT_TRUE(set.try_insert(10));
  ASSERT_TRUE(set.try_insert(20));

  auto first = set.try_first();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->get(), 10);

  auto last = set.try_last();
  ASSERT_TRUE(last.has_value());
  EXPECT_EQ(last->get(), 30);

  // Not removed.
  EXPECT_EQ(set.size(), 3);
}

TEST(TreeSetTest, TryPopFirstAndTryPopLast) {
  auto set_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  auto empty_pop = set.try_pop_first();
  ASSERT_FALSE(empty_pop.has_value());
  EXPECT_EQ(empty_pop.error(), reloco::error::container_empty);

  ASSERT_TRUE(set.try_insert(30));
  ASSERT_TRUE(set.try_insert(10));
  ASSERT_TRUE(set.try_insert(20));

  auto popped_first = set.try_pop_first();
  ASSERT_TRUE(popped_first.has_value());
  EXPECT_EQ(*popped_first, 10);
  EXPECT_EQ(set.size(), 2);
  EXPECT_FALSE(set.contains(10));

  auto popped_last = set.try_pop_last();
  ASSERT_TRUE(popped_last.has_value());
  EXPECT_EQ(*popped_last, 30);
  EXPECT_EQ(set.size(), 1);
  EXPECT_FALSE(set.contains(30));

  EXPECT_TRUE(set.contains(20));
}

TEST(TreeSetTest, Retain) {
  auto set_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  for (int v : {1, 2, 3, 4, 5, 6}) {
    ASSERT_TRUE(set.try_insert(v));
  }

  set.retain([](const int &v) { return v % 2 == 0; });

  EXPECT_EQ(set.size(), 3);
  EXPECT_TRUE(set.contains(2));
  EXPECT_TRUE(set.contains(4));
  EXPECT_TRUE(set.contains(6));
  EXPECT_FALSE(set.contains(1));
  EXPECT_FALSE(set.contains(3));
  EXPECT_FALSE(set.contains(5));
}

TEST(TreeSetTest, Append) {
  auto set1_res = reloco::tree_set<int>::try_create();
  auto set2_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set1_res.has_value());
  ASSERT_TRUE(set2_res.has_value());
  auto &set1 = *set1_res;
  auto &set2 = *set2_res;

  ASSERT_TRUE(set1.try_insert(1));
  ASSERT_TRUE(set1.try_insert(3));
  ASSERT_TRUE(set2.try_insert(2));
  ASSERT_TRUE(set2.try_insert(4));

  set1.append(set2);

  EXPECT_EQ(set1.size(), 4);
  EXPECT_TRUE(set1.contains(1));
  EXPECT_TRUE(set1.contains(2));
  EXPECT_TRUE(set1.contains(3));
  EXPECT_TRUE(set1.contains(4));
  EXPECT_TRUE(set2.empty());
}

TEST(TreeSetTest, AppendOverwritesOnKeyCollision) {
  auto set1_res = reloco::tree_set<int>::try_create();
  auto set2_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set1_res.has_value());
  ASSERT_TRUE(set2_res.has_value());
  auto &set1 = *set1_res;
  auto &set2 = *set2_res;

  ASSERT_TRUE(set1.try_insert(1));
  ASSERT_TRUE(set1.try_insert(2));
  ASSERT_TRUE(set2.try_insert(2));
  ASSERT_TRUE(set2.try_insert(3));

  set1.append(set2);

  EXPECT_EQ(set1.size(), 3);
  EXPECT_TRUE(set1.contains(1));
  EXPECT_TRUE(set1.contains(2));
  EXPECT_TRUE(set1.contains(3));
  EXPECT_TRUE(set2.empty());
}

TEST(TreeSetTest, IsSubsetSupersetDisjoint) {
  auto a_res = reloco::tree_set<int>::try_create();
  auto b_res = reloco::tree_set<int>::try_create();
  auto c_res = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(a_res.has_value());
  ASSERT_TRUE(b_res.has_value());
  ASSERT_TRUE(c_res.has_value());
  auto &a = *a_res;
  auto &b = *b_res;
  auto &c = *c_res;

  for (int v : {1, 2}) {
    ASSERT_TRUE(a.try_insert(v));
  }
  for (int v : {1, 2, 3, 4}) {
    ASSERT_TRUE(b.try_insert(v));
  }
  for (int v : {5, 6}) {
    ASSERT_TRUE(c.try_insert(v));
  }

  EXPECT_TRUE(a.is_subset(b));
  EXPECT_FALSE(b.is_subset(a));
  EXPECT_TRUE(b.is_superset(a));
  EXPECT_FALSE(a.is_superset(b));

  EXPECT_TRUE(a.is_disjoint(c));
  EXPECT_TRUE(c.is_disjoint(a));
  EXPECT_FALSE(a.is_disjoint(b));

  EXPECT_TRUE(a.is_subset(a));
  EXPECT_TRUE(a.is_superset(a));
}
