// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/def_function_ref.hpp"
#include <gtest/gtest.h>

namespace reloco {
namespace {

int add(int base, int x) { return base + x; }

struct adder {
  int offset;
  int operator()(int base, int x) const { return base + x + offset; }
};

// Simulates a real API boundary that only knows about `function_ref<R(Args...)>`,
// with no awareness that the callee is actually a `def_function_ref` underneath.
int call_with_five(function_ref<int(int)> f) { return f(5); }

class DefFunctionRefTest : public ::testing::Test {};

TEST_F(DefFunctionRefTest, FromValueSubstitutesFixedFirstArgument) {
  auto bound = def_function_ref<int(int, int)>::from_value(add, 10);
  EXPECT_EQ(bound(5), 15);
  EXPECT_EQ(bound(7), 17);
}

TEST_F(DefFunctionRefTest, FromValueOwnsACopyIndependentOfTheOriginal) {
  int original = 10;
  auto bound = def_function_ref<int(int, int)>::from_value(add, original);
  original = 999; // must not affect the already-bound (owned) copy
  EXPECT_EQ(bound(5), 15);
}

TEST_F(DefFunctionRefTest, FromGeneratorIsInvokedFreshOnEveryCall) {
  int next = 0;
  auto generator = [&next]() mutable noexcept { return next++; };
  auto bound = def_function_ref<int(int, int)>::from_generator(add, generator);

  EXPECT_EQ(bound(100), 100); // base = 0
  EXPECT_EQ(bound(100), 101); // base = 1
  EXPECT_EQ(bound(100), 102); // base = 2
}

TEST_F(DefFunctionRefTest, BindsToFunctorTarget) {
  adder a{100};
  auto bound = def_function_ref<int(int, int)>::from_value(a, 3);
  EXPECT_EQ(bound(5), 108); // a(3, 5) == 3 + 5 + 100
}

TEST_F(DefFunctionRefTest, PresentsItselfAsFunctionRef) {
  // A named `def_function_ref` lvalue binds directly to `function_ref<R(Args...)>`
  // through the ordinary stateful-callable constructor -- no conversion operator
  // needed, exactly like any other named callable lvalue.
  auto bound = def_function_ref<int(int, int)>::from_value(add, 10);
  EXPECT_EQ(call_with_five(bound), 15);
}

TEST_F(DefFunctionRefTest, AsFunctionRefProvidesAnExplicitAdapter) {
  auto bound = def_function_ref<int(int, int)>::from_value(add, 10);
  function_ref<int(int)> adapted = bound.as_function_ref();
  EXPECT_EQ(adapted(5), 15);
  EXPECT_EQ(call_with_five(bound.as_function_ref()), 15);
}

TEST_F(DefFunctionRefTest, IsMoveOnly) {
  static_assert(!std::is_copy_constructible_v<def_function_ref<int(int, int)>>);
  static_assert(!std::is_copy_assignable_v<def_function_ref<int(int, int)>>);
  static_assert(std::is_move_constructible_v<def_function_ref<int(int, int)>>);
  static_assert(std::is_move_assignable_v<def_function_ref<int(int, int)>>);

  auto bound = def_function_ref<int(int, int)>::from_value(add, 10);
  auto moved = std::move(bound);
  EXPECT_EQ(moved(5), 15);
}

} // namespace
} // namespace reloco
