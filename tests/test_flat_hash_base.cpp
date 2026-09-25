// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/detail/flat_hash_base.hpp>
#include <reloco/heap_allocator.hpp>

#include <algorithm>
#include <functional>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using reloco::detail::flat_hash_base;
using reloco::detail::identity_key_of;
using reloco::detail::pair_key_of;

namespace {

using int_set = flat_hash_base<int, std::hash<int>, std::equal_to<int>, identity_key_of>;
using string_set = flat_hash_base<std::string, std::hash<std::string>, std::equal_to<std::string>, identity_key_of>;
using int_map = flat_hash_base<std::pair<int, std::string>, std::hash<int>, std::equal_to<int>, pair_key_of>;

// Forces many collisions so tests exercise probing/backward-shift deletion
// instead of the (much less interesting) all-distinct-slots case.
struct bad_hash {
  std::size_t operator()(int v) const noexcept { return static_cast<std::size_t>(v) & 0x3; }
};
using collision_prone_set = flat_hash_base<int, bad_hash, std::equal_to<int>, identity_key_of>;

reloco::allocator_ref heap() { return reloco::allocator<reloco::heap_allocator_tag>::ref(); }

std::vector<int> sorted_contents(const int_set &set) {
  std::vector<int> values(set.begin(), set.end());
  std::sort(values.begin(), values.end());
  return values;
}

} // namespace

TEST(FlatHashBaseTest, DefaultConstructedIsEmpty) {
  int_set table;
  EXPECT_EQ(table.size(), 0u);
  EXPECT_TRUE(table.empty());
  EXPECT_EQ(table.capacity(), 0u);
  EXPECT_EQ(table.begin(), table.end());
}

TEST(FlatHashBaseTest, TryCreateSucceeds) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  EXPECT_TRUE(table->empty());
}

TEST(FlatHashBaseTest, InsertAndFindSingleElement) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);

  auto ins = table->try_insert(42);
  ASSERT_TRUE(ins);
  EXPECT_EQ(ins->get(), 42);
  EXPECT_EQ(table->size(), 1u);

  EXPECT_TRUE(table->contains(42));
  auto found = table->try_find(42);
  ASSERT_TRUE(found);
  EXPECT_EQ(found->get(), 42);
}

TEST(FlatHashBaseTest, DuplicateInsertFails) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  ASSERT_TRUE(table->try_insert(5));
  auto dup = table->try_insert(5);
  ASSERT_FALSE(dup);
  EXPECT_EQ(dup.error(), reloco::error::already_exists);
  EXPECT_EQ(table->size(), 1u);
}

TEST(FlatHashBaseTest, FindMissingFails) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  ASSERT_TRUE(table->try_insert(1));
  EXPECT_FALSE(table->contains(2));
  auto found = table->try_find(2);
  ASSERT_FALSE(found);
  EXPECT_EQ(found.error(), reloco::error::not_found);
}

TEST(FlatHashBaseTest, RemoveExistingSucceeds) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  ASSERT_TRUE(table->try_insert(1));
  ASSERT_TRUE(table->try_insert(2));

  ASSERT_TRUE(table->try_remove(1));
  EXPECT_FALSE(table->contains(1));
  EXPECT_TRUE(table->contains(2));
  EXPECT_EQ(table->size(), 1u);
}

TEST(FlatHashBaseTest, RemoveMissingFails) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  ASSERT_TRUE(table->try_insert(1));
  auto rr = table->try_remove(2);
  ASSERT_FALSE(rr);
  EXPECT_EQ(rr.error(), reloco::error::not_found);
}

TEST(FlatHashBaseTest, TryTakeRemovesAndReturnsValue) {
  auto table = string_set::try_create();
  ASSERT_TRUE(table);
  ASSERT_TRUE(table->try_insert("apple"));
  ASSERT_TRUE(table->try_insert("banana"));

  auto taken = table->try_take(std::string("apple"));
  ASSERT_TRUE(taken);
  EXPECT_EQ(*taken, "apple");
  EXPECT_FALSE(table->contains("apple"));
  EXPECT_TRUE(table->contains("banana"));
}

TEST(FlatHashBaseTest, TryTakeMissingFails) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  auto taken = table->try_take(1);
  ASSERT_FALSE(taken);
  EXPECT_EQ(taken.error(), reloco::error::not_found);
}

TEST(FlatHashBaseTest, GrowsAcrossManyInsertions) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  for (int v = 0; v < 500; ++v) {
    ASSERT_TRUE(table->try_insert(v));
  }
  EXPECT_EQ(table->size(), 500u);
  EXPECT_GE(table->capacity(), 500u);
  for (int v = 0; v < 500; ++v) {
    EXPECT_TRUE(table->contains(v));
  }
}

TEST(FlatHashBaseTest, CapacityIsAlwaysPowerOfTwoOrZero) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  for (int v = 0; v < 1000; ++v) {
    ASSERT_TRUE(table->try_insert(v));
    std::size_t cap = table->capacity();
    EXPECT_TRUE(cap == 0 || (cap & (cap - 1)) == 0) << "capacity=" << cap;
  }
}

TEST(FlatHashBaseTest, IterationVisitsEveryElementExactlyOnce) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  for (int v : {5, 3, 8, 1, 4, 7, 9, 2, 6, 0}) {
    ASSERT_TRUE(table->try_insert(v));
  }
  EXPECT_EQ(sorted_contents(*table), (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
}

TEST(FlatHashBaseTest, ForEachVisitsEveryElement) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  for (int v : {1, 2, 3}) {
    ASSERT_TRUE(table->try_insert(v));
  }
  std::vector<int> seen;
  table->for_each([&](const int &v) { seen.push_back(v); });
  std::sort(seen.begin(), seen.end());
  EXPECT_EQ(seen, (std::vector<int>{1, 2, 3}));
}

TEST(FlatHashBaseTest, ClearEmptiesTheTable) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  for (int v : {5, 3, 8, 1, 4}) {
    ASSERT_TRUE(table->try_insert(v));
  }
  table->clear();
  EXPECT_TRUE(table->empty());
  EXPECT_EQ(table->size(), 0u);
  EXPECT_EQ(table->begin(), table->end());
}

TEST(FlatHashBaseTest, DestructorDestroysNonTrivialElements) {
  auto table = string_set::try_create();
  ASSERT_TRUE(table);
  for (const char *s : {"delta", "alpha", "charlie", "bravo"}) {
    ASSERT_TRUE(table->try_insert(std::string(s)));
  }
  EXPECT_EQ(table->size(), 4u);
}

TEST(FlatHashBaseTest, MoveConstructionTransfersOwnership) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  for (int v : {1, 2, 3}) {
    ASSERT_TRUE(table->try_insert(v));
  }
  int_set moved(std::move(*table));
  EXPECT_EQ(moved.size(), 3u);
  EXPECT_EQ(sorted_contents(moved), (std::vector<int>{1, 2, 3}));
}

TEST(FlatHashBaseTest, MoveAssignmentTransfersOwnership) {
  auto table_a = int_set::try_create();
  auto table_b = int_set::try_create();
  ASSERT_TRUE(table_a);
  ASSERT_TRUE(table_b);
  ASSERT_TRUE(table_a->try_insert(1));
  ASSERT_TRUE(table_b->try_insert(2));

  *table_b = std::move(*table_a);
  EXPECT_EQ(table_b->size(), 1u);
  EXPECT_TRUE(table_b->contains(1));
  EXPECT_FALSE(table_b->contains(2));
}

TEST(FlatHashBaseTest, TryCloneProducesIndependentCopy) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  for (int v : {5, 3, 8, 1, 4}) {
    ASSERT_TRUE(table->try_insert(v));
  }

  auto cloned = table->try_clone();
  ASSERT_TRUE(cloned);
  EXPECT_EQ(sorted_contents(*cloned), (std::vector<int>{1, 3, 4, 5, 8}));

  ASSERT_TRUE(table->try_remove(3));
  EXPECT_TRUE(cloned->contains(3));
  EXPECT_FALSE(table->contains(3));
}

TEST(FlatHashBaseTest, MapLikeUsageWithPairKeyOf) {
  auto table = int_map::try_create();
  ASSERT_TRUE(table);
  ASSERT_TRUE(table->try_insert({2, "two"}));
  ASSERT_TRUE(table->try_insert({1, "one"}));
  ASSERT_TRUE(table->try_insert({3, "three"}));

  auto found = table->try_find(2);
  ASSERT_TRUE(found);
  EXPECT_EQ(found->get().second, "two");

  std::vector<int> keys;
  for (const auto &kv : *table) {
    keys.push_back(kv.first);
  }
  std::sort(keys.begin(), keys.end());
  EXPECT_EQ(keys, (std::vector<int>{1, 2, 3}));
}

TEST(FlatHashBaseTest, ExplicitAllocatorConstruction) {
  auto table = int_set::try_allocate(heap());
  ASSERT_TRUE(table);
  ASSERT_TRUE(table->try_insert(7));
  EXPECT_TRUE(table->contains(7));
}

TEST(FlatHashBaseTest, TryReserveIsNoOpForZeroAdditional) {
  int_set table;
  ASSERT_TRUE(table.try_reserve(0));
  EXPECT_EQ(table.capacity(), 0u);
}

TEST(FlatHashBaseTest, TryReserveGrowsCapacityUpfront) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  ASSERT_TRUE(table->try_reserve(100));
  std::size_t cap_after_reserve = table->capacity();
  EXPECT_GT(cap_after_reserve, 0u);
  for (int v = 0; v < 100; ++v) {
    ASSERT_TRUE(table->try_insert(v));
  }
  // No growth should have been necessary since try_reserve already sized
  // the table for 100 elements.
  EXPECT_EQ(table->capacity(), cap_after_reserve);
}

TEST(FlatHashBaseTest, RetainKeepsOnlyMatchingElements) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  for (int v : {1, 2, 3, 4, 5, 6}) {
    ASSERT_TRUE(table->try_insert(v));
  }

  table->retain([](const int &v) { return v % 2 == 0; });

  EXPECT_EQ(table->size(), 3u);
  EXPECT_EQ(sorted_contents(*table), (std::vector<int>{2, 4, 6}));
}

TEST(FlatHashBaseTest, RetainOnEmptyTableIsNoOp) {
  auto table = int_set::try_create();
  ASSERT_TRUE(table);
  table->retain([](const int &) { return false; });
  EXPECT_TRUE(table->empty());
}

TEST(FlatHashBaseTest, RetainWithHeavyCollisionsPreservesReachability) {
  // Regression test for backward-shift deletion: with a hash that maps
  // every key into one of four buckets, removing elements during retain()
  // must not strand any surviving element outside its probe sequence.
  auto table = collision_prone_set::try_create();
  ASSERT_TRUE(table);
  for (int v = 0; v < 64; ++v) {
    ASSERT_TRUE(table->try_insert(v));
  }

  table->retain([](const int &v) { return v % 3 == 0; });

  std::vector<int> expected;
  for (int v = 0; v < 64; ++v) {
    if (v % 3 == 0)
      expected.push_back(v);
  }
  EXPECT_EQ(table->size(), expected.size());
  for (int v : expected) {
    EXPECT_TRUE(table->contains(v));
  }
}

TEST(FlatHashBaseTest, IsSubsetIsSupersetIsDisjoint) {
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
  EXPECT_FALSE(a->is_superset(*b));
  EXPECT_TRUE(a->is_disjoint(*c));
  EXPECT_FALSE(a->is_disjoint(*b));
}

TEST(FlatHashBaseTest, RandomizedInsertRemoveMatchesReferenceSetWithHeavyCollisions) {
  auto table = collision_prone_set::try_create();
  ASSERT_TRUE(table);
  std::unordered_set<int> reference;

  std::mt19937 rng(12345);
  std::uniform_int_distribution<int> value_dist(0, 500);
  std::uniform_int_distribution<int> op_dist(0, 1);

  for (int iter = 0; iter < 20000; ++iter) {
    int v = value_dist(rng);
    if (op_dist(rng) == 0) {
      auto ins = table->try_insert(v);
      EXPECT_EQ(static_cast<bool>(ins), reference.insert(v).second);
    } else {
      auto rem = table->try_remove(v);
      EXPECT_EQ(static_cast<bool>(rem), reference.erase(v) > 0);
    }
    ASSERT_EQ(table->size(), reference.size());
  }

  for (int v : reference) {
    EXPECT_TRUE(table->contains(v));
  }
  std::size_t iterated = 0;
  for (const auto &v : *table) {
    (void)v;
    ++iterated;
  }
  EXPECT_EQ(iterated, table->size());
}
