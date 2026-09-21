// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <reloco/container_ref.hpp>
#include <reloco/container_ref_std.hpp>

#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using reloco::mutable_container_ref;

namespace {

template <typename Container, typename T, typename Key, typename = void>
struct can_bind_container_ref : std::false_type {};

template <typename Container, typename T, typename Key>
struct can_bind_container_ref<Container, T, Key,
                              std::void_t<decltype(mutable_container_ref<T, Key>(std::declval<Container &>()))>>
    : std::true_type {};

} // namespace

TEST(ContainerRefTest, SequenceRefOverVectorReportsAssociativity) {
  std::vector<int> v{1, 2, 3};
  mutable_container_ref<int> ref(v);
  static_assert(!decltype(ref)::is_associative());
  EXPECT_FALSE(ref.is_associative());
  EXPECT_EQ(ref.size(), 3u);
  EXPECT_FALSE(ref.empty());
}

TEST(ContainerRefTest, DefaultConstructedSequenceRefIsEmptyAndUnbound) {
  mutable_container_ref<int> ref;
  EXPECT_EQ(ref.size(), 0u);
  EXPECT_TRUE(ref.empty());
  EXPECT_FALSE(ref.try_push_back(1).has_value());
  EXPECT_FALSE(ref.try_push_front(1).has_value());
  EXPECT_FALSE(ref.try_insert_at(0, 1).has_value());
  EXPECT_FALSE(ref.try_erase_at(0).has_value());
  EXPECT_FALSE(ref.try_at(0).has_value());
  ref.clear(); // no-op, must not crash
}

TEST(ContainerRefTest, TryPushBackAppendsToVector) {
  std::vector<int> v{1, 2};
  mutable_container_ref<int> ref(v);

  auto r = ref.try_push_back(3);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(v, (std::vector<int>{1, 2, 3}));
}

TEST(ContainerRefTest, TryPushFrontPrependsToVector) {
  std::vector<int> v{2, 3};
  mutable_container_ref<int> ref(v);

  auto r = ref.try_push_front(1);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(v, (std::vector<int>{1, 2, 3}));
}

TEST(ContainerRefTest, TryInsertAtInsertsAtGivenIndex) {
  std::vector<int> v{1, 3};
  mutable_container_ref<int> ref(v);

  auto r = ref.try_insert_at(1, 2);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(v, (std::vector<int>{1, 2, 3}));

  // index == size() behaves like push_back.
  auto r2 = ref.try_insert_at(ref.size(), 4);
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(v, (std::vector<int>{1, 2, 3, 4}));
}

TEST(ContainerRefTest, TryInsertAtReportsOutOfBoundsIndex) {
  std::vector<int> v{1, 2};
  mutable_container_ref<int> ref(v);

  auto r = ref.try_insert_at(100, 1);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::out_of_bounds);
}

TEST(ContainerRefTest, TryEraseAtRemovesElement) {
  std::vector<int> v{1, 2, 3};
  mutable_container_ref<int> ref(v);

  auto r = ref.try_erase_at(1);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(v, (std::vector<int>{1, 3}));
}

TEST(ContainerRefTest, TryEraseAtReportsOutOfBoundsIndex) {
  std::vector<int> v{1, 2};
  mutable_container_ref<int> ref(v);

  auto r = ref.try_erase_at(100);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::out_of_bounds);
}

TEST(ContainerRefTest, AtAccessesAndMutatesExistingElement) {
  std::vector<int> v{1, 2, 3};
  mutable_container_ref<int> ref(v);

  static_assert(std::is_same_v<decltype(ref.at(0)), int &>);
  EXPECT_EQ(ref.at(1), 2);
  ref.at(1) = 20;
  EXPECT_EQ(v[1], 20);
}

TEST(ContainerRefTest, TryAtReportsOutOfBounds) {
  std::vector<int> v{1, 2};
  mutable_container_ref<int> ref(v);

  auto ok = ref.try_at(1);
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok->get(), 2);

  auto oob = ref.try_at(2);
  ASSERT_FALSE(oob.has_value());
  EXPECT_EQ(oob.error(), reloco::error::out_of_bounds);
}

TEST(ContainerRefTest, ForEachVisitsAndMutatesEveryElement) {
  std::vector<int> v{1, 2, 3};
  mutable_container_ref<int> ref(v);

  ref.for_each([](int &x) { x *= 10; });
  EXPECT_EQ(v, (std::vector<int>{10, 20, 30}));
}

TEST(ContainerRefTest, ClearEmptiesVector) {
  std::vector<int> v{1, 2, 3};
  mutable_container_ref<int> ref(v);

  ref.clear();
  EXPECT_TRUE(v.empty());
  EXPECT_TRUE(ref.empty());
  EXPECT_EQ(ref.size(), 0u);
}

TEST(ContainerRefTest, AssociativeRefOverMapReportsAssociativity) {
  std::map<std::string, int> m;
  mutable_container_ref<int, std::string> ref(m);
  static_assert(decltype(ref)::is_associative());
  EXPECT_TRUE(ref.is_associative());
  EXPECT_TRUE(ref.empty());
}

TEST(ContainerRefTest, DefaultConstructedAssociativeRefIsEmptyAndUnbound) {
  mutable_container_ref<int, std::string> ref;
  EXPECT_EQ(ref.size(), 0u);
  EXPECT_TRUE(ref.empty());
  EXPECT_FALSE(ref.try_insert_at("a", 1).has_value());
  EXPECT_FALSE(ref.try_erase("a").has_value());
  EXPECT_FALSE(ref.try_at("a").has_value());
  ref.clear(); // no-op, must not crash
}

TEST(ContainerRefTest, TryInsertAtInsertsNewKey) {
  std::map<std::string, int> m;
  mutable_container_ref<int, std::string> ref(m);

  auto r = ref.try_insert_at("a", 1);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(m.at("a"), 1);
  EXPECT_EQ(ref.size(), 1u);
}

TEST(ContainerRefTest, TryInsertAtReportsAlreadyExists) {
  std::map<std::string, int> m{{"a", 1}};
  mutable_container_ref<int, std::string> ref(m);

  auto r = ref.try_insert_at("a", 2);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::already_exists);
  EXPECT_EQ(m.at("a"), 1); // unchanged
}

TEST(ContainerRefTest, TryEraseRemovesExistingKey) {
  std::map<std::string, int> m{{"a", 1}, {"b", 2}};
  mutable_container_ref<int, std::string> ref(m);

  auto r = ref.try_erase("a");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(m.count("a"), 0u);
  EXPECT_EQ(m.size(), 1u);
}

TEST(ContainerRefTest, TryEraseReportsNotFound) {
  std::map<std::string, int> m{{"a", 1}};
  mutable_container_ref<int, std::string> ref(m);

  auto r = ref.try_erase("missing");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::not_found);
}

TEST(ContainerRefTest, AtAccessesAndMutatesExistingEntry) {
  std::map<std::string, int> m{{"a", 1}};
  mutable_container_ref<int, std::string> ref(m);

  static_assert(std::is_same_v<decltype(ref.at("a")), int &>);
  EXPECT_EQ(ref.at("a"), 1);
  ref.at("a") = 42;
  EXPECT_EQ(m.at("a"), 42);
}

TEST(ContainerRefTest, TryAtReportsNotFound) {
  std::map<std::string, int> m{{"a", 1}};
  mutable_container_ref<int, std::string> ref(m);

  auto ok = ref.try_at("a");
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok->get(), 1);

  auto missing = ref.try_at("missing");
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), reloco::error::not_found);
}

TEST(ContainerRefTest, ForEachVisitsAndMutatesEveryEntry) {
  std::map<std::string, int> m{{"a", 1}, {"b", 2}};
  mutable_container_ref<int, std::string> ref(m);

  int sum = 0;
  ref.for_each([&](const std::string &, int &value) {
    sum += value;
    value *= 10;
  });
  EXPECT_EQ(sum, 3);
  EXPECT_EQ(m.at("a"), 10);
  EXPECT_EQ(m.at("b"), 20);
}

TEST(ContainerRefTest, ClearEmptiesMap) {
  std::map<std::string, int> m{{"a", 1}, {"b", 2}};
  mutable_container_ref<int, std::string> ref(m);

  ref.clear();
  EXPECT_TRUE(m.empty());
  EXPECT_TRUE(ref.empty());
  EXPECT_EQ(ref.size(), 0u);
}

TEST(ContainerRefTest, ConvertingConstructorIsExplicit) {
  EXPECT_FALSE((std::is_convertible_v<std::vector<int> &, mutable_container_ref<int>>));
  EXPECT_TRUE((std::is_constructible_v<mutable_container_ref<int>, std::vector<int> &>));

  EXPECT_FALSE((std::is_convertible_v<std::map<std::string, int> &, mutable_container_ref<int, std::string>>));
  EXPECT_TRUE((std::is_constructible_v<mutable_container_ref<int, std::string>, std::map<std::string, int> &>));
}

TEST(ContainerRefTest, WrongElementTypeCannotBind) {
  EXPECT_FALSE((can_bind_container_ref<std::vector<int>, double, void>::value));
  EXPECT_TRUE((can_bind_container_ref<std::vector<int>, int, void>::value));
}

TEST(ContainerRefTest, WrongKeyTypeCannotBindAssociative) {
  EXPECT_FALSE((can_bind_container_ref<std::map<std::string, int>, int, int>::value));
  EXPECT_TRUE((can_bind_container_ref<std::map<std::string, int>, int, std::string>::value));
}

TEST(ContainerRefTest, UnadaptedTypeCannotBindAsSequenceOrAssociative) {
  EXPECT_FALSE((can_bind_container_ref<std::map<std::string, int>, int, void>::value));
  EXPECT_FALSE((can_bind_container_ref<std::vector<int>, int, std::string>::value));
}
