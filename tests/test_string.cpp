#include <algorithm>
#include <gtest/gtest.h>
#include <reloco/string.hpp>

TEST(StringTest, BasicOperations) {
  auto str_res = reloco::string::try_create("Hello");
  ASSERT_TRUE(str_res.has_value());

  auto &str = *str_res;
  EXPECT_STREQ(str.c_str(), "Hello");
  EXPECT_EQ(str.length(), 5);

  ASSERT_TRUE(str.try_append(" Reloco"));
  EXPECT_STREQ(str.c_str(), "Hello Reloco");
}

TEST(StringTest, GrowthRelocation) {
  auto str = reloco::string::try_create("Short").value();
  const char *original_ptr = str.c_str();

  // Trigger a massive reservation
  ASSERT_TRUE(str.try_reserve(1024 * 1024));

  // On many systems, this might be the same pointer due to in-place growth!
  // But regardless, the data is preserved.
  EXPECT_STREQ(str.c_str(), "Short");
}

TEST(StringTest, SpaceshipComparisons) {
  auto a = reloco::string::try_create("apple").value();
  auto b = reloco::string::try_create("banana").value();
  auto a2 = reloco::string::try_create("apple").value();

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
  auto str = reloco::string::try_create("bravo").value();

  // Test sorting via iterators
  std::sort(str.begin(), str.end());
  EXPECT_STREQ(str.c_str(), "aborv");

  // Test accessors
  EXPECT_EQ(str[0], 'a');
  str.front() = 'z';
  EXPECT_EQ(str[0], 'z');

  // Test Comparison
  auto str2 = reloco::string::try_create("zborv").value();
  EXPECT_TRUE(str == str2);
  EXPECT_TRUE(str == "zborv");
}

TEST(StringTest, ShrinkToFit) {
  auto str =
      reloco::string::try_create("Long string that we will shrink").value();
  auto initial_cap = str.capacity();

  str.clear();
  ASSERT_TRUE(str.shrink_to_fit());

  EXPECT_EQ(str.capacity(), 0);
}

TEST(StringTest, StringViewInteropperability) {
  auto fs = reloco::string::try_create("Hardware Honest").value();

  // Test implicit conversion to a function taking string_view
  auto length_check = [](std::string_view sv) { return sv.length(); };
  EXPECT_EQ(length_check(fs), 15);

  // Test from_view
  std::string_view sv = "From View";
  auto fs2 = reloco::string::from_view(sv).value();
  EXPECT_STREQ(fs2.c_str(), "From View");
}

TEST(StringTest, StringViewAppend) {
  auto str = reloco::string::try_create("Part1").value();

  std::string_view sv = "Part2_ExtraContent";
  // Append only a portion of the view
  ASSERT_TRUE(str.try_append(sv.substr(0, 5)));

  EXPECT_STREQ(str.c_str(), "Part1Part2");
  EXPECT_EQ(str.length(), 10);
}

TEST(StringTest, CreateFromLiteral) {
  // Literals implicitly convert to string_view
  auto str = reloco::string::try_create("FromLiteral").value();
  EXPECT_EQ(str.view(), "FromLiteral");
}

TEST(StringTest, Formatting) {
  auto str = reloco::string::try_create("Log: ").value();

  // Append formatted data
  int errorCode = 404;
  const char *msg = "Not Found";
  ASSERT_TRUE(str.try_append_fmt("Error %d - %s", errorCode, msg));

  EXPECT_STREQ(str.c_str(), "Log: Error 404 - Not Found");
  EXPECT_EQ(str.length(), 26);
}

TEST(StringTest, FormatLargeData) {
  auto str = reloco::string::try_create("").value();

  // Test a large format that forces a resize
  ASSERT_TRUE(str.try_append_fmt("%0100d", 7));

  EXPECT_EQ(str.length(), 100);
  EXPECT_EQ(str[99], '7');
}

TEST(StringTest, AdvancedStlFeatures) {
  auto str = reloco::string::try_create("reloco").value();

  // Test Resize
  ASSERT_TRUE(str.try_resize(10, '!'));
  EXPECT_STREQ(str.c_str(), "reloco!!!!");

  // Test Searching
  EXPECT_TRUE(str.contains("loco"));
  EXPECT_EQ(str.find("!!!!"), 6);

  // Test Reverse Iterators
  std::string reversed;
  for (auto it = str.rbegin(); it != str.rend(); ++it) {
    reversed += *it;
  }
  EXPECT_EQ(reversed, "!!!!ocoler");

  // Test Insertion
  ASSERT_TRUE(str.try_insert(0, "C++ "));
  EXPECT_STREQ(str.c_str(), "C++ reloco!!!!");

  // Verify it works with iterator traits
  auto str2 = reloco::string::try_create("test").value();

  // Test Erase
  str2.erase(1, 2); // "test" -> "tt"
  EXPECT_STREQ(str2.c_str(), "tt");
}

TEST(StringTest, TypeTraitsVerification) {
  using StringType = reloco::string;

  // Verify standard types exist
  static_assert(std::is_same_v<typename StringType::value_type, char>);
  static_assert(std::is_same_v<typename StringType::pointer, char *>);

  // Verify it works with iterator traits
  StringType str = StringType::try_create("test").value();
  auto dist = std::distance(str.begin(), str.end());
  EXPECT_EQ(dist, 4);
}

TEST(StringTest, FallibleMutationErrors) {
  auto str = reloco::string::try_create("A").value();

  // Successful pop
  ASSERT_TRUE(str.try_pop_back());
  EXPECT_EQ(str.length(), 0);

  // Fail on empty pop
  auto res_pop = str.try_pop_back();
  EXPECT_FALSE(res_pop.has_value());
  EXPECT_EQ(res_pop.error(), reloco::error::out_of_range);

  // Fail on out of bounds erase
  auto res_erase = str.try_erase(10, 1);
  EXPECT_FALSE(res_erase.has_value());
  EXPECT_EQ(res_erase.error(), reloco::error::out_of_range);
}

TEST(StringTest, StringAssignFailurePreservesData) {
  auto str = reloco::string::try_create("KeepMe").value();

  // Simulate a failure by trying to assign an impossible size
  std::string_view huge_view(nullptr, static_cast<std::size_t>(-1) / 2);

  auto res = str.try_assign(huge_view);

  EXPECT_FALSE(res.has_value());
  EXPECT_STREQ(str.c_str(), "KeepMe"); // Data is still intact!
}