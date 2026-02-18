#include <gtest/gtest.h>
#include <reloco/string.hpp>

TEST(StringTest, BasicOperations) {
  auto str_res = reloco::string::create("Hello");
  ASSERT_TRUE(str_res.has_value());

  auto &str = *str_res;
  EXPECT_STREQ(str.c_str(), "Hello");
  EXPECT_EQ(str.length(), 5);

  ASSERT_TRUE(str.try_append(" Reloco"));
  EXPECT_STREQ(str.c_str(), "Hello Reloco");
}

TEST(StringTest, GrowthRelocation) {
  auto str = reloco::string::create("Short").value();
  const char *original_ptr = str.c_str();

  // Trigger a massive reservation
  ASSERT_TRUE(str.try_reserve(1024 * 1024));

  // On many systems, this might be the same pointer due to in-place growth!
  // But regardless, the data is preserved.
  EXPECT_STREQ(str.c_str(), "Short");
}

TEST(StringTest, SpaceshipComparisons) {
  auto a = reloco::string::create("apple").value();
  auto b = reloco::string::create("banana").value();
  auto a2 = reloco::string::create("apple").value();

  // The spaceship handles all of these:
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(b > a);
  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(a == a2);
  EXPECT_TRUE(a != b);

  // Test against literals
  EXPECT_TRUE(a < "apricot");
  EXPECT_TRUE(b > "apple");
}

TEST(StringTest, StlInterface) {
  auto str = reloco::string::create("bravo").value();

  // Test sorting via iterators
  std::sort(str.begin(), str.end());
  EXPECT_STREQ(str.c_str(), "aborv");

  // Test accessors
  EXPECT_EQ(str[0], 'a');
  str.front() = 'z';
  EXPECT_EQ(str[0], 'z');

  // Test Comparison
  auto str2 = reloco::string::create("zborv").value();
  EXPECT_TRUE(str == str2);
  EXPECT_TRUE(str == "zborv");
}

TEST(StringTest, ShrinkToFit) {
  auto str = reloco::string::create("Long string that we will shrink").value();
  auto initial_cap = str.capacity();

  str.clear();
  ASSERT_TRUE(str.shrink_to_fit());

  EXPECT_EQ(str.capacity(), 0);
}