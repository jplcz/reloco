// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/error.hpp"
#include "reloco/optional.hpp"
#include <gtest/gtest.h>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

using namespace reloco;

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

  LifetimeTracker &operator=(const LifetimeTracker &o) {
    value = o.value;
    return *this;
  }

  LifetimeTracker &operator=(LifetimeTracker &&o) noexcept {
    value = o.value;
    o.value = -1;
    return *this;
  }

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

class OptionalTest : public ::testing::Test {
protected:
  void SetUp() override { LifetimeTracker::reset_counts(); }
};

// --- Tests ---

TEST_F(OptionalTest, EmptyState) {
  optional<int> empty1;
  optional<int> empty2(nullopt);

  EXPECT_FALSE(empty1.has_value());
  EXPECT_FALSE(static_cast<bool>(empty2));

  // Fallible access
  auto res = empty1.try_value();
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::not_found);

  EXPECT_EQ(empty1.value_or(42), 42);
}

TEST_F(OptionalTest, ValueStateAndAccessTiers) {
  optional<int> opt(42);
  EXPECT_TRUE(opt.has_value());

  // Checked tier
  EXPECT_EQ(opt.value(), 42);
  EXPECT_EQ(*opt, 42);

  // Fallible tier
  auto res = opt.try_value();
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res.value().get(), 42);

  // Unsafe tier
  EXPECT_EQ(opt.unsafe_value(), 42);
  EXPECT_EQ(*opt.unsafe_ptr(), 42);
}

TEST_F(OptionalTest, OkOrIntegration) {
  optional<int> empty;
  optional<int> full(99);

  auto res1 = empty.ok_or(error::invalid_argument);
  ASSERT_FALSE(res1.has_value());
  EXPECT_EQ(res1.error(), error::invalid_argument);

  auto res2 = full.ok_or(error::invalid_argument);
  ASSERT_TRUE(res2.has_value());
  EXPECT_EQ(res2.value(), 99);
}

TEST_F(OptionalTest, InPlaceConstruction) {
  struct MultiArg {
    int a;
    float b;
    MultiArg(int a_arg, float b_arg) : a(a_arg), b(b_arg) {}
  };

  optional<MultiArg> opt(std::in_place, 10, 3.14f);
  EXPECT_TRUE(opt.has_value());
  EXPECT_EQ(opt->a, 10);
  EXPECT_FLOAT_EQ(opt->b, 3.14f);
}

TEST_F(OptionalTest, LifecycleTracking) {
  {
    optional<LifetimeTracker> opt1;
    EXPECT_EQ(LifetimeTracker::instances_alive, 0);

    opt1.emplace(42);
    EXPECT_EQ(LifetimeTracker::instances_alive, 1);

    optional<LifetimeTracker> opt2 = opt1; // Copy
    EXPECT_EQ(LifetimeTracker::instances_alive, 2);

    opt2.reset();
    EXPECT_EQ(LifetimeTracker::instances_alive, 1);
    EXPECT_EQ(LifetimeTracker::destructions, 1);

    opt2 = std::move(opt1); // Move assign full to empty
    EXPECT_EQ(LifetimeTracker::instances_alive, 2);
    EXPECT_EQ(opt2->value, 42);
    EXPECT_EQ(opt1.as_known()->value, -1); // Moved from
  }

  // Both out of scope
  EXPECT_EQ(LifetimeTracker::instances_alive, 0);
}

TEST_F(OptionalTest, Swap) {
  optional<int> empty;
  optional<int> full(10);

  empty.swap(full);
  EXPECT_TRUE(empty.has_value());
  EXPECT_EQ(*empty.as_known(), 10);
  EXPECT_FALSE(full.has_value());

  optional<int> full2(20);
  empty.swap(full2);
  EXPECT_EQ(*empty.as_known(), 20);
  EXPECT_EQ(*full2.as_known(), 10);
}

TEST_F(OptionalTest, Comparisons) {
  optional<int> empty;
  optional<int> opt1(10);
  optional<int> opt2(10);
  optional<int> opt3(20);

  // Against nullopt
  EXPECT_TRUE(empty == nullopt);
  EXPECT_FALSE(opt1 == nullopt);
  EXPECT_TRUE(opt1 != nullopt);

  // Against values
  EXPECT_TRUE(opt1 == 10);
  EXPECT_FALSE(opt1 == 20);
  EXPECT_TRUE(empty != 10);

  // Against optionals
  EXPECT_TRUE(opt1 == opt2);
  EXPECT_FALSE(opt1 == opt3);
  EXPECT_FALSE(opt1 == empty);
}

TEST_F(OptionalTest, TypestateAsKnownEscapeHatch) {
  optional<int> opt(77);

  // Test mutable escape hatch
  int &val_mut = opt.as_known().value();
  EXPECT_EQ(val_mut, 77);
  val_mut = 88;
  EXPECT_EQ(*opt, 88);

  // Test const escape hatch
  const optional<int> &c_opt = opt;
  EXPECT_EQ(c_opt.as_known().value(), 88);
}

TEST_F(OptionalTest, Take) {
  optional<int> full(42);
  optional<int> taken = full.take();
  EXPECT_TRUE(taken.has_value());
  EXPECT_EQ(*taken.as_known(), 42);
  EXPECT_FALSE(full.has_value());

  optional<int> empty;
  optional<int> taken_empty = empty.take();
  EXPECT_FALSE(taken_empty.has_value());
}

TEST_F(OptionalTest, Replace) {
  optional<int> full(1);
  optional<int> old = full.replace(2);
  EXPECT_TRUE(old.has_value());
  EXPECT_EQ(*old.as_known(), 1);
  EXPECT_TRUE(full.has_value());
  EXPECT_EQ(*full.as_known(), 2);

  optional<int> empty;
  optional<int> old_empty = empty.replace(9);
  EXPECT_FALSE(old_empty.has_value());
  EXPECT_TRUE(empty.has_value());
  EXPECT_EQ(*empty.as_known(), 9);
}

TEST_F(OptionalTest, GetOrInsert) {
  optional<int> empty;
  int &ref = empty.get_or_insert(5);
  EXPECT_EQ(ref, 5);
  EXPECT_TRUE(empty.has_value());

  optional<int> full(10);
  int &ref2 = full.get_or_insert(20);
  EXPECT_EQ(ref2, 10);
  EXPECT_EQ(*full.as_known(), 10);
}

TEST_F(OptionalTest, GetOrInsertWith) {
  optional<int> empty;
  int calls = 0;
  int &ref = empty.get_or_insert_with([&calls]() {
    ++calls;
    return 7;
  });
  EXPECT_EQ(ref, 7);
  EXPECT_EQ(calls, 1);

  optional<int> full(3);
  int &ref2 = full.get_or_insert_with([&calls]() {
    ++calls;
    return 99;
  });
  EXPECT_EQ(ref2, 3);
  EXPECT_EQ(calls, 1); // factory must not be invoked when already present
}

TEST_F(OptionalTest, Then) {
  optional<int> yes = then(true, []() { return 42; });
  ASSERT_TRUE(yes.has_value());
  EXPECT_EQ(*yes.as_known(), 42);

  bool invoked = false;
  optional<int> no = then(false, [&invoked]() {
    invoked = true;
    return 42;
  });
  EXPECT_FALSE(no.has_value());
  EXPECT_FALSE(invoked); // f must not be invoked when condition is false
}

TEST_F(OptionalTest, ThenSome) {
  optional<int> yes = then_some(true, 5);
  ASSERT_TRUE(yes.has_value());
  EXPECT_EQ(*yes.as_known(), 5);

  optional<int> no = then_some(false, 5);
  EXPECT_FALSE(no.has_value());
}

// Ensure the traits propagate cleanly at compile time
static_assert(is_trivially_relocatable<optional<int>>::value, "optional<int> should be trivially relocatable");

// std::string is NOT trivially relocatable in some standard libraries,
// but if reloco::string is, optional<reloco::string> will be too.
struct TrivialRelocMock {};
template <> struct reloco::is_trivially_relocatable<TrivialRelocMock> : std::true_type {};

static_assert(is_trivially_relocatable<optional<TrivialRelocMock>>::value,
              "optional<T> must propagate relocatability trait");

RELOCO_END_UNSAFE_BUFFER_USAGE
