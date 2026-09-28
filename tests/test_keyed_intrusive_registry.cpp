// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/default_allocator.hpp>
#include <reloco/keyed_intrusive_registry.hpp>
#include <reloco/spin_lock.hpp>

#include <array>
#include <string>

namespace {
struct int_tag {};
struct string_tag {};
} // namespace

TEST(KeyedIntrusiveRegistryTest, GetOrCreateThenFind) {
  using registry_type = reloco::keyed_intrusive_registry<int, int, int_tag>;
  std::array<registry_type::node *, 8> buckets{};
  registry_type registry(reloco::span<registry_type::node *>(buckets.data(), buckets.size()));

  auto created = registry.get_or_create(1, reloco::default_allocator(), 42);
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(created->get(), 42);
  EXPECT_EQ(registry.size(), 1u);

  auto found = registry.try_find(1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 42);

  // Second get_or_create for the same key returns the existing value, not a fresh default.
  auto again = registry.get_or_create(1, reloco::default_allocator(), 999);
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(again->get(), 42);
  EXPECT_EQ(registry.size(), 1u);
}

TEST(KeyedIntrusiveRegistryTest, TryFindMissingFails) {
  using registry_type = reloco::keyed_intrusive_registry<int, int, int_tag>;
  std::array<registry_type::node *, 8> buckets{};
  registry_type registry(reloco::span<registry_type::node *>(buckets.data(), buckets.size()));

  auto found = registry.try_find(1);
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error(), reloco::error::not_found);
}

TEST(KeyedIntrusiveRegistryTest, SetCreatesThenOverwrites) {
  using registry_type = reloco::keyed_intrusive_registry<std::string, int, string_tag>;
  std::array<registry_type::node *, 8> buckets{};
  registry_type registry(reloco::span<registry_type::node *>(buckets.data(), buckets.size()));

  ASSERT_TRUE(registry.set(1, "one", reloco::default_allocator()).has_value());
  auto found = registry.try_find(1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), "one");

  ASSERT_TRUE(registry.set(1, "uno", reloco::default_allocator()).has_value());
  found = registry.try_find(1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), "uno");
  EXPECT_EQ(registry.size(), 1u);
}

TEST(KeyedIntrusiveRegistryTest, EraseRemovesAndDeallocates) {
  using registry_type = reloco::keyed_intrusive_registry<int, int, int_tag>;
  std::array<registry_type::node *, 8> buckets{};
  registry_type registry(reloco::span<registry_type::node *>(buckets.data(), buckets.size()));

  ASSERT_TRUE(registry.get_or_create(1, reloco::default_allocator(), 1).has_value());
  ASSERT_TRUE(registry.get_or_create(2, reloco::default_allocator(), 2).has_value());
  EXPECT_EQ(registry.size(), 2u);

  ASSERT_TRUE(registry.erase(1).has_value());
  EXPECT_EQ(registry.size(), 1u);
  EXPECT_FALSE(registry.try_find(1).has_value());
  EXPECT_TRUE(registry.try_find(2).has_value());

  // Erasing an already-absent key fails.
  auto second_erase = registry.erase(1);
  ASSERT_FALSE(second_erase.has_value());
  EXPECT_EQ(second_erase.error(), reloco::error::not_found);
}

TEST(KeyedIntrusiveRegistryTest, MultipleKeysCoexistAcrossBucketChains) {
  using registry_type = reloco::keyed_intrusive_registry<int, int, int_tag>;
  std::array<registry_type::node *, 4> buckets{};
  registry_type registry(reloco::span<registry_type::node *>(buckets.data(), buckets.size()));

  for (int i = 0; i < 20; ++i)
    ASSERT_TRUE(registry.get_or_create(i, reloco::default_allocator(), i * 10).has_value());
  EXPECT_EQ(registry.size(), 20u);

  for (int i = 0; i < 20; ++i) {
    auto found = registry.try_find(i);
    ASSERT_TRUE(found.has_value()) << "key=" << i;
    EXPECT_EQ(found->get(), i * 10);
  }

  for (int i = 0; i < 20; ++i)
    ASSERT_TRUE(registry.erase(i).has_value());
  EXPECT_EQ(registry.size(), 0u);
}

TEST(KeyedIntrusiveRegistryTest, RehashGrowsBucketArrayAndPreservesEntries) {
  using registry_type = reloco::keyed_intrusive_registry<int, int, int_tag>;
  std::array<registry_type::node *, 2> small_buckets{};
  registry_type registry(reloco::span<registry_type::node *>(small_buckets.data(), small_buckets.size()));

  for (int i = 0; i < 10; ++i)
    ASSERT_TRUE(registry.get_or_create(i, reloco::default_allocator(), i).has_value());

  std::size_t suggested = registry.suggest_bucket_count_for_insert(0, 1, 1000);
  EXPECT_GE(suggested, 10u);

  std::array<registry_type::node *, 16> big_buckets{};
  ASSERT_TRUE(registry.rehash(reloco::span<registry_type::node *>(big_buckets.data(), big_buckets.size()))
                  .has_value());
  EXPECT_EQ(registry.bucket_count(), 16u);
  EXPECT_EQ(registry.size(), 10u);

  for (int i = 0; i < 10; ++i) {
    auto found = registry.try_find(i);
    ASSERT_TRUE(found.has_value()) << "key=" << i;
    EXPECT_EQ(found->get(), i);
  }
}

TEST(KeyedIntrusiveRegistryTest, LoadFactorPermilleTracksSizeOverBuckets) {
  using registry_type = reloco::keyed_intrusive_registry<int, int, int_tag>;
  std::array<registry_type::node *, 4> buckets{};
  registry_type registry(reloco::span<registry_type::node *>(buckets.data(), buckets.size()));

  EXPECT_EQ(registry.load_factor_permille(), 0u);
  ASSERT_TRUE(registry.get_or_create(1, reloco::default_allocator(), 1).has_value());
  ASSERT_TRUE(registry.get_or_create(2, reloco::default_allocator(), 2).has_value());
  EXPECT_EQ(registry.load_factor_permille(), 500u); // 2 elements / 4 buckets == 500 permille.
}

TEST(KeyedIntrusiveRegistryTest, UsableWithASpinLock) {
  using registry_type = reloco::keyed_intrusive_registry<int, int, int_tag, reloco::spin_lock>;
  std::array<registry_type::node *, 8> buckets{};
  registry_type registry(reloco::span<registry_type::node *>(buckets.data(), buckets.size()));

  ASSERT_TRUE(registry.get_or_create(1, reloco::default_allocator(), 7).has_value());
  auto found = registry.try_find(1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 7);
}
