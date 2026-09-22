// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/mutex.hpp>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

TEST(MutexTest, LockUnlockRoundTrips) {
  reloco::mutex m;
  m.lock();
  m.unlock();
}

TEST(MutexTest, TryLockFailsWhileHeld) {
  reloco::mutex m;
  m.lock();
  EXPECT_FALSE(m.try_lock());
  m.unlock();
  EXPECT_TRUE(m.try_lock());
  m.unlock();
}

TEST(MutexTest, MutualExclusionAcrossThreads) {
  reloco::mutex m;
  int counter = 0;
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < 1000; ++j) {
        m.lock();
        ++counter;
        m.unlock();
      }
    });
  }
  for (auto &t : threads)
    t.join();
  EXPECT_EQ(counter, 8000);
}

TEST(RecursiveMutexTest, SameThreadCanLockMultipleTimes) {
  reloco::recursive_mutex m;
  m.lock();
  m.lock();
  m.unlock();
  m.unlock();
  EXPECT_TRUE(m.try_lock());
  m.unlock();
}

TEST(ErrorCheckingMutexTest, RelockingFromSameThreadFailsWithDeadlock) {
  reloco::error_checking_mutex m;
  ASSERT_TRUE(m.lock());
  auto res = m.lock();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), reloco::error::deadlock);
  ASSERT_TRUE(m.unlock());
}

TEST(ErrorCheckingMutexTest, UnlockingFromNonOwnerThreadFails) {
  reloco::error_checking_mutex m;
  ASSERT_TRUE(m.lock());

  std::atomic<reloco::error> observed{reloco::error::invalid_argument};
  std::thread other([&]() {
    auto res = m.unlock();
    if (!res)
      observed.store(res.error());
  });
  other.join();

  EXPECT_EQ(observed.load(), reloco::error::invalid_owner);
  ASSERT_TRUE(m.unlock());
}

TEST(ErrorCheckingMutexTest, TryLockFromSameThreadFailsWhileHeld) {
  reloco::error_checking_mutex m;
  ASSERT_TRUE(m.lock());
  EXPECT_FALSE(m.try_lock());
  ASSERT_TRUE(m.unlock());
  EXPECT_TRUE(m.try_lock());
  ASSERT_TRUE(m.unlock());
}

TEST(SharedMutexTest, ExclusiveLockExcludesEverything) {
  reloco::shared_mutex m;
  m.lock();
  EXPECT_FALSE(m.try_lock());
  EXPECT_FALSE(m.try_lock_shared());
  m.unlock();
}

TEST(SharedMutexTest, MultipleReadersAllowedConcurrently) {
  reloco::shared_mutex m;
  m.lock_shared();
  EXPECT_TRUE(m.try_lock_shared());
  EXPECT_FALSE(m.try_lock());
  m.unlock_shared();
  m.unlock_shared();
}

TEST(ConditionVariableTest, NotifyOneWakesWaitingThread) {
  reloco::mutex m;
  reloco::condition_variable cv;
  bool ready = false;
  bool woke = false;

  std::thread waiter([&]() {
    m.lock();
    std::unique_lock<reloco::mutex> locker(m, std::adopt_lock);
    auto res = cv.wait(locker, [&]() { return ready; });
    ASSERT_TRUE(res);
    woke = true;
  });

  {
    m.lock();
    std::unique_lock<reloco::mutex> locker(m, std::adopt_lock);
    ready = true;
  }
  cv.notify_one();
  waiter.join();

  EXPECT_TRUE(woke);
}

TEST(ConditionVariableTest, WaitFailsIfLockerDoesNotOwnLock) {
  reloco::mutex m;
  reloco::condition_variable cv;
  std::unique_lock<reloco::mutex> locker(m, std::defer_lock);
  auto res = cv.wait(locker);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), reloco::error::not_locked);
}
