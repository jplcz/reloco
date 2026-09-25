// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/duration.hpp>
#include <reloco/futex.hpp>

#include <chrono>
#include <thread>

TEST(FutexTest, WaitTimeoutReturnsTrueImmediatelyWhenWordAlreadyChanged) {
  reloco::futex_word word{1};
  EXPECT_TRUE(reloco::futex_wait_timeout(word, 0, reloco::duration::from_millis(50)));
}

TEST(FutexTest, WaitTimeoutReturnsFalseOnceTimeoutElapses) {
  reloco::futex_word word{0};
  EXPECT_FALSE(reloco::futex_wait_timeout(word, 0, reloco::duration::from_millis(20)));
}

TEST(FutexTest, WaitTimeoutReturnsTrueWhenWokenBeforeTimeoutElapses) {
  reloco::futex_word word{0};
  std::thread woken([&word] {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    word.store(1, std::memory_order_release);
    reloco::futex_wake_all(word);
  });
  EXPECT_TRUE(reloco::futex_wait_timeout(word, 0, reloco::duration::from_secs(5)));
  woken.join();
}
