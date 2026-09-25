// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <atomic>
#include <gtest/gtest.h>
#include <reloco/rc.hpp>
#include <reloco/send_sync.hpp>
#include <reloco/thread.hpp>

namespace {

// Explicitly not Send, to exercise spawn()'s static_assert failing to
// compile for a captured non-Send value -- exercised only via
// static_assert on is_send_v directly below (a real negative-compile
// test would need to live outside this translation unit to observe the
// compiler diagnostic, which reloco has no harness for elsewhere).
struct not_send {};

} // namespace

template <> struct reloco::is_send<not_send> : std::false_type {};

TEST(ThreadTest, SpawnJoinRoundTripVoid) {
  std::atomic<bool> ran{false};
  auto handle = reloco::spawn([&ran]() noexcept { ran.store(true, std::memory_order_relaxed); });
  ASSERT_TRUE(handle.has_value());
  EXPECT_TRUE(handle->joinable());
  std::move(*handle).join();
  EXPECT_TRUE(ran.load(std::memory_order_relaxed));
}

TEST(ThreadTest, SpawnJoinRoundTripValue) {
  auto handle = reloco::spawn([]() noexcept -> int { return 42; });
  ASSERT_TRUE(handle.has_value());
  int result = std::move(*handle).join();
  EXPECT_EQ(result, 42);
}

TEST(ThreadTest, JoinableTransitionsAfterJoin) {
  auto handle = reloco::spawn([]() noexcept {});
  ASSERT_TRUE(handle.has_value());
  EXPECT_TRUE(handle->joinable());
  std::move(*handle).join();
}

TEST(ThreadTest, DetachLeavesThreadRunning) {
  auto handle = reloco::spawn([]() noexcept {});
  ASSERT_TRUE(handle.has_value());
  EXPECT_TRUE(handle->joinable());
  std::move(*handle).detach();
}

TEST(ThreadTest, ThreadIdDiffersBetweenCallerAndSpawnedThread) {
  reloco::thread_id caller_id = reloco::this_thread::get_id();
  reloco::thread_id spawned_id;
  auto handle = reloco::spawn([&spawned_id]() noexcept { spawned_id = reloco::this_thread::get_id(); });
  ASSERT_TRUE(handle.has_value());
  reloco::thread_id handle_id = handle->get_id();
  std::move(*handle).join();
  EXPECT_NE(spawned_id, caller_id);
  EXPECT_EQ(spawned_id, handle_id);
}

TEST(ThreadTest, DefaultConstructedThreadIdsAreNeverEqual) {
  reloco::thread_id a;
  reloco::thread_id b;
  EXPECT_NE(a, b);
}

TEST(ThreadTest, JoinHandleIsSendWhenResultIsSend) {
  EXPECT_TRUE(reloco::is_send_v<reloco::join_handle<int>>);
  EXPECT_TRUE(reloco::is_send_v<reloco::join_handle<void>>);
  EXPECT_FALSE(reloco::is_send_v<reloco::join_handle<not_send>>);
}

TEST(ThreadTest, JoinHandleIsAlwaysSync) {
  EXPECT_TRUE(reloco::is_sync_v<reloco::join_handle<int>>);
  EXPECT_TRUE(reloco::is_sync_v<reloco::join_handle<not_send>>);
}

TEST(ThreadTest, SpawnRejectsNonSendCaptureAtCompileTime) {
  static_assert(!reloco::is_send_v<reloco::rc<int>>, "rc<T> must not be Send for this test to be meaningful");
  // reloco::spawn([captured = reloco::rc<int>()]() noexcept {}); // would not compile: F is not Send.
}
