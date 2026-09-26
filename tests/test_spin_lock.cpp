// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/guarded_mutex.hpp>
#include <reloco/spin_lock.hpp>

#include <thread>
#include <vector>

TEST(SpinLockTest, DefaultConstructedIsUnlocked) {
  reloco::spin_lock lock;
  EXPECT_FALSE(lock.is_locked());
}

TEST(SpinLockTest, LockThenUnlockRoundTrips) {
  reloco::spin_lock lock;
  lock.lock();
  EXPECT_TRUE(lock.is_locked());
  lock.unlock();
  EXPECT_FALSE(lock.is_locked());
}

TEST(SpinLockTest, TryLockSucceedsWhenFree) {
  reloco::spin_lock lock;
  EXPECT_TRUE(lock.try_lock());
  EXPECT_TRUE(lock.is_locked());
  lock.unlock();
}

TEST(SpinLockTest, TryLockFailsWhileHeld) {
  reloco::spin_lock lock;
  lock.lock();
  EXPECT_FALSE(lock.try_lock());
  lock.unlock();
}

TEST(SpinLockTest, MutualExclusionAcrossThreads) {
  reloco::spin_lock lock;
  int counter = 0;
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < 1000; ++j) {
        lock.lock();
        ++counter;
        lock.unlock();
      }
    });
  }
  for (auto &t : threads)
    t.join();
  EXPECT_EQ(counter, 8000);
}

TEST(SpinLockTest, WorksAsGuardedMutexBackend) {
  reloco::guarded_mutex<int, reloco::spin_lock> m(5);
  {
    auto g = m.lock();
    *g = 6;
  }
  EXPECT_EQ(*m.lock(), 6);
}

TEST(SpinLockTest, GuardedMutexTryLockFailsWhileHeld) {
  reloco::guarded_mutex<int, reloco::spin_lock> m(0);
  auto g1 = m.lock();
  auto g2 = m.try_lock();
  EXPECT_FALSE(g2.has_value());
}
