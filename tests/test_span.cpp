#include <algorithm>
#include <gtest/gtest.h>
#include <reloco/span.hpp>

class HardenedSpanTest : public ::testing::Test {
protected:
  int raw_data[5] = {10, 20, 30, 40, 50};
};

TEST_F(HardenedSpanTest, STLInteroperability) {
  int data[] = {3, 1, 2};
  reloco::span<int> s(data, 3);

  // 1. Works with range-based for
  int sum = 0;
  for (int val : s)
    sum += val;
  EXPECT_EQ(sum, 6);

  // 2. Works with std algorithms
  std::sort(s.begin(), s.end());
  EXPECT_EQ(s[0], 1);
  EXPECT_EQ(s[2], 3);

  // 3. Subspan safety
  auto sub = s.try_first(10); // Too big!
  EXPECT_FALSE(sub.has_value());
}

TEST_F(HardenedSpanTest, TryAtReturnsValidReference) {
  reloco::span<int> s(raw_data, 5);

  // Success case
  auto res = s.try_at(2);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->get(), 30);

  // Verify it is an actual reference (modify the value)
  res->get() = 99;
  EXPECT_EQ(raw_data[2], 99);

  // Failure case
  auto out_of_bounds = s.try_at(10);
  EXPECT_FALSE(out_of_bounds.has_value());
  EXPECT_EQ(out_of_bounds.error(), reloco::error::out_of_bounds);
}

TEST_F(HardenedSpanTest, OperatorSquareBracketsAsserts) {
  reloco::span<int> s(raw_data, 5);

  EXPECT_EQ(s[0], 10);

  EXPECT_DEATH(s[10], "");
}

TEST_F(HardenedSpanTest, UnsafeAtPerformanceHole) {
  reloco::span<int> s(raw_data, 5);

  // No checks, just raw speed.
  EXPECT_EQ(s.unsafe_at(4), 50);
}

TEST_F(HardenedSpanTest, TrySubspanLogic) {
  reloco::span<int> s(raw_data, 5);

  // Valid subspan
  auto sub = s.try_subspan(1, 3); // {20, 30, 40}
  ASSERT_TRUE(sub.has_value());
  EXPECT_EQ(sub->size(), 3);
  EXPECT_EQ(sub->unsafe_at(0), 20);

  // Invalid: offset out of range
  EXPECT_FALSE(s.try_subspan(6, 1).has_value());

  // Invalid: count overflows size
  EXPECT_FALSE(s.try_subspan(2, 4).has_value());
}

TEST_F(HardenedSpanTest, IteratorCompatibility) {
  reloco::span<int> s(raw_data, 5);

  // Test range-based for loop
  int sum = 0;
  for (int &x : s) {
    sum += x;
  }
  EXPECT_EQ(sum, 150);

  // Test standard algorithm
  auto it = std::find(s.begin(), s.end(), 30);
  ASSERT_NE(it, s.end());
  EXPECT_EQ(*it, 30);
}

TEST_F(HardenedSpanTest, ConstSpanPreventsModification) {
  const int const_data[3] = {1, 2, 3};
  reloco::span<const int> s(const_data, 3);

  auto res = s.try_at(0);
  // res->get() = 10; // This would be a compile-time error
  EXPECT_EQ(res->get(), 1);
}

TEST_F(HardenedSpanTest, EmptySpanSafety) {
  reloco::span<int> empty_s(nullptr, 0);

  EXPECT_TRUE(empty_s.empty());
  EXPECT_EQ(empty_s.size(), 0);

  auto res = empty_s.try_at(0);
  EXPECT_FALSE(res.has_value());

  auto front_res = empty_s.try_front();
  EXPECT_FALSE(front_res.has_value());
}
