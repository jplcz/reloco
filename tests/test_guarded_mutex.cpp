// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/guarded_mutex.hpp>

#include <string>
#include <thread>
#include <vector>

TEST(GuardedMutexTest, DefaultConstructsValue) {
  reloco::guarded_mutex<int> m;
  EXPECT_EQ(*m.lock(), 0);
}

TEST(GuardedMutexTest, ConstructsFromValue) {
  reloco::guarded_mutex<int> m(42);
  EXPECT_EQ(*m.lock(), 42);
}

TEST(GuardedMutexTest, LockGrantsMutableAccess) {
  reloco::guarded_mutex<int> m(1);
  {
    auto g = m.lock();
    *g = 2;
  }
  EXPECT_EQ(*m.lock(), 2);
}

TEST(GuardedMutexTest, ArrowOperatorReachesMembers) {
  reloco::guarded_mutex<std::string> m(std::string("hello"));
  {
    auto g = m.lock();
    g->append(" world");
  }
  EXPECT_EQ(*m.lock(), "hello world");
}

TEST(GuardedMutexTest, TryLockFailsWhileHeld) {
  reloco::guarded_mutex<int> m(0);
  auto g1 = m.lock();
  auto g2 = m.try_lock();
  ASSERT_FALSE(g2.has_value());
  EXPECT_EQ(g2.error(), reloco::error::busy);
}

TEST(GuardedMutexTest, TryLockSucceedsWhenFree) {
  reloco::guarded_mutex<int> m(7);
  auto g = m.try_lock();
  ASSERT_TRUE(g.has_value());
  EXPECT_EQ(*g.value(), 7);
}

TEST(GuardedMutexTest, GuardReleasesLockOnDestruction) {
  reloco::guarded_mutex<int> m(0);
  { auto g = m.lock(); }
  auto g2 = m.try_lock();
  EXPECT_TRUE(g2.has_value());
}

TEST(GuardedMutexTest, GetMutBypassesLocking) {
  reloco::guarded_mutex<int> m(1);
  m.get_mut() = 9;
  EXPECT_EQ(*m.lock(), 9);
}

TEST(GuardedMutexTest, MoveConstructingGuardTransfersOwnership) {
  reloco::guarded_mutex<int> m(3);
  auto g1 = m.lock();
  reloco::guarded_mutex<int>::guard g2(std::move(g1));
  *g2 = 4;
  // The moved-from guard no longer holds the lock; a second lock attempt
  // from the (now sole) owning guard would deadlock, so we simply verify
  // the value was updated through the moved-to guard before it releases.
  EXPECT_EQ(*g2, 4);
}

TEST(GuardedMutexTest, MutualExclusionAcrossThreads) {
  reloco::guarded_mutex<int> counter(0);
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < 1000; ++j) {
        auto g = counter.lock();
        ++*g;
      }
    });
  }
  for (auto &t : threads) {
    t.join();
  }
  EXPECT_EQ(*counter.lock(), 8000);
}

TEST(GuardedMutexTest, WorksWithRecursiveMutexBackend) {
  reloco::guarded_mutex<int, reloco::recursive_mutex> m(5);
  auto g = m.lock();
  *g = 6;
  EXPECT_EQ(*g, 6);
}
