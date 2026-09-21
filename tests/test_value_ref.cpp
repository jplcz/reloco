// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <reloco/value_ptr.hpp>
#include <reloco/value_ref.hpp>

#include <type_traits>
#include <utility>

namespace {

struct base_value {
  int value;
};

struct derived_value : base_value {};

struct unrelated_value {};

static_assert(std::is_constructible_v<reloco::value_ref<int>, int &>);
static_assert(!std::is_constructible_v<reloco::value_ref<int>, const int &>);
static_assert(std::is_constructible_v<reloco::value_ref<const int>, int &>);
static_assert(std::is_constructible_v<reloco::value_ref<const int>, const int &>);
static_assert(!std::is_constructible_v<reloco::value_ref<int>, int &&>);
static_assert(!std::is_constructible_v<reloco::value_ref<int>, const int &&>);
static_assert(std::is_constructible_v<reloco::value_ref<base_value>, derived_value &>);
static_assert(!std::is_constructible_v<reloco::value_ref<base_value>, unrelated_value &>);
static_assert(std::is_same_v<decltype(*std::declval<reloco::value_ref<int> &>()), int &>);
static_assert(std::is_same_v<decltype(std::declval<reloco::value_ref<int> &>().get()), int *>);
static_assert(std::is_same_v<decltype(std::declval<reloco::value_ref<int> &>().pointer()), reloco::value_ptr<int>>);
static_assert(
    std::is_same_v<decltype(std::declval<reloco::value_ref<const int> &>().pointer()), reloco::value_ptr<const int>>);
static_assert(std::is_trivially_copyable_v<reloco::value_ptr<int>>);
static_assert(sizeof(reloco::value_ptr<int>) == sizeof(int *));
static_assert(std::is_constructible_v<reloco::value_ptr<int>, int *>);
static_assert(std::is_constructible_v<reloco::value_ptr<const int>, int *>);
static_assert(!std::is_constructible_v<reloco::value_ptr<int>, const int *>);
static_assert(std::is_constructible_v<reloco::value_ptr<base_value>, derived_value *>);
static_assert(std::is_constructible_v<reloco::value_ptr<void>, derived_value *>);

TEST(ValueRef, PreservesMutableReferencedObjectIdentity) {
  int value = 42;
  reloco::value_ref<int> ref(value);

  EXPECT_EQ(ref.get(), &value);
  EXPECT_EQ(ref.pointer().get(), &value);
  EXPECT_EQ(*ref, 42);

  *ref = 7;
  EXPECT_EQ(value, 7);
}

TEST(ValueRef, ProvidesMutableMemberAccess) {
  derived_value value{{11}};
  reloco::value_ref<base_value> ref(value);

  EXPECT_EQ(ref.get(), static_cast<base_value *>(&value));
  EXPECT_EQ(ref->value, 11);
  ref->value = 13;
  EXPECT_EQ(value.value, 13);
}

TEST(ValueRef, ProvidesExplicitReadOnlyAccess) {
  int value = 17;
  reloco::value_ref<const int> ref(value);

  static_assert(std::is_same_v<decltype(*ref), const int &>);
  EXPECT_EQ(ref.get(), &value);
  EXPECT_EQ(*ref, 17);
}

TEST(ValueRef, DeductionGuidePreservesReferencedType) {
  const int value = 19;
  reloco::value_ref ref(value);

  static_assert(std::is_same_v<decltype(ref), reloco::value_ref<const int>>);
  EXPECT_EQ(*ref, 19);
}

TEST(ValuePtr, SupportsNullableBorrowedPointers) {
  reloco::value_ptr<int> ptr;

  EXPECT_FALSE(ptr);
  EXPECT_EQ(ptr.get(), nullptr);

  int value = 42;
  ptr = reloco::value_ptr<int>(&value);

  ASSERT_TRUE(ptr);
  EXPECT_EQ(ptr.get(), &value);
  EXPECT_EQ(*ptr, 42);

  *ptr = 7;
  EXPECT_EQ(value, 7);
}

TEST(ValuePtr, PreservesPointeeConversions) {
  derived_value value{{11}};
  reloco::value_ptr<derived_value> derived(&value);
  reloco::value_ptr<base_value> base(derived);
  reloco::value_ptr<const void> erased(base);

  EXPECT_EQ(base.get(), static_cast<base_value *>(&value));
  EXPECT_EQ(base->value, 11);
  EXPECT_EQ(erased.get(), static_cast<const void *>(&value));
}

TEST(ValuePtr, UnsafeDerefSkipsTheCheckedNullGuard) {
  int value = 5;
  reloco::value_ptr<int> ptr(&value);

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(ptr.unsafe_deref(), 5);
  ptr.unsafe_deref() = 6;
  RELOCO_END_UNSAFE_BUFFER_USAGE

  EXPECT_EQ(value, 6);
}

TEST(ValuePtr, DeductionGuidePreservesPointeeType) {
  const int value = 19;
  reloco::value_ptr ptr(&value);

  static_assert(std::is_same_v<decltype(ptr), reloco::value_ptr<const int>>);
  EXPECT_EQ(*ptr, 19);
}

} // namespace
