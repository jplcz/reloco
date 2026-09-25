// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Model-parameterized tls_provider<T, Tag> coverage: compiled once per
// RELOCO_TLS_MODEL (RELOCO_TLS_MODEL_THREAD_LOCAL/_SINGLE/_PTHREAD, each
// forced via a -DRELOCO_TLS_MODEL=... command-line define on its own
// CMake target -- see CMakeLists.txt's jplcz_reloco_tls_*_tests targets),
// so every backend's actual behavior is exercised, not just whichever one
// happens to be the default/host-selected one. tests/test_tls_provider.cpp
// covers the same API against whatever RELOCO_TLS_MODEL the main test
// binary was built with (the default, RELOCO_TLS_MODEL_THREAD_LOCAL,
// unless overridden).

#include <gtest/gtest.h>
#include <reloco/thread.hpp>
#include <reloco/tls_provider.hpp>

#include <atomic>
#include <functional>
#include <string>
#include <type_traits>

namespace {
struct int_tag {};
struct string_tag {};

template <typename> constexpr bool is_reference_wrapper_v = false;
template <typename U> constexpr bool is_reference_wrapper_v<std::reference_wrapper<U>> = true;

// tls_provider<T, Tag>::get() returns result<std::reference_wrapper<T>> for
// every model/specialization with genuine addressable per-thread storage
// (every model apart from RELOCO_TLS_MODEL_PTHREAD's raw-pointer/small-
// trivial specializations, which return result<T> by value instead, since
// they have no such storage to reference -- e.g. `int` takes this path
// under RELOCO_TLS_MODEL_PTHREAD specifically). Unwraps either shape to a
// plain T for the tests below.
template <typename Provider> auto get_value() {
  auto result = Provider::get();
  EXPECT_TRUE(result.has_value());
  using value_type = std::decay_t<decltype(*result)>;
  if constexpr (is_reference_wrapper_v<value_type>) {
    return result->get();
  } else {
    return *result;
  }
}
template <typename Provider> auto get_value(reloco::allocator_ref alloc) {
  auto result = Provider::get(alloc);
  EXPECT_TRUE(result.has_value());
  using value_type = std::decay_t<decltype(*result)>;
  if constexpr (is_reference_wrapper_v<value_type>) {
    return result->get();
  } else {
    return *result;
  }
}
} // namespace

#if RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_THREAD_LOCAL
#define TLS_PROVIDER_MODEL_SUITE TlsProviderModelTest_ThreadLocal
#elif RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_SINGLE
#define TLS_PROVIDER_MODEL_SUITE TlsProviderModelTest_Single
#elif RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_PTHREAD
#define TLS_PROVIDER_MODEL_SUITE TlsProviderModelTest_Pthread
#else
#error "test_tls_provider_models.cpp: unhandled RELOCO_TLS_MODEL"
#endif

TEST(TLS_PROVIDER_MODEL_SUITE, DefaultsToZeroValueBeforeAnySet) {
  using provider = reloco::tls_provider<int, struct default_zero_tag>;
  EXPECT_EQ(get_value<provider>(), 0);
}

TEST(TLS_PROVIDER_MODEL_SUITE, SetThenGetRoundTripsOnTheCallingThread) {
  using provider = reloco::tls_provider<int, int_tag>;
  EXPECT_TRUE(provider::set(42).has_value());
  EXPECT_EQ(get_value<provider>(), 42);
  EXPECT_TRUE(provider::set(7).has_value());
  EXPECT_EQ(get_value<provider>(), 7);
}

TEST(TLS_PROVIDER_MODEL_SUITE, DistinctTagsAreIndependentSlotsForTheSameType) {
  using a = reloco::tls_provider<int, struct tag_a>;
  using b = reloco::tls_provider<int, struct tag_b>;
  ASSERT_TRUE(a::set(1).has_value());
  ASSERT_TRUE(b::set(2).has_value());
  EXPECT_EQ(get_value<a>(), 1);
  EXPECT_EQ(get_value<b>(), 2);
}

TEST(TLS_PROVIDER_MODEL_SUITE, WorksWithNonTrivialTypes) {
  using provider = reloco::tls_provider<std::string, string_tag>;
  EXPECT_TRUE(get_value<provider>().empty());
  ASSERT_TRUE(provider::set("hello").has_value());
  EXPECT_EQ(get_value<provider>(), "hello");
}

TEST(TLS_PROVIDER_MODEL_SUITE, AllocatorParameterDefaultsToDefaultAllocator) {
  using provider = reloco::tls_provider<int, struct explicit_allocator_tag>;
  ASSERT_TRUE(provider::set(5, reloco::default_allocator()).has_value());
  EXPECT_EQ(get_value<provider>(reloco::default_allocator()), 5);
}

#if RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_SINGLE

// RELOCO_TLS_MODEL_SINGLE is deliberately *not* per-thread: exactly one
// global static instance backs every "thread's" slot, for single-threaded
// builds that still want to link against tls_provider<T, Tag>-shaped code.
TEST(TLS_PROVIDER_MODEL_SUITE, SingleModelSharesOneGlobalInstanceAcrossEveryThread) {
  using provider = reloco::tls_provider<int, struct single_model_tag>;
  ASSERT_TRUE(provider::set(100).has_value());

  std::atomic<int> other_thread_initial{-1};
  auto handle = reloco::spawn([&]() noexcept {
    other_thread_initial.store(get_value<provider>(), std::memory_order_relaxed);
    ASSERT_TRUE(provider::set(999).has_value());
  });
  ASSERT_TRUE(handle.has_value());
  std::move(*handle).join();

  EXPECT_EQ(other_thread_initial.load(), 100); // shared with the main thread's earlier set()
  EXPECT_EQ(get_value<provider>(), 999);       // the worker thread's set() is visible here too
}

#else

// RELOCO_TLS_MODEL_THREAD_LOCAL and RELOCO_TLS_MODEL_PTHREAD are both
// genuinely per-thread.
TEST(TLS_PROVIDER_MODEL_SUITE, EachThreadObservesItsOwnValue) {
  using provider = reloco::tls_provider<int, struct per_thread_model_tag>;
  ASSERT_TRUE(provider::set(100).has_value());

  std::atomic<int> other_thread_initial{-1};
  std::atomic<int> other_thread_after_set{-1};

  auto handle = reloco::spawn([&]() noexcept {
    other_thread_initial.store(get_value<provider>(), std::memory_order_relaxed);
    ASSERT_TRUE(provider::set(999).has_value());
    other_thread_after_set.store(get_value<provider>(), std::memory_order_relaxed);
  });
  ASSERT_TRUE(handle.has_value());
  std::move(*handle).join();

  EXPECT_EQ(other_thread_initial.load(), 0); // fresh thread, not the main thread's 100
  EXPECT_EQ(other_thread_after_set.load(), 999);
  EXPECT_EQ(get_value<provider>(), 100); // main thread's own slot is unaffected
}

#endif
