// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/inline_flat_set.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/string.hpp>

#include <cstddef>
#include <string>

TEST(InlineFlatSetTest, DefaultConstructedIsEmpty) {
  reloco::inline_flat_set<int, 4> set;
  EXPECT_EQ(set.size(), 0);
  EXPECT_TRUE(set.empty());
  EXPECT_EQ(set.begin(), set.end());
  EXPECT_EQ(set.capacity(), 4);
}

TEST(InlineFlatSetTest, MaintainsSortedOrder) {
  reloco::inline_flat_set<int, 8> set;

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

TEST(InlineFlatSetTest, EnforcesUniqueness) {
  reloco::inline_flat_set<int, 4> set;

  auto ins1 = set.try_insert(42);
  ASSERT_TRUE(ins1.has_value());
  EXPECT_EQ(set.size(), 1);

  auto ins2 = set.try_insert(42);
  ASSERT_FALSE(ins2.has_value());
  EXPECT_EQ(ins2.error(), reloco::error::already_exists);
  EXPECT_EQ(set.size(), 1);
}

TEST(InlineFlatSetTest, TryInsertFailsWhenAtCapacity) {
  reloco::inline_flat_set<int, 2> set;

  ASSERT_TRUE(set.try_insert(1));
  ASSERT_TRUE(set.try_insert(2));

  auto res = set.try_insert(3);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), reloco::error::capacity_exceeded);
  EXPECT_EQ(set.size(), 2);
}

TEST(InlineFlatSetTest, ContainsAndTryFind) {
  reloco::inline_flat_set<int, 8> set;

  ASSERT_TRUE(set.try_insert(100));
  ASSERT_TRUE(set.try_insert(200));

  EXPECT_TRUE(set.contains(100));
  EXPECT_FALSE(set.contains(150));

  auto found = set.try_find(200);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 200);

  auto not_found = set.try_find(999);
  ASSERT_FALSE(not_found.has_value());
  EXPECT_EQ(not_found.error(), reloco::error::not_found);
}

TEST(InlineFlatSetTest, TryRemoveErasesElement) {
  reloco::inline_flat_set<int, 4> set;

  ASSERT_TRUE(set.try_insert(1));
  ASSERT_TRUE(set.try_insert(2));

  ASSERT_TRUE(set.try_remove(1));
  EXPECT_EQ(set.size(), 1);
  EXPECT_FALSE(set.contains(1));

  auto missing = set.try_remove(1);
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), reloco::error::not_found);
}

TEST(InlineFlatSetTest, ClearResetsSize) {
  reloco::inline_flat_set<int, 4> set;

  ASSERT_TRUE(set.try_insert(1));
  ASSERT_TRUE(set.try_insert(2));
  EXPECT_EQ(set.size(), 2);

  set.clear();
  EXPECT_EQ(set.size(), 0);
  EXPECT_TRUE(set.empty());
}

TEST(InlineFlatSetTest, DeepCloneWithDefaultAllocator) {
  reloco::inline_flat_set<int, 4> set;
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

TEST(InlineFlatSetTest, DeepCloneWithExplicitAllocator) {
  reloco::inline_flat_set<int, 4> set;
  ASSERT_TRUE(set.try_insert(1));

  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto clone_res = set.try_clone(heap);
  ASSERT_TRUE(clone_res.has_value());
  EXPECT_EQ(clone_res->size(), 1);
}

TEST(InlineFlatSetTest, MutableContainerRefIntegration) {
  reloco::inline_flat_set<int, 4> set;

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

TEST(InlineFlatSetTest, IsTriviallyRelocatableDependsOnElementType) {
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<reloco::inline_flat_set<int, 4>>));
  EXPECT_FALSE((reloco::is_trivially_relocatable_v<reloco::inline_flat_set<std::string, 4>>));
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<reloco::inline_flat_set<reloco::string, 4>>));
}

TEST(InlineFlatSetTest, TryToFlatSetClonesWithoutConsumingSource) {
  reloco::inline_flat_set<int, 4> set;
  ASSERT_TRUE(set.try_insert(30));
  ASSERT_TRUE(set.try_insert(10));
  ASSERT_TRUE(set.try_insert(20));

  auto flat_res = set.try_to_flat_set();
  ASSERT_TRUE(flat_res.has_value());
  auto &flat = *flat_res;

  EXPECT_EQ(flat.size(), 3);
  EXPECT_TRUE(flat.contains(10) && flat.contains(20) && flat.contains(30));

  // Original set is untouched, and independent of the clone.
  EXPECT_EQ(set.size(), 3);
  ASSERT_TRUE(set.try_insert(40));
  EXPECT_EQ(set.size(), 4);
  EXPECT_FALSE(flat.contains(40));
}

TEST(InlineFlatSetTest, TryToFlatSetMoveConsumesSource) {
  reloco::inline_flat_set<int, 4> set;
  ASSERT_TRUE(set.try_insert(1));
  ASSERT_TRUE(set.try_insert(2));

  auto flat_res = std::move(set).try_to_flat_set();
  ASSERT_TRUE(flat_res.has_value());
  EXPECT_EQ(flat_res->size(), 2);
  EXPECT_TRUE(flat_res->contains(1) && flat_res->contains(2));

  EXPECT_EQ(set.size(), 0);
}

TEST(InlineFlatSetTest, TryToFlatSetWithExplicitAllocator) {
  reloco::inline_flat_set<int, 4> set;
  ASSERT_TRUE(set.try_insert(1));

  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto flat_res = set.try_to_flat_set(heap);
  ASSERT_TRUE(flat_res.has_value());
  EXPECT_EQ(flat_res->size(), 1);
}

TEST(InlineFlatSetTest, TryToFlatSetEmptySourceProducesEmptySet) {
  reloco::inline_flat_set<int, 4> set;
  auto flat_res = set.try_to_flat_set();
  ASSERT_TRUE(flat_res.has_value());
  EXPECT_TRUE(flat_res->empty());
}
