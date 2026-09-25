// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <atomic>
#include <gtest/gtest.h>
#include <reloco/scope.hpp>
#include <reloco/send_sync.hpp>

#include <vector>

TEST(ScopeTest, BodyRunsAndReturnsVoid) {
  bool ran = false;
  auto result = reloco::scope([&](reloco::thread_scope &) { ran = true; });
  ASSERT_TRUE(result);
  EXPECT_TRUE(ran);
}

TEST(ScopeTest, BodyReturnValueIsForwarded) {
  auto result = reloco::scope([](reloco::thread_scope &) -> int { return 7; });
  ASSERT_TRUE(result);
  EXPECT_EQ(*result, 7);
}

TEST(ScopeTest, SpawnedThreadCanBorrowAStackLocal) {
  int local = 0;
  auto result = reloco::scope([&](reloco::thread_scope &s) {
    auto handle = s.spawn([&local]() noexcept { local = 42; });
    ASSERT_TRUE(handle);
    std::move(*handle).join();
  });
  ASSERT_TRUE(result);
  EXPECT_EQ(local, 42);
}

TEST(ScopeTest, ScopeReturnsOnlyAfterEverySpawnedThreadFinishesEvenWithoutExplicitJoin) {
  std::atomic<int> counter{0};
  auto result = reloco::scope([&](reloco::thread_scope &s) {
    for (int i = 0; i < 8; ++i) {
      auto handle = s.spawn([&counter]() noexcept { counter.fetch_add(1, std::memory_order_relaxed); });
      ASSERT_TRUE(handle);
      // Deliberately never join(); scope() itself must still wait.
    }
  });
  ASSERT_TRUE(result);
  EXPECT_EQ(counter.load(), 8);
}

TEST(ScopeTest, SpawnedThreadResultIsRetrievableThroughJoin) {
  auto result = reloco::scope([](reloco::thread_scope &s) -> int {
    auto handle = s.spawn([]() noexcept -> int { return 123; });
    if (!handle)
      return -1;
    return std::move(*handle).join();
  });
  ASSERT_TRUE(result);
  EXPECT_EQ(*result, 123);
}

TEST(ScopeTest, MultipleSpawnedThreadsCanBorrowDifferentStackLocals) {
  std::vector<int> values(4, 0);
  auto result = reloco::scope([&](reloco::thread_scope &s) {
    std::vector<reloco::scoped_join_handle<void>> handles;
    for (std::size_t i = 0; i < values.size(); ++i) {
      auto handle = s.spawn([&values, i]() noexcept { values[i] = static_cast<int>(i) * 10; });
      ASSERT_TRUE(handle);
      handles.push_back(std::move(*handle));
    }
    for (auto &h : handles)
      std::move(h).join();
  });
  ASSERT_TRUE(result);
  EXPECT_EQ(values[0], 0);
  EXPECT_EQ(values[1], 10);
  EXPECT_EQ(values[2], 20);
  EXPECT_EQ(values[3], 30);
}

TEST(ScopeTest, ScopedJoinHandleIsSendAndSync) {
  EXPECT_TRUE(reloco::is_send_v<reloco::scoped_join_handle<int>>);
  EXPECT_TRUE(reloco::is_sync_v<reloco::scoped_join_handle<int>>);
  EXPECT_TRUE(reloco::is_send_v<reloco::scoped_join_handle<void>>);
  EXPECT_TRUE(reloco::is_sync_v<reloco::scoped_join_handle<void>>);
}
