// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <atomic>
#include <gtest/gtest.h>
#include <reloco/barrier.hpp>

#include <thread>
#include <vector>

TEST(BarrierTest, SingleThreadIsAlwaysItsOwnLeader) {
  reloco::barrier b(1);
  EXPECT_TRUE(b.wait());
  EXPECT_TRUE(b.wait());
  EXPECT_TRUE(b.wait());
}

TEST(BarrierTest, ZeroThreadsBehavesLikeOne) {
  reloco::barrier b(0);
  EXPECT_TRUE(b.wait());
  EXPECT_TRUE(b.wait());
}

TEST(BarrierTest, ReleasesExactlyOneLeaderPerWave) {
  constexpr int kThreads = 8;
  reloco::barrier b(kThreads);
  std::atomic<int> leaders{0};

  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&] {
      if (b.wait())
        leaders.fetch_add(1, std::memory_order_relaxed);
    });
  }
  for (auto &t : threads)
    t.join();

  EXPECT_EQ(leaders.load(), 1);
}

TEST(BarrierTest, EveryThreadIsReleasedTogetherEachWave) {
  constexpr int kThreads = 4;
  constexpr int kWaves = 50;
  reloco::barrier b(kThreads);
  std::atomic<int> arrived_this_wave{0};
  std::atomic<int> observed_mismatch{0};

  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&, i] {
      for (int wave = 0; wave < kWaves; ++wave) {
        arrived_this_wave.fetch_add(1, std::memory_order_relaxed);
        static_cast<void>(b.wait()); // phase 1: every thread has incremented before any reads.
        if (i == 0) {
          // Every thread must have already incremented arrived_this_wave
          // by the time any thread is released from phase 1.
          if (arrived_this_wave.load(std::memory_order_relaxed) != kThreads)
            observed_mismatch.fetch_add(1, std::memory_order_relaxed);
          arrived_this_wave.store(0, std::memory_order_relaxed);
        }
        static_cast<void>(b.wait()); // phase 2: the reset above finishes before the next wave starts incrementing.
      }
    });
  }
  for (auto &t : threads)
    t.join();

  EXPECT_EQ(observed_mismatch.load(), 0);
}

TEST(BarrierTest, IsReusableAcrossManyWavesWithoutDeadlock) {
  reloco::barrier b(3);
  for (int wave = 0; wave < 100; ++wave) {
    std::vector<std::thread> threads;
    std::atomic<int> leaders{0};
    for (int i = 0; i < 3; ++i) {
      threads.emplace_back([&] {
        if (b.wait())
          leaders.fetch_add(1, std::memory_order_relaxed);
      });
    }
    for (auto &t : threads)
      t.join();
    EXPECT_EQ(leaders.load(), 1);
  }
}
