#include <gtest/gtest.h>
#include <reloco/array.hpp>

TEST(HardenedArrayTest, BasicOperations) {
  reloco::array<int, 3> arr = {1, 2, 3};

  // Safe access
  auto res = arr.try_at(1);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->get(), 2);

  // Modification through reference_wrapper
  res->get() = 20;
  EXPECT_EQ(arr[1], 20);

  // Out of bounds check
  auto bad = arr.try_at(5);
  EXPECT_FALSE(bad.has_value());
  EXPECT_EQ(bad.error(), reloco::error::out_of_bounds);
}

TEST(HardenedArrayTest, SpanConversion) {
  reloco::array<int, 5> arr = {1, 2, 3, 4, 5};

  auto process = [](reloco::span<int> s) { return s.size(); };

  EXPECT_EQ(process(arr.as_span()), 5);
}

TEST(HardenedArrayTest, Initialization) {
  reloco::array<int, 3> arr = {10, 20, 30};

  EXPECT_EQ(arr.size(), 3);
  EXPECT_EQ(arr[0], 10);

  auto res = arr.try_at(2);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->get(), 30);
}

TEST(HardenedArrayTest, AdvancedAPIs) {
  reloco::array arr = {1, 2, 3, 4}; // Using deduction guide

  auto sub = arr.static_subspan<1, 2>();
  EXPECT_EQ(sub.size(), 2);
  EXPECT_EQ(sub[0], 2);

  // 2. Mapping
  auto doubled = arr.map([](int x) { return x * 2; });
  EXPECT_EQ(doubled[0], 2);
  EXPECT_EQ(doubled[3], 8);

  reloco::array other = {1, 2, 3, 4};
  EXPECT_TRUE(arr == other);
}

TEST(HardenedArrayTest, StructuredBindings) {
  reloco::array arr = {10, 20, 30};

  // Unpack the array into individual variables
  auto &[x, y, z] = arr;

  EXPECT_EQ(x, 10);
  EXPECT_EQ(y, 20);
  EXPECT_EQ(z, 30);

  // Verify they are actual references
  x = 100;
  EXPECT_EQ(arr[0], 100);
}
