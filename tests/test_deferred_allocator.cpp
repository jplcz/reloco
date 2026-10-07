// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/deferred_allocator.hpp"
#include "reloco/heap_allocator.hpp"

#include <gtest/gtest.h>
#include <type_traits>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {
namespace {

allocator_ref heap_ref() { return allocator<heap_allocator_tag>::ref(); }

class DeferredAllocatorTest : public ::testing::Test {
protected:
  deferred_allocator defer;
  allocator_ref ref{defer.ref()};
};

TEST_F(DeferredAllocatorTest, IsNeitherCopyableNorMovable) {
  static_assert(!std::is_copy_constructible_v<deferred_allocator>);
  static_assert(!std::is_move_constructible_v<deferred_allocator>);
  static_assert(!std::is_copy_assignable_v<deferred_allocator>);
  static_assert(!std::is_move_assignable_v<deferred_allocator>);
}

TEST_F(DeferredAllocatorTest, StartsUnbound) { EXPECT_FALSE(defer.is_bound()); }

TEST_F(DeferredAllocatorTest, CapabilitiesAlwaysReportedPresent) {
  // Always present at compile time; actual success depends on bound-state
  // and, once bound, on the real backend.
  EXPECT_TRUE(ref.can_expand_in_place());
  EXPECT_TRUE(ref.can_reallocate());
  EXPECT_TRUE(ref.can_advise());
}

TEST_F(DeferredAllocatorTest, AllocateFailsBeforeBind) {
  auto res = ref.allocate(64, alignof(std::max_align_t));
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::not_initialized);
}

TEST_F(DeferredAllocatorTest, ExpandInPlaceAndReallocateFailBeforeBind) {
  auto expand = ref.expand_in_place(nullptr, 0, 64);
  ASSERT_FALSE(expand.has_value());
  EXPECT_EQ(expand.error(), error::unsupported_operation);

  auto realloc_res = ref.reallocate(nullptr, 0, 64, alignof(std::max_align_t));
  ASSERT_FALSE(realloc_res.has_value());
  EXPECT_EQ(realloc_res.error(), error::unsupported_operation);
}

TEST_F(DeferredAllocatorTest, AdviseIsANoOpBeforeBind) {
  // Must not crash/UB even though there is nothing to forward to yet.
  ref.advise(nullptr, 0, usage_hint::sequential);
}

TEST_F(DeferredAllocatorTest, BindThenAllocateForwardsToRealBackend) {
  ASSERT_TRUE(defer.try_bind(heap_ref()).has_value());
  EXPECT_TRUE(defer.is_bound());

  auto res = ref.allocate(128, alignof(std::max_align_t));
  ASSERT_TRUE(res.has_value());
  EXPECT_NE(res->ptr, nullptr);
  ref.deallocate(res->ptr, res->size);
}

TEST_F(DeferredAllocatorTest, SecondBindFailsWithAlreadyExists) {
  ASSERT_TRUE(defer.try_bind(heap_ref()).has_value());

  auto second = defer.try_bind(heap_ref());
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), error::already_exists);
}

TEST_F(DeferredAllocatorTest, AlreadyObtainedRefStartsForwardingAfterBind) {
  // ref was captured before try_bind(): a key use case (early-init code
  // stores .ref() before the real allocator exists).
  EXPECT_FALSE(ref.allocate(16, 4).has_value());

  ASSERT_TRUE(defer.try_bind(heap_ref()).has_value());

  auto res = ref.allocate(16, 4);
  ASSERT_TRUE(res.has_value());
  ref.deallocate(res->ptr, res->size);
}

} // namespace
} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
