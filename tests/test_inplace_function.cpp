// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/inplace_function.hpp"
#include <gtest/gtest.h>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {
namespace {

// --- Helper class to track non-trivial lifecycles ---
struct LifetimeTracker {
  static int instances_alive;
  static int destructions;

  int value;

  explicit LifetimeTracker(int v) : value(v) { ++instances_alive; }
  LifetimeTracker(const LifetimeTracker &o) : value(o.value) { ++instances_alive; }
  LifetimeTracker(LifetimeTracker &&o) noexcept : value(o.value) {
    ++instances_alive;
    o.value = -1;
  }

  LifetimeTracker &operator=(const LifetimeTracker &) = delete;
  LifetimeTracker &operator=(LifetimeTracker &&) = delete;

  ~LifetimeTracker() {
    --instances_alive;
    ++destructions;
  }

  static void reset_counts() {
    instances_alive = 0;
    destructions = 0;
  }
};

int LifetimeTracker::instances_alive = 0;
int LifetimeTracker::destructions = 0;

class InplaceFunctionTest : public ::testing::Test {
protected:
  void SetUp() override { LifetimeTracker::reset_counts(); }
};

// --- Free Functions ---
int multiply_by_two(int x) { return x * 2; }

// --- Tests ---

TEST_F(InplaceFunctionTest, EmptyState) {
  inplace_function<int(int)> f;
  EXPECT_FALSE(f.has_value());
  EXPECT_FALSE(static_cast<bool>(f));

  inplace_function<void()> f2(nullptr);
  EXPECT_FALSE(f2.has_value());
}

TEST_F(InplaceFunctionTest, FreeFunctionInvocation) {
  inplace_function<int(int)> f = multiply_by_two;

  EXPECT_TRUE(f.has_value());
  // We use `if (f)` to prove to Clang's -Wconsumed analyzer that `f` is unconsumed.
  // Using f() directly after EXPECT_TRUE would trigger a warning because the macro hides the branch.
  if (f) {
    EXPECT_EQ(f(21), 42);
  }
}

TEST_F(InplaceFunctionTest, StatelessLambda) {
  inplace_function<int(int, int)> f = [](int a, int b) { return a + b; };

  EXPECT_TRUE(f.has_value());
  if (f) {
    EXPECT_EQ(f(10, 20), 30);
  }
}

TEST_F(InplaceFunctionTest, StatefulLambda) {
  int multiplier = 3;
  inplace_function<int(int)> f = [multiplier](int x) { return x * multiplier; };

  EXPECT_TRUE(f.has_value());
  if (f) {
    EXPECT_EQ(f(10), 30);
  }
}

TEST_F(InplaceFunctionTest, UnsafeInvoke) {
  inplace_function<int()> f = [] { return 99; };

  if (f) {
    // Unsafe tier requires the explicit boundary macro
    EXPECT_EQ(f.unsafe_invoke(), 99);
  }
}

TEST_F(InplaceFunctionTest, LifecycleTrackingAndDestruction) {
  {
    inplace_function<int()> f;
    {
      LifetimeTracker tracker(42);
      EXPECT_EQ(LifetimeTracker::instances_alive, 1);

      // Capture the tracker by value (moves into the inplace_function's inline storage)
      f = [t = std::move(tracker)]() { return t.value; };
      EXPECT_EQ(LifetimeTracker::instances_alive, 2);
    } // Original tracker goes out of scope and is destroyed.

    EXPECT_EQ(LifetimeTracker::instances_alive, 1); // The one inside `f` remains

    if (f) {
      EXPECT_EQ(f(), 42);
    }
  } // `f` goes out of scope, destroying the lambda and its captures

  EXPECT_EQ(LifetimeTracker::instances_alive, 0);
  EXPECT_EQ(LifetimeTracker::destructions, 3); // Original, Temporary Lambda, Stored Lambda
}

TEST_F(InplaceFunctionTest, MoveSemantics) {
  inplace_function<int()> f1 = [t = LifetimeTracker(10)]() { return t.value; };
  EXPECT_EQ(LifetimeTracker::instances_alive, 1);

  // Move-construct f2 from f1
  inplace_function<int()> f2 = std::move(f1);

  // f1 should now be empty (unknown typestate initially, checked dynamically)
  EXPECT_FALSE(f1.has_value());
  EXPECT_TRUE(f2.has_value());

  // The tracker was moved, so the total alive instances should still be exactly 1
  // (unless it's trivially relocatable, but LifetimeTracker isn't).
  EXPECT_EQ(LifetimeTracker::instances_alive, 1);

  if (f2) {
    EXPECT_EQ(f2(), 10);
  }
}

TEST_F(InplaceFunctionTest, Swap) {
  inplace_function<int()> f1 = []() { return 1; };
  inplace_function<int()> f2 = []() { return 2; };

  using std::swap;
  swap(f1, f2); // ADL lookup

  // Clang loses typestate tracking across swap(), degrading them to 'unknown'.
  // Re-establishing typestate with `if` checks:
  if (f1) {
    EXPECT_EQ(f1(), 2);
  }
  if (f2) {
    EXPECT_EQ(f2(), 1);
  }
}

TEST_F(InplaceFunctionTest, CapacityAndReassignment) {
  // Test assigning a small lambda, then replacing it with another
  inplace_function<int(), 32> f = []() { return 100; };

  if (f)
    EXPECT_EQ(f(), 100);

  // Reassign to a different lambda with a different size
  struct LargeCapture {
    int data[4] = {1, 2, 3, 4}; // 16 bytes
  };

  LargeCapture large;
  f = [large]() { return large.data[3]; };

  if (f)
    EXPECT_EQ(f(), 4);
}

} // namespace
} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
