// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/unique_ptr.hpp>

#include <cstddef>
#include <utility>

namespace {

struct plain_widget {
  int value;
  explicit plain_widget(int v) noexcept : value(v) {}
};

struct creatable_widget {
  int value;
  static reloco::result<creatable_widget> try_create(int v) noexcept {
    if (v < 0)
      return reloco::unexpected(reloco::error::invalid_argument);
    return creatable_widget{v};
  }
};

struct allocating_widget {
  int value;
  static reloco::result<allocating_widget> try_allocate(reloco::allocator_ref, int v) noexcept {
    if (v < 0)
      return reloco::unexpected(reloco::error::invalid_argument);
    return allocating_widget{v};
  }
};

struct constructible_widget {
  int value = 0;
  reloco::result<void> try_construct(int v) noexcept {
    if (v < 0)
      return reloco::unexpected(reloco::error::invalid_argument);
    value = v;
    return {};
  }
};

// Every allocation request fails, exercising unique_ptr::try_allocate's
// box-allocation-failure path.
struct exhausted_allocator_tag {};

} // namespace

template <> struct reloco::allocator_traits<exhausted_allocator_tag> {
  using context_type = void;

  static reloco::result<reloco::mem_block> allocate(std::size_t, std::size_t) noexcept {
    return reloco::unexpected(reloco::error::allocation_failed);
  }

  static void deallocate(void *, std::size_t) noexcept {}
};

TEST(UniquePtrTest, TryAllocateUsesNothrowFallbackTier) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto ptr = reloco::unique_ptr<plain_widget>::try_allocate(heap, 7);
  ASSERT_TRUE(ptr);
  EXPECT_TRUE(*ptr);
  EXPECT_EQ((*ptr)->value, 7);
}

TEST(UniquePtrTest, TryCreateUsesDefaultAllocator) {
  auto ptr = reloco::unique_ptr<plain_widget>::try_create(8);
  ASSERT_TRUE(ptr);
  EXPECT_EQ((*ptr)->value, 8);
}

TEST(UniquePtrTest, TryAllocateUsesCreateTier) {
  auto ok = reloco::unique_ptr<creatable_widget>::try_create(9);
  ASSERT_TRUE(ok);
  EXPECT_EQ((*ok)->value, 9);

  auto failed = reloco::unique_ptr<creatable_widget>::try_create(-1);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), reloco::error::invalid_argument);
}

TEST(UniquePtrTest, TryAllocateUsesAllocateTier) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto ok = reloco::unique_ptr<allocating_widget>::try_allocate(heap, 10);
  ASSERT_TRUE(ok);
  EXPECT_EQ((*ok)->value, 10);

  auto failed = reloco::unique_ptr<allocating_widget>::try_allocate(heap, -1);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), reloco::error::invalid_argument);
}

TEST(UniquePtrTest, TryAllocateUsesConstructTier) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto ok = reloco::unique_ptr<constructible_widget>::try_allocate(heap, 11);
  ASSERT_TRUE(ok);
  EXPECT_EQ((*ok)->value, 11);

  auto failed = reloco::unique_ptr<constructible_widget>::try_allocate(heap, -1);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), reloco::error::invalid_argument);
}

TEST(UniquePtrTest, TryAllocateReportsBoxAllocationFailure) {
  reloco::allocator_ref exhausted = reloco::allocator<exhausted_allocator_tag>::ref();
  auto failed = reloco::unique_ptr<plain_widget>::try_allocate(exhausted, 1);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), reloco::error::allocation_failed);
}

TEST(UniquePtrTest, DefaultConstructedIsNull) {
  reloco::unique_ptr<plain_widget> ptr;
  EXPECT_FALSE(ptr);
  EXPECT_EQ(ptr.get(), nullptr);
}

TEST(UniquePtrTest, NullptrConstructedIsNull) {
  reloco::unique_ptr<plain_widget> ptr(nullptr);
  EXPECT_FALSE(ptr);
}

TEST(UniquePtrTest, MoveConstructionTransfersOwnershipAndNullsSource) {
  auto ptr = reloco::unique_ptr<plain_widget>::try_create(1);
  ASSERT_TRUE(ptr);
  reloco::unique_ptr<plain_widget> moved(std::move(*ptr));
  EXPECT_TRUE(moved);
  EXPECT_EQ(moved->value, 1);
  EXPECT_FALSE(*ptr);
}

TEST(UniquePtrTest, MoveAssignmentResetsPreviousOwnerAndTransfersOwnership) {
  auto first = reloco::unique_ptr<plain_widget>::try_create(1);
  auto second = reloco::unique_ptr<plain_widget>::try_create(2);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  *first = std::move(*second);
  EXPECT_EQ((*first)->value, 2);
  EXPECT_FALSE(*second);
}

TEST(UniquePtrTest, ResetDestroysAndDeallocatesThenNulls) {
  auto ptr = reloco::unique_ptr<plain_widget>::try_create(3);
  ASSERT_TRUE(ptr);
  ptr->reset();
  EXPECT_FALSE(*ptr);
}

TEST(UniquePtrTest, DereferenceOperatorsBorrowTheOwnedValue) {
  auto ptr = reloco::unique_ptr<plain_widget>::try_create(4);
  ASSERT_TRUE(ptr);
  EXPECT_EQ((**ptr).value, 4);
  EXPECT_EQ((*ptr)->value, 4);
}

TEST(UniquePtrTest, UnsafeGetSkipsTheNullCheck) {
  auto ptr = reloco::unique_ptr<plain_widget>::try_create(5);
  ASSERT_TRUE(ptr);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(ptr->unsafe_get()->value, 5);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}
