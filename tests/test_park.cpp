// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/duration.hpp>
#include <reloco/mutex.hpp>
#include <reloco/park.hpp>
#include <reloco/thread.hpp>

#include <atomic>
#include <chrono>

TEST(ParkTest, ParkReturnsImmediatelyWhenTokenAlreadyAvailable) {
  auto handle = reloco::this_thread::current();
  handle.unpark();
  reloco::this_thread::park(); // must not block: token already available
  SUCCEED();
}

TEST(ParkTest, ThreadHandleUnparkWakesTheOwningThread) {
  std::atomic<bool> got_handle{false};
  reloco::optional<reloco::thread_handle> worker_handle;
  reloco::mutex handle_mutex;
  std::atomic<bool> woke{false};

  auto worker = reloco::spawn([&]() noexcept {
    {
      std::lock_guard<reloco::mutex> lock(handle_mutex);
      worker_handle.emplace(reloco::this_thread::current());
    }
    got_handle.store(true, std::memory_order_release);
    reloco::this_thread::park();
    woke.store(true, std::memory_order_release);
  });
  ASSERT_TRUE(worker.has_value());

  while (!got_handle.load(std::memory_order_acquire))
    reloco::this_thread::yield();

  reloco::this_thread::sleep_for(reloco::duration::from_millis(20));

  {
    std::lock_guard<reloco::mutex> lock(handle_mutex);
    ASSERT_TRUE(worker_handle.has_value());
    worker_handle.as_known()->unpark();
  }

  std::move(*worker).join();
  EXPECT_TRUE(woke.load(std::memory_order_acquire));
}

TEST(ParkTest, ParkTimeoutReturnsFalseOnTimeoutAndTrueWhenUnparked) {
  auto handle = reloco::this_thread::current();

  auto start = std::chrono::steady_clock::now();
  bool got_token = reloco::this_thread::park_timeout(reloco::duration::from_millis(20));
  auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_FALSE(got_token);
  EXPECT_GE(elapsed, std::chrono::milliseconds(15)); // allow minor scheduling slack

  handle.unpark();
  EXPECT_TRUE(reloco::this_thread::park_timeout(reloco::duration::from_secs(5)));
}

TEST(ParkTest, UnparkDoesNotAccumulateTokens) {
  auto handle = reloco::this_thread::current();
  handle.unpark();
  handle.unpark();
  handle.unpark();

  reloco::this_thread::park(); // consumes the single outstanding token
  EXPECT_FALSE(reloco::this_thread::park_timeout(reloco::duration::from_millis(20)));
}

TEST(ParkTest, ThreadHandleIsCloneable) {
  auto handle = reloco::this_thread::current();
  reloco::thread_handle clone = handle;
  clone.unpark();
  reloco::this_thread::park(); // token delivered through the clone
  SUCCEED();
}

TEST(ParkTest, SleepForBlocksForAtLeastTheRequestedDuration) {
  auto start = std::chrono::steady_clock::now();
  reloco::this_thread::sleep_for(reloco::duration::from_millis(20));
  auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_GE(elapsed, std::chrono::milliseconds(15));
}
