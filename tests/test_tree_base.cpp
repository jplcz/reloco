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

TEST(TreeBaseTest, TryFirstAndTryLastOnEmptyTree) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);

  auto first = tree->try_first();
  ASSERT_FALSE(first);
  EXPECT_EQ(first.error(), reloco::error::container_empty);

  auto last = tree->try_last();
  ASSERT_FALSE(last);
  EXPECT_EQ(last.error(), reloco::error::container_empty);
}

TEST(TreeBaseTest, TryFirstAndTryLastReturnExtremes) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  for (int v : {50, 10, 30, 70, 20}) {
    ASSERT_TRUE(tree->try_insert(v));
  }

  auto first = tree->try_first();
  ASSERT_TRUE(first);
  EXPECT_EQ(first->get(), 10);

  auto last = tree->try_last();
  ASSERT_TRUE(last);
  EXPECT_EQ(last->get(), 70);

  EXPECT_EQ(tree->size(), 5);
}

TEST(TreeBaseTest, TryPopFirstAndTryPopLastOnEmptyTree) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);

  auto popped = tree->try_pop_first();
  ASSERT_FALSE(popped);
  EXPECT_EQ(popped.error(), reloco::error::container_empty);
}

TEST(TreeBaseTest, TryPopFirstAndTryPopLastRemoveExtremes) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  for (int v : {50, 10, 30, 70, 20}) {
    ASSERT_TRUE(tree->try_insert(v));
  }

  auto popped_first = tree->try_pop_first();
  ASSERT_TRUE(popped_first);
  EXPECT_EQ(*popped_first, 10);
  EXPECT_EQ(tree->size(), 4);
  EXPECT_FALSE(tree->contains(10));

  auto popped_last = tree->try_pop_last();
  ASSERT_TRUE(popped_last);
  EXPECT_EQ(*popped_last, 70);
  EXPECT_EQ(tree->size(), 3);
  EXPECT_FALSE(tree->contains(70));

  std::vector<int> remaining(tree->begin(), tree->end());
  EXPECT_EQ(remaining, (std::vector<int>{20, 30, 50}));
}

TEST(TreeBaseTest, TryPopFirstDestroysNonTrivialPayload) {
  auto tree = string_set::try_create();
  ASSERT_TRUE(tree);
  ASSERT_TRUE(tree->try_insert("banana"));
  ASSERT_TRUE(tree->try_insert("apple"));

  auto popped = tree->try_pop_first();
  ASSERT_TRUE(popped);
  EXPECT_EQ(*popped, "apple");
  EXPECT_EQ(tree->size(), 1);
  EXPECT_TRUE(tree->contains("banana"));
}

TEST(TreeBaseTest, RetainKeepsOnlyMatchingElements) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  for (int v : {1, 2, 3, 4, 5, 6}) {
    ASSERT_TRUE(tree->try_insert(v));
  }

  tree->retain([](const int &v) { return v % 2 == 0; });

  EXPECT_EQ(tree->size(), 3);
  std::vector<int> remaining(tree->begin(), tree->end());
  EXPECT_EQ(remaining, (std::vector<int>{2, 4, 6}));
}

TEST(TreeBaseTest, RetainOnEmptyTreeIsNoOp) {
  auto tree = int_set::try_create();
  ASSERT_TRUE(tree);
  tree->retain([](const int &) { return false; });
  EXPECT_TRUE(tree->empty());
}

TEST(TreeBaseTest, RetainDestroysNonTrivialPayloadOfRemovedElements) {
  auto tree = string_set::try_create();
  ASSERT_TRUE(tree);
  ASSERT_TRUE(tree->try_insert("apple"));
  ASSERT_TRUE(tree->try_insert("banana"));
  ASSERT_TRUE(tree->try_insert("cherry"));

  tree->retain([](const std::string &s) { return s != "banana"; });

  EXPECT_EQ(tree->size(), 2);
  EXPECT_TRUE(tree->contains("apple"));
  EXPECT_TRUE(tree->contains("cherry"));
  EXPECT_FALSE(tree->contains("banana"));
}

TEST(TreeBaseTest, AppendMovesElementsWithoutReallocating) {
  auto tree1 = int_set::try_create();
  auto tree2 = int_set::try_create();
  ASSERT_TRUE(tree1);
  ASSERT_TRUE(tree2);

  ASSERT_TRUE(tree1->try_insert(1));
  ASSERT_TRUE(tree1->try_insert(3));
  ASSERT_TRUE(tree2->try_insert(2));
  ASSERT_TRUE(tree2->try_insert(4));

  tree1->append(*tree2);

  EXPECT_EQ(tree1->size(), 4);
  EXPECT_TRUE(tree2->empty());
  std::vector<int> all(tree1->begin(), tree1->end());
  EXPECT_EQ(all, (std::vector<int>{1, 2, 3, 4}));
}

TEST(TreeBaseTest, AppendOnKeyCollisionOverwritesWithOthersValue) {
  auto tree1 = int_map::try_create();
  auto tree2 = int_map::try_create();
  ASSERT_TRUE(tree1);
  ASSERT_TRUE(tree2);

  ASSERT_TRUE(tree1->try_insert({1, "one"}));
  ASSERT_TRUE(tree1->try_insert({2, "two-old"}));
  ASSERT_TRUE(tree2->try_insert({2, "two-new"}));

  tree1->append(*tree2);

  EXPECT_EQ(tree1->size(), 2);
  EXPECT_TRUE(tree2->empty());
  auto found = tree1->try_find(2);
  ASSERT_TRUE(found);
  EXPECT_EQ(found->get().second, "two-new");
}

TEST(TreeBaseTest, AppendEmptyOtherIsNoOp) {
  auto tree1 = int_set::try_create();
  auto tree2 = int_set::try_create();
  ASSERT_TRUE(tree1);
  ASSERT_TRUE(tree2);
  ASSERT_TRUE(tree1->try_insert(1));

  tree1->append(*tree2);

  EXPECT_EQ(tree1->size(), 1);
  EXPECT_TRUE(tree2->empty());
}

TEST(TreeBaseTest, IsSubsetIsSupersetIsDisjoint) {
  auto a = int_set::try_create();
  auto b = int_set::try_create();
  auto c = int_set::try_create();
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);

  for (int v : {1, 2}) {
    ASSERT_TRUE(a->try_insert(v));
  }
  for (int v : {1, 2, 3}) {
    ASSERT_TRUE(b->try_insert(v));
  }
  for (int v : {9, 10}) {
    ASSERT_TRUE(c->try_insert(v));
  }

  EXPECT_TRUE(a->is_subset(*b));
  EXPECT_FALSE(b->is_subset(*a));
  EXPECT_TRUE(b->is_superset(*a));
  EXPECT_TRUE(a->is_disjoint(*c));
  EXPECT_FALSE(a->is_disjoint(*b));
}
