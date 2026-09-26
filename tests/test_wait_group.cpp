// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <reloco/wait_group.hpp>

#include <thread>
#include <utility>
#include <vector>

TEST(WaitGroupTest, WaitReturnsImmediatelyWithNoOutstandingClones) {
  auto wg = reloco::wait_group::try_create();
  ASSERT_TRUE(wg);
  std::move(*wg).wait();
  SUCCEED();
}

TEST(WaitGroupTest, WaitBlocksUntilEveryCloneIsDropped) {
  auto wg = reloco::wait_group::try_create();
  ASSERT_TRUE(wg);

  std::atomic<bool> worker_done{false};
  {
    reloco::wait_group clone = *wg;
    std::thread worker([clone = std::move(clone), &worker_done]() mutable {
      worker_done.store(true, std::memory_order_release);
      // clone destroyed here, releasing its outstanding unit
    });
    worker.join();
  }

  std::move(*wg).wait();
  EXPECT_TRUE(worker_done.load(std::memory_order_acquire));
}

TEST(WaitGroupTest, WaitBlocksUntilAllClonesAcrossMultipleThreadsAreDropped) {
  constexpr int kWorkers = 8;
  auto wg = reloco::wait_group::try_create();
  ASSERT_TRUE(wg);

  std::atomic<int> completed{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < kWorkers; ++i) {
    reloco::wait_group clone = *wg;
    threads.emplace_back([clone = std::move(clone), &completed]() mutable {
      completed.fetch_add(1, std::memory_order_relaxed);
    });
  }

  std::move(*wg).wait();

  for (auto &t : threads)
    t.join();

  EXPECT_EQ(completed.load(), kWorkers);
}

TEST(WaitGroupTest, CopyIncrementsOutstandingCount) {
  auto wg_result = reloco::wait_group::try_create();
  ASSERT_TRUE(wg_result);
  reloco::wait_group wg = std::move(*wg_result);

  // Clone before moving wg itself into the waiter thread below.
  reloco::wait_group clone1 = wg;
  reloco::wait_group clone2 = clone1;

  std::atomic<bool> waited{false};
  std::thread waiter([w = std::move(wg), &waited]() mutable {
    std::move(w).wait();
    waited.store(true, std::memory_order_release);
  });

  // Best-effort: gives the waiter a chance to actually block on the two
  // still-outstanding clones below before we check it hasn't finished yet.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_FALSE(waited.load(std::memory_order_acquire));

  {
    reloco::wait_group drop1 = std::move(clone1);
    reloco::wait_group drop2 = std::move(clone2);
    (void)drop1;
    (void)drop2;
  } // drop1/drop2 destroyed here, releasing the remaining outstanding units.

  waiter.join();
  EXPECT_TRUE(waited.load(std::memory_order_acquire));
}
