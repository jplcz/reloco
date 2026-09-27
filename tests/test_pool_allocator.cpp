// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/pool_allocator.hpp"
#include "reloco/heap_allocator.hpp"
#include "reloco/mutex.hpp"
#include "reloco/spin_lock.hpp"

#include <gtest/gtest.h>
#include <type_traits>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {
namespace {

allocator_ref heap_ref() { return allocator<heap_allocator_tag>::ref(); }

class PoolAllocatorTest : public ::testing::Test {
protected:
  static constexpr std::size_t block_size = 64;
  static constexpr std::size_t block_align = alignof(std::max_align_t);
  static constexpr std::size_t blocks_per_slab = 4;
  static constexpr std::size_t slab_bytes = (blocks_per_slab + 1) * block_size;

  pool_allocator<> pool{block_size, block_align, heap_ref(), slab_bytes};
  allocator_ref ref{pool.ref()};
};

TEST_F(PoolAllocatorTest, IsNeitherCopyableNorMovable) {
  static_assert(!std::is_copy_constructible_v<pool_allocator<>>);
  static_assert(!std::is_move_constructible_v<pool_allocator<>>);
  static_assert(!std::is_copy_assignable_v<pool_allocator<>>);
  static_assert(!std::is_move_assignable_v<pool_allocator<>>);
}

TEST_F(PoolAllocatorTest, CapabilitiesDetectedCorrectly) {
  // expand_in_place/reallocate/advise are all intentionally omitted.
  EXPECT_FALSE(ref.can_expand_in_place());
  EXPECT_FALSE(ref.can_reallocate());
  EXPECT_FALSE(ref.can_advise());
}

TEST_F(PoolAllocatorTest, BasicAllocationReturnsExactBlockSize) {
  auto res = ref.allocate(16, 4);
  ASSERT_TRUE(res.has_value());
  EXPECT_NE(res->ptr, nullptr);
  EXPECT_EQ(res->size, block_size);
  ref.deallocate(res->ptr, res->size);
}

TEST_F(PoolAllocatorTest, RejectsRequestLargerThanBlockSize) {
  auto res = ref.allocate(block_size + 1, 4);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::allocation_failed);
}

TEST_F(PoolAllocatorTest, RejectsOveralignedRequest) {
  auto res = ref.allocate(4, block_align * 2);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::allocation_failed);
}

TEST_F(PoolAllocatorTest, AllocatedBlocksAreDistinct) {
  auto a = ref.allocate(4, 4);
  auto b = ref.allocate(4, 4);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_NE(a->ptr, b->ptr);
  ref.deallocate(a->ptr, a->size);
  ref.deallocate(b->ptr, b->size);
}

TEST_F(PoolAllocatorTest, DeallocatedBlockIsReusedByNextAllocation) {
  auto a = ref.allocate(4, 4);
  ASSERT_TRUE(a.has_value());
  void *first_ptr = a->ptr;
  ref.deallocate(a->ptr, a->size);

  auto b = ref.allocate(4, 4);
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->ptr, first_ptr); // LIFO free list reuses the just-freed block.
  ref.deallocate(b->ptr, b->size);
}

TEST_F(PoolAllocatorTest, RefillsAcrossMultipleSlabs) {
  // blocks_per_slab == 4 (slab_bytes covers 4 usable blocks + 1 header
  // block): allocate well beyond a single slab's worth and confirm every
  // block is unique (i.e. refill() correctly grows the pool instead of
  // reusing/corrupting memory).
  constexpr int count = 37;
  void *ptrs[count];
  for (int i = 0; i < count; ++i) {
    auto res = ref.allocate(8, 4);
    ASSERT_TRUE(res.has_value()) << "allocation " << i << " failed";
    ptrs[i] = res->ptr;
  }
  for (int i = 0; i < count; ++i) {
    for (int j = i + 1; j < count; ++j) {
      EXPECT_NE(ptrs[i], ptrs[j]);
    }
  }
  for (void *p : ptrs) {
    ref.deallocate(p, block_size);
  }
}

TEST_F(PoolAllocatorTest, WorksWithMutexLock) {
  pool_allocator<mutex> mtx_pool(32, 8, heap_ref(), 5 * 32);
  auto res = mtx_pool.ref().allocate(8, 4);
  ASSERT_TRUE(res.has_value());
  mtx_pool.ref().deallocate(res->ptr, res->size);
}

TEST_F(PoolAllocatorTest, WorksWithSpinLock) {
  pool_allocator<spin_lock> spin_pool(32, 8, heap_ref(), 5 * 32);
  auto res = spin_pool.ref().allocate(8, 4);
  ASSERT_TRUE(res.has_value());
  spin_pool.ref().deallocate(res->ptr, res->size);
}

TEST(NullMutexTest, IsAlwaysUnlockable) {
  null_mutex m;
  m.lock();
  EXPECT_TRUE(m.try_lock());
  m.unlock();
}

} // namespace
} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
