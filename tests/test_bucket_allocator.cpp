// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/bucket_allocator.hpp"
#include "reloco/detail/sanitizer.hpp"
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
  // Must be a multiple of every bucket size (16, 32, 64, 128): shared
  // across all buckets, not configured per-bucket.
  static constexpr std::size_t slab_bytes = (blocks_per_slab + 1) * 128;

  bucket_allocator<null_mutex, 16, 32, 64, 128> pool{alignment, heap_ref(), slab_bytes};
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
  EXPECT_TRUE(ref.can_expand_in_place());
  EXPECT_TRUE(ref.can_reallocate());
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

TEST_F(BucketAllocatorTest, DefersToUpstreamWhenLargerThanLargestBucket) {
  // No configured bucket is >= 129, so this must be forwarded straight to
  // the shared upstream allocator (heap_ref()) instead of failing.
  auto res = ref.allocate(129, 4);
  ASSERT_TRUE(res.has_value());
  EXPECT_NE(res->ptr, nullptr);
  EXPECT_GE(res->size, 129u);
  ref.deallocate(res->ptr, res->size); // also forwarded straight to upstream_
}

TEST_F(BucketAllocatorTest, RejectsOveralignedRequestWithinBucketRange) {
  // 8 bytes fits the 16-byte bucket, but the requested alignment exceeds
  // every bucket's shared alignment -- rejected by that bucket, not
  // retried against upstream.
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

TEST_F(BucketAllocatorTest, ExpandInPlaceWithinSameBucket) {
  auto a = ref.allocate(20, 4); // rounds up to the 32-byte bucket
  ASSERT_TRUE(a.has_value());
  void *ptr = a->ptr;

  auto res = ref.expand_in_place(ptr, 20, 30); // still <= 32-byte bucket
#if RELOCO_ASAN_ENABLED
  // The zero-copy shortcut is deliberately compiled out under
  // AddressSanitizer -- see the file docs.
  EXPECT_FALSE(res.has_value());
#else
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, 30u);
  EXPECT_EQ(ptr, a->ptr); // no move happened
#endif
  ref.deallocate(ptr, 20);
}

TEST_F(BucketAllocatorTest, ExpandInPlaceFailsBeyondOwningBucketSize) {
  auto a = ref.allocate(20, 4); // 32-byte bucket
  ASSERT_TRUE(a.has_value());

  auto res = ref.expand_in_place(a->ptr, 20, 40); // 40 > 32-byte bucket capacity
  EXPECT_FALSE(res.has_value());
  ref.deallocate(a->ptr, 20);
}

TEST_F(BucketAllocatorTest, ExpandInPlaceDefersToUpstreamForOversizedBlock) {
  auto a = ref.allocate(200, alignment); // > largest bucket, serviced by heap upstream
  ASSERT_TRUE(a.has_value());

  // heap_allocator_tag does not implement expand_in_place.
  auto res = ref.expand_in_place(a->ptr, 200, 5000);
  EXPECT_FALSE(res.has_value());
  ref.deallocate(a->ptr, a->size);
}

TEST_F(BucketAllocatorTest, ReallocateMovesToBiggerBucketAndPreservesData) {
  auto a = ref.allocate(10, 4); // 16-byte bucket
  ASSERT_TRUE(a.has_value());
  auto *bytes = static_cast<unsigned char *>(a->ptr);
  for (unsigned char i = 0; i < 10; ++i)
    bytes[i] = i;

  auto res = ref.reallocate(a->ptr, 10, 50, 4); // needs the 64-byte bucket
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->size, 64u);
  auto *new_bytes = static_cast<unsigned char *>(res->ptr);
  for (unsigned char i = 0; i < 10; ++i)
    EXPECT_EQ(new_bytes[i], i);
  ref.deallocate(res->ptr, res->size);
}

TEST_F(BucketAllocatorTest, ReallocateDefersToUpstreamWhenOldBlockWasFromUpstream) {
  auto a = ref.allocate(200, alignment); // serviced directly by heap upstream
  ASSERT_TRUE(a.has_value());
  static_cast<unsigned char *>(a->ptr)[0] = 0x42;

  auto res = ref.reallocate(a->ptr, a->size, 5000, alignment);
  ASSERT_TRUE(res.has_value()); // heap_allocator_tag does implement reallocate
  EXPECT_EQ(static_cast<unsigned char *>(res->ptr)[0], 0x42);
  ref.deallocate(res->ptr, res->size);
}

TEST_F(BucketAllocatorTest, WorksWithMutexLock) {
  bucket_allocator<mutex, 16, 32> mtx_pool(8, heap_ref(), 5 * 32);
  auto res = mtx_pool.ref().allocate(10, 4);
  ASSERT_TRUE(res.has_value());
  mtx_pool.ref().deallocate(res->ptr, res->size);
}

TEST_F(BucketAllocatorTest, WorksWithSpinLock) {
  bucket_allocator<spin_lock, 16, 32> spin_pool(8, heap_ref(), 5 * 32);
  auto res = spin_pool.ref().allocate(10, 4);
  ASSERT_TRUE(res.has_value());
  spin_pool.ref().deallocate(res->ptr, res->size);
}

} // namespace
} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
