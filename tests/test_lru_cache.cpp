// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/lru_cache.hpp>
#include <reloco/relocatable.hpp>

#include <string>
#include <utility>

TEST(LruCacheTest, DefaultConstruction) {
  auto cache_res = reloco::lru_cache<int, int>::try_create(4);
  ASSERT_TRUE(cache_res.has_value());

  const auto &cache = *cache_res;
  EXPECT_EQ(cache.capacity(), 4);
  EXPECT_EQ(cache.size(), 0);
  EXPECT_TRUE(cache.empty());
  EXPECT_FALSE(cache.is_full());
  EXPECT_EQ(cache.begin(), cache.end());
}

TEST(LruCacheTest, ZeroCapacityFails) {
  auto cache_res = reloco::lru_cache<int, int>::try_create(0);
  ASSERT_FALSE(cache_res.has_value());
  EXPECT_EQ(cache_res.error(), reloco::error::invalid_argument);
}

TEST(LruCacheTest, PutAndGet) {
  auto cache_res = reloco::lru_cache<int, std::string>::try_create(2);
  ASSERT_TRUE(cache_res.has_value());
  auto &cache = *cache_res;

  EXPECT_TRUE(cache.try_put(1, "one").has_value());
  EXPECT_TRUE(cache.try_put(2, "two").has_value());
  EXPECT_EQ(cache.size(), 2);
  EXPECT_TRUE(cache.is_full());

  auto found = cache.try_get(1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), "one");
}

TEST(LruCacheTest, GetPromotesEntryEvictionSpareOldest) {
  auto cache_res = reloco::lru_cache<int, std::string>::try_create(2);
  ASSERT_TRUE(cache_res.has_value());
  auto &cache = *cache_res;

  ASSERT_TRUE(cache.try_put(1, "one").has_value());
  ASSERT_TRUE(cache.try_put(2, "two").has_value());
  ASSERT_TRUE(cache.try_get(1).has_value()); // promote 1 -- 2 is now LRU

  ASSERT_TRUE(cache.try_put(3, "three").has_value()); // evicts 2
  EXPECT_FALSE(cache.contains(2));
  EXPECT_TRUE(cache.contains(1));
  EXPECT_TRUE(cache.contains(3));
  EXPECT_EQ(cache.size(), 2);
}

TEST(LruCacheTest, PutOnExistingKeyUpdatesAndPromotesWithoutEviction) {
  auto cache_res = reloco::lru_cache<int, std::string>::try_create(2);
  ASSERT_TRUE(cache_res.has_value());
  auto &cache = *cache_res;

  ASSERT_TRUE(cache.try_put(1, "one").has_value());
  ASSERT_TRUE(cache.try_put(2, "two").has_value());
  ASSERT_TRUE(cache.try_put(1, "ONE").has_value()); // update + promote, no eviction

  EXPECT_EQ(cache.size(), 2);
  auto found = cache.try_peek(1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), "ONE");
  EXPECT_TRUE(cache.contains(2));
}

TEST(LruCacheTest, PeekDoesNotPromote) {
  auto cache_res = reloco::lru_cache<int, int>::try_create(2);
  ASSERT_TRUE(cache_res.has_value());
  auto &cache = *cache_res;

  ASSERT_TRUE(cache.try_put(1, 100).has_value());
  ASSERT_TRUE(cache.try_put(2, 200).has_value());
  ASSERT_TRUE(cache.try_peek(1).has_value()); // no promotion -- 1 stays LRU

  ASSERT_TRUE(cache.try_put(3, 300).has_value()); // evicts 1, not 2
  EXPECT_FALSE(cache.contains(1));
  EXPECT_TRUE(cache.contains(2));
  EXPECT_TRUE(cache.contains(3));
}

TEST(LruCacheTest, PeekMutMutatesWithoutPromoting) {
  auto cache_res = reloco::lru_cache<int, int>::try_create(2);
  ASSERT_TRUE(cache_res.has_value());
  auto &cache = *cache_res;

  ASSERT_TRUE(cache.try_put(1, 100).has_value());
  ASSERT_TRUE(cache.try_put(2, 200).has_value());

  auto mut_ref = cache.try_peek_mut(1);
  ASSERT_TRUE(mut_ref.has_value());
  mut_ref->get() = 111;

  ASSERT_TRUE(cache.try_put(3, 300).has_value()); // 1 is still LRU -- gets evicted
  EXPECT_FALSE(cache.contains(1));
  auto peeked = cache.try_peek(2);
  ASSERT_TRUE(peeked.has_value());
  EXPECT_EQ(peeked->get(), 200);
}

TEST(LruCacheTest, GetOnMissingKeyFails) {
  auto cache_res = reloco::lru_cache<int, int>::try_create(2);
  ASSERT_TRUE(cache_res.has_value());
  auto &cache = *cache_res;

  auto found = cache.try_get(42);
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error(), reloco::error::not_found);
}

TEST(LruCacheTest, RemoveReturnsValueAndFreesSlotForReuse) {
  auto cache_res = reloco::lru_cache<int, std::string>::try_create(2);
  ASSERT_TRUE(cache_res.has_value());
  auto &cache = *cache_res;

  ASSERT_TRUE(cache.try_put(1, "one").has_value());
  ASSERT_TRUE(cache.try_put(2, "two").has_value());

  auto removed = cache.try_remove(1);
  ASSERT_TRUE(removed.has_value());
  EXPECT_EQ(*removed, "one");
  EXPECT_FALSE(cache.contains(1));
  EXPECT_EQ(cache.size(), 1);

  // Freed slot should be reusable without hitting capacity limits.
  ASSERT_TRUE(cache.try_put(3, "three").has_value());
  ASSERT_TRUE(cache.try_put(4, "four").has_value());
  EXPECT_TRUE(cache.is_full());

  auto missing = cache.try_remove(1);
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), reloco::error::not_found);
}

TEST(LruCacheTest, ClearDropsEverythingButKeepsCapacity) {
  auto cache_res = reloco::lru_cache<int, int>::try_create(3);
  ASSERT_TRUE(cache_res.has_value());
  auto &cache = *cache_res;

  ASSERT_TRUE(cache.try_put(1, 1).has_value());
  ASSERT_TRUE(cache.try_put(2, 2).has_value());
  cache.clear();

  EXPECT_TRUE(cache.empty());
  EXPECT_EQ(cache.capacity(), 3);
  EXPECT_EQ(cache.begin(), cache.end());

  // Cache remains fully usable after clear().
  ASSERT_TRUE(cache.try_put(10, 100).has_value());
  ASSERT_TRUE(cache.try_put(20, 200).has_value());
  ASSERT_TRUE(cache.try_put(30, 300).has_value());
  EXPECT_TRUE(cache.is_full());
}

TEST(LruCacheTest, IterationOrderIsMostToLeastRecentlyUsed) {
  auto cache_res = reloco::lru_cache<int, int>::try_create(3);
  ASSERT_TRUE(cache_res.has_value());
  auto &cache = *cache_res;

  ASSERT_TRUE(cache.try_put(1, 10).has_value());
  ASSERT_TRUE(cache.try_put(2, 20).has_value());
  ASSERT_TRUE(cache.try_put(3, 30).has_value());
  ASSERT_TRUE(cache.try_get(1).has_value()); // promotes 1 to MRU

  auto it = cache.begin();
  ASSERT_NE(it, cache.end());
  EXPECT_EQ((*it).first, 1);
  ++it;
  ASSERT_NE(it, cache.end());
  EXPECT_EQ((*it).first, 3);
  ++it;
  ASSERT_NE(it, cache.end());
  EXPECT_EQ((*it).first, 2);
  ++it;
  EXPECT_EQ(it, cache.end());
}

TEST(LruCacheTest, TryCloneDeepCopiesAndPreservesOrder) {
  auto cache_res = reloco::lru_cache<int, std::string>::try_create(2);
  ASSERT_TRUE(cache_res.has_value());
  auto &cache = *cache_res;

  ASSERT_TRUE(cache.try_put(1, "one").has_value());
  ASSERT_TRUE(cache.try_put(2, "two").has_value());

  auto clone_res = cache.try_clone();
  ASSERT_TRUE(clone_res.has_value());
  auto &clone = *clone_res;

  EXPECT_EQ(clone.size(), 2);
  EXPECT_TRUE(clone.contains(1));
  EXPECT_TRUE(clone.contains(2));

  // Mutating the clone must not affect the source.
  ASSERT_TRUE(clone.try_remove(1).has_value());
  EXPECT_FALSE(clone.contains(1));
  EXPECT_TRUE(cache.contains(1));

  auto it = cache.begin();
  ASSERT_NE(it, cache.end());
  EXPECT_EQ((*it).first, 2);
  ++it;
  ASSERT_NE(it, cache.end());
  EXPECT_EQ((*it).first, 1);
}

TEST(LruCacheTest, MoveConstructionTransfersOwnership) {
  auto cache_res = reloco::lru_cache<int, int>::try_create(2);
  ASSERT_TRUE(cache_res.has_value());
  auto &cache = *cache_res;
  ASSERT_TRUE(cache.try_put(1, 100).has_value());

  reloco::lru_cache<int, int> moved(std::move(cache));
  EXPECT_TRUE(moved.contains(1));
  EXPECT_EQ(moved.capacity(), 2);
}

TEST(LruCacheTest, IsTriviallyRelocatable) {
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<reloco::lru_cache<int, int>>));
}
