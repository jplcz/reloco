// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>

#include <cstddef>
#include <memory>

namespace {

// Stateful example backend: a bump-pointer arena, mirroring
// reloco_legacy's stack_allocator. Exercises expand_in_place while
// deliberately omitting reallocate/advise so the "unsupported operation"
// path is covered too.
struct arena_allocator_context {
  std::byte *buffer;
  std::size_t capacity;
  std::size_t offset = 0;
};

struct arena_allocator_tag {};

} // namespace

template <> struct reloco::allocator_traits<arena_allocator_tag> {
  using context_type = arena_allocator_context;

  static reloco::result<reloco::mem_block> allocate(reloco::value_ref<context_type> ctx, std::size_t bytes,
                                                    std::size_t alignment) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    void *current = ctx->buffer + ctx->offset;
    std::size_t space = ctx->capacity - ctx->offset;
    void *aligned = std::align(alignment, bytes, current, space);
    if (!aligned)
      return reloco::unexpected(reloco::error::allocation_failed);
    ctx->offset = static_cast<std::size_t>(static_cast<std::byte *>(aligned) - ctx->buffer) + bytes;
    RELOCO_END_UNSAFE_BUFFER_USAGE
    return reloco::mem_block{aligned, bytes};
  }

  static void deallocate(reloco::value_ref<context_type>, void *, std::size_t) noexcept {}

  static reloco::result<std::size_t> expand_in_place(reloco::value_ref<context_type> ctx, void *ptr,
                                                     std::size_t old_size, std::size_t new_size) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    bool is_last_allocation = static_cast<std::byte *>(ptr) + old_size == ctx->buffer + ctx->offset;
    RELOCO_END_UNSAFE_BUFFER_USAGE
    if (is_last_allocation) {
      std::size_t added = new_size - old_size;
      if (ctx->offset + added <= ctx->capacity) {
        ctx->offset += added;
        return new_size;
      }
    }
    return reloco::unexpected(reloco::error::unsupported_operation);
  }
};

TEST(AllocatorTest, HeapBackendAllocatesReallocatesAndDeallocates) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  auto blk = heap.allocate(64, 8);
  ASSERT_TRUE(blk);
  EXPECT_NE(blk->ptr, nullptr);
  EXPECT_EQ(blk->size, 64u);

  ASSERT_TRUE(heap.can_reallocate());
  auto grown = heap.reallocate(blk->ptr, 64, 128, 8);
  ASSERT_TRUE(grown);
  EXPECT_EQ(grown->size, 128u);

  heap.deallocate(grown->ptr, 128);
  RELOCO_END_UNSAFE_BUFFER_USAGE

  EXPECT_FALSE(heap.can_expand_in_place());
  EXPECT_FALSE(heap.can_advise());
}

TEST(AllocatorTest, StatefulArenaBackendViaOwningWrapper) {
  alignas(64) std::byte storage[256];
  reloco::allocator<arena_allocator_tag> arena{arena_allocator_context{storage, sizeof(storage)}};
  reloco::allocator_ref ref = arena.ref();

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  auto a1 = ref.allocate(16, 8);
  ASSERT_TRUE(a1);

  ASSERT_TRUE(ref.can_expand_in_place());
  auto grown = ref.expand_in_place(a1->ptr, 16, 32);
  ASSERT_TRUE(grown);
  EXPECT_EQ(*grown, 32u);

  EXPECT_FALSE(ref.can_reallocate());
  EXPECT_FALSE(ref.can_advise());

  ref.deallocate(a1->ptr, 32);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

TEST(AllocatorTest, StatefulBackendCanBindACallerOwnedContextDirectly) {
  alignas(64) std::byte storage[256];
  arena_allocator_context ctx{storage, sizeof(storage)};
  reloco::allocator_ref ref{arena_allocator_tag{}, ctx};

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  auto a = ref.allocate(8, 8);
  ASSERT_TRUE(a);
  ref.deallocate(a->ptr, 8);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

TEST(AllocatorTest, DefaultConstructedRefIsInvalid) {
  reloco::allocator_ref ref;
  EXPECT_FALSE(static_cast<bool>(ref));
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  auto blk = ref.allocate(16, 8);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  ASSERT_FALSE(blk);
  EXPECT_EQ(blk.error(), reloco::error::unsupported_operation);
}

TEST(AllocatorTest, DefaultAllocatorIsBackedByTheProcessHeap) {
  reloco::allocator_ref def = reloco::default_allocator();
  ASSERT_TRUE(static_cast<bool>(def));

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  auto blk = def.allocate(32, 8);
  ASSERT_TRUE(blk);
  EXPECT_NE(blk->ptr, nullptr);
  def.deallocate(blk->ptr, 32);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}
