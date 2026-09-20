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

TEST_F(FunctionRefTest, InlineTemporaryLambdas) {
  // Because of RELOCO_LIFETIMEBOUND, this would (correctly) fail to compile:
  // function_ref<int()> f = [] { return 1; };

  // Instead, the primary use case is passing inline lambdas directly down the
  // call stack. The lambda lives until the end of the statement, which is
  // perfectly safe for synchronous execution.

  int captured = 50;
  int result1 = execute_synchronously([&]() { return captured * 2; });
  EXPECT_EQ(result1, 100);

  // Stateless temporary lambda
  int result2 = execute_synchronously([] { return 77; });
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

} // namespace
} // namespace reloco
