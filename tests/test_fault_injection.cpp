// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Exercises the *enabled* fault-injection path: RELOCO_ENABLE_FAULT_INJECTION
// must be defined before the first inclusion of fault_injection.hpp (and
// transitively tls_provider.hpp), matching every other reloco
// compile-time-selected-backend header (mutex.hpp/thread.hpp/tls_provider.hpp
// itself, ...).
#define RELOCO_ENABLE_FAULT_INJECTION

#include <gtest/gtest.h>
#include <reloco/fault_injection.hpp>

#include <type_traits>

namespace {

struct simple_point {};
struct mutate_point {};
struct nested_point {};
struct multi_arg_point {};
struct other_tag_point {};
struct direct_invoke_point {};
struct outer_tag_point {};
struct inner_tag_point {};

} // namespace

TEST(FaultInjectionTest, NoOpWhenNothingArmed) {
  EXPECT_FALSE(reloco::fault_armed<simple_point>());
  RELOCO_FAULT_POINT(simple_point); // must not crash/assert with nothing armed
}

TEST(FaultInjectionTest, HookFiresOnlyWhileArmed) {
  int fire_count = 0;
  auto hook = [&] { ++fire_count; };

  RELOCO_FAULT_POINT(simple_point);
  EXPECT_EQ(0, fire_count);

  {
    reloco::fault_injector<simple_point> fi(hook);
    EXPECT_TRUE(reloco::fault_armed<simple_point>());
    RELOCO_FAULT_POINT(simple_point);
    RELOCO_FAULT_POINT(simple_point);
    EXPECT_EQ(2, fire_count);
  }

  EXPECT_FALSE(reloco::fault_armed<simple_point>());
  RELOCO_FAULT_POINT(simple_point);
  EXPECT_EQ(2, fire_count); // no further increments once disarmed
}

TEST(FaultInjectionTest, MutatesLocalStateByReference) {
  // Simulates a concurrent CPU/ISR mutating shared state at the exact
  // instant the code under test is about to act on it.
  auto hook = [](std::uint64_t &w) { w = 0xDEADBEEF; };
  reloco::fault_injector<mutate_point, std::uint64_t> fi(hook);

  std::uint64_t w = 42;
  RELOCO_FAULT_POINT_ARGS(mutate_point, w);
  EXPECT_EQ(0xDEADBEEFu, w);
}

TEST(FaultInjectionTest, MultipleArguments) {
  auto hook = [](int &a, bool &blocked) {
    a *= 10;
    blocked = true;
  };
  reloco::fault_injector<multi_arg_point, int, bool> fi(hook);

  int a = 3;
  bool blocked = false;
  RELOCO_FAULT_POINT_ARGS(multi_arg_point, a, blocked);
  EXPECT_EQ(30, a);
  EXPECT_TRUE(blocked);
}

TEST(FaultInjectionTest, NestingRestoresPreviousInjectorOnDestruction) {
  int outer_calls = 0;
  int inner_calls = 0;
  auto outer_hook = [&] { ++outer_calls; };
  auto inner_hook = [&] { ++inner_calls; };

  reloco::fault_injector<nested_point> outer(outer_hook);
  RELOCO_FAULT_POINT(nested_point);
  EXPECT_EQ(1, outer_calls);

  {
    reloco::fault_injector<nested_point> inner(inner_hook);
    RELOCO_FAULT_POINT(nested_point);
    EXPECT_EQ(1, inner_calls);
    EXPECT_EQ(1, outer_calls); // inner shadows outer while alive
  }

  // outer is restored once inner is destroyed
  RELOCO_FAULT_POINT(nested_point);
  EXPECT_EQ(2, outer_calls);
  EXPECT_EQ(1, inner_calls);
}

TEST(FaultInjectionTest, DistinctTagsAreIndependent) {
  int a_calls = 0;
  int b_calls = 0;
  auto hook_a = [&] { ++a_calls; };

  reloco::fault_injector<simple_point> fi(hook_a);
  RELOCO_FAULT_POINT(simple_point);
  RELOCO_FAULT_POINT(other_tag_point); // distinct, unrelated tag: no effect

  EXPECT_EQ(1, a_calls);
  EXPECT_EQ(0, b_calls);
  EXPECT_FALSE(reloco::fault_armed<other_tag_point>());
}

// The framework backs every Tag with a single, shared, program-wide TLS
// slot (see fault_injection.hpp's file-level docs), so fault_injectors for
// *different* tags nested on top of one another must still each be found
// correctly, and destroyed in strict LIFO order across tags, not just
// within the same tag.
TEST(FaultInjectionTest, NestedDifferentTagsShareOneStackCorrectly) {
  int outer_calls = 0;
  int inner_calls = 0;
  auto hook_outer = [&] { ++outer_calls; };
  auto hook_inner = [&] { ++inner_calls; };

  reloco::fault_injector<outer_tag_point> outer(hook_outer);
  RELOCO_FAULT_POINT(outer_tag_point);
  EXPECT_EQ(1, outer_calls);

  {
    reloco::fault_injector<inner_tag_point> inner(hook_inner);
    RELOCO_FAULT_POINT(inner_tag_point);
    RELOCO_FAULT_POINT(outer_tag_point); // outer_tag_point is still findable while nested
    EXPECT_EQ(1, inner_calls);
    EXPECT_EQ(2, outer_calls);
  }

  RELOCO_FAULT_POINT(outer_tag_point);
  EXPECT_EQ(3, outer_calls);
  EXPECT_EQ(1, inner_calls); // inner_tag_point's hook did not fire again after its guard died
}

TEST(FaultInjectionTest, InvokeDirectlyWithoutTheMacro) {
  int calls = 0;
  auto hook = [&](int &x) { x += calls++; };
  reloco::fault_injector<direct_invoke_point, int> fi(hook); // named local, outlives `fi`

  int x = 10;
  fi.invoke(x);
  EXPECT_EQ(10, x);
  fi.invoke(x);
  EXPECT_EQ(11, x);
}

RELOCO_FAULT_TAG(macro_point);

TEST(FaultInjectionTest, ConvenienceMacroDeducesArgsAndArms) {
  int calls = 0;
  RELOCO_FAULT_INJECTOR(fi, macro_point, [&](int &a, bool &blocked) {
    ++calls;
    a *= 10;
    blocked = true;
  });
  static_assert(std::is_same_v<decltype(fi), reloco::fault_injector<macro_point, int, bool>>);

  int a = 3;
  bool blocked = false;
  RELOCO_FAULT_POINT_ARGS(macro_point, a, blocked);
  EXPECT_EQ(1, calls);
  EXPECT_EQ(30, a);
  EXPECT_TRUE(blocked);
}
