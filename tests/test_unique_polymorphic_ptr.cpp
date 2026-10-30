// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/unique_polymorphic_ptr.hpp>

#include <cstddef>
#include <new>
#include <utility>

namespace {

struct padded_tag {};
std::size_t g_freed_bytes = 0;
int g_destroyed = 0;

// Deliberately no virtual destructor.
struct plain_base {
  int base_value = 1;
};

struct plain_derived : plain_base {
  explicit plain_derived(int v) noexcept { extra[0] = v; }
  ~plain_derived() { ++g_destroyed; }
  int extra[64]{};
};

struct virtual_root {
  int root = 5;
};
struct left : virtual virtual_root {
  int l = 0;
};
struct right : virtual virtual_root {
  int r = 0;
};
struct diamond : left, right {
  ~diamond() { ++g_destroyed; }
};

} // namespace

template <> struct reloco::allocator_traits<padded_tag> {
  using context_type = void;
  static result<mem_block> allocate(std::size_t bytes, std::size_t alignment) noexcept {
    auto blk = allocator_traits<heap_allocator_tag>::allocate(bytes + 16, alignment);
    if (!blk)
      return blk;
    return mem_block{blk->ptr, blk->size};
  }
  static void deallocate(void *ptr, std::size_t bytes) noexcept {
    g_freed_bytes = bytes;
    allocator_traits<heap_allocator_tag>::deallocate(ptr, bytes);
  }
};

TEST(UniquePolymorphicPtrTest, NonVirtualBaseDestroysDerivedAndFreesAllocatedSize) {
  g_destroyed = 0;
  g_freed_bytes = 0;
  reloco::allocator_ref alloc = reloco::allocator<padded_tag>::ref();
  {
    auto p = reloco::unique_polymorphic_ptr<plain_base>::try_allocate<plain_derived>(alloc, 7);
    ASSERT_TRUE(p);
    EXPECT_EQ((*p)->base_value, 1);
  }
  EXPECT_EQ(g_destroyed, 1);
  EXPECT_GE(g_freed_bytes, sizeof(plain_derived) + 16);
}

TEST(UniquePolymorphicPtrTest, VirtualInheritanceUpcastWorks) {
  g_destroyed = 0;
  reloco::allocator_ref alloc = reloco::allocator<reloco::heap_allocator_tag>::ref();
  {
    auto p = reloco::unique_polymorphic_ptr<virtual_root>::try_allocate<diamond>(alloc);
    ASSERT_TRUE(p);
    EXPECT_EQ((*p)->root, 5);
  }
  EXPECT_EQ(g_destroyed, 1);
}

TEST(UniquePolymorphicPtrTest, ConvertsFromUniquePtr) {
  g_destroyed = 0;
  reloco::allocator_ref alloc = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto exact = reloco::unique_ptr<plain_derived>::try_allocate(alloc, 3);
  ASSERT_TRUE(exact);
  reloco::unique_polymorphic_ptr<plain_base> base = std::move(*exact);
  EXPECT_FALSE(*exact);
  ASSERT_TRUE(base);
  base.reset();
  EXPECT_FALSE(base);
  EXPECT_EQ(g_destroyed, 1);
}

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
TEST(UniquePolymorphicPtrTest, AdoptTakesExistingBlock) {
  g_destroyed = 0;
  g_freed_bytes = 0;
  reloco::allocator_ref alloc = reloco::allocator<padded_tag>::ref();
  auto block = alloc.allocate(sizeof(plain_derived), alignof(plain_derived));
  ASSERT_TRUE(block);
  auto *obj = ::new (block->ptr) plain_derived(2);
  {
    auto owned = reloco::unique_polymorphic_ptr<plain_base>::adopt(alloc, obj, block->size);
    EXPECT_TRUE(owned);
  }
  EXPECT_EQ(g_destroyed, 1);
  EXPECT_EQ(g_freed_bytes, block->size);
}
RELOCO_END_UNSAFE_BUFFER_USAGE

TEST(UniquePolymorphicPtrTest, DefaultsToExactBaseAndMoves) {
  auto a = reloco::unique_polymorphic_ptr<plain_base>::try_create();
  ASSERT_TRUE(a);
  reloco::unique_polymorphic_ptr<plain_base> b = std::move(*a);
  EXPECT_FALSE(*a);
  EXPECT_TRUE(b);
}
