// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/tree_map.hpp>

#include <cstddef>
#include <string>
#include <utility>

TEST(TreeMapTest, DefaultConstruction) {
  auto map_res = reloco::tree_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());

  const auto &map = *map_res;
  EXPECT_EQ(map.size(), 0);
  EXPECT_TRUE(map.empty());
  EXPECT_EQ(map.begin(), map.end());
}

TEST(TreeMapTest, MaintainsSortedOrderByKey) {
  auto map_res = reloco::tree_map<int, int>::try_create();
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

TEST(TreeMapTest, EnforcesUniqueKeys) {
  auto map_res = reloco::tree_map<int, int>::try_create();
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

TEST(TreeMapTest, ContainsAndTryAt) {
  auto map_res = reloco::tree_map<int, std::string>::try_create();
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

TEST(TreeMapTest, TryAtAllowsMutatingMappedValue) {
  auto map_res = reloco::tree_map<int, int>::try_create();
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

TEST(TreeMapTest, TryAtConstReturnsReadOnlyReference) {
  auto map_res = reloco::tree_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  ASSERT_TRUE(map_res->try_insert(1, 100));

  const auto &map = *map_res;
  auto found = map.try_at(1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 100);
}

TEST(TreeMapTest, TryRemoveErasesEntry) {
  auto map_res = reloco::tree_map<int, int>::try_create();
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

TEST(TreeMapTest, ClearResetsSize) {
  auto map_res = reloco::tree_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

  ASSERT_TRUE(map.try_insert(1, 10));
  ASSERT_TRUE(map.try_insert(2, 20));
  EXPECT_EQ(map.size(), 2);

  map.clear();
  EXPECT_EQ(map.size(), 0);
  EXPECT_TRUE(map.empty());
}

TEST(TreeMapTest, DeepClone) {
  auto map_res = reloco::tree_map<int, int>::try_create();
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

TEST(TreeMapTest, DeepCloneWithExplicitAllocator) {
  auto map_res = reloco::tree_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  ASSERT_TRUE(map_res->try_insert(1, 10));

  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto clone_res = map_res->try_clone(heap);
  ASSERT_TRUE(clone_res.has_value());
  EXPECT_EQ(clone_res->size(), 1);
}

TEST(TreeMapTest, TryEntryOrInsert) {
  auto map_res = reloco::tree_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

  auto inserted = map.try_entry_or_insert(1, 10);
  ASSERT_TRUE(inserted.has_value());
  EXPECT_EQ(inserted->get(), 10);

  // Existing key: value is not overwritten.
  auto existing = map.try_entry_or_insert(1, 999);
  ASSERT_TRUE(existing.has_value());
  EXPECT_EQ(existing->get(), 10);
  EXPECT_EQ(map.size(), 1);
}

TEST(TreeMapTest, TryEntryAndModify) {
  auto map_res = reloco::tree_map<int, int>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

  ASSERT_TRUE(map.try_insert(1, 10));

  auto modified = map.try_entry_and_modify(1, [](int &value) { value += 5; });
  ASSERT_TRUE(modified.has_value());
  EXPECT_EQ(modified->get(), 15);

  auto missing = map.try_entry_and_modify(2, [](int &value) { value += 5; });
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), reloco::error::not_found);
}

TEST(TreeMapTest, MutableContainerRefIntegration) {
  auto map_res = reloco::tree_map<int, int>::try_create();
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

TEST(TreeMapTest, IsTriviallyRelocatableRegardlessOfMappedType) {
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<reloco::tree_map<int, int>>));
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<reloco::tree_map<int, std::string>>));
}

TEST(TreeMapTest, TryFirstAndLastKeyValue) {
  auto map_res = reloco::tree_map<int, std::string>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

  auto empty_first = map.try_first_key_value();
  ASSERT_FALSE(empty_first.has_value());
  EXPECT_EQ(empty_first.error(), reloco::error::container_empty);
  auto empty_last = map.try_last_key_value();
  ASSERT_FALSE(empty_last.has_value());
  EXPECT_EQ(empty_last.error(), reloco::error::container_empty);

  ASSERT_TRUE(map.try_insert(30, "thirty"));
  ASSERT_TRUE(map.try_insert(10, "ten"));
  ASSERT_TRUE(map.try_insert(20, "twenty"));

  auto first = map.try_first_key_value();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->first.get(), 10);
  EXPECT_EQ(first->second.get(), "ten");

  auto last = map.try_last_key_value();
  ASSERT_TRUE(last.has_value());
  EXPECT_EQ(last->first.get(), 30);
  EXPECT_EQ(last->second.get(), "thirty");

  // Not removed.
  EXPECT_EQ(map.size(), 3);
}

TEST(TreeMapTest, TryPopFirstAndTryPopLast) {
  auto map_res = reloco::tree_map<int, std::string>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

  auto empty_pop = map.try_pop_first();
  ASSERT_FALSE(empty_pop.has_value());
  EXPECT_EQ(empty_pop.error(), reloco::error::container_empty);

  ASSERT_TRUE(map.try_insert(30, "thirty"));
  ASSERT_TRUE(map.try_insert(10, "ten"));
  ASSERT_TRUE(map.try_insert(20, "twenty"));

  auto popped_first = map.try_pop_first();
  ASSERT_TRUE(popped_first.has_value());
  EXPECT_EQ(popped_first->first, 10);
  EXPECT_EQ(popped_first->second, "ten");
  EXPECT_EQ(map.size(), 2);
  EXPECT_FALSE(map.contains(10));

  auto popped_last = map.try_pop_last();
  ASSERT_TRUE(popped_last.has_value());
  EXPECT_EQ(popped_last->first, 30);
  EXPECT_EQ(popped_last->second, "thirty");
  EXPECT_EQ(map.size(), 1);
  EXPECT_FALSE(map.contains(30));

  EXPECT_TRUE(map.contains(20));
}

TEST(TreeMapTest, Retain) {
  auto map_res = reloco::tree_map<int, std::string>::try_create();
  ASSERT_TRUE(map_res.has_value());
  auto &map = *map_res;

  ASSERT_TRUE(map.try_insert(1, "one"));
  ASSERT_TRUE(map.try_insert(2, "two"));
  ASSERT_TRUE(map.try_insert(3, "three"));
  ASSERT_TRUE(map.try_insert(4, "four"));

  map.retain([](const int &key, std::string &value) {
    if (key % 2 == 0) {
      value += "-kept";
      return true;
    }
    return false;
  });

  EXPECT_EQ(map.size(), 2);
  EXPECT_FALSE(map.contains(1));
  EXPECT_FALSE(map.contains(3));

  auto two = map.try_at(2);
  ASSERT_TRUE(two.has_value());
  EXPECT_EQ(two->get(), "two-kept");

  auto four = map.try_at(4);
  ASSERT_TRUE(four.has_value());
  EXPECT_EQ(four->get(), "four-kept");
}

TEST(TreeMapTest, Append) {
  auto map1_res = reloco::tree_map<int, std::string>::try_create();
  auto map2_res = reloco::tree_map<int, std::string>::try_create();
  ASSERT_TRUE(map1_res.has_value());
  ASSERT_TRUE(map2_res.has_value());
  auto &map1 = *map1_res;
  auto &map2 = *map2_res;

  ASSERT_TRUE(map1.try_insert(1, "one"));
  ASSERT_TRUE(map1.try_insert(2, "two-old"));
  ASSERT_TRUE(map2.try_insert(2, "two-new"));
  ASSERT_TRUE(map2.try_insert(3, "three"));

  map1.append(map2);

  EXPECT_EQ(map1.size(), 3);
  EXPECT_TRUE(map2.empty());

  auto one = map1.try_at(1);
  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(one->get(), "one");

  // Colliding key 2: other's value wins, matching Rust BTreeMap::append.
  auto two = map1.try_at(2);
  ASSERT_TRUE(two.has_value());
  EXPECT_EQ(two->get(), "two-new");

  auto three = map1.try_at(3);
  ASSERT_TRUE(three.has_value());
  EXPECT_EQ(three->get(), "three");
}
