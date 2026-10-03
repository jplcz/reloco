// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/heap_allocator.hpp"
#include "reloco/malloc_allocator.hpp"
#include "reloco/mutex.hpp"
#include "reloco/spin_lock.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <type_traits>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {
namespace {

// Tracks every allocate/deallocate call made against an upstream so tests
// can verify malloc_allocator actually returns idle arenas to it.
class TrackingUpstream {
public:
  static int live_allocations;

  [[nodiscard]] static result<mem_block> allocate(std::size_t bytes, std::size_t alignment) noexcept {
    auto res = allocator<heap_allocator_tag>::ref().allocate(bytes, alignment);
    if (res)
      ++live_allocations;
    return res;
  }

  static void deallocate(void *ptr, std::size_t bytes) noexcept {
    --live_allocations;
    allocator<heap_allocator_tag>::ref().deallocate(ptr, bytes);
  }
};

int TrackingUpstream::live_allocations = 0;

struct tracking_upstream_tag {};

} // namespace

template <> struct allocator_traits<tracking_upstream_tag> {
  using context_type = void;

  [[nodiscard]] static result<mem_block> allocate(std::size_t bytes, std::size_t alignment) noexcept {
    return TrackingUpstream::allocate(bytes, alignment);
  }

  static void deallocate(void *ptr, std::size_t bytes) noexcept { TrackingUpstream::deallocate(ptr, bytes); }
};

namespace {

allocator_ref heap_ref() { return allocator<heap_allocator_tag>::ref(); }

allocator_ref tracking_ref() { return allocator<tracking_upstream_tag>::ref(); }

class MallocAllocatorTest : public ::testing::Test {
protected:
  void SetUp() override { TrackingUpstream::live_allocations = 0; }

  static constexpr std::size_t default_arena_bytes = 4096;

  malloc_allocator<> heap{heap_ref(), default_arena_bytes};
  allocator_ref ref{heap.ref()};
};

TEST_F(MallocAllocatorTest, IsNeitherCopyableNorMovable) {
  static_assert(!std::is_copy_constructible_v<malloc_allocator<>>);
  static_assert(!std::is_move_constructible_v<malloc_allocator<>>);
  static_assert(!std::is_copy_assignable_v<malloc_allocator<>>);
  static_assert(!std::is_move_assignable_v<malloc_allocator<>>);
}

TEST_F(MallocAllocatorTest, CapabilitiesDetectedCorrectly) {
  EXPECT_TRUE(ref.can_expand_in_place());
  EXPECT_TRUE(ref.can_reallocate());
  EXPECT_FALSE(ref.can_advise());
}

TEST_F(MallocAllocatorTest, BasicMallocFreeRoundTrip) {
  auto res = heap.malloc(64);
  ASSERT_TRUE(res.has_value());
  EXPECT_NE(res->ptr, nullptr);
  EXPECT_GE(res->size, 64U);
  std::memset(res->ptr, 0xAB, 64);
  heap.free(res->ptr);
}

TEST_F(MallocAllocatorTest, AllocatedBlocksAreDistinctAndWritable) {
  auto a = heap.malloc(32);
  auto b = heap.malloc(48);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_NE(a->ptr, b->ptr);
  std::memset(a->ptr, 0x11, 32);
  std::memset(b->ptr, 0x22, 48);
  EXPECT_EQ(static_cast<unsigned char *>(a->ptr)[0], 0x11);
  EXPECT_EQ(static_cast<unsigned char *>(b->ptr)[0], 0x22);
  heap.free(a->ptr);
  heap.free(b->ptr);
}

TEST_F(MallocAllocatorTest, MemalignHonorsVariousAlignments) {
  for (std::size_t alignment : {8U, 16U, 32U, 64U, 128U, 256U}) {
    auto res = heap.memalign(alignment, 24);
    ASSERT_TRUE(res.has_value()) << "alignment " << alignment;
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(res->ptr) % alignment, 0U);
    heap.free(res->ptr);
  }
}

TEST_F(MallocAllocatorTest, MemalignHonorsOverMaxAlignAlignment) {
  // Request an alignment well beyond alignof(std::max_align_t) to ensure
  // the back-offset trick correctly handles large alignment slack too.
  constexpr std::size_t big_alignment = 512;
  auto res = heap.memalign(big_alignment, 16);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(res->ptr) % big_alignment, 0U);
  heap.free(res->ptr);
}

TEST_F(MallocAllocatorTest, ReallocGrowsInPlaceViaForwardCoalescing) {
  auto a = heap.malloc(32);
  auto b = heap.malloc(32);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  void *a_ptr = a->ptr;
  heap.free(b->ptr); // frees the block immediately after `a`, enabling in-place growth

  auto grown = heap.realloc(a_ptr, 48);
  ASSERT_TRUE(grown.has_value());
  EXPECT_EQ(grown->ptr, a_ptr); // should not have moved
  heap.free(grown->ptr);
}

TEST_F(MallocAllocatorTest, ReallocGrowsByMovingWhenNoRoom) {
  auto a = heap.malloc(32);
  auto b = heap.malloc(32);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  std::memset(a->ptr, 0x55, 32);
  void *a_ptr = a->ptr;

  // `b` is still live immediately after `a`, so growth must move the block.
  auto grown = heap.realloc(a_ptr, default_arena_bytes); // much bigger than remaining arena room
  ASSERT_TRUE(grown.has_value());
  EXPECT_NE(grown->ptr, a_ptr);
  EXPECT_EQ(static_cast<unsigned char *>(grown->ptr)[0], 0x55);
  EXPECT_EQ(static_cast<unsigned char *>(grown->ptr)[31], 0x55);

  heap.free(grown->ptr);
  heap.free(b->ptr);
}

TEST_F(MallocAllocatorTest, ReallocShrinks) {
  auto a = heap.malloc(128);
  ASSERT_TRUE(a.has_value());
  std::memset(a->ptr, 0x77, 128);

  auto shrunk = heap.realloc(a->ptr, 16);
  ASSERT_TRUE(shrunk.has_value());
  EXPECT_GE(shrunk->size, 16U);
  EXPECT_EQ(static_cast<unsigned char *>(shrunk->ptr)[0], 0x77);
  heap.free(shrunk->ptr);
}

TEST_F(MallocAllocatorTest, ReallocNullPointerActsLikeMalloc) {
  auto res = heap.realloc(nullptr, 24);
  ASSERT_TRUE(res.has_value());
  EXPECT_NE(res->ptr, nullptr);
  heap.free(res->ptr);
}

TEST_F(MallocAllocatorTest, FreeOfNullPointerIsNoOp) { heap.free(nullptr); }

TEST_F(MallocAllocatorTest, CoalescingAllowsReuseOfCombinedFreedSpace) {
  auto a = heap.malloc(64);
  auto b = heap.malloc(64);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  heap.free(a->ptr);
  heap.free(b->ptr); // adjacent free blocks must merge back together

  // A request needing roughly the combined size of both prior blocks
  // should succeed purely from the coalesced free space, without
  // triggering a new arena (verified indirectly via the tracking test
  // below; here we just confirm it doesn't fail/crash).
  auto c = heap.malloc(100);
  ASSERT_TRUE(c.has_value());
  heap.free(c->ptr);
}

TEST(MallocAllocatorArenaTest, FullyFreedArenaIsReturnedToUpstream) {
  TrackingUpstream::live_allocations = 0;
  {
    malloc_allocator<> heap(tracking_ref(), 4096);
    auto a = heap.malloc(64);
    auto b = heap.malloc(64);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(TrackingUpstream::live_allocations, 1); // one arena so far

    heap.free(a->ptr);
    EXPECT_EQ(TrackingUpstream::live_allocations, 1); // arena still partly in use

    heap.free(b->ptr);
    EXPECT_EQ(TrackingUpstream::live_allocations, 0); // arena fully idle: returned
  }
  EXPECT_EQ(TrackingUpstream::live_allocations, 0);
}

TEST(MallocAllocatorArenaTest, DestructorReleasesAllRemainingArenas) {
  TrackingUpstream::live_allocations = 0;
  {
    malloc_allocator<> heap(tracking_ref(), 4096);
    auto a = heap.malloc(64);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(TrackingUpstream::live_allocations, 1);
    // Intentionally leak `a` within this scope; the destructor must still
    // return the whole arena to upstream.
  }
  EXPECT_EQ(TrackingUpstream::live_allocations, 0);
}

TEST(MallocAllocatorArenaTest, RefillsWithAdditionalArenaWhenExhausted) {
  TrackingUpstream::live_allocations = 0;
  malloc_allocator<> heap(tracking_ref(), 256);

  constexpr int count = 64;
  void *ptrs[count];
  for (int i = 0; i < count; ++i) {
    auto res = heap.malloc(16);
    ASSERT_TRUE(res.has_value()) << "allocation " << i << " failed";
    ptrs[i] = res->ptr;
  }
  EXPECT_GT(TrackingUpstream::live_allocations, 1); // must have refilled at least once

  for (int i = 0; i < count; ++i) {
    for (int j = i + 1; j < count; ++j) {
      EXPECT_NE(ptrs[i], ptrs[j]);
    }
  }
  for (void *p : ptrs) {
    heap.free(p);
  }
  EXPECT_EQ(TrackingUpstream::live_allocations, 0);
}

TEST_F(MallocAllocatorTest, WorksWithMutexLock) {
  malloc_allocator<mutex> mtx_heap(heap_ref(), default_arena_bytes);
  auto res = mtx_heap.malloc(32);
  ASSERT_TRUE(res.has_value());
  mtx_heap.free(res->ptr);
}

TEST_F(MallocAllocatorTest, WorksWithSpinLock) {
  malloc_allocator<spin_lock> spin_heap(heap_ref(), default_arena_bytes);
  auto res = spin_heap.malloc(32);
  ASSERT_TRUE(res.has_value());
  spin_heap.free(res->ptr);
}

TEST_F(MallocAllocatorTest, WorksThroughTypeErasedAllocatorRef) {
  auto res = ref.allocate(40, 8);
  ASSERT_TRUE(res.has_value());
  ref.deallocate(res->ptr, res->size);
}

} // namespace
} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
