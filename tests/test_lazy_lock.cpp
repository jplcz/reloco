// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <atomic>
#include <gtest/gtest.h>
#include <reloco/lazy_lock.hpp>
#include <reloco/send_sync.hpp>

#include <string>
#include <thread>
#include <vector>

TEST(LazyLockTest, DeducesTypeFromClosureReturnType) {
  reloco::lazy_lock config([]() -> std::string { return "hello"; });
  EXPECT_EQ(*config, "hello");
}

TEST(LazyLockTest, RunsTheClosureExactlyOnce) {
  int calls = 0;
  reloco::lazy_lock value([&] {
    ++calls;
    return 42;
  });

  EXPECT_EQ(*value, 42);
  EXPECT_EQ(*value, 42);
  EXPECT_EQ(calls, 1);
}

TEST(LazyLockTest, OperatorArrowAccessesMembers) {
  reloco::lazy_lock value([] { return std::string("hello world"); });
  EXPECT_EQ(value->size(), 11u);
}

TEST(LazyLockTest, GetReturnsAPointer) {
  reloco::lazy_lock value([] { return 7; });
  const int *ptr = value.get();
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 7);
}

TEST(LazyLockTest, ConcurrentDereferenceFromMultipleThreadsRunsTheClosureExactlyOnce) {
  std::atomic<int> calls{0};
  reloco::lazy_lock value([&]() -> int {
    calls.fetch_add(1, std::memory_order_relaxed);
    return 123;
  });

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&] { EXPECT_EQ(*value, 123); });
  }
  for (auto &t : threads)
    t.join();

  EXPECT_EQ(calls.load(), 1);
}

TEST(LazyLockTest, IsSendAndSyncWhenTAndFAreSendAndSync) {
  auto init = [] { return 1; };
  using lazy_type = reloco::lazy_lock<int, decltype(init)>;
  EXPECT_TRUE(reloco::is_send_v<lazy_type>);
  EXPECT_TRUE(reloco::is_sync_v<lazy_type>);
}
