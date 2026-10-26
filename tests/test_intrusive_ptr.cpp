// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/intrusive_ptr.hpp>

#include <functional>
#include <thread>
#include <utility>
#include <vector>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

struct widget : reloco::intrusive_ref_counted<widget> {
  int value;
  static int live_count;

  explicit widget(int v) noexcept : value(v) { ++live_count; }
  ~widget() { --live_count; }
};

int widget::live_count = 0;

struct local_widget : reloco::intrusive_ref_counted<local_widget, reloco::local_refcount> {
  int value{0};
};

struct shape : reloco::intrusive_ref_counted<shape> {
  static int live_count;
  shape() noexcept { ++live_count; }
  virtual ~shape() { --live_count; }
  [[nodiscard]] virtual int sides() const noexcept = 0;
};

int shape::live_count = 0;

struct square final : shape {
  // padding makes sizeof(square) differ from sizeof(shape): freeing through the base must still use the right size.
  long padding[4]{};
  [[nodiscard]] int sides() const noexcept override { return 4; }
};

// Legacy type with its own retain/release, managed through a traits specialization.
struct legacy_obj {
  int refs{0};
  int releases{0};
};

struct exhausted_allocator_tag {};

} // namespace

template <> struct reloco::allocator_traits<exhausted_allocator_tag> {
  using context_type = void;

  static reloco::result<reloco::mem_block> allocate(std::size_t, std::size_t) noexcept {
    return reloco::unexpected(reloco::error::allocation_failed);
  }

  static void deallocate(void *, std::size_t) noexcept {}
};

template <> struct reloco::intrusive_ptr_traits<legacy_obj> {
  static void add_ref(const legacy_obj &o) noexcept { ++const_cast<legacy_obj &>(o).refs; }
  static void release(const legacy_obj &o) noexcept { ++const_cast<legacy_obj &>(o).releases; }
  static std::size_t use_count(const legacy_obj &o) noexcept { return static_cast<std::size_t>(o.refs - o.releases); }
};

TEST(IntrusivePtrTest, CreateAndDestroy) {
  widget::live_count = 0;
  {
    auto r = reloco::try_create_intrusive<widget>(42);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ((*r)->value, 42);
    EXPECT_EQ(r->use_count(), 1U);
    EXPECT_EQ(widget::live_count, 1);
  }
  EXPECT_EQ(widget::live_count, 0);
}

TEST(IntrusivePtrTest, CopyAndMoveTrackCount) {
  widget::live_count = 0;
  auto first = reloco::try_create_intrusive<widget>(1).value();
  {
    reloco::intrusive_ptr<widget> second = first;
    EXPECT_EQ(first.use_count(), 2U);
    EXPECT_EQ(first, second);
    reloco::intrusive_ptr<widget> third = std::move(second);
    EXPECT_FALSE(second);
    EXPECT_EQ(first.use_count(), 2U);
    third.reset();
    EXPECT_EQ(first.use_count(), 1U);
  }
  EXPECT_EQ(widget::live_count, 1);
  first.reset();
  EXPECT_EQ(widget::live_count, 0);
  EXPECT_EQ(first.use_count(), 0U);
}

TEST(IntrusivePtrTest, SelfAssignmentIsSafe) {
  auto p = reloco::try_create_intrusive<widget>(3).value();
  auto &alias = p;
  p = alias;
  EXPECT_EQ(p.use_count(), 1U);
  p = std::move(alias);
  EXPECT_EQ(p.use_count(), 1U);
  EXPECT_EQ(p->value, 3);
}

TEST(IntrusivePtrTest, TryGetOnEmptyReportsError) {
  reloco::intrusive_ptr<widget> empty;
  auto r = empty.try_get();
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::empty_pointer);
  EXPECT_TRUE(empty == nullptr);
}

TEST(IntrusivePtrTest, AllocationFailureIsReported) {
  reloco::allocator<exhausted_allocator_tag> alloc;
  auto r = reloco::try_allocate_intrusive<widget>(alloc.ref(), 1);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::allocation_failed);
}

TEST(IntrusivePtrTest, FromThisRetainsOwnedObject) {
  widget::live_count = 0;
  auto p = reloco::try_create_intrusive<widget>(5).value();
  auto again = p->intrusive_from_this();
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(again->get(), p.get());
  EXPECT_EQ(p.use_count(), 2U);
}

TEST(IntrusivePtrTest, FromThisFailsForUnownedObject) {
  widget stack_obj{1};
  auto r = stack_obj.intrusive_from_this();
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::pointer_expired);
}

TEST(IntrusivePtrTest, StackObjectIsNeverFreed) {
  widget::live_count = 0;
  {
    widget stack_obj{9};
    auto p = reloco::intrusive_ptr<widget>::unsafe_retain(&stack_obj);
    EXPECT_EQ(p.use_count(), 1U);
    p.reset();
    EXPECT_EQ(widget::live_count, 1);
  }
  EXPECT_EQ(widget::live_count, 0);
}

TEST(IntrusivePtrTest, PolymorphicDeleteUsesMostDerivedType) {
  shape::live_count = 0;
  {
    auto sq = reloco::try_create_intrusive<square>().value();
    reloco::intrusive_ptr<shape> base = sq;
    sq.reset();
    EXPECT_EQ(shape::live_count, 1);
    EXPECT_EQ(base->sides(), 4);
    auto back = reloco::dynamic_pointer_cast<square>(base);
    ASSERT_TRUE(back);
    EXPECT_EQ(base.use_count(), 2U);
    auto same = reloco::static_pointer_cast<square>(base);
    EXPECT_EQ(base.use_count(), 3U);
  }
  EXPECT_EQ(shape::live_count, 0);
}

TEST(IntrusivePtrTest, ConstPointeeCanRetainAndRelease) {
  widget::live_count = 0;
  {
    auto p = reloco::try_create_intrusive<widget>(8).value();
    reloco::intrusive_ptr<const widget> cp = p;
    EXPECT_EQ(cp.use_count(), 2U);
    p.reset();
    EXPECT_EQ(cp->value, 8);
    auto again = cp->intrusive_from_this();
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(cp.use_count(), 2U);
  }
  EXPECT_EQ(widget::live_count, 0);
}

TEST(IntrusivePtrTest, UnsafeReleaseAndAdoptRoundTrip) {
  widget::live_count = 0;
  auto p = reloco::try_create_intrusive<widget>(2).value();
  widget *raw = p.unsafe_release();
  EXPECT_FALSE(p);
  EXPECT_EQ(widget::live_count, 1);
  auto owner = reloco::intrusive_ptr<widget>::unsafe_adopt(raw);
  EXPECT_EQ(owner.use_count(), 1U);
  owner.reset();
  EXPECT_EQ(widget::live_count, 0);
}

TEST(IntrusivePtrTest, CustomTraitsManageLegacyType) {
  legacy_obj obj;
  {
    auto a = reloco::intrusive_ptr<legacy_obj>::unsafe_retain(&obj);
    auto b = a;
    EXPECT_EQ(a.use_count(), 2U);
  }
  EXPECT_EQ(obj.refs, 2);
  EXPECT_EQ(obj.releases, 2);
}

TEST(IntrusivePtrTest, CopyOfCountedObjectStartsUnowned) {
  auto p = reloco::try_create_intrusive<widget>(4).value();
  widget copy = *p;
  EXPECT_EQ(copy.intrusive_use_count(), 0U);
  EXPECT_EQ(copy.value, 4);
  EXPECT_EQ(p.use_count(), 1U);
}

TEST(IntrusivePtrTest, LocalRefcountWorks) {
  auto p = reloco::try_create_intrusive<local_widget>().value();
  auto q = p;
  EXPECT_EQ(p.use_count(), 2U);
  q.reset();
  EXPECT_EQ(p.use_count(), 1U);
  EXPECT_FALSE(reloco::is_send_v<reloco::intrusive_ptr<local_widget>>);
  EXPECT_FALSE(reloco::is_sync_v<reloco::intrusive_ptr<local_widget>>);
}

TEST(IntrusivePtrTest, SendSyncFollowsCounterAndPointee) {
  EXPECT_TRUE(reloco::is_send_v<reloco::intrusive_ptr<widget>>);
  EXPECT_TRUE(reloco::is_sync_v<reloco::intrusive_ptr<widget>>);
  EXPECT_FALSE(reloco::is_send_v<reloco::intrusive_ptr<legacy_obj>>);
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<reloco::intrusive_ptr<widget>>);
  EXPECT_EQ(sizeof(reloco::intrusive_ptr<widget>), sizeof(void *));
}

TEST(IntrusivePtrTest, HashAndOrdering) {
  auto a = reloco::try_create_intrusive<widget>(1).value();
  auto b = a;
  EXPECT_EQ(std::hash<reloco::intrusive_ptr<widget>>{}(a), std::hash<reloco::intrusive_ptr<widget>>{}(b));
  auto c = reloco::try_create_intrusive<widget>(2).value();
  EXPECT_TRUE((a < c) != (c < a));
}

TEST(IntrusivePtrTest, ConcurrentCopiesAreAtomic) {
  widget::live_count = 0;
  {
    auto p = reloco::try_create_intrusive<widget>(1).value();
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
      threads.emplace_back([p] {
        for (int i = 0; i < 10000; ++i) {
          auto copy = p;
          (void)copy;
        }
      });
    }
    for (auto &th : threads)
      th.join();
    EXPECT_EQ(p.use_count(), 1U);
  }
  EXPECT_EQ(widget::live_count, 0);
}

RELOCO_END_UNSAFE_BUFFER_USAGE
