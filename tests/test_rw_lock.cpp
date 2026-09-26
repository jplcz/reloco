// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/rw_lock.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

TEST(RwLockTest, DefaultConstructsValue) {
  reloco::rw_lock<int> m;
  EXPECT_EQ(*m.read(), 0);
}

TEST(RwLockTest, ConstructsFromValue) {
  reloco::rw_lock<int> m(42);
  EXPECT_EQ(*m.read(), 42);
}

TEST(RwLockTest, WriteGrantsMutableAccess) {
  reloco::rw_lock<int> m(1);
  {
    auto g = m.write();
    *g = 2;
  }
  EXPECT_EQ(*m.read(), 2);
}

TEST(RwLockTest, ArrowOperatorReachesMembers) {
  reloco::rw_lock<std::string> m(std::string("hello"));
  {
    auto g = m.write();
    g->append(" world");
  }
  auto g = m.read();
  EXPECT_EQ(g->size(), 11u);
  EXPECT_EQ(*g, "hello world");
}

TEST(RwLockTest, MultipleReadGuardsAllowedConcurrently) {
  reloco::rw_lock<int> m(0);
  auto g1 = m.read();
  auto g2 = m.try_read();
  ASSERT_TRUE(g2.has_value());
  EXPECT_EQ(**g2, 0);
}

TEST(RwLockTest, TryReadFailsWhileWriteHeld) {
  reloco::rw_lock<int> m(0);
  auto g1 = m.write();
  auto g2 = m.try_read();
  ASSERT_FALSE(g2.has_value());
  EXPECT_EQ(g2.error(), reloco::error::busy);
}

TEST(RwLockTest, TryWriteFailsWhileReadHeld) {
  reloco::rw_lock<int> m(0);
  auto g1 = m.read();
  auto g2 = m.try_write();
  ASSERT_FALSE(g2.has_value());
  EXPECT_EQ(g2.error(), reloco::error::busy);
}

TEST(RwLockTest, TryWriteFailsWhileWriteHeld) {
  reloco::rw_lock<int> m(0);
  auto g1 = m.write();
  auto g2 = m.try_write();
  ASSERT_FALSE(g2.has_value());
  EXPECT_EQ(g2.error(), reloco::error::busy);
}

TEST(RwLockTest, TryReadSucceedsWhenFree) {
  reloco::rw_lock<int> m(7);
  auto g = m.try_read();
  ASSERT_TRUE(g.has_value());
  EXPECT_EQ(**g, 7);
}

TEST(RwLockTest, TryWriteSucceedsWhenFree) {
  reloco::rw_lock<int> m(7);
  auto g = m.try_write();
  ASSERT_TRUE(g.has_value());
  EXPECT_EQ(**g, 7);
}

TEST(RwLockTest, ReadGuardReleasesLockOnDestruction) {
  reloco::rw_lock<int> m(0);
  {
    auto g = m.read();
  }
  auto g2 = m.try_write();
  EXPECT_TRUE(g2.has_value());
}

TEST(RwLockTest, WriteGuardReleasesLockOnDestruction) {
  reloco::rw_lock<int> m(0);
  {
    auto g = m.write();
  }
  auto g2 = m.try_read();
  EXPECT_TRUE(g2.has_value());
}

TEST(RwLockTest, GetMutBypassesLocking) {
  reloco::rw_lock<int> m(1);
  m.get_mut() = 9;
  EXPECT_EQ(*m.read(), 9);
}

TEST(RwLockTest, MoveConstructingReadGuardTransfersOwnership) {
  reloco::rw_lock<int> m(3);
  auto g1 = m.read();
  reloco::rw_lock<int>::read_guard g2(std::move(g1));
  EXPECT_EQ(*g2, 3);
}

TEST(RwLockTest, MoveConstructingWriteGuardTransfersOwnership) {
  reloco::rw_lock<int> m(3);
  auto g1 = m.write();
  reloco::rw_lock<int>::write_guard g2(std::move(g1));
  *g2 = 4;
  EXPECT_EQ(*g2, 4);
}

TEST(RwLockTest, MutualExclusionOfWritersAcrossThreads) {
  reloco::rw_lock<int> counter(0);
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < 1000; ++j) {
        auto g = counter.write();
        ++*g;
      }
    });
  }
  for (auto &t : threads) {
    t.join();
  }
  EXPECT_EQ(*counter.read(), 8000);
}

TEST(RwLockTest, ReadersObserveWriterResultsAcrossThreads) {
  reloco::rw_lock<int> value(0);
  std::thread writer([&]() {
    auto g = value.write();
    *g = 42;
  });
  writer.join();

  std::vector<std::thread> readers;
  std::atomic<int> observed{0};
  for (int i = 0; i < 8; ++i) {
    readers.emplace_back([&]() { observed.fetch_add(*value.read(), std::memory_order_relaxed); });
  }
  for (auto &t : readers) {
    t.join();
  }
  EXPECT_EQ(observed.load(std::memory_order_relaxed), 42 * 8);
}
