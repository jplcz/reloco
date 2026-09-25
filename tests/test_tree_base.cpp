// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/detail/tree_base.hpp>
#include <reloco/heap_allocator.hpp>

#include <algorithm>
#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>

using reloco::detail::identity_key_of;
using reloco::detail::pair_key_of;
using reloco::detail::tree_base;

namespace {

using int_set = tree_base<int, std::less<>, identity_key_of>;
using string_set = tree_base<std::string, std::less<>, identity_key_of>;
using int_map = tree_base<std::pair<int, std::string>, std::less<>, pair_key_of>;

reloco::allocator_ref heap() { return reloco::allocator<reloco::heap_allocator_tag>::ref(); }

} // namespace

TEST(TreeBaseTest, DefaultConstructedIsEmpty) {
  int_set tree;
  EXPECT_EQ(tree.size(), 0u);
  EXPECT_TRUE(tree.empty());
  EXPECT_EQ(tree.begin(), tree.end());
}

TEST(TreeBaseTest, TryCreateSucceeds) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  EXPECT_TRUE(tree->empty());
}

TEST(TreeBaseTest, InsertAndFindSingleElement) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);

  auto ins = tree->try_insert(42);
  ASSERT_TRUE(ins);
  EXPECT_EQ(ins->get(), 42);
  EXPECT_EQ(tree->size(), 1u);

  EXPECT_TRUE(tree->contains(42));
  auto found = tree->try_find(42);
  ASSERT_TRUE(found);
  EXPECT_EQ(found->get(), 42);
}

TEST(TreeBaseTest, DuplicateInsertFails) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  ASSERT_TRUE(tree->try_insert(5));
  auto dup = tree->try_insert(5);
  ASSERT_FALSE(dup);
  EXPECT_EQ(dup.error(), reloco::error::already_exists);
  EXPECT_EQ(tree->size(), 1u);
}

TEST(TreeBaseTest, FindMissingFails) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  ASSERT_TRUE(tree->try_insert(1));
  EXPECT_FALSE(tree->contains(2));
  auto found = tree->try_find(2);
  ASSERT_FALSE(found);
  EXPECT_EQ(found.error(), reloco::error::not_found);
}

TEST(TreeBaseTest, InOrderIterationIsSorted) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  for (int v : {5, 3, 8, 1, 4, 7, 9, 2, 6, 0}) {
    ASSERT_TRUE(tree->try_insert(v));
  }
  std::vector<int> collected(tree->begin(), tree->end());
  EXPECT_EQ(collected, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
  EXPECT_EQ(tree->size(), 10u);
}

TEST(TreeBaseTest, ReverseIterationIsSorted) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  for (int v : {5, 3, 8, 1, 4}) {
    ASSERT_TRUE(tree->try_insert(v));
  }
  std::vector<int> collected;
  for (auto it = tree->end(); it != tree->begin();) {
    --it;
    collected.push_back(*it);
  }
  EXPECT_EQ(collected, (std::vector<int>{8, 5, 4, 3, 1}));
}

TEST(TreeBaseTest, RemoveLeafNode) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  for (int v : {5, 3, 8}) {
    ASSERT_TRUE(tree->try_insert(v));
  }
  ASSERT_TRUE(tree->try_remove(3));
  EXPECT_FALSE(tree->contains(3));
  EXPECT_EQ(tree->size(), 2u);
  std::vector<int> collected(tree->begin(), tree->end());
  EXPECT_EQ(collected, (std::vector<int>{5, 8}));
}

TEST(TreeBaseTest, RemoveNodeWithOneChild) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  for (int v : {5, 3, 8, 1}) {
    ASSERT_TRUE(tree->try_insert(v));
  }
  ASSERT_TRUE(tree->try_remove(3));
  EXPECT_FALSE(tree->contains(3));
  EXPECT_TRUE(tree->contains(1));
  std::vector<int> collected(tree->begin(), tree->end());
  EXPECT_EQ(collected, (std::vector<int>{1, 5, 8}));
}

TEST(TreeBaseTest, RemoveNodeWithTwoChildren) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  for (int v : {5, 3, 8, 1, 4, 7, 9}) {
    ASSERT_TRUE(tree->try_insert(v));
  }
  ASSERT_TRUE(tree->try_remove(5));
  EXPECT_FALSE(tree->contains(5));
  std::vector<int> collected(tree->begin(), tree->end());
  EXPECT_EQ(collected, (std::vector<int>{1, 3, 4, 7, 8, 9}));
  EXPECT_EQ(tree->size(), 6u);
}

TEST(TreeBaseTest, RemoveRoot) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  ASSERT_TRUE(tree->try_insert(1));
  ASSERT_TRUE(tree->try_remove(1));
  EXPECT_TRUE(tree->empty());
  EXPECT_EQ(tree->begin(), tree->end());
}

TEST(TreeBaseTest, RemoveMissingFails) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  ASSERT_TRUE(tree->try_insert(1));
  auto res = tree->try_remove(2);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), reloco::error::not_found);
}

TEST(TreeBaseTest, RandomizedInsertRemoveMatchesReferenceSet) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  std::mt19937 rng(12345);
  std::uniform_int_distribution<int> dist(0, 199);
  std::vector<int> reference;

  for (int i = 0; i < 500; ++i) {
    int v = dist(rng);
    bool already = std::find(reference.begin(), reference.end(), v) != reference.end();
    auto ins = tree->try_insert(v);
    if (already) {
      EXPECT_FALSE(ins);
    } else {
      ASSERT_TRUE(ins);
      reference.push_back(v);
    }
  }

  std::sort(reference.begin(), reference.end());
  std::vector<int> collected(tree->begin(), tree->end());
  EXPECT_EQ(collected, reference);

  std::mt19937 removal_rng(999);
  std::shuffle(reference.begin(), reference.end(), removal_rng);
  for (int v : reference) {
    ASSERT_TRUE(tree->try_remove(v));
    EXPECT_FALSE(tree->contains(v));
  }
  EXPECT_TRUE(tree->empty());
}

TEST(TreeBaseTest, ClearEmptiesTheTree) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  for (int v : {5, 3, 8, 1, 4}) {
    ASSERT_TRUE(tree->try_insert(v));
  }
  tree->clear();
  EXPECT_TRUE(tree->empty());
  EXPECT_EQ(tree->size(), 0u);
  EXPECT_EQ(tree->begin(), tree->end());
}

TEST(TreeBaseTest, DestructorDestroysNonTrivialElements) {
  auto tree = string_set::try_create();
  ASSERT_TRUE(tree);
  for (const char *s : {"delta", "alpha", "charlie", "bravo"}) {
    ASSERT_TRUE(tree->try_insert(std::string(s)));
  }
  std::vector<std::string> collected(tree->begin(), tree->end());
  EXPECT_EQ(collected, (std::vector<std::string>{"alpha", "bravo", "charlie", "delta"}));
}

TEST(TreeBaseTest, MoveConstructionTransfersOwnership) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  for (int v : {1, 2, 3}) {
    ASSERT_TRUE(tree->try_insert(v));
  }
  int_set moved(std::move(*tree));
  EXPECT_EQ(moved.size(), 3u);
  std::vector<int> collected(moved.begin(), moved.end());
  EXPECT_EQ(collected, (std::vector<int>{1, 2, 3}));
}

TEST(TreeBaseTest, MoveAssignmentTransfersOwnership) {
  auto tree_a = int_set::try_create();
  auto tree_b = int_set::try_create();
  ASSERT_TRUE(tree_a);
  ASSERT_TRUE(tree_b);
  ASSERT_TRUE(tree_a->try_insert(1));
  ASSERT_TRUE(tree_b->try_insert(2));

  *tree_b = std::move(*tree_a);
  EXPECT_EQ(tree_b->size(), 1u);
  EXPECT_TRUE(tree_b->contains(1));
  EXPECT_FALSE(tree_b->contains(2));
}

TEST(TreeBaseTest, TryCloneProducesIndependentCopy) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  for (int v : {5, 3, 8, 1, 4}) {
    ASSERT_TRUE(tree->try_insert(v));
  }

  auto cloned = tree->try_clone();
  ASSERT_TRUE(cloned);
  std::vector<int> collected(cloned->begin(), cloned->end());
  EXPECT_EQ(collected, (std::vector<int>{1, 3, 4, 5, 8}));

  ASSERT_TRUE(tree->try_remove(3));
  EXPECT_TRUE(cloned->contains(3));
  EXPECT_FALSE(tree->contains(3));
}

TEST(TreeBaseTest, MapLikeUsageWithPairKeyOf) {
  auto tree = int_map::try_create();
  ASSERT_TRUE(tree);
  ASSERT_TRUE(tree->try_insert({2, "two"}));
  ASSERT_TRUE(tree->try_insert({1, "one"}));
  ASSERT_TRUE(tree->try_insert({3, "three"}));

  auto found = tree->try_find(2);
  ASSERT_TRUE(found);
  EXPECT_EQ(found->get().second, "two");

  std::vector<int> keys;
  for (const auto &kv : *tree) {
    keys.push_back(kv.first);
  }
  EXPECT_EQ(keys, (std::vector<int>{1, 2, 3}));
}

TEST(TreeBaseTest, ExplicitAllocatorConstruction) {
  auto tree = int_set::try_allocate(heap());
  ASSERT_TRUE(tree);
  ASSERT_TRUE(tree->try_insert(7));
  EXPECT_TRUE(tree->contains(7));
}
