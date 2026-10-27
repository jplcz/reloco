// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// per_cpu_provider<T, Tag> and per_cpu_slot<T>. Compiled once per RELOCO_PER_CPU_MODEL (see CMakeLists.txt's
// jplcz_reloco_per_cpu_*_tests targets); the model-independent checks run in every build.

#include <gtest/gtest.h>
#include <reloco/per_cpu_provider.hpp>
#include <reloco/per_cpu_slot.hpp>
#include <reloco/thread.hpp>

#include <cstdint>
#include <algorithm>
#include <reloco/span.hpp>
#include <type_traits>

namespace {

struct counters {
  std::uint64_t rx = 0;
  std::uint64_t tx = 0;
};

counters make_counters() noexcept { return counters{100, 200}; }

struct block {
  std::uint64_t hits;
  reloco::per_cpu_slot<counters> stats;
};
struct block_tag {};
struct other_tag {};

} // namespace

TEST(PerCpuSlotTest, AllZeroBytesIsTheEmptyState) {
  reloco::per_cpu_slot<counters> slot{};
  EXPECT_FALSE(slot.is_ready());
  const reloco::span<const decltype(slot)> whole(&slot, 1);
  const auto bytes = whole.as_bytes();
  EXPECT_TRUE(std::all_of(bytes.begin(), bytes.end(), [](std::byte b) { return b == std::byte{0}; }));
}

TEST(PerCpuSlotTest, InitBuildsOnceAndAccessIsScoped) {
  reloco::per_cpu_slot<counters> slot;
  slot.init(&make_counters);
  EXPECT_TRUE(slot.is_ready());
  slot.with_mut([](counters &c) { c.rx += 1; });
  slot.init(+[]() noexcept { return counters{}; }); // a second init is a no-op
  EXPECT_EQ(slot.with([](const counters &c) { return c.rx; }), 101u);
  EXPECT_EQ(slot.with([](const counters &c) { return c.tx; }), 200u);
}

TEST(PerCpuProviderTest, TheBlockIsZeroFilledAndStable) {
  static_assert(std::is_standard_layout_v<block>);
  block &b = reloco::per_cpu_provider<block, block_tag>::get();
  EXPECT_EQ(b.hits, 0u);
  EXPECT_FALSE(b.stats.is_ready());
  b.hits = 5;
  using provider = reloco::per_cpu_provider<block, block_tag>;
  EXPECT_EQ(&provider::get(), &b);
  EXPECT_EQ(provider::get().hits, 5u);
  b.hits = 0;
}

TEST(PerCpuProviderTest, DifferentTagsAreDifferentBlocks) {
  auto &a = reloco::per_cpu_provider<std::uint64_t, block_tag>::get();
  auto &b = reloco::per_cpu_provider<std::uint64_t, other_tag>::get();
  EXPECT_NE(&a, &b);
}

TEST(PerCpuProviderTest, GetCpuIsBoundsChecked) {
  using provider = reloco::per_cpu_provider<std::uint64_t, block_tag>;
  ASSERT_GE(provider::cpu_count(), 1u);
  auto first = provider::get_cpu(0);
  ASSERT_TRUE(first.has_value());
  auto past = provider::get_cpu(provider::cpu_count());
  ASSERT_FALSE(past.has_value());
  EXPECT_EQ(past.error(), reloco::error::out_of_bounds);
}

#if (RELOCO_PER_CPU_MODEL == RELOCO_PER_CPU_MODEL_THREAD_LOCAL)
TEST(PerCpuProviderTest, ThreadLocalModelGivesEachThreadItsOwnBlock) {
  struct tag {};
  using provider = reloco::per_cpu_provider<std::uint64_t, tag>;
  provider::get() = 11;
  std::uint64_t other = 99;
  auto handle = reloco::spawn([&]() noexcept { other = provider::get(); });
  ASSERT_TRUE(handle.has_value());
  std::move(*handle).join();
  EXPECT_EQ(other, 0u);
  EXPECT_EQ(provider::get(), 11u);
}
#endif
