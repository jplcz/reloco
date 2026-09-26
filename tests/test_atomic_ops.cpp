// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/atomic_ops.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using reloco::optional;
using reloco::atomic::fetch_max;
using reloco::atomic::fetch_min;
using reloco::atomic::fetch_update;

TEST(AtomicOpsTest, FetchMaxReturnsPreviousValueAndRaisesWhenGreater) {
  std::atomic<int> a{5};
  EXPECT_EQ(fetch_max(a, 10), 5);
  EXPECT_EQ(a.load(), 10);
}

TEST(AtomicOpsTest, FetchMaxLeavesValueUnchangedWhenNotGreater) {
  std::atomic<int> a{10};
  EXPECT_EQ(fetch_max(a, 3), 10);
  EXPECT_EQ(a.load(), 10);
}

TEST(AtomicOpsTest, FetchMaxWorksOnPointers) {
  int arr[4] = {};
  std::atomic<int *> a{&arr[0]};
  EXPECT_EQ(fetch_max(a, &arr[2]), &arr[0]);
  EXPECT_EQ(a.load(), &arr[2]);
}

TEST(AtomicOpsTest, FetchMinReturnsPreviousValueAndLowersWhenSmaller) {
  std::atomic<int> a{10};
  EXPECT_EQ(fetch_min(a, 3), 10);
  EXPECT_EQ(a.load(), 3);
}

TEST(AtomicOpsTest, FetchMinLeavesValueUnchangedWhenNotSmaller) {
  std::atomic<int> a{3};
  EXPECT_EQ(fetch_min(a, 10), 3);
  EXPECT_EQ(a.load(), 3);
}

TEST(AtomicOpsTest, FetchMaxIsAtomicAcrossThreads) {
  std::atomic<int> a{0};
  std::vector<std::thread> threads;
  for (int i = 1; i <= 8; ++i)
    threads.emplace_back([&a, i] { fetch_max(a, i * 100); });
  for (auto &t : threads)
    t.join();
  EXPECT_EQ(a.load(), 800);
}

TEST(AtomicOpsTest, FetchUpdateSucceedsAndReturnsPreviousValue) {
  std::atomic<int> a{5};
  auto result = fetch_update(a, [](int current) { return optional<int>(current + 1); });
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 5);
  EXPECT_EQ(a.load(), 6);
}

TEST(AtomicOpsTest, FetchUpdateAbortsAndReturnsLastObservedValue) {
  std::atomic<int> a{5};
  auto result = fetch_update(a, [](int) { return optional<int>(); });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), 5);
  EXPECT_EQ(a.load(), 5); // never stored
}

TEST(AtomicOpsTest, FetchUpdateSaturatingIncrementStopsAtCap) {
  std::atomic<int> a{9};
  auto saturating_increment = [](int current) -> optional<int> {
    if (current >= 10)
      return optional<int>();
    return optional<int>(current + 1);
  };
  auto first = fetch_update(a, saturating_increment);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first.value(), 9);
  EXPECT_EQ(a.load(), 10);

  auto second = fetch_update(a, saturating_increment);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), 10);
  EXPECT_EQ(a.load(), 10);
}

TEST(AtomicOpsTest, FetchUpdateHonorsExplicitMemoryOrders) {
  std::atomic<int> a{1};
  auto result = fetch_update(a, std::memory_order_release, std::memory_order_relaxed,
                             [](int current) { return optional<int>(current * 2); });
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 1);
  EXPECT_EQ(a.load(), 2);
}

TEST(AtomicOpsTest, FetchUpdateIsAtomicAcrossThreads) {
  std::atomic<int> a{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
    threads.emplace_back([&a] {
      for (int j = 0; j < 1000; ++j)
        (void)fetch_update(a, [](int current) { return optional<int>(current + 1); });
    });
  for (auto &t : threads)
    t.join();
  EXPECT_EQ(a.load(), 8000);
}
