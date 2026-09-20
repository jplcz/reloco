// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/stack_allocator.hpp"
#include <cstdint>
#include <gtest/gtest.h>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {
namespace {

class StackAllocatorTest : public ::testing::Test {
protected:
  // Provide a fixed 256-byte arena aligned to max_align_t for all tests
  alignas(std::max_align_t) std::byte arena[256];

  stack_allocator alloc{stack_allocator_context(arena, sizeof(arena))};
  allocator_ref ref{alloc.ref()};

  void SetUp() override { alloc.context()->reset(); }
};

TEST_F(StackAllocatorTest, CapabilitiesDetectedCorrectly) {
  // The traits specialize allocate, deallocate, and expand_in_place.
  // reallocate and advise are omitted and should evaluate to false.
  EXPECT_TRUE(ref.can_expand_in_place());
  EXPECT_FALSE(ref.can_reallocate());
  EXPECT_FALSE(ref.can_advise());
}

TEST_F(StackAllocatorTest, BasicAllocation) {
  auto res = ref.allocate(16, 4);
  ASSERT_TRUE(res.has_value());

  EXPECT_NE(res->ptr, nullptr);
  EXPECT_EQ(res->size, 16);

  // Verify it points into our arena
  auto *ptr_bytes = static_cast<std::byte *>(res->ptr);
  EXPECT_GE(ptr_bytes, arena);
  EXPECT_LT(ptr_bytes, std::end(arena));
}

TEST_F(StackAllocatorTest, AlignmentRespectsBoundaries) {
  // Force offset to something unaligned
  auto first = ref.allocate(1, 1);
  ASSERT_TRUE(first.has_value());

  // Allocate with large alignment requirement
  auto second = ref.allocate(16, 32);
  ASSERT_TRUE(second.has_value());

  // Check mathematical alignment
  auto addr = reinterpret_cast<std::uintptr_t>(second->ptr);
  EXPECT_EQ(addr % 32, 0);
}

TEST_F(StackAllocatorTest, OutOfMemoryFailsGracefully) {
  // Try to allocate more than the entire arena
  auto res = ref.allocate(512, 8);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::allocation_failed);

  // Allocate exactly what's left (assuming arena starts at 0 offset)
  auto res_full = ref.allocate(256, 1);
  ASSERT_TRUE(res_full.has_value());

  // The next allocation must fail
  auto res_empty = ref.allocate(1, 1);
  ASSERT_FALSE(res_empty.has_value());
  EXPECT_EQ(res_empty.error(), error::allocation_failed);
}

TEST_F(StackAllocatorTest, ExpandInPlaceLastAllocation) {
  auto res = ref.allocate(16, 8);
  ASSERT_TRUE(res.has_value());

  // Expanding the most recently allocated block should succeed
  auto exp_res = ref.expand_in_place(res->ptr, 16, 32);
  ASSERT_TRUE(exp_res.has_value());
  EXPECT_EQ(*exp_res, 32);

  // Offset should now reflect the 32 bytes taken
  EXPECT_EQ(alloc.context()->offset, 32);
}

TEST_F(StackAllocatorTest, ExpandInPlaceFailsForNonLastAllocation) {
  auto block1 = ref.allocate(16, 8);
  ASSERT_TRUE(block1.has_value());

  auto block2 = ref.allocate(16, 8);
  ASSERT_TRUE(block2.has_value());

  // Trying to expand block1 should fail because block2 is on top of it
  auto exp_res = ref.expand_in_place(block1->ptr, 16, 32);
  ASSERT_FALSE(exp_res.has_value());
  EXPECT_EQ(exp_res.error(), error::allocation_failed);

  // But expanding block2 should still succeed
  auto exp_res2 = ref.expand_in_place(block2->ptr, 16, 32);
  ASSERT_TRUE(exp_res2.has_value());
  EXPECT_EQ(*exp_res2, 32);
}

TEST_F(StackAllocatorTest, ExpandInPlaceFailsWhenOutOfMemory) {
  auto res = ref.allocate(128, 8);
  ASSERT_TRUE(res.has_value());

  // Try to expand beyond the remaining 128 bytes (128 + 200 = 328 > 256)
  auto exp_res = ref.expand_in_place(res->ptr, 128, 328);
  ASSERT_FALSE(exp_res.has_value());
  EXPECT_EQ(exp_res.error(), error::allocation_failed);
}

TEST_F(StackAllocatorTest, DeallocateIsNoOpButSafe) {
  auto res = ref.allocate(16, 8);
  ASSERT_TRUE(res.has_value());

  // Deallocation in a bump-pointer stack allocator is a no-op,
  // but calling it through the vtable shouldn't crash or trap.
  ref.deallocate(res->ptr, 16);

  // Offset should still be 16 (hasn't moved)
  EXPECT_EQ(alloc.context()->offset, 16);
}

TEST_F(StackAllocatorTest, ResetReclaimsMemory) {
  // Exhaust the arena
  auto res = ref.allocate(256, 1);
  ASSERT_TRUE(res.has_value());

  auto res_fail = ref.allocate(1, 1);
  ASSERT_FALSE(res_fail.has_value());

  // Reset the arena
  alloc.context()->reset();

  // Allocation should work again
  auto res_success = ref.allocate(128, 8);
  ASSERT_TRUE(res_success.has_value());
  EXPECT_EQ(res_success->size, 128);
}

TEST_F(StackAllocatorTest, UnsupportedOperationsReturnError) {
  auto res = ref.allocate(16, 8);
  ASSERT_TRUE(res.has_value());

  // Calling an omitted trait operation returns unsupported_operation
  auto re_res = ref.reallocate(res->ptr, 16, 32, 8);
  ASSERT_FALSE(re_res.has_value());
  EXPECT_EQ(re_res.error(), error::unsupported_operation);
}

} // namespace
} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE