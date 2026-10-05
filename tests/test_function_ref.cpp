// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/function_ref.hpp"
#include <gtest/gtest.h>

namespace reloco {
namespace {

// --- Free Functions for Testing ---
int multiply_by_two(int x) { return x * 2; }
void increment_counter(int &x) { ++x; }

// --- Functors for Testing ---
struct StatefulFunctor {
  int multiplier;
  int operator()(int x) { return x * multiplier; }
};

struct ConstFunctor {
  int operator()(int x) const { return x + 1; }
};

// --- Member functions for Testing ---
struct Counter {
  int value = 0;
  int add(int x) { return value += x; }
  int add_const(int x) const { return value + x; }
};

int free_mul_by_three(int x) { return x * 3; }

// --- Synchronous execution helper (simulates real API boundaries) ---
int execute_synchronously(function_ref<int()> task) { return task(); }

class FunctionRefTest : public ::testing::Test {};

// --- Tests ---

TEST_F(FunctionRefTest, ZeroAllocationSize) {
  // function_ref must never allocate and must exactly equal the size of two pointers
  // (the payload union and the trampoline function pointer).
  static_assert(sizeof(function_ref<void()>) == 2 * sizeof(void *),
                "function_ref must be exactly two pointers in size");
  static_assert(sizeof(function_ref<int(int, float)>) == 2 * sizeof(void *),
                "function_ref must be exactly two pointers in size");
}

TEST_F(FunctionRefTest, FreeFunctionPointers) {
  function_ref<int(int)> f1 = multiply_by_two;
  EXPECT_EQ(f1(21), 42);

  int counter = 0;
  function_ref<void(int &)> f2 = increment_counter;
  f2(counter);
  EXPECT_EQ(counter, 1);
}

TEST_F(FunctionRefTest, LvalueLambdas) {
  int captured = 10;
  // Create an lvalue lambda
  auto lambda = [&captured](int x) { return captured + x; };

  // Bind function_ref to the lvalue
  function_ref<int(int)> f = lambda;
  EXPECT_EQ(f(5), 15);

  // Modifying the captured state should be immediately reflected
  // because function_ref is just borrowing the lambda.
  captured = 20;
  EXPECT_EQ(f(5), 25);
}

TEST_F(FunctionRefTest, NamedLambdasRequired) {
  // The stateful-callable constructor takes its argument through a plain
  // lvalue reference (not a universal reference), so binding a temporary
  // lambda directly -- even one only ever used synchronously -- fails to
  // compile on every compiler, not just Clang:
  // function_ref<int()> f = [] { return 1; };
  // execute_synchronously([] { return 1; });

  // Naming the lambda first makes it an lvalue, which is all that's required.
  int captured = 50;
  auto stateful = [&]() { return captured * 2; };
  int result1 = execute_synchronously(stateful);
  EXPECT_EQ(result1, 100);

  // Stateless lambda, named.
  auto stateless = [] { return 77; };
  int result2 = execute_synchronously(stateless);
  EXPECT_EQ(result2, 77);
}

TEST_F(FunctionRefTest, FunctorObjects) {
  StatefulFunctor sf{3};
  function_ref<int(int)> f_stateful = sf;
  EXPECT_EQ(f_stateful(10), 30);

  // Test mutating the functor through the original object
  sf.multiplier = 4;
  EXPECT_EQ(f_stateful(10), 40);

  ConstFunctor cf;
  function_ref<int(int)> f_const = cf;
  EXPECT_EQ(f_const(10), 11);

  // Explicitly test binding from a const lvalue reference.
  // The trampoline must safely cast the const back to respect the original type.
  const ConstFunctor &cf_ref = cf;
  function_ref<int(int)> f_const_ref = cf_ref;
  EXPECT_EQ(f_const_ref(20), 21);
}

TEST_F(FunctionRefTest, Reassignment) {
  auto lambda1 = [] { return 1; };
  auto lambda2 = [] { return 2; };

  function_ref<int()> f = lambda1;
  EXPECT_EQ(f(), 1);

  // Safely rebind the borrow to a new lvalue
  f = lambda2;
  EXPECT_EQ(f(), 2);
}

TEST_F(FunctionRefTest, MutableLambda) {
  // `mutable` lambdas change state upon invocation
  auto counter_lambda = [count = 0]() mutable { return ++count; };

  function_ref<int()> f = counter_lambda;
  EXPECT_EQ(f(), 1);
  EXPECT_EQ(f(), 2);
  EXPECT_EQ(f(), 3);

  // Ensure the original lambda actually mutated
  EXPECT_EQ(counter_lambda(), 4);
}

TEST_F(FunctionRefTest, BoundMemberFunction) {
  Counter c;

  // Non-const member function, bound via the `nontype<&T::method>` tag.
  function_ref<int(int)> f = function_ref<int(int)>(nontype<&Counter::add>, c);
  EXPECT_EQ(f(5), 5);
  EXPECT_EQ(f(5), 10);

  // The borrow observes mutations made through the original object too.
  c.value = 100;
  EXPECT_EQ(f(1), 101);

  // Const member function bound to a const object.
  const Counter cc{7};
  function_ref<int(int)> f_const = function_ref<int(int)>(nontype<&Counter::add_const>, cc);
  EXPECT_EQ(f_const(3), 10);
}

TEST_F(FunctionRefTest, BoundFreeFunctionViaNontype) {
  // Free functions can also be bound through the same `nontype<Fn>` tag,
  // for symmetry with the bound-member-function call site.
  function_ref<int(int)> f = function_ref<int(int)>(nontype<free_mul_by_three>);
  EXPECT_EQ(f(4), 12);
}

} // namespace
} // namespace reloco
