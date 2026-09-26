// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/intrusive_hash_table.hpp>

#include <array>
#include <string>
#include <type_traits>

namespace {

struct node {
  int key = 0;
  std::string value;
  reloco::intrusive_hash_hook<node> hook;
};

struct node_key_of {
  const int &operator()(const node &n) const noexcept { return n.key; }
};

using table_type = reloco::intrusive_hash_table<node, &node::hook, node_key_of>;

} // namespace

TEST(IntrusiveHashTableTest, EmptyBucketSpanFails) {
  auto table_res = table_type::try_create(reloco::span<node *>());
  ASSERT_FALSE(table_res.has_value());
  EXPECT_EQ(table_res.error(), reloco::error::invalid_argument);
}

TEST(IntrusiveHashTableTest, DefaultStateAfterCreate) {
  std::array<node *, 4> buckets{};
  auto table_res = table_type::try_create(reloco::span<node *>(buckets.data(), buckets.size()));
  ASSERT_TRUE(table_res.has_value());
  auto &table = *table_res;

  EXPECT_EQ(table.size(), 0);
  EXPECT_TRUE(table.empty());
  EXPECT_EQ(table.bucket_count(), 4);
  EXPECT_EQ(table.load_factor_permille(), 0);
}

TEST(IntrusiveHashTableTest, InsertFindContains) {
  std::array<node *, 4> buckets{};
  auto table_res = table_type::try_create(reloco::span<node *>(buckets.data(), buckets.size()));
  ASSERT_TRUE(table_res.has_value());
  auto &table = *table_res;

  node a{1, "one", {}};
  node b{2, "two", {}};
  ASSERT_TRUE(table.try_insert(a).has_value());
  ASSERT_TRUE(table.try_insert(b).has_value());
  EXPECT_EQ(table.size(), 2);
  EXPECT_FALSE(table.empty());
  EXPECT_EQ(table.load_factor_permille(), 500); // 2 / 4 = 0.5 == 500 permille

  EXPECT_TRUE(table.contains(1));
  EXPECT_TRUE(table.contains(2));
  EXPECT_FALSE(table.contains(3));

  auto found = table.try_find(1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get().value, "one");
}

TEST(IntrusiveHashTableTest, DuplicateKeyRejected) {
  std::array<node *, 4> buckets{};
  auto table_res = table_type::try_create(reloco::span<node *>(buckets.data(), buckets.size()));
  ASSERT_TRUE(table_res.has_value());
  auto &table = *table_res;

  node a{1, "one", {}};
  node b{1, "also one", {}};
  ASSERT_TRUE(table.try_insert(a).has_value());
  auto dup_res = table.try_insert(b);
  ASSERT_FALSE(dup_res.has_value());
  EXPECT_EQ(dup_res.error(), reloco::error::already_exists);
}

TEST(IntrusiveHashTableTest, TryFindMissingFails) {
  std::array<node *, 4> buckets{};
  auto table_res = table_type::try_create(reloco::span<node *>(buckets.data(), buckets.size()));
  ASSERT_TRUE(table_res.has_value());
  auto &table = *table_res;

  auto found = table.try_find(42);
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error(), reloco::error::not_found);
}

TEST(IntrusiveHashTableTest, TryRemoveByKey) {
  std::array<node *, 4> buckets{};
  auto table_res = table_type::try_create(reloco::span<node *>(buckets.data(), buckets.size()));
  ASSERT_TRUE(table_res.has_value());
  auto &table = *table_res;

  node a{1, "one", {}};
  node b{2, "two", {}};
  ASSERT_TRUE(table.try_insert(a).has_value());
  ASSERT_TRUE(table.try_insert(b).has_value());

  ASSERT_TRUE(table.try_remove(1).has_value());
  EXPECT_EQ(table.size(), 1);
  EXPECT_FALSE(table.contains(1));
  EXPECT_TRUE(table.contains(2));
  EXPECT_FALSE(a.hook.is_linked());

  auto missing_res = table.try_remove(1);
  ASSERT_FALSE(missing_res.has_value());
  EXPECT_EQ(missing_res.error(), reloco::error::not_found);
}

TEST(IntrusiveHashTableTest, RemoveByNodeIsConstantTime) {
  std::array<node *, 4> buckets{};
  auto table_res = table_type::try_create(reloco::span<node *>(buckets.data(), buckets.size()));
  ASSERT_TRUE(table_res.has_value());
  auto &table = *table_res;

  node a{1, "one", {}};
  node b{2, "two", {}};
  ASSERT_TRUE(table.try_insert(a).has_value());
  ASSERT_TRUE(table.try_insert(b).has_value());

  table.remove(a);
  EXPECT_EQ(table.size(), 1);
  EXPECT_FALSE(a.hook.is_linked());
  EXPECT_FALSE(table.contains(1));
  EXPECT_TRUE(table.contains(2));
}

TEST(IntrusiveHashTableTest, ReinsertAfterRemove) {
  std::array<node *, 4> buckets{};
  auto table_res = table_type::try_create(reloco::span<node *>(buckets.data(), buckets.size()));
  ASSERT_TRUE(table_res.has_value());
  auto &table = *table_res;

  node a{1, "one", {}};
  ASSERT_TRUE(table.try_insert(a).has_value());
  table.remove(a);
  ASSERT_TRUE(table.try_insert(a).has_value());
  EXPECT_TRUE(table.contains(1));
  EXPECT_EQ(table.size(), 1);
}

TEST(IntrusiveHashTableTest, RehashPreservesAllNodes) {
  std::array<node *, 2> small_buckets{};
  auto table_res = table_type::try_create(reloco::span<node *>(small_buckets.data(), small_buckets.size()));
  ASSERT_TRUE(table_res.has_value());
  auto &table = *table_res;

  node a{1, "one", {}};
  node b{2, "two", {}};
  node c{3, "three", {}};
  ASSERT_TRUE(table.try_insert(a).has_value());
  ASSERT_TRUE(table.try_insert(b).has_value());
  ASSERT_TRUE(table.try_insert(c).has_value());
  EXPECT_EQ(table.size(), 3);

  std::array<node *, 8> big_buckets{};
  ASSERT_TRUE(table.rehash(reloco::span<node *>(big_buckets.data(), big_buckets.size())).has_value());
  EXPECT_EQ(table.bucket_count(), 8);
  EXPECT_EQ(table.size(), 3);
  EXPECT_TRUE(table.contains(1));
  EXPECT_TRUE(table.contains(2));
  EXPECT_TRUE(table.contains(3));

  auto found = table.try_find(2);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get().value, "two");
}

TEST(IntrusiveHashTableTest, RehashThenInsertMore) {
  std::array<node *, 2> small_buckets{};
  auto table_res = table_type::try_create(reloco::span<node *>(small_buckets.data(), small_buckets.size()));
  ASSERT_TRUE(table_res.has_value());
  auto &table = *table_res;

  node a{1, "one", {}};
  ASSERT_TRUE(table.try_insert(a).has_value());

  std::array<node *, 4> bigger_buckets{};
  ASSERT_TRUE(table.rehash(reloco::span<node *>(bigger_buckets.data(), bigger_buckets.size())).has_value());

  node b{2, "two", {}};
  ASSERT_TRUE(table.try_insert(b).has_value());
  EXPECT_EQ(table.size(), 2);
  EXPECT_TRUE(table.contains(1));
  EXPECT_TRUE(table.contains(2));
}

TEST(IntrusiveHashTableTest, RehashRejectsEmptySpan) {
  std::array<node *, 2> buckets{};
  auto table_res = table_type::try_create(reloco::span<node *>(buckets.data(), buckets.size()));
  ASSERT_TRUE(table_res.has_value());
  auto &table = *table_res;

  auto rehash_res = table.rehash(reloco::span<node *>());
  ASSERT_FALSE(rehash_res.has_value());
  EXPECT_EQ(rehash_res.error(), reloco::error::invalid_argument);
}

TEST(IntrusiveHashTableTest, ClearUnlinksEveryNode) {
  std::array<node *, 4> buckets{};
  auto table_res = table_type::try_create(reloco::span<node *>(buckets.data(), buckets.size()));
  ASSERT_TRUE(table_res.has_value());
  auto &table = *table_res;

  node a{1, "one", {}};
  node b{2, "two", {}};
  ASSERT_TRUE(table.try_insert(a).has_value());
  ASSERT_TRUE(table.try_insert(b).has_value());

  table.clear();
  EXPECT_EQ(table.size(), 0);
  EXPECT_TRUE(table.empty());
  EXPECT_EQ(table.bucket_count(), 4); // unchanged
  EXPECT_FALSE(a.hook.is_linked());
  EXPECT_FALSE(b.hook.is_linked());
  EXPECT_FALSE(table.contains(1));
  EXPECT_FALSE(table.contains(2));

  // The table must be reusable after clear().
  ASSERT_TRUE(table.try_insert(a).has_value());
  EXPECT_TRUE(table.contains(1));
}

TEST(IntrusiveHashTableTest, MoveConstructionTransfersOwnership) {
  std::array<node *, 4> buckets{};
  auto table_res = table_type::try_create(reloco::span<node *>(buckets.data(), buckets.size()));
  ASSERT_TRUE(table_res.has_value());

  node a{1, "one", {}};
  ASSERT_TRUE(table_res->try_insert(a).has_value());

  table_type moved(std::move(*table_res));
  EXPECT_EQ(moved.size(), 1);
  EXPECT_TRUE(moved.contains(1));
  EXPECT_EQ(table_res->bucket_count(), 0); // moved-from
  EXPECT_EQ(table_res->size(), 0);
}

TEST(IntrusiveHashTableTest, IsNotCopyable) {
  EXPECT_FALSE(std::is_copy_constructible_v<table_type>);
  EXPECT_FALSE(std::is_copy_assignable_v<table_type>);
}

// Rvalue-argument and rvalue-`this` hardening: these must all be compile-time
// rejections (see `intrusive_hash_table.hpp`'s explicit `= delete` overloads).
// `std::is_invocable` can't be used here since `&table_type::try_insert`/
// `&table_type::size` are overloaded (ambiguous to take the address of
// directly) -- the `void_t` detection idiom (this codebase's usual
// C++17-compatible SFINAE tool, see e.g. `concepts.hpp`) works for deleted
// overloads too, since overload resolution still selects the (deleted) best
// match and forming that call is checked the same way any other ill-formed
// expression is in `decltype`'s unevaluated operand.
namespace {
template <typename T, typename U, typename = void> struct can_try_insert : std::false_type {};
template <typename T, typename U>
struct can_try_insert<T, U, std::void_t<decltype(std::declval<T &>().try_insert(std::declval<U>()))>>
    : std::true_type {};

template <typename T, typename = void> struct can_query_size : std::false_type {};
template <typename T> struct can_query_size<T, std::void_t<decltype(std::declval<T>().size())>> : std::true_type {};

template <typename T, typename = void> struct can_query_load_factor : std::false_type {};
template <typename T>
struct can_query_load_factor<T, std::void_t<decltype(std::declval<T>().load_factor_permille())>> : std::true_type {};
} // namespace

TEST(IntrusiveHashTableTest, RvalueHardeningIsCompileTimeRejected) {
  static_assert(can_try_insert<table_type, node &>::value, "lvalue try_insert must remain callable");
  static_assert(!can_try_insert<table_type, node>::value, "rvalue try_insert must be rejected");
  static_assert(can_query_size<const table_type &>::value, "lvalue size() must remain callable");
  static_assert(!can_query_size<const table_type>::value, "rvalue-this size() must be rejected");
  static_assert(can_query_load_factor<const table_type &>::value, "lvalue load_factor_permille() must remain callable");
  static_assert(!can_query_load_factor<const table_type>::value,
                "rvalue-this load_factor_permille() must be rejected");
}
