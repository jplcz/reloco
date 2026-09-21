// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/inline_flat_map.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/string.hpp>

#include <cstddef>
#include <string>

TEST(InlineFlatMapTest, DefaultConstructedIsEmpty) {
  reloco::inline_flat_map<int, int, 4> map;
  EXPECT_EQ(map.size(), 0);
  EXPECT_TRUE(map.empty());
  EXPECT_EQ(map.begin(), map.end());
  EXPECT_EQ(map.capacity(), 4);
}

TEST(InlineFlatMapTest, MaintainsSortedOrderByKey) {
  reloco::inline_flat_map<int, int, 8> map;

  ASSERT_TRUE(map.try_insert(30, 300).has_value());
  ASSERT_TRUE(map.try_insert(10, 100).has_value());
  ASSERT_TRUE(map.try_insert(20, 200).has_value());
  ASSERT_TRUE(map.try_insert(5, 50).has_value());

  EXPECT_EQ(map.size(), 4);

  int expected_keys[] = {5, 10, 20, 30};
  std::size_t idx = 0;
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  for (auto it = map.begin(); it != map.end(); ++it, ++idx) {
    EXPECT_EQ(it->first, expected_keys[idx]);
    EXPECT_EQ(it->second, expected_keys[idx] * 10);
  }
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(InlineFlatMapTest, EnforcesUniqueKeys) {
  reloco::inline_flat_map<int, int, 4> map;

  auto ins1 = map.try_insert(42, 1);
  ASSERT_TRUE(ins1.has_value());
  EXPECT_EQ(ins1->get(), 1);
  EXPECT_EQ(map.size(), 1);

  auto ins2 = map.try_insert(42, 2);
  ASSERT_FALSE(ins2.has_value());
  EXPECT_EQ(ins2.error(), reloco::error::already_exists);
  EXPECT_EQ(map.size(), 1);
}

TEST(InlineFlatMapTest, TryInsertFailsWhenAtCapacity) {
  reloco::inline_flat_map<int, int, 2> map;

  ASSERT_TRUE(map.try_insert(1, 10));
  ASSERT_TRUE(map.try_insert(2, 20));

  auto res = map.try_insert(3, 30);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), reloco::error::capacity_exceeded);
  EXPECT_EQ(map.size(), 2);
}

TEST(InlineFlatMapTest, ContainsAndTryAt) {
  reloco::inline_flat_map<int, int, 4> map;

  ASSERT_TRUE(map.try_insert(1, 10));
  ASSERT_TRUE(map.try_insert(2, 20));

  EXPECT_TRUE(map.contains(1));
  EXPECT_FALSE(map.contains(3));

  auto found = map.try_at(2);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 20);

  auto not_found = map.try_at(999);
  ASSERT_FALSE(not_found.has_value());
  EXPECT_EQ(not_found.error(), reloco::error::not_found);
}

TEST(InlineFlatMapTest, TryAtAllowsMutatingMappedValue) {
  reloco::inline_flat_map<int, int, 4> map;
  ASSERT_TRUE(map.try_insert(1, 100));

  auto found = map.try_at(1);
  ASSERT_TRUE(found.has_value());
  found->get() = 999;

  auto found2 = map.try_at(1);
  ASSERT_TRUE(found2.has_value());
  EXPECT_EQ(found2->get(), 999);
}

TEST(InlineFlatMapTest, TryAtConstReturnsReadOnlyReference) {
  reloco::inline_flat_map<int, int, 4> map;
  ASSERT_TRUE(map.try_insert(1, 100));

  const auto &const_map = map;
  auto found = const_map.try_at(1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 100);
}

TEST(InlineFlatMapTest, TryRemoveErasesEntry) {
  reloco::inline_flat_map<int, int, 4> map;

  ASSERT_TRUE(map.try_insert(1, 10));
  ASSERT_TRUE(map.try_insert(2, 20));

  ASSERT_TRUE(map.try_remove(1));
  EXPECT_EQ(map.size(), 1);
  EXPECT_FALSE(map.contains(1));

  auto missing = map.try_remove(1);
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), reloco::error::not_found);
}

TEST(InlineFlatMapTest, ClearResetsSize) {
  reloco::inline_flat_map<int, int, 4> map;

  ASSERT_TRUE(map.try_insert(1, 10));
  ASSERT_TRUE(map.try_insert(2, 20));
  EXPECT_EQ(map.size(), 2);

  map.clear();
  EXPECT_EQ(map.size(), 0);
  EXPECT_TRUE(map.empty());
}

TEST(InlineFlatMapTest, DeepCloneWithDefaultAllocator) {
  reloco::inline_flat_map<int, int, 4> map;
  ASSERT_TRUE(map.try_insert(10, 100));
  ASSERT_TRUE(map.try_insert(20, 200));

  auto clone_res = map.try_clone();
  ASSERT_TRUE(clone_res.has_value());
  auto &clone = *clone_res;

  EXPECT_EQ(clone.size(), 2);
  EXPECT_TRUE(clone.contains(10));
  EXPECT_TRUE(clone.contains(20));

  ASSERT_TRUE(map.try_insert(30, 300));
  EXPECT_EQ(map.size(), 3);
  EXPECT_EQ(clone.size(), 2);
  EXPECT_FALSE(clone.contains(30));
}

TEST(InlineFlatMapTest, DeepCloneWithExplicitAllocator) {
  reloco::inline_flat_map<int, int, 4> map;
  ASSERT_TRUE(map.try_insert(1, 10));

  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto clone_res = map.try_clone(heap);
  ASSERT_TRUE(clone_res.has_value());
  EXPECT_EQ(clone_res->size(), 1);
}

TEST(InlineFlatMapTest, MutableContainerRefIntegration) {
  reloco::inline_flat_map<int, int, 4> map;

  reloco::mutable_container_ref<int, int> cref(map);

  EXPECT_TRUE(cref.empty());
  EXPECT_TRUE(cref.is_associative());

  ASSERT_TRUE(cref.try_insert_at(50, 500).has_value());
  ASSERT_TRUE(cref.try_insert_at(25, 250).has_value());
  EXPECT_EQ(cref.size(), 2);

  auto val = cref.try_at(25);
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(val->get(), 250);

  ASSERT_TRUE(cref.try_erase(25).has_value());
  EXPECT_EQ(cref.size(), 1);
  EXPECT_FALSE(map.contains(25));
}

TEST(InlineFlatMapTest, IsTriviallyRelocatableDependsOnKeyAndMappedType) {
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<reloco::inline_flat_map<int, int, 4>>));
  EXPECT_FALSE((reloco::is_trivially_relocatable_v<reloco::inline_flat_map<int, std::string, 4>>));
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<reloco::inline_flat_map<int, reloco::string, 4>>));
}

TEST(InlineFlatMapTest, TryToFlatMapClonesWithoutConsumingSource) {
  reloco::inline_flat_map<int, int, 4> map;
  ASSERT_TRUE(map.try_insert(30, 300));
  ASSERT_TRUE(map.try_insert(10, 100));
  ASSERT_TRUE(map.try_insert(20, 200));

  auto flat_res = map.try_to_flat_map();
  ASSERT_TRUE(flat_res.has_value());
  auto &flat = *flat_res;

  EXPECT_EQ(flat.size(), 3);
  auto v10 = flat.try_at(10);
  ASSERT_TRUE(v10.has_value());
  EXPECT_EQ(v10->get(), 100);

  // Original map is untouched, and independent of the clone.
  EXPECT_EQ(map.size(), 3);
  ASSERT_TRUE(map.try_insert(40, 400));
  EXPECT_EQ(map.size(), 4);
  EXPECT_FALSE(flat.contains(40));
}

TEST(InlineFlatMapTest, TryToFlatMapMoveConsumesSource) {
  reloco::inline_flat_map<int, int, 4> map;
  ASSERT_TRUE(map.try_insert(1, 10));
  ASSERT_TRUE(map.try_insert(2, 20));

  auto flat_res = std::move(map).try_to_flat_map();
  ASSERT_TRUE(flat_res.has_value());
  EXPECT_EQ(flat_res->size(), 2);
  auto v = flat_res->try_at(2);
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->get(), 20);

  EXPECT_EQ(map.size(), 0);
}

TEST(InlineFlatMapTest, TryToFlatMapWithExplicitAllocator) {
  reloco::inline_flat_map<int, int, 4> map;
  ASSERT_TRUE(map.try_insert(1, 10));

  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto flat_res = map.try_to_flat_map(heap);
  ASSERT_TRUE(flat_res.has_value());
  EXPECT_EQ(flat_res->size(), 1);
}

TEST(InlineFlatMapTest, TryToFlatMapEmptySourceProducesEmptyMap) {
  reloco::inline_flat_map<int, int, 4> map;
  auto flat_res = map.try_to_flat_map();
  ASSERT_TRUE(flat_res.has_value());
  EXPECT_TRUE(flat_res->empty());
}
