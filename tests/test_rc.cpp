// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/rc.hpp>

#include <utility>

namespace {

struct widget {
  int value;
  static int live_count;

  explicit widget(int v) noexcept : value(v) { ++live_count; }
  ~widget() { --live_count; }
};

int widget::live_count = 0;

struct self_aware_widget : reloco::enable_rc_from_this<self_aware_widget> {
  int value;
  explicit self_aware_widget(int v) noexcept : value(v) {}
};

// Every allocation request fails, exercising the box-allocation-failure
// path of both try_allocate_rc and try_allocate_combined_rc.
struct exhausted_allocator_tag {};

} // namespace

template <> struct reloco::allocator_traits<exhausted_allocator_tag> {
  using context_type = void;

  static reloco::result<reloco::mem_block> allocate(std::size_t, std::size_t) noexcept {
    return reloco::unexpected(reloco::error::allocation_failed);
  }

  static void deallocate(void *, std::size_t) noexcept {}
};

TEST(RcTest, TryCreateRcUsesDefaultAllocator) {
  widget::live_count = 0;
  auto r = reloco::try_create_rc<widget>(42);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->get()->value, 42);
}

TEST(RcTest, TryCreateCombinedRcUsesDefaultAllocator) {
  widget::live_count = 0;
  auto r = reloco::try_create_combined_rc<widget>(7);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->get()->value, 7);
}

TEST(RcTest, TryAllocateRcReportsBoxAllocationFailure) {
  reloco::allocator<exhausted_allocator_tag> alloc;
  auto r = reloco::try_allocate_rc<widget>(alloc.ref(), 1);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::allocation_failed);
}

TEST(RcTest, TryAllocateCombinedRcReportsBoxAllocationFailure) {
  reloco::allocator<exhausted_allocator_tag> alloc;
  auto r = reloco::try_allocate_combined_rc<widget>(alloc.ref(), 1);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::allocation_failed);
}

TEST(RcTest, DefaultConstructedIsNull) {
  reloco::rc<widget> p;
  EXPECT_FALSE(p);
  EXPECT_EQ(p.get(), nullptr);
  EXPECT_EQ(p.use_count(), 0u);
}

TEST(RcTest, CopyIncrementsUseCount) {
  widget::live_count = 0;
  auto r = reloco::try_create_combined_rc<widget>(1);
  reloco::rc<widget> a = std::move(r.value());
  reloco::rc<widget> b = a;
  EXPECT_EQ(a.use_count(), 2u);
  EXPECT_EQ(b.use_count(), 2u);
  EXPECT_EQ(widget::live_count, 1);
}

TEST(RcTest, LastOwnerReleasingDestroysTheObject) {
  widget::live_count = 0;
  auto r = reloco::try_create_combined_rc<widget>(1);
  reloco::rc<widget> a = std::move(r.value());
  {
    reloco::rc<widget> b = a;
    EXPECT_EQ(widget::live_count, 1);
  }
  EXPECT_EQ(a.use_count(), 1u);
  EXPECT_EQ(widget::live_count, 1);
  a.reset();
  EXPECT_EQ(widget::live_count, 0);
}

TEST(RcTest, MoveConstructionTransfersOwnershipAndNullsSource) {
  auto r = reloco::try_create_rc<widget>(9);
  reloco::rc<widget> a = std::move(r.value());
  reloco::rc<widget> b = std::move(a);
  EXPECT_FALSE(a);
  EXPECT_TRUE(b);
  EXPECT_EQ(b->value, 9);
}

TEST(RcTest, WeakRcLockSucceedsWhileObjectAlive) {
  auto r = reloco::try_create_combined_rc<widget>(3);
  reloco::rc<widget> a = std::move(r.value());
  reloco::weak_rc<widget> w = a;
  EXPECT_FALSE(w.expired());

  auto locked = w.lock();
  ASSERT_TRUE(locked.has_value());
  EXPECT_EQ(locked->get()->value, 3);
  EXPECT_EQ(a.use_count(), 2u);
}

TEST(RcTest, WeakRcLockFailsAfterObjectDestroyed) {
  reloco::weak_rc<widget> w;
  {
    auto r = reloco::try_create_combined_rc<widget>(4);
    reloco::rc<widget> a = std::move(r.value());
    w = a;
  }
  EXPECT_TRUE(w.expired());
  auto locked = w.lock();
  ASSERT_FALSE(locked.has_value());
  EXPECT_EQ(locked.error(), reloco::error::pointer_expired);
}

TEST(RcTest, WeakRcLockFailsOnDefaultConstructed) {
  reloco::weak_rc<widget> w;
  auto locked = w.lock();
  ASSERT_FALSE(locked.has_value());
  EXPECT_EQ(locked.error(), reloco::error::empty_pointer);
}

TEST(RcTest, RcFromThisReturnsASharedOwner) {
  auto r = reloco::try_create_combined_rc<self_aware_widget>(11);
  reloco::rc<self_aware_widget> a = std::move(r.value());

  auto self = a->rc_from_this();
  ASSERT_TRUE(self.has_value());
  EXPECT_EQ(self->get(), a.get());
  EXPECT_EQ(a.use_count(), 2u);
}

TEST(RcTest, RcFromThisFailsBeforeOwnedByAnyRc) {
  self_aware_widget w(5);
  auto self = w.rc_from_this();
  EXPECT_FALSE(self.has_value());
}

TEST(RcTest, EqualityComparesUnderlyingPointer) {
  auto r1 = reloco::try_create_rc<widget>(1);
  auto r2 = reloco::try_create_rc<widget>(1);
  reloco::rc<widget> a = std::move(r1.value());
  reloco::rc<widget> b = std::move(r2.value());
  reloco::rc<widget> c = a;

  EXPECT_TRUE(a == c);
  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a != b);
  EXPECT_TRUE(a != nullptr);

  reloco::rc<widget> empty;
  EXPECT_TRUE(empty == nullptr);
}

TEST(RcTest, IsTriviallyRelocatable) {
  static_assert(reloco::is_trivially_relocatable<reloco::rc<widget>>::value,
                "rc<T> should be trivially relocatable");
  static_assert(reloco::is_trivially_relocatable<reloco::weak_rc<widget>>::value,
                "weak_rc<T> should be trivially relocatable");
}
