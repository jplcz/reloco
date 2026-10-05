// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/call_location.hpp>
#include <reloco/mutex.hpp>
#include <reloco/unique_lock.hpp>

#include <atomic>
#include <thread>
#include <vector>

TEST(UniqueLockTest, DefaultConstructedOwnsNothing) {
  reloco::unique_lock<reloco::mutex> lk;
  EXPECT_FALSE(lk.owns_lock());
  EXPECT_FALSE(static_cast<bool>(lk));
  EXPECT_EQ(lk.mutex(), nullptr);
}

TEST(UniqueLockTest, ConstructingWithMutexLocksImmediately) {
  reloco::mutex m;
  {
    reloco::unique_lock<reloco::mutex> lk(m);
    EXPECT_TRUE(lk.owns_lock());
    EXPECT_TRUE(static_cast<bool>(lk));
    EXPECT_EQ(lk.mutex(), &m);
    EXPECT_FALSE(m.try_lock());
  }
  EXPECT_TRUE(m.try_lock());
  m.unlock();
}

TEST(UniqueLockTest, DeferLockDoesNotAcquire) {
  reloco::mutex m;
  reloco::unique_lock<reloco::mutex> lk(m, reloco::defer_lock);
  EXPECT_FALSE(lk.owns_lock());
  EXPECT_TRUE(m.try_lock());
  m.unlock();
}

TEST(UniqueLockTest, DeferLockThenExplicitLockAcquires) {
  reloco::mutex m;
  reloco::unique_lock<reloco::mutex> lk(m, reloco::defer_lock);
  lk.lock();
  EXPECT_TRUE(lk.owns_lock());
  EXPECT_FALSE(m.try_lock());
}

TEST(UniqueLockTest, TryToLockSucceedsWhenFree) {
  reloco::mutex m;
  reloco::unique_lock<reloco::mutex> lk(m, reloco::try_to_lock);
  EXPECT_TRUE(lk.owns_lock());
}

TEST(UniqueLockTest, TryToLockFailsWhileHeld) {
  reloco::mutex m;
  m.lock();
  reloco::unique_lock<reloco::mutex> lk(m, reloco::try_to_lock);
  EXPECT_FALSE(lk.owns_lock());
  m.unlock();
}

TEST(UniqueLockTest, AdoptLockRecordsOwnershipOfAlreadyHeldMutex) {
  reloco::mutex m;
  m.lock();
  reloco::unique_lock<reloco::mutex> lk(m, reloco::adopt_lock);
  EXPECT_TRUE(lk.owns_lock());
}

TEST(UniqueLockTest, UnlockThenLockRoundTrips) {
  reloco::mutex m;
  reloco::unique_lock<reloco::mutex> lk(m);
  lk.unlock();
  EXPECT_FALSE(lk.owns_lock());
  EXPECT_TRUE(m.try_lock());
  m.unlock();
  lk.lock();
  EXPECT_TRUE(lk.owns_lock());
}

TEST(UniqueLockTest, TryLockOnAlreadyUnlockedObjectCanFail) {
  reloco::mutex m;
  reloco::unique_lock<reloco::mutex> lk(m);
  lk.unlock();
  m.lock();
  EXPECT_FALSE(lk.try_lock());
  m.unlock();
}

TEST(UniqueLockTest, MoveConstructionTransfersOwnership) {
  reloco::mutex m;
  reloco::unique_lock<reloco::mutex> lk1(m);
  reloco::unique_lock<reloco::mutex> lk2(std::move(lk1));
  EXPECT_FALSE(lk1.owns_lock());
  EXPECT_EQ(lk1.mutex(), nullptr);
  EXPECT_TRUE(lk2.owns_lock());
  EXPECT_EQ(lk2.mutex(), &m);
}

TEST(UniqueLockTest, MoveAssignmentReleasesPreviouslyHeldMutex) {
  reloco::mutex m1;
  reloco::mutex m2;
  reloco::unique_lock<reloco::mutex> lk1(m1);
  reloco::unique_lock<reloco::mutex> lk2(m2);
  lk1 = std::move(lk2);
  EXPECT_TRUE(m1.try_lock());
  m1.unlock();
  EXPECT_TRUE(lk1.owns_lock());
  EXPECT_EQ(lk1.mutex(), &m2);
}

TEST(UniqueLockTest, ReleaseDetachesWithoutUnlocking) {
  reloco::mutex m;
  reloco::unique_lock<reloco::mutex> lk(m);
  reloco::mutex *released = lk.release();
  EXPECT_EQ(released, &m);
  EXPECT_FALSE(lk.owns_lock());
  EXPECT_EQ(lk.mutex(), nullptr);
  EXPECT_FALSE(m.try_lock());
  m.unlock();
}

TEST(UniqueLockTest, SwapExchangesOwnedMutexes) {
  reloco::mutex m1;
  reloco::mutex m2;
  reloco::unique_lock<reloco::mutex> lk1(m1);
  reloco::unique_lock<reloco::mutex> lk2(m2);
  lk1.swap(lk2);
  EXPECT_EQ(lk1.mutex(), &m2);
  EXPECT_EQ(lk2.mutex(), &m1);
}

TEST(UniqueLockTest, FreeSwapFunctionExchangesOwnedMutexes) {
  reloco::mutex m1;
  reloco::mutex m2;
  reloco::unique_lock<reloco::mutex> lk1(m1);
  reloco::unique_lock<reloco::mutex> lk2(m2);
  using reloco::swap;
  swap(lk1, lk2);
  EXPECT_EQ(lk1.mutex(), &m2);
  EXPECT_EQ(lk2.mutex(), &m1);
}

TEST(UniqueLockTest, MutualExclusionAcrossThreads) {
  reloco::mutex m;
  int counter = 0;
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < 1000; ++j) {
        reloco::unique_lock<reloco::mutex> lk(m);
        ++counter;
      }
    });
  }
  for (auto &t : threads) {
    t.join();
  }
  EXPECT_EQ(counter, 8000);
}

TEST(UniqueLockTest, WorksAsConditionVariableLocker) {
  reloco::mutex m;
  reloco::condition_variable cv;
  bool ready = false;

  std::thread waiter([&]() {
    reloco::unique_lock<reloco::mutex> lk(m);
    auto result = cv.wait(lk, [&]() { return ready; });
    EXPECT_TRUE(result.has_value());
  });

  {
    reloco::unique_lock<reloco::mutex> lk(m);
    ready = true;
  }
  cv.notify_one();
  waiter.join();
  EXPECT_TRUE(ready);
}

TEST(UniqueLockTest, ExplicitDebugAndReleaseLocationOverloadsBothWork) {
  reloco::mutex m;

  // Explicit debug_call_location_ref overload, on both the acquiring
  // constructor and lock()/try_lock()/unlock().
  {
    reloco::unique_lock<reloco::mutex> lk(m, reloco::debug_call_location_ref::current());
    EXPECT_TRUE(lk.owns_lock());
    lk.unlock(reloco::debug_call_location_ref::current());
    EXPECT_FALSE(lk.owns_lock());
    EXPECT_TRUE(lk.try_lock(reloco::debug_call_location_ref::current()));
    lk.unlock(reloco::debug_call_location_ref::current());
    lk.lock(reloco::debug_call_location_ref::current());
    EXPECT_TRUE(lk.owns_lock());
  }
  EXPECT_TRUE(m.try_lock());
  m.unlock();

  // Explicit release_call_location_ref overload, on both the acquiring
  // constructor and lock()/try_lock()/unlock() -- carries no data, but
  // must still compile and behave identically.
  {
    reloco::unique_lock<reloco::mutex> lk(m, reloco::release_call_location_ref{});
    EXPECT_TRUE(lk.owns_lock());
    lk.unlock(reloco::release_call_location_ref{});
    EXPECT_FALSE(lk.owns_lock());
    EXPECT_TRUE(lk.try_lock(reloco::release_call_location_ref{}));
    lk.unlock(reloco::release_call_location_ref{});
    lk.lock(reloco::release_call_location_ref{});
    EXPECT_TRUE(lk.owns_lock());
  }
  EXPECT_TRUE(m.try_lock());
  m.unlock();
}

TEST(UniqueLockTest, ExplicitLocationOverloadsOnTaggedConstructors) {
  reloco::mutex m;
  {
    reloco::unique_lock<reloco::mutex> lk(m, reloco::try_to_lock, reloco::debug_call_location_ref::current());
    EXPECT_TRUE(lk.owns_lock());
  }
  {
    reloco::unique_lock<reloco::mutex> lk(m, reloco::try_to_lock, reloco::release_call_location_ref{});
    EXPECT_TRUE(lk.owns_lock());
  }
}
