// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/bucket_allocator.hpp"
#include "reloco/heap_allocator.hpp"
#include "reloco/mutex.hpp"
#include "reloco/spin_lock.hpp"

#include <gtest/gtest.h>
#include <type_traits>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {
namespace {

allocator_ref heap_ref() { return allocator<heap_allocator_tag>::ref(); }

class BucketAllocatorTest : public ::testing::Test {
protected:
  static constexpr std::size_t alignment = alignof(std::max_align_t);
  static constexpr std::size_t blocks_per_slab = 4;

  bucket_allocator<null_mutex, 16, 32, 64, 128> pool{alignment, heap_ref(), blocks_per_slab};
  allocator_ref ref{pool.ref()};
};

TEST_F(BucketAllocatorTest, IsNeitherCopyableNorMovable) {
  using pool_t = bucket_allocator<null_mutex, 16, 32, 64, 128>;
  static_assert(!std::is_copy_constructible_v<pool_t>);
  static_assert(!std::is_move_constructible_v<pool_t>);
  static_assert(!std::is_copy_assignable_v<pool_t>);
  static_assert(!std::is_move_assignable_v<pool_t>);
}

TEST_F(BucketAllocatorTest, CapabilitiesDetectedCorrectly) {
  EXPECT_FALSE(ref.can_expand_in_place());
  EXPECT_FALSE(ref.can_reallocate());
  EXPECT_FALSE(ref.can_advise());
}

TEST_F(BucketAllocatorTest, RoutesToSmallestFittingBucket) {
  auto a = ref.allocate(8, 4);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->size, 16u);
  ref.deallocate(a->ptr, a->size);

  auto b = ref.allocate(17, 4);
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->size, 32u);
  ref.deallocate(b->ptr, b->size);

  auto c = ref.allocate(100, 4);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->size, 128u);
  ref.deallocate(c->ptr, c->size);

  auto exact = ref.allocate(64, 4);
  ASSERT_TRUE(exact.has_value());
  EXPECT_EQ(exact->size, 64u);
  ref.deallocate(exact->ptr, exact->size);
}

TEST_F(BucketAllocatorTest, RejectsRequestLargerThanLargestBucket) {
  auto res = ref.allocate(129, 4);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::allocation_failed);
}

TEST_F(BucketAllocatorTest, RejectsOveralignedRequest) {
  auto res = ref.allocate(8, alignment * 2);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::allocation_failed);
}

TEST_F(BucketAllocatorTest, DeallocateTranslatesTruncatedSizeToExactBucketSize) {
  // Simulates a container that recorded a smaller byte count than the
  // bucket's actual block size (e.g. after allocate() over-provisioned)
  // and later passes that smaller count back to deallocate().
  auto a = ref.allocate(20, 4); // rounds up to the 32-byte bucket
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->size, 32u);
  void *first_ptr = a->ptr;

  ref.deallocate(a->ptr, 20); // caller passes back 20, not 32

  // The block must have been returned to the 32-byte bucket's free list,
  // not lost/misrouted -- the next same-bucket allocation reuses it.
  auto b = ref.allocate(25, 4);
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->ptr, first_ptr);
  ref.deallocate(b->ptr, b->size);
}

TEST_F(BucketAllocatorTest, AllocatedBlocksAreDistinctAcrossBucketsAndRefills) {
  constexpr int count = 50;
  void *ptrs[count];
  for (int i = 0; i < count; ++i) {
    const std::size_t size = 8 + static_cast<std::size_t>(i % 4) * 30; // spans all 4 buckets
    auto res = ref.allocate(size, 4);
    ASSERT_TRUE(res.has_value()) << "allocation " << i << " failed";
    ptrs[i] = res->ptr;
  }
  for (int i = 0; i < count; ++i) {
    for (int j = i + 1; j < count; ++j) {
      EXPECT_NE(ptrs[i], ptrs[j]);
    }
  }
}

TEST_F(BucketAllocatorTest, WorksWithMutexLock) {
  bucket_allocator<mutex, 16, 32> mtx_pool(8, heap_ref(), 4);
  auto res = mtx_pool.ref().allocate(10, 4);
  ASSERT_TRUE(res.has_value());
  mtx_pool.ref().deallocate(res->ptr, res->size);
}

TEST_F(BucketAllocatorTest, WorksWithSpinLock) {
  bucket_allocator<spin_lock, 16, 32> spin_pool(8, heap_ref(), 4);
  auto res = spin_pool.ref().allocate(10, 4);
  ASSERT_TRUE(res.has_value());
  spin_pool.ref().deallocate(res->ptr, res->size);
}

} // namespace
} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
