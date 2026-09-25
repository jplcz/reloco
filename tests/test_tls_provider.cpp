// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/detail/tls_provider.hpp>
#include <reloco/thread.hpp>

#include <atomic>
#include <string>

namespace {
struct int_tag {};
struct string_tag {};
struct other_int_tag {};
} // namespace

TEST(TlsProviderTest, DefaultsToZeroValueBeforeAnySet) {
  using provider = reloco::detail::tls_provider<int, struct default_zero_tag>;
  EXPECT_EQ(provider::get(), 0);
}

TEST(TlsProviderTest, SetThenGetRoundTripsOnTheCallingThread) {
  using provider = reloco::detail::tls_provider<int, int_tag>;
  provider::set(42);
  EXPECT_EQ(provider::get(), 42);
  provider::set(7);
  EXPECT_EQ(provider::get(), 7);
}

TEST(TlsProviderTest, DistinctTagsAreIndependentSlotsForTheSameType) {
  using a = reloco::detail::tls_provider<int, struct tag_a>;
  using b = reloco::detail::tls_provider<int, struct tag_b>;
  a::set(1);
  b::set(2);
  EXPECT_EQ(a::get(), 1);
  EXPECT_EQ(b::get(), 2);
}

TEST(TlsProviderTest, WorksWithNonTrivialTypes) {
  using provider = reloco::detail::tls_provider<std::string, string_tag>;
  EXPECT_TRUE(provider::get().empty());
  provider::set("hello");
  EXPECT_EQ(provider::get(), "hello");
}

TEST(TlsProviderTest, EachThreadObservesItsOwnValue) {
  using provider = reloco::detail::tls_provider<int, other_int_tag>;
  provider::set(100);

  std::atomic<int> other_thread_initial{-1};
  std::atomic<int> other_thread_after_set{-1};

  auto handle = reloco::spawn([&]() noexcept {
    other_thread_initial.store(provider::get(), std::memory_order_relaxed);
    provider::set(999);
    other_thread_after_set.store(provider::get(), std::memory_order_relaxed);
  });
  ASSERT_TRUE(handle.has_value());
  std::move(*handle).join();

  EXPECT_EQ(other_thread_initial.load(), 0); // fresh thread, not the main thread's 100
  EXPECT_EQ(other_thread_after_set.load(), 999);
  EXPECT_EQ(provider::get(), 100); // main thread's own slot is unaffected
}
