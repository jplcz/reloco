// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <atomic>
#include <gtest/gtest.h>
#include <reloco/error.hpp>
#include <reloco/once_lock.hpp>
#include <reloco/send_sync.hpp>
#include <reloco/thread.hpp>

#include <string>

TEST(OnceLockTest, StartsEmpty) {
  reloco::once_lock<int> cell;
  EXPECT_EQ(cell.get(), nullptr);
  EXPECT_EQ(cell.get_mut(), nullptr);
}

TEST(OnceLockTest, TrySetInitializesAnEmptyCell) {
  reloco::once_lock<int> cell;
  ASSERT_TRUE(cell.try_set(42));
  ASSERT_NE(cell.get(), nullptr);
  EXPECT_EQ(*cell.get(), 42);
}

TEST(OnceLockTest, TrySetFailsOnAnAlreadyInitializedCell) {
  reloco::once_lock<int> cell;
  ASSERT_TRUE(cell.try_set(1));

  auto second = cell.try_set(2);
  ASSERT_FALSE(second);
  EXPECT_EQ(second.error(), reloco::error::already_exists);
  EXPECT_EQ(*cell.get(), 1); // unchanged
}

TEST(OnceLockTest, GetMutAllowsInPlaceMutation) {
  reloco::once_lock<std::string> cell;
  ASSERT_TRUE(cell.try_set("hello"));

  auto *mut = cell.get_mut();
  ASSERT_NE(mut, nullptr);
  *mut += " world";

  EXPECT_EQ(*cell.get(), "hello world");
}

TEST(OnceLockTest, GetOrTryInitRunsInitializerExactlyOnceOnAnEmptyCell) {
  reloco::once_lock<int> cell;
  int init_calls = 0;

  auto first = cell.get_or_try_init([&]() -> reloco::result<int> {
    ++init_calls;
    return 7;
  });
  ASSERT_TRUE(first);
  EXPECT_EQ(**first, 7);
  EXPECT_EQ(init_calls, 1);

  auto second = cell.get_or_try_init([&]() -> reloco::result<int> {
    ++init_calls;
    return 99;
  });
  ASSERT_TRUE(second);
  EXPECT_EQ(**second, 7);   // still the first value
  EXPECT_EQ(init_calls, 1); // initializer not called again
}

TEST(OnceLockTest, GetOrTryInitPropagatesInitializerFailureAndAllowsRetry) {
  reloco::once_lock<int> cell;

  auto failed =
      cell.get_or_try_init([]() -> reloco::result<int> { return reloco::unexpected(reloco::error::out_of_range); });
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), reloco::error::out_of_range);
  EXPECT_EQ(cell.get(), nullptr); // still empty, may retry

  auto retried = cell.get_or_try_init([]() -> reloco::result<int> { return 5; });
  ASSERT_TRUE(retried);
  EXPECT_EQ(**retried, 5);
}

TEST(OnceLockTest, TakeResetsAnInitializedCellAndReturnsThePreviousValue) {
  reloco::once_lock<int> cell;
  ASSERT_TRUE(cell.try_set(3));

  auto taken = cell.take();
  ASSERT_TRUE(taken);
  EXPECT_EQ(*taken, 3);
  EXPECT_EQ(cell.get(), nullptr);

  ASSERT_TRUE(cell.try_set(4));
  EXPECT_EQ(*cell.get(), 4);
}

TEST(OnceLockTest, TakeOnAnEmptyCellFailsWithNotInitialized) {
  reloco::once_lock<int> cell;
  auto taken = cell.take();
  ASSERT_FALSE(taken);
  EXPECT_EQ(taken.error(), reloco::error::not_initialized);
}

TEST(OnceLockTest, ConcurrentGetOrTryInitFromMultipleThreadsRunsInitializerExactlyOnce) {
  reloco::once_lock<int> cell;
  std::atomic<int> init_calls{0};

  auto worker = [&]() noexcept -> int {
    auto result = cell.get_or_try_init([&]() -> reloco::result<int> {
      init_calls.fetch_add(1, std::memory_order_relaxed);
      return 123;
    });
    return result ? **result : -1;
  };

  auto h1 = reloco::spawn(worker);
  auto h2 = reloco::spawn(worker);
  auto h3 = reloco::spawn(worker);
  ASSERT_TRUE(h1);
  ASSERT_TRUE(h2);
  ASSERT_TRUE(h3);

  EXPECT_EQ(std::move(*h1).join(), 123);
  EXPECT_EQ(std::move(*h2).join(), 123);
  EXPECT_EQ(std::move(*h3).join(), 123);
  EXPECT_EQ(init_calls.load(), 1);
}

TEST(OnceLockTest, OnceLockIsSendAndSyncWhenTIsSendAndSync) {
  EXPECT_TRUE(reloco::is_send_v<reloco::once_lock<int>>);
  EXPECT_TRUE(reloco::is_sync_v<reloco::once_lock<int>>);
}
