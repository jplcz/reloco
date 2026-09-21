// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/fallible_singleton.hpp>
#include <reloco/heap_allocator.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct plain_widget {
  int value = 42;
  static inline int construct_count = 0;
  plain_widget() noexcept { ++construct_count; }
};

struct creatable_widget {
  int value;
  static inline bool should_fail = false;
  static reloco::result<creatable_widget> try_create() noexcept {
    if (should_fail)
      return reloco::unexpected(reloco::error::invalid_argument);
    return creatable_widget{7};
  }
};

struct allocating_widget {
  int value;
  static reloco::result<allocating_widget> try_allocate(reloco::allocator_ref) noexcept { return allocating_widget{9}; }
};

struct constructing_widget {
  int value = 0;
  static inline int construct_calls = 0;
  reloco::result<void> try_construct() noexcept {
    ++construct_calls;
    value = 11;
    return {};
  }
};

struct alignas(64) wide_widget {
  int value = 5;
};

struct mutex_lock_traits {
  using lock_type = std::mutex;
  static void lock(lock_type &m) noexcept { m.lock(); }
  static void unlock(lock_type &m) noexcept { m.unlock(); }
};

struct not_a_lock_traits {
  int value;
};

} // namespace

TEST(FallibleSingletonTest, ConstructsOnceOnFirstCall) {
  plain_widget::construct_count = 0;
  auto res1 = reloco::fallible_singleton<plain_widget>::instance();
  ASSERT_TRUE(res1);
  auto res2 = reloco::fallible_singleton<plain_widget>::instance();
  ASSERT_TRUE(res2);
  EXPECT_EQ(*res1, *res2);
  EXPECT_EQ(plain_widget::construct_count, 1);
}

TEST(FallibleSingletonTest, TryCreateTierIsUsed) {
  creatable_widget::should_fail = false;
  auto res = reloco::fallible_singleton<creatable_widget>::instance();
  ASSERT_TRUE(res);
  EXPECT_EQ((*res)->value, 7);
}

TEST(FallibleSingletonTest, TryAllocateTierReceivesGivenAllocator) {
  auto res = reloco::fallible_singleton<allocating_widget>::instance(reloco::default_allocator());
  ASSERT_TRUE(res);
  EXPECT_EQ((*res)->value, 9);
}

TEST(FallibleSingletonTest, TryConstructTierIsUsedExactlyOnce) {
  constructing_widget::construct_calls = 0;
  auto res1 = reloco::fallible_singleton<constructing_widget>::instance();
  ASSERT_TRUE(res1);
  auto res2 = reloco::fallible_singleton<constructing_widget>::instance();
  ASSERT_TRUE(res2);
  EXPECT_EQ((*res1)->value, 11);
  EXPECT_EQ(constructing_widget::construct_calls, 1);
}

TEST(FallibleSingletonTest, HonorsOverAlignedType) {
  auto res = reloco::fallible_singleton<wide_widget>::instance();
  ASSERT_TRUE(res);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(*res) % alignof(wide_widget), 0u);
}

TEST(AtomicFallibleSingletonTest, HasLockTraitsDetectsShape) {
  static_assert(reloco::has_lock_traits_v<mutex_lock_traits>);
  static_assert(!reloco::has_lock_traits_v<not_a_lock_traits>);
}

TEST(AtomicFallibleSingletonTest, ConstructsOnceUnderLock) {
  std::mutex m;
  auto res1 = reloco::atomic_fallible_singleton<plain_widget, mutex_lock_traits>::instance(m);
  ASSERT_TRUE(res1);
  auto res2 = reloco::atomic_fallible_singleton<plain_widget, mutex_lock_traits>::instance(m);
  ASSERT_TRUE(res2);
  EXPECT_EQ(*res1, *res2);
}

TEST(AtomicFallibleSingletonTest, ConcurrentFirstCallsConstructExactlyOnce) {
  using singleton = reloco::atomic_fallible_singleton<constructing_widget, mutex_lock_traits>;
  constructing_widget::construct_calls = 0;

  std::mutex m;
  std::vector<std::thread> threads;
  std::atomic<constructing_widget *> seen{nullptr};
  std::atomic<bool> mismatch{false};

  for (int i = 0; i < 16; ++i) {
    threads.emplace_back([&]() {
      auto res = singleton::instance(m);
      if (!res)
        return;
      auto *expected = seen.exchange(*res);
      if (expected != nullptr && expected != *res)
        mismatch.store(true);
    });
  }
  for (auto &t : threads)
    t.join();

  EXPECT_FALSE(mismatch.load());
  EXPECT_EQ(constructing_widget::construct_calls, 1);
}

#if RELOCO_CXX20
TEST(AtomicFallibleSingletonTest, LockTraitsConceptMatchesTrait) {
  static_assert(reloco::lock_traits<mutex_lock_traits>);
  static_assert(!reloco::lock_traits<not_a_lock_traits>);
}
#endif
