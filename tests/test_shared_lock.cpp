// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/call_location.hpp>
#include <reloco/mutex.hpp>
#include <reloco/shared_lock.hpp>

#include <atomic>
#include <thread>
#include <vector>

TEST(SharedLockTest, DefaultConstructedOwnsNothing) {
  reloco::shared_lock<reloco::shared_mutex> lk;
  EXPECT_FALSE(lk.owns_lock());
  EXPECT_FALSE(static_cast<bool>(lk));
  EXPECT_EQ(lk.mutex(), nullptr);
}

TEST(SharedLockTest, ConstructingWithMutexLocksImmediately) {
  reloco::shared_mutex m;
  {
    reloco::shared_lock<reloco::shared_mutex> lk(m);
    EXPECT_TRUE(lk.owns_lock());
    EXPECT_TRUE(static_cast<bool>(lk));
    EXPECT_EQ(lk.mutex(), &m);
    EXPECT_FALSE(m.try_lock());
  }
  EXPECT_TRUE(m.try_lock());
  m.unlock();
}

TEST(SharedLockTest, MultipleSharedLocksAreConcurrentlyHeld) {
  reloco::shared_mutex m;
  reloco::shared_lock<reloco::shared_mutex> lk1(m);
  reloco::shared_lock<reloco::shared_mutex> lk2(m, reloco::try_to_lock);
  EXPECT_TRUE(lk2.owns_lock());
}

TEST(SharedLockTest, DeferLockDoesNotAcquire) {
  reloco::shared_mutex m;
  reloco::shared_lock<reloco::shared_mutex> lk(m, reloco::defer_lock);
  EXPECT_FALSE(lk.owns_lock());
  EXPECT_TRUE(m.try_lock());
  m.unlock();
}

TEST(SharedLockTest, DeferLockThenExplicitLockAcquires) {
  reloco::shared_mutex m;
  reloco::shared_lock<reloco::shared_mutex> lk(m, reloco::defer_lock);
  lk.lock();
  EXPECT_TRUE(lk.owns_lock());
  EXPECT_FALSE(m.try_lock());
}

TEST(SharedLockTest, TryToLockFailsWhileExclusivelyHeld) {
  reloco::shared_mutex m;
  m.lock();
  reloco::shared_lock<reloco::shared_mutex> lk(m, reloco::try_to_lock);
  EXPECT_FALSE(lk.owns_lock());
  m.unlock();
}

TEST(SharedLockTest, AdoptLockRecordsOwnershipOfAlreadyHeldSharedLock) {
  reloco::shared_mutex m;
  m.lock_shared();
  reloco::shared_lock<reloco::shared_mutex> lk(m, reloco::adopt_lock);
  EXPECT_TRUE(lk.owns_lock());
}

TEST(SharedLockTest, UnlockThenLockRoundTrips) {
  reloco::shared_mutex m;
  reloco::shared_lock<reloco::shared_mutex> lk(m);
  lk.unlock();
  EXPECT_FALSE(lk.owns_lock());
  EXPECT_TRUE(m.try_lock());
  m.unlock();
  lk.lock();
  EXPECT_TRUE(lk.owns_lock());
}

TEST(SharedLockTest, MoveConstructionTransfersOwnership) {
  reloco::shared_mutex m;
  reloco::shared_lock<reloco::shared_mutex> lk1(m);
  reloco::shared_lock<reloco::shared_mutex> lk2(std::move(lk1));
  EXPECT_FALSE(lk1.owns_lock());
  EXPECT_EQ(lk1.mutex(), nullptr);
  EXPECT_TRUE(lk2.owns_lock());
  EXPECT_EQ(lk2.mutex(), &m);
}

TEST(SharedLockTest, MoveAssignmentReleasesPreviouslyHeldMutex) {
  reloco::shared_mutex m1;
  reloco::shared_mutex m2;
  reloco::shared_lock<reloco::shared_mutex> lk1(m1);
  reloco::shared_lock<reloco::shared_mutex> lk2(m2);
  lk1 = std::move(lk2);
  EXPECT_TRUE(m1.try_lock());
  m1.unlock();
  EXPECT_TRUE(lk1.owns_lock());
  EXPECT_EQ(lk1.mutex(), &m2);
}

TEST(SharedLockTest, ReleaseDetachesWithoutUnlocking) {
  reloco::shared_mutex m;
  reloco::shared_lock<reloco::shared_mutex> lk(m);
  reloco::shared_mutex *released = lk.release();
  EXPECT_EQ(released, &m);
  EXPECT_FALSE(lk.owns_lock());
  EXPECT_EQ(lk.mutex(), nullptr);
  EXPECT_FALSE(m.try_lock());
  m.unlock_shared();
}

TEST(SharedLockTest, SwapExchangesOwnedMutexes) {
  reloco::shared_mutex m1;
  reloco::shared_mutex m2;
  reloco::shared_lock<reloco::shared_mutex> lk1(m1);
  reloco::shared_lock<reloco::shared_mutex> lk2(m2);
  lk1.swap(lk2);
  EXPECT_EQ(lk1.mutex(), &m2);
  EXPECT_EQ(lk2.mutex(), &m1);
}

TEST(SharedLockTest, FreeSwapFunctionExchangesOwnedMutexes) {
  reloco::shared_mutex m1;
  reloco::shared_mutex m2;
  reloco::shared_lock<reloco::shared_mutex> lk1(m1);
  reloco::shared_lock<reloco::shared_mutex> lk2(m2);
  using reloco::swap;
  swap(lk1, lk2);
  EXPECT_EQ(lk1.mutex(), &m2);
  EXPECT_EQ(lk2.mutex(), &m1);
}

TEST(SharedLockTest, ReadersAcrossThreadsDoNotBlockEachOther) {
  reloco::shared_mutex m;
  std::vector<std::thread> threads;
  std::atomic<int> concurrent{0};
  std::atomic<int> max_concurrent{0};
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&]() {
      reloco::shared_lock<reloco::shared_mutex> lk(m);
      int now = concurrent.fetch_add(1, std::memory_order_relaxed) + 1;
      int prev = max_concurrent.load(std::memory_order_relaxed);
      while (now > prev && !max_concurrent.compare_exchange_weak(prev, now, std::memory_order_relaxed)) {
      }
      concurrent.fetch_sub(1, std::memory_order_relaxed);
    });
  }
  for (auto &t : threads) {
    t.join();
  }
  EXPECT_GE(max_concurrent.load(std::memory_order_relaxed), 1);
}

TEST(SharedLockTest, ExplicitDebugAndReleaseLocationOverloadsBothWork) {
  reloco::shared_mutex m;

  // Explicit debug_call_location_ref overload, on both the acquiring
  // constructor and lock()/try_lock()/unlock().
  {
    reloco::shared_lock<reloco::shared_mutex> lk(m, reloco::debug_call_location_ref::current());
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
    reloco::shared_lock<reloco::shared_mutex> lk(m, reloco::release_call_location_ref{});
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

TEST(SharedLockTest, ExplicitLocationOverloadsOnTaggedConstructors) {
  reloco::shared_mutex m;
  {
    reloco::shared_lock<reloco::shared_mutex> lk(m, reloco::try_to_lock, reloco::debug_call_location_ref::current());
    EXPECT_TRUE(lk.owns_lock());
  }
  {
    reloco::shared_lock<reloco::shared_mutex> lk(m, reloco::try_to_lock, reloco::release_call_location_ref{});
    EXPECT_TRUE(lk.owns_lock());
  }
}
