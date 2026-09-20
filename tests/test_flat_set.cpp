// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/flat_set.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>

#include <cstddef>
#include <string>
#include <utility>

TEST(FlatSetTest, DefaultConstruction) {
  auto set_res = reloco::flat_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());

  const auto &set = *set_res;
  EXPECT_EQ(set.size(), 0);
  EXPECT_TRUE(set.empty());
  EXPECT_EQ(set.begin(), set.end());
}

TEST(FlatSetTest, AllocateWithCapacity) {
  auto set_res = reloco::flat_set<int>::try_create(16);
  ASSERT_TRUE(set_res.has_value());

  auto &set = *set_res;
  EXPECT_EQ(set.size(), 0);
  EXPECT_GE(set.capacity(), 16);
}

TEST(FlatSetTest, MaintainsSortedOrder) {
  auto set_res = reloco::flat_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  // Insert elements out of order
  ASSERT_TRUE(set.try_insert(30).has_value());
  ASSERT_TRUE(set.try_insert(10).has_value());
  ASSERT_TRUE(set.try_insert(20).has_value());
  ASSERT_TRUE(set.try_insert(5).has_value());

  EXPECT_EQ(set.size(), 4);

  // Verify elements are strictly sorted
  int expected[] = {5, 10, 20, 30};
  std::size_t idx = 0;
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  for (auto it = set.begin(); it != set.end(); ++it, ++idx) {
    EXPECT_EQ(*it, expected[idx]);
  }
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(FlatSetTest, EnforcesUniqueness) {
  auto set_res = reloco::flat_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  // First insertion succeeds
  auto ins1 = set.try_insert(42);
  ASSERT_TRUE(ins1.has_value());
  EXPECT_EQ(ins1->get(), 42);
  EXPECT_EQ(set.size(), 1);

  // Duplicate insertion fails with already_exists
  auto ins2 = set.try_insert(42);
  ASSERT_FALSE(ins2.has_value());
  EXPECT_EQ(ins2.error(), reloco::error::already_exists);
  EXPECT_EQ(set.size(), 1);
}

TEST(FlatSetTest, ContainsAndTryFind) {
  auto set_res = reloco::flat_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  ASSERT_TRUE(set.try_insert(100));
  ASSERT_TRUE(set.try_insert(200));
  ASSERT_TRUE(set.try_insert(300));

  // Test contains
  EXPECT_TRUE(set.contains(100));
  EXPECT_TRUE(set.contains(200));
  EXPECT_TRUE(set.contains(300));
  EXPECT_FALSE(set.contains(150));
  EXPECT_FALSE(set.contains(999));

  // Test try_find success
  auto found = set.try_find(200);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 200);

  // Test try_find failure
  auto not_found = set.try_find(999);
  ASSERT_FALSE(not_found.has_value());
  EXPECT_EQ(not_found.error(), reloco::error::not_found);
}

TEST(FlatSetTest, ClearResetsSize) {
  auto set_res = reloco::flat_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  ASSERT_TRUE(set.try_insert(1));
  ASSERT_TRUE(set.try_insert(2));
  EXPECT_EQ(set.size(), 2);

  set.clear();
  EXPECT_EQ(set.size(), 0);
  EXPECT_TRUE(set.empty());
}

TEST(FlatSetTest, DeepClone) {
  auto set_res = reloco::flat_set<int>::try_create();
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

  // Verify independence by modifying original
  ASSERT_TRUE(set.try_insert(30));
  EXPECT_EQ(set.size(), 3);
  EXPECT_EQ(clone.size(), 2);
  EXPECT_FALSE(clone.contains(30));
}

TEST(FlatSetTest, MutableContainerRefIntegration) {
  auto set_res = reloco::flat_set<int>::try_create();
  ASSERT_TRUE(set_res.has_value());
  auto &set = *set_res;

  // Bind via type-erased associative container ref
  reloco::mutable_container_ref<int, int> cref(set);

  EXPECT_TRUE(cref.empty());
  EXPECT_TRUE(cref.is_associative());

  // Insert via ref
  ASSERT_TRUE(cref.try_insert_at(50, 50).has_value());
  ASSERT_TRUE(cref.try_insert_at(25, 25).has_value());
  EXPECT_EQ(cref.size(), 2);

  // Access via find / try_at
  auto val = cref.try_at(25);
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(val->get(), 25);

  // Erase via ref
  ASSERT_TRUE(cref.try_erase(25).has_value());
  EXPECT_EQ(cref.size(), 1);
  EXPECT_FALSE(set.contains(25));
}
