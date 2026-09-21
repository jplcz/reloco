// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/flat_map.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/relocatable.hpp>

#include <cstddef>
#include <string>
#include <utility>

TEST(FlatMapTest, DefaultConstruction) {
  auto map_res = reloco::flat_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());

  const auto &map = *map_res;
  EXPECT_EQ(map.size(), 0);
  EXPECT_TRUE(map.empty());
  EXPECT_EQ(map.begin(), map.end());
}

TEST(FlatMapTest, AllocateWithCapacity) {
  auto map_res = reloco::flat_map<int, int>::try_create(16);
  ASSERT_TRUE(map_res.has_value());

  auto &map = *map_res;
  EXPECT_EQ(map.size(), 0);
  EXPECT_GE(map.capacity(), 16);
}

TEST(FlatMapTest, MaintainsSortedOrderByKey) {
  auto map_res = reloco::flat_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

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

TEST(FlatMapTest, EnforcesUniqueKeys) {
  auto map_res = reloco::flat_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

  auto ins1 = map.try_insert(42, 1);
  ASSERT_TRUE(ins1.has_value());
  EXPECT_EQ(ins1->get(), 1);
  EXPECT_EQ(map.size(), 1);

  auto ins2 = map.try_insert(42, 2);
  ASSERT_FALSE(ins2.has_value());
  EXPECT_EQ(ins2.error(), reloco::error::already_exists);
  EXPECT_EQ(map.size(), 1);
}

TEST(FlatMapTest, ContainsAndTryAt) {
  auto map_res = reloco::flat_map<int, std::string>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

  ASSERT_TRUE(map.try_insert(1, "one"));
  ASSERT_TRUE(map.try_insert(2, "two"));

  EXPECT_TRUE(map.contains(1));
  EXPECT_FALSE(map.contains(3));

  auto found = map.try_at(2);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), "two");

  auto not_found = map.try_at(999);
  ASSERT_FALSE(not_found.has_value());
  EXPECT_EQ(not_found.error(), reloco::error::not_found);
}

TEST(FlatMapTest, TryAtAllowsMutatingMappedValue) {
  auto map_res = reloco::flat_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

  ASSERT_TRUE(map.try_insert(1, 100));

  auto found = map.try_at(1);
  ASSERT_TRUE(found.has_value());
  found->get() = 999;

  auto found2 = map.try_at(1);
  ASSERT_TRUE(found2.has_value());
  EXPECT_EQ(found2->get(), 999);
}

TEST(FlatMapTest, TryAtConstReturnsReadOnlyReference) {
  auto map_res = reloco::flat_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  ASSERT_TRUE(map_res->try_insert(1, 100));

  const auto &map = *map_res;
  auto found = map.try_at(1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 100);
}

TEST(FlatMapTest, TryRemoveErasesEntry) {
  auto map_res = reloco::flat_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

  ASSERT_TRUE(map.try_insert(1, 10));
  ASSERT_TRUE(map.try_insert(2, 20));

  ASSERT_TRUE(map.try_remove(1));
  EXPECT_EQ(map.size(), 1);
  EXPECT_FALSE(map.contains(1));
  EXPECT_TRUE(map.contains(2));

  auto missing = map.try_remove(1);
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), reloco::error::not_found);
}

TEST(FlatMapTest, ClearResetsSize) {
  auto map_res = reloco::flat_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

  ASSERT_TRUE(map.try_insert(1, 10));
  ASSERT_TRUE(map.try_insert(2, 20));
  EXPECT_EQ(map.size(), 2);

  map.clear();
  EXPECT_EQ(map.size(), 0);
  EXPECT_TRUE(map.empty());
}

TEST(FlatMapTest, DeepClone) {
  auto map_res = reloco::flat_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

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

TEST(FlatMapTest, DeepCloneWithExplicitAllocator) {
  auto map_res = reloco::flat_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  ASSERT_TRUE(map_res->try_insert(1, 10));

  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto clone_res = map_res->try_clone(heap);
  ASSERT_TRUE(clone_res.has_value());
  EXPECT_EQ(clone_res->size(), 1);
}

TEST(FlatMapTest, MutableContainerRefIntegration) {
  auto map_res = reloco::flat_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

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

TEST(FlatMapTest, IsTriviallyRelocatableRegardlessOfMappedType) {
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<reloco::flat_map<int, int>>));
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<reloco::flat_map<int, std::string>>));
}
