// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <atomic>
#include <gtest/gtest.h>
#include <reloco/error.hpp>
#include <reloco/once.hpp>
#include <reloco/send_sync.hpp>
#include <reloco/thread.hpp>

TEST(OnceTest, StartsNotCompleted) {
  reloco::once flag;
  EXPECT_FALSE(flag.is_completed());
}

TEST(OnceTest, CallOnceRunsTheClosure) {
  reloco::once flag;
  int calls = 0;

  flag.call_once([&] { ++calls; });

  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(flag.is_completed());
}

TEST(OnceTest, CallOnceRunsTheClosureExactlyOnce) {
  reloco::once flag;
  int calls = 0;

  flag.call_once([&] { ++calls; });
  flag.call_once([&] { ++calls; });
  flag.call_once([&] { ++calls; });

  EXPECT_EQ(calls, 1);
}

TEST(OnceTest, TryCallOncePropagatesFailureAndAllowsRetry) {
  reloco::once flag;

  auto failed = flag.try_call_once([]() -> reloco::result<void> { return reloco::unexpected(reloco::error::out_of_range); });
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), reloco::error::out_of_range);
  EXPECT_FALSE(flag.is_completed());

  int calls = 0;
  auto retried = flag.try_call_once([&]() -> reloco::result<void> {
    ++calls;
    return {};
  });
  ASSERT_TRUE(retried);
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(flag.is_completed());
}

TEST(OnceTest, TryCallOnceOnAnAlreadyCompletedFlagSucceedsWithoutRunningTheClosure) {
  reloco::once flag;
  flag.call_once([] {});

  bool called = false;
  auto result = flag.try_call_once([&]() -> reloco::result<void> {
    called = true;
    return {};
  });
  ASSERT_TRUE(result);
  EXPECT_FALSE(called);
}

TEST(OnceTest, ConcurrentCallOnceFromMultipleThreadsRunsTheClosureExactlyOnce) {
  reloco::once flag;
  std::atomic<int> calls{0};

  auto worker = [&]() noexcept -> void { flag.call_once([&] { calls.fetch_add(1, std::memory_order_relaxed); }); };

  auto h1 = reloco::spawn(worker);
  auto h2 = reloco::spawn(worker);
  auto h3 = reloco::spawn(worker);
  ASSERT_TRUE(h1);
  ASSERT_TRUE(h2);
  ASSERT_TRUE(h3);

  std::move(*h1).join();
  std::move(*h2).join();
  std::move(*h3).join();

  EXPECT_EQ(calls.load(), 1);
  EXPECT_TRUE(flag.is_completed());
}

TEST(OnceTest, UnsafeResetAllowsTheClosureToRunAgain) {
  reloco::once flag;
  int calls = 0;

  flag.call_once([&] { ++calls; });
  EXPECT_TRUE(flag.is_completed());

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  flag.unsafe_reset();
  RELOCO_END_UNSAFE_BUFFER_USAGE
  EXPECT_FALSE(flag.is_completed());

  flag.call_once([&] { ++calls; });
  EXPECT_EQ(calls, 2);
  EXPECT_TRUE(flag.is_completed());
}

TEST(OnceTest, UnsafeResetOnANotYetCompletedFlagIsANoop) {
  reloco::once flag;
  EXPECT_FALSE(flag.is_completed());

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  flag.unsafe_reset();
  RELOCO_END_UNSAFE_BUFFER_USAGE
  EXPECT_FALSE(flag.is_completed());

  int calls = 0;
  flag.call_once([&] { ++calls; });
  EXPECT_EQ(calls, 1);
}

TEST(OnceTest, OnceIsSendAndSync) {
  EXPECT_TRUE(reloco::is_send_v<reloco::once>);
  EXPECT_TRUE(reloco::is_sync_v<reloco::once>);
}
