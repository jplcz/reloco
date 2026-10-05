// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/function_ptr_adapter.hpp"
#include <gtest/gtest.h>

namespace reloco {
namespace {

int g_last_sum = -1;

void record_sum(int a, int b) { g_last_sum = a + b; }
int add3(int a, int b, int c) { return a + b + c; }
int identity(int x) { return x; }

class FunctionPtrAdapterTest : public ::testing::Test {
  void SetUp() override { g_last_sum = -1; }
};

TEST_F(FunctionPtrAdapterTest, StripsTrailingArguments) {
  void (*f)(int, int, int, int) = as_function_ptr<record_sum>;
  f(1, 2, 3, 4);
  EXPECT_EQ(g_last_sum, 3);
}

TEST_F(FunctionPtrAdapterTest, PropagatesReturnValue) {
  int (*f)(int, int, int, int, int) = as_function_ptr<add3>;
  EXPECT_EQ(f(10, 20, 30, 999, 999), 60);
}

TEST_F(FunctionPtrAdapterTest, SupportsExactArityWithNoTruncation) {
  void (*f)(int, int) = as_function_ptr<record_sum>;
  f(5, 6);
  EXPECT_EQ(g_last_sum, 11);
}

TEST_F(FunctionPtrAdapterTest, TargetSignatureIsDeducedFromAssignmentContext) {
  // No explicit template signature is spelled out anywhere at the call site;
  // the destination function pointer's own type drives the deduction.
  auto f = static_cast<int (*)(int, int, int)>(as_function_ptr<identity>);
  EXPECT_EQ(f(42, 0, 0), 42);
}

} // namespace
} // namespace reloco
