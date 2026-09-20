// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/shared_ptr.hpp>

#include <utility>
#include <vector>

namespace {

struct widget {
  int value;
  static int live_count;

  explicit widget(int v) noexcept : value(v) { ++live_count; }
  widget(const widget &other) noexcept : value(other.value) { ++live_count; }
  ~widget() { --live_count; }
};

int widget::live_count = 0;

struct base_widget {
  virtual ~base_widget() = default;
  virtual int tag() const noexcept = 0;
};

struct derived_widget : base_widget {
  int value;
  explicit derived_widget(int v) noexcept : value(v) {}
  int tag() const noexcept override { return value; }
};

struct creatable_widget {
  int value;
  static reloco::result<creatable_widget> try_create(int v) noexcept {
    if (v < 0)
      return reloco::unexpected(reloco::error::invalid_argument);
    return creatable_widget{v};
  }
};

struct self_aware_widget : reloco::enable_shared_from_this<self_aware_widget> {
  int value;
  explicit self_aware_widget(int v) noexcept : value(v) {}
};

// Every allocation request fails, exercising the box-allocation-failure
// path of both try_allocate_shared and try_allocate_combined_shared.
struct exhausted_allocator_tag {};

} // namespace

template <> struct reloco::allocator_traits<exhausted_allocator_tag> {
  using context_type = void;

  static reloco::result<reloco::mem_block> allocate(std::size_t, std::size_t) noexcept {
    return reloco::unexpected(reloco::error::allocation_failed);
  }

  static void deallocate(void *, std::size_t) noexcept {}
};

TEST(SharedPtrTest, TryCreateSharedUsesDefaultAllocator) {
  auto ptr = reloco::try_create_shared<widget>(7);
  ASSERT_TRUE(ptr);
  EXPECT_TRUE(*ptr);
  EXPECT_EQ((*ptr)->value, 7);
  EXPECT_EQ(ptr->use_count(), 1u);
}

TEST(SharedPtrTest, TryCreateCombinedSharedUsesDefaultAllocator) {
  auto ptr = reloco::try_create_combined_shared<widget>(8);
  ASSERT_TRUE(ptr);
  EXPECT_EQ((*ptr)->value, 8);
}

TEST(SharedPtrTest, TryAllocateSharedUsesExplicitAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto ptr = reloco::try_allocate_shared<widget>(heap, 9);
  ASSERT_TRUE(ptr);
  EXPECT_EQ((*ptr)->value, 9);
}

TEST(SharedPtrTest, TryAllocateCombinedSharedUsesExplicitAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto ptr = reloco::try_allocate_combined_shared<widget>(heap, 10);
  ASSERT_TRUE(ptr);
  EXPECT_EQ((*ptr)->value, 10);
}

TEST(SharedPtrTest, TryCreateSharedUsesTryCreateTierAndForwardsFailure) {
  auto ok = reloco::try_create_shared<creatable_widget>(11);
  ASSERT_TRUE(ok);
  EXPECT_EQ((*ok)->value, 11);

  auto failed = reloco::try_create_shared<creatable_widget>(-1);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), reloco::error::invalid_argument);
}

TEST(SharedPtrTest, TryAllocateSharedReportsBoxAllocationFailure) {
  reloco::allocator_ref exhausted = reloco::allocator<exhausted_allocator_tag>::ref();
  auto failed = reloco::try_allocate_shared<widget>(exhausted, 1);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), reloco::error::allocation_failed);
}

TEST(SharedPtrTest, TryAllocateCombinedSharedReportsBoxAllocationFailure) {
  reloco::allocator_ref exhausted = reloco::allocator<exhausted_allocator_tag>::ref();
  auto failed = reloco::try_allocate_combined_shared<widget>(exhausted, 1);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), reloco::error::allocation_failed);
}

TEST(SharedPtrTest, DefaultConstructedIsNull) {
  reloco::shared_ptr<widget> ptr;
  EXPECT_FALSE(ptr);
  EXPECT_EQ(ptr.get(), nullptr);
  EXPECT_EQ(ptr.use_count(), 0u);
}

TEST(SharedPtrTest, NullptrConstructedIsNull) {
  reloco::shared_ptr<widget> ptr(nullptr);
  EXPECT_FALSE(ptr);
}

TEST(SharedPtrTest, CopyIncrementsUseCount) {
  auto ptr = reloco::try_create_shared<widget>(1);
  ASSERT_TRUE(ptr);
  reloco::shared_ptr<widget> copy = *ptr;
  EXPECT_EQ(ptr->use_count(), 2u);
  EXPECT_EQ(copy.use_count(), 2u);
  EXPECT_EQ(copy->value, 1);
}

TEST(SharedPtrTest, DestroyingACopyDecrementsUseCountButKeepsObjectAlive) {
  auto ptr = reloco::try_create_shared<widget>(2);
  ASSERT_TRUE(ptr);
  {
    reloco::shared_ptr<widget> copy = *ptr;
    EXPECT_EQ(ptr->use_count(), 2u);
  }
  EXPECT_EQ(ptr->use_count(), 1u);
  EXPECT_EQ((*ptr)->value, 2);
}

TEST(SharedPtrTest, LastOwnerReleasingDestroysTheObject) {
  widget::live_count = 0;
  {
    auto ptr = reloco::try_create_shared<widget>(3);
    ASSERT_TRUE(ptr);
    EXPECT_EQ(widget::live_count, 1);
  }
  EXPECT_EQ(widget::live_count, 0);
}

TEST(SharedPtrTest, MoveConstructionTransfersOwnershipAndNullsSource) {
  auto ptr = reloco::try_create_shared<widget>(4);
  ASSERT_TRUE(ptr);
  reloco::shared_ptr<widget> moved(std::move(*ptr));
  EXPECT_TRUE(moved);
  EXPECT_EQ(moved->value, 4);
  EXPECT_FALSE(*ptr);
  EXPECT_EQ(moved.use_count(), 1u);
}

TEST(SharedPtrTest, MoveAssignmentResetsPreviousOwnerAndTransfersOwnership) {
  auto first = reloco::try_create_shared<widget>(1);
  auto second = reloco::try_create_shared<widget>(2);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  *first = std::move(*second);
  EXPECT_EQ((*first)->value, 2);
  EXPECT_FALSE(*second);
}

TEST(SharedPtrTest, CopyAssignmentSharesOwnershipAndReleasesPrevious) {
  auto first = reloco::try_create_shared<widget>(1);
  auto second = reloco::try_create_shared<widget>(2);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  *first = *second;
  EXPECT_EQ((*first)->value, 2);
  EXPECT_EQ(first->use_count(), 2u);
  EXPECT_EQ(second->use_count(), 2u);
}

TEST(SharedPtrTest, ResetReleasesOwnershipAndNulls) {
  auto ptr = reloco::try_create_shared<widget>(5);
  ASSERT_TRUE(ptr);
  ptr->reset();
  EXPECT_FALSE(*ptr);
}

TEST(SharedPtrTest, DereferenceOperatorsBorrowTheOwnedValue) {
  auto ptr = reloco::try_create_shared<widget>(6);
  ASSERT_TRUE(ptr);
  EXPECT_EQ((**ptr).value, 6);
  EXPECT_EQ((*ptr)->value, 6);
}

TEST(SharedPtrTest, UnsafeGetSkipsTheNullCheck) {
  auto ptr = reloco::try_create_shared<widget>(7);
  ASSERT_TRUE(ptr);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(ptr->unsafe_get()->value, 7);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

TEST(SharedPtrTest, TryGetFailsOnEmptyPointer) {
  reloco::shared_ptr<widget> ptr;
  auto got = ptr.try_get();
  ASSERT_FALSE(got);
  EXPECT_EQ(got.error(), reloco::error::empty_pointer);
}

TEST(SharedPtrTest, WeakPtrLockSucceedsWhileObjectAlive) {
  auto ptr = reloco::try_create_shared<widget>(9);
  ASSERT_TRUE(ptr);
  reloco::weak_ptr<widget> weak = *ptr;
  EXPECT_FALSE(weak.expired());
  auto locked = weak.lock();
  ASSERT_TRUE(locked);
  EXPECT_EQ((*locked)->value, 9);
  EXPECT_EQ(ptr->use_count(), 2u);
}

TEST(SharedPtrTest, WeakPtrLockFailsAfterObjectDestroyed) {
  reloco::weak_ptr<widget> weak;
  {
    auto ptr = reloco::try_create_shared<widget>(10);
    ASSERT_TRUE(ptr);
    weak = *ptr;
  }
  EXPECT_TRUE(weak.expired());
  auto locked = weak.lock();
  ASSERT_FALSE(locked);
  EXPECT_EQ(locked.error(), reloco::error::pointer_expired);
}

TEST(SharedPtrTest, WeakPtrLockFailsOnDefaultConstructed) {
  reloco::weak_ptr<widget> weak;
  auto locked = weak.lock();
  ASSERT_FALSE(locked);
  EXPECT_EQ(locked.error(), reloco::error::empty_pointer);
}

TEST(SharedPtrTest, SeparateControlBlockReleasesObjectBeforeWeakExpires) {
  widget::live_count = 0;
  reloco::weak_ptr<widget> weak;
  {
    auto ptr = reloco::try_create_shared<widget>(11);
    ASSERT_TRUE(ptr);
    weak = *ptr;
    EXPECT_EQ(widget::live_count, 1);
  }
  // The shared_ptr released the object; the weak_ptr alone keeps only the
  // (small, separate) control block alive.
  EXPECT_EQ(widget::live_count, 0);
  EXPECT_TRUE(weak.expired());
}

TEST(SharedPtrTest, StaticPointerCastSharesOwnership) {
  auto derived = reloco::try_create_shared<derived_widget>(42);
  ASSERT_TRUE(derived);
  reloco::shared_ptr<base_widget> base = reloco::static_pointer_cast<base_widget>(*derived);
  EXPECT_EQ(base->tag(), 42);
  EXPECT_EQ(derived->use_count(), 2u);
}

TEST(SharedPtrTest, DynamicPointerCastSucceedsForMatchingType) {
  auto derived = reloco::try_create_shared<derived_widget>(43);
  ASSERT_TRUE(derived);
  reloco::shared_ptr<base_widget> base = reloco::static_pointer_cast<base_widget>(*derived);
  reloco::shared_ptr<derived_widget> back = reloco::dynamic_pointer_cast<derived_widget>(base);
  ASSERT_TRUE(back);
  EXPECT_EQ(back->value, 43);
}

TEST(SharedPtrTest, ShareFromThisReturnsASharedOwner) {
  auto self_aware = reloco::try_create_shared<self_aware_widget>(21);
  ASSERT_TRUE(self_aware);
  auto shared_again = (*self_aware)->shared_from_this();
  ASSERT_TRUE(shared_again);
  EXPECT_EQ((*shared_again)->value, 21);
  EXPECT_EQ(self_aware->use_count(), 2u);
}

TEST(SharedPtrTest, ShareFromThisFailsBeforeOwnedByAnySharedPtr) {
  self_aware_widget standalone(1);
  auto result = standalone.shared_from_this();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), reloco::error::empty_pointer);
}

TEST(SharedPtrTest, EqualityAndOrderingCompareUnderlyingPointer) {
  auto a = reloco::try_create_shared<widget>(1);
  auto b = reloco::try_create_shared<widget>(1);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  EXPECT_TRUE(*a == *a);
  EXPECT_FALSE(*a == *b);
  EXPECT_TRUE(*a != *b);
  reloco::shared_ptr<widget> null_ptr;
  EXPECT_TRUE(null_ptr == nullptr);
  EXPECT_FALSE(*a == nullptr);
}

TEST(SharedPtrTest, VectorOfSharedPtrsIsRelocatableFriendly) {
  static_assert(reloco::is_trivially_relocatable_v<reloco::shared_ptr<widget>>);
  static_assert(reloco::is_trivially_relocatable_v<reloco::weak_ptr<widget>>);

  std::vector<reloco::shared_ptr<widget>> ptrs;
  for (int i = 0; i < 4; ++i) {
    auto ptr = reloco::try_create_shared<widget>(i);
    ASSERT_TRUE(ptr);
    ptrs.push_back(std::move(*ptr));
  }
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(ptrs[static_cast<std::size_t>(i)]->value, i);
  }
}
