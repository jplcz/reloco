// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/thread.hpp>
#include <reloco/tls_provider.hpp>
#include <reloco/tls_slot.hpp>
#include <reloco/vector.hpp>

#include <algorithm>
#include <reloco/span.hpp>
#include <type_traits>

namespace {

int live_objects = 0;

struct tracked {
  int value;
  explicit tracked(int v) noexcept : value(v) { ++live_objects; }
  tracked(tracked &&o) noexcept : value(o.value) { ++live_objects; }
  tracked(const tracked &) = delete;
  ~tracked() { --live_objects; }
};

reloco::result<tracked> make_ok() noexcept { return tracked(7); }
reloco::result<tracked> make_fail() noexcept { return reloco::unexpected(reloco::error::allocation_failed); }

int init_calls = 0;
reloco::result<tracked> make_counting() noexcept {
  ++init_calls;
  return tracked(init_calls);
}

} // namespace

TEST(TlsSlotTest, AllZeroBytesIsTheEmptyState) {
  reloco::tls_slot<tracked> slot{};
  const reloco::span<const decltype(slot)> whole(&slot, 1);
  const auto bytes = whole.as_bytes();
  EXPECT_TRUE(std::all_of(bytes.begin(), bytes.end(), [](std::byte b) { return b == std::byte{0}; }));
  EXPECT_FALSE(slot.is_ready());
  EXPECT_FALSE(slot.with_ready([](const tracked &t) { return t.value; }).has_value());
}

TEST(TlsSlotTest, TryWithBuildsOnFirstUseAndReusesAfterwards) {
  init_calls = 0;
  reloco::tls_slot<tracked> slot;
  auto first = slot.try_with(&make_counting, [](const tracked &t) { return t.value; });
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first.value(), 1);
  auto second = slot.try_with_mut(&make_counting, [](tracked &t) {
    t.value += 10;
    return t.value;
  });
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second.value(), 11);
  EXPECT_EQ(init_calls, 1);
  EXPECT_TRUE(slot.is_ready());
  slot.destroy();
}

TEST(TlsSlotTest, InitErrorIsSurfacedAndTheSlotStaysEmpty) {
  reloco::tls_slot<tracked> slot;
  auto r = slot.try_with(&make_fail, [](const tracked &t) { return t.value; });
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::allocation_failed);
  EXPECT_FALSE(slot.is_ready());
  EXPECT_EQ(live_objects, 0);
  auto retry = slot.try_with(&make_ok, [](const tracked &t) { return t.value; }); // a failed init can be retried
  ASSERT_TRUE(retry.has_value());
  EXPECT_EQ(retry.value(), 7);
  slot.destroy();
}

TEST(TlsSlotTest, WithReadyNeverInitialisesAndWorksOnceReady) {
  reloco::tls_slot<tracked> slot;
  EXPECT_FALSE(slot.with_ready([](const tracked &) { return 1; }).has_value());
  EXPECT_FALSE(slot.is_ready());
  ASSERT_TRUE(slot.try_with(&make_ok, [](const tracked &) { return 0; }).has_value());
  auto v = slot.with_ready([](const tracked &t) { return t.value; });
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v.value(), 7);
  bool ran = slot.with_ready_mut([](tracked &t) { t.value = 9; });
  EXPECT_TRUE(ran);
  EXPECT_EQ(slot.with_ready([](const tracked &t) { return t.value; }).value(), 9);
  slot.destroy();
}

TEST(TlsSlotTest, VoidClosuresReportRanOrNot) {
  reloco::tls_slot<tracked> slot;
  EXPECT_FALSE(slot.with_ready([](const tracked &) {}));
  auto r = slot.try_with(&make_ok, [](const tracked &) {});
  static_assert(std::is_same_v<decltype(r), reloco::result<void>>);
  EXPECT_TRUE(r.has_value());
  EXPECT_TRUE(slot.with_ready([](const tracked &) {}));
  slot.destroy();
}

TEST(TlsSlotTest, DestroyRunsTheDestructorOnceAndEmptiesTheSlot) {
  ASSERT_EQ(live_objects, 0);
  reloco::tls_slot<tracked> slot;
  ASSERT_TRUE(slot.try_with(&make_ok, [](const tracked &) { return 0; }).has_value());
  EXPECT_EQ(live_objects, 1);
  slot.destroy();
  EXPECT_EQ(live_objects, 0);
  EXPECT_FALSE(slot.is_ready());
  slot.destroy(); // idempotent
  EXPECT_EQ(live_objects, 0);
}

namespace {
reloco::tls_slot<tracked> *reentrant_slot = nullptr;
reloco::error reentrant_error = reloco::error::allocation_failed;

reloco::result<tracked> make_reentrant() noexcept {
  auto inner = reentrant_slot->try_with(&make_ok, [](const tracked &) { return 0; });
  if (!inner.has_value()) {
    reentrant_error = inner.error();
  }
  return tracked(1);
}
} // namespace

TEST(TlsSlotTest, AccessFromInsideTheInitialiserIsBusy) {
  static reloco::tls_slot<tracked> slot;
  reentrant_slot = &slot;
  ASSERT_TRUE(slot.try_with(&make_reentrant, [](const tracked &) { return 0; }).has_value());
  EXPECT_EQ(reentrant_error, reloco::error::busy);
  slot.destroy();
}

TEST(TlsSlotTest, ComplexObjectsThatAllocateWorkInASlot) {
  using vec = reloco::vector<int>;
  reloco::tls_slot<vec> slot;
  auto make = []() noexcept -> reloco::result<vec> {
    vec v;
    if (auto r = v.try_push_back(1); !r)
      return reloco::unexpected(r.error());
    if (auto r = v.try_push_back(2); !r)
      return reloco::unexpected(r.error());
    return v;
  };
  auto n = slot.try_with_mut(+make, [](vec &v) { return v.size(); });
  ASSERT_TRUE(n.has_value());
  EXPECT_EQ(n.value(), 2u);
  slot.destroy();
}

// A module's thread-local block: a struct of cells reached through tls_provider; every thread has its own, created
// zero-filled on first use, and the slots in it are destroyed by the owner at thread exit.
namespace {
struct module_block {
  int counter;
  reloco::tls_slot<tracked> state;
};
struct module_tag {};
} // namespace

TEST(TlsSlotTest, ABlockOfCellsIsZeroValidAndPerThread) {
  static_assert(std::is_standard_layout_v<module_block>);
  auto mine = reloco::tls_provider<module_block, module_tag>::get();
  ASSERT_TRUE(mine.has_value());
  module_block &block = mine->get();
  EXPECT_EQ(block.counter, 0);
  EXPECT_FALSE(block.state.is_ready());
  ASSERT_TRUE(block.state.try_with(&make_ok, [](const tracked &) { return 0; }).has_value());
  bool other_thread_ready = true;
  auto handle = reloco::spawn([&]() noexcept {
    auto theirs = reloco::tls_provider<module_block, module_tag>::get();
    other_thread_ready = theirs.has_value() && theirs->get().state.is_ready();
  });
  ASSERT_TRUE(handle.has_value());
  std::move(*handle).join();
  EXPECT_FALSE(other_thread_ready);
  block.state.destroy();
}
