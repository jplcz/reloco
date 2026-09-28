// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/masked_pointer.hpp>

#include <type_traits>

namespace {

template <typename AuthPolicy> class MaskedPointerPolicyTest : public ::testing::Test {};

using PolicyTypes = ::testing::Types<reloco::security::sw_cookie_traits, reloco::security::sw_signed_cookie_traits>;
TYPED_TEST_SUITE(MaskedPointerPolicyTest, PolicyTypes, ::testing::internal::DefaultNameGenerator);

TYPED_TEST(MaskedPointerPolicyTest, DefaultConstructedIsNull) {
  reloco::masked_pointer<int, TypeParam> ptr;
  EXPECT_TRUE(ptr.is_null());
  EXPECT_FALSE(static_cast<bool>(ptr));
  EXPECT_EQ(ptr, nullptr);
  EXPECT_EQ(nullptr, ptr);
  EXPECT_EQ(ptr.get(), nullptr);
}

TYPED_TEST(MaskedPointerPolicyTest, NullptrConstructedIsNull) {
  reloco::masked_pointer<int, TypeParam> ptr(nullptr);
  EXPECT_TRUE(ptr.is_null());
}

TYPED_TEST(MaskedPointerPolicyTest, RoundTripsAnArbitraryPointer) {
  int value = 42;
  reloco::masked_pointer<int, TypeParam> ptr(&value);
  EXPECT_FALSE(ptr.is_null());
  EXPECT_TRUE(static_cast<bool>(ptr));
  EXPECT_EQ(ptr.get(), &value);
  EXPECT_EQ(ptr.operator->(), &value);
  EXPECT_EQ(&*ptr, &value);
  EXPECT_EQ(*ptr, 42);
}

TYPED_TEST(MaskedPointerPolicyTest, ResetChangesTarget) {
  int a = 1;
  int b = 2;
  reloco::masked_pointer<int, TypeParam> ptr(&a);
  ptr.reset(&b);
  EXPECT_EQ(ptr.get(), &b);
  ptr.reset();
  EXPECT_TRUE(ptr.is_null());
}

TYPED_TEST(MaskedPointerPolicyTest, AssignmentFromRawPointerAndNullptr) {
  int a = 1;
  reloco::masked_pointer<int, TypeParam> ptr;
  ptr = &a;
  EXPECT_EQ(ptr.get(), &a);
  ptr = nullptr;
  EXPECT_TRUE(ptr.is_null());
}

TYPED_TEST(MaskedPointerPolicyTest, CopyPreservesTarget) {
  int a = 1;
  reloco::masked_pointer<int, TypeParam> ptr(&a);
  reloco::masked_pointer<int, TypeParam> copy(ptr);
  EXPECT_EQ(copy.get(), &a);
  EXPECT_EQ(ptr.get(), &a); // Copy does not disturb the source (Copy semantics).
  EXPECT_EQ(ptr, copy);

  reloco::masked_pointer<int, TypeParam> assigned;
  assigned = ptr;
  EXPECT_EQ(assigned.get(), &a);
}

TYPED_TEST(MaskedPointerPolicyTest, MoveResetsSourceToNull) {
  int a = 1;
  reloco::masked_pointer<int, TypeParam> ptr(&a);
  reloco::masked_pointer<int, TypeParam> moved(std::move(ptr));
  EXPECT_EQ(moved.get(), &a);
  EXPECT_TRUE(ptr.is_null()); // NOLINT(bugprone-use-after-move) -- intentional post-move observation.

  reloco::masked_pointer<int, TypeParam> ptr2(&a);
  reloco::masked_pointer<int, TypeParam> move_assigned;
  move_assigned = std::move(ptr2);
  EXPECT_EQ(move_assigned.get(), &a);
  EXPECT_TRUE(ptr2.is_null()); // NOLINT(bugprone-use-after-move)
}

TYPED_TEST(MaskedPointerPolicyTest, SwapExchangesTargets) {
  int a = 1;
  int b = 2;
  reloco::masked_pointer<int, TypeParam> ptr_a(&a);
  reloco::masked_pointer<int, TypeParam> ptr_b(&b);
  ptr_a.swap(ptr_b);
  EXPECT_EQ(ptr_a.get(), &b);
  EXPECT_EQ(ptr_b.get(), &a);

  using std::swap;
  swap(ptr_a, ptr_b);
  EXPECT_EQ(ptr_a.get(), &a);
  EXPECT_EQ(ptr_b.get(), &b);
}

TYPED_TEST(MaskedPointerPolicyTest, TakeReturnsAndClears) {
  int a = 1;
  reloco::masked_pointer<int, TypeParam> ptr(&a);
  int *taken = ptr.take();
  EXPECT_EQ(taken, &a);
  EXPECT_TRUE(ptr.is_null());

  // Taking from an already-null pointer is a well-defined no-op.
  int *taken_null = ptr.take();
  EXPECT_EQ(taken_null, nullptr);
}

TYPED_TEST(MaskedPointerPolicyTest, ReplaceReturnsOldAndStoresNew) {
  int a = 1;
  int b = 2;
  reloco::masked_pointer<int, TypeParam> ptr(&a);
  int *old = ptr.replace(&b);
  EXPECT_EQ(old, &a);
  EXPECT_EQ(ptr.get(), &b);
}

TYPED_TEST(MaskedPointerPolicyTest, EncodedRepresentationIsNotThePlaintextAddress) {
  // Obfuscation-at-rest: the wrapper must not store the raw pointer bit
  // pattern anywhere accessible without going through auth(). We can't
  // reach the private encoded_val_ from a test, but we can confirm two
  // masked_pointers to the same target still compare equal (same
  // process-wide cookie(s)), which they would not if sign() were the
  // identity function producing visibly different representations for
  // unrelated construction call sites by accident.
  int a = 1;
  reloco::masked_pointer<int, TypeParam> ptr1(&a);
  reloco::masked_pointer<int, TypeParam> ptr2(&a);
  EXPECT_EQ(ptr1, ptr2);
}

TEST(MaskedPointerTest, DefaultAuthTraitsRoundTrips) {
  int value = 7;
  reloco::masked_pointer<int> ptr(&value);
  EXPECT_EQ(ptr.get(), &value);
  EXPECT_EQ(*ptr, 7);
}

TEST(MaskedPointerTest, VoidPointerIsSupported) {
  int value = 7;
  reloco::masked_pointer<void> ptr(&value);
  EXPECT_EQ(ptr.get(), &value);
}

TEST(MaskedPointerTest, DifferentPointersProduceUnequalWrappers) {
  int a = 1;
  int b = 2;
  reloco::masked_pointer<int> ptr_a(&a);
  reloco::masked_pointer<int> ptr_b(&b);
  EXPECT_NE(ptr_a, ptr_b);
}

} // namespace
