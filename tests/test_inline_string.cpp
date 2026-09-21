// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/inline_string.hpp"
#include "reloco/string_view.hpp"
#include <gtest/gtest.h>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {
namespace {

using test_string = inline_string<16>;

TEST(InlineStringTest, DefaultConstructor) {
  test_string s;
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.size(), 0);
  EXPECT_EQ(s.capacity(), 16);
  EXPECT_EQ(s.unsafe_c_str()[0], '\0');
  EXPECT_EQ(s.view(), "");
}

TEST(InlineStringTest, TryCreate) {
  auto res = test_string::try_create("hello");
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->size(), 5);
  EXPECT_EQ(res->view(), "hello");

  // Exceeding capacity should fail gracefully
  auto res_fail = test_string::try_create("this string is definitely longer than 16 characters");
  ASSERT_FALSE(res_fail.has_value());
  EXPECT_EQ(res_fail.error(), error::out_of_bounds);
}

TEST(InlineStringTest, TryAssign) {
  test_string s;
  auto res = s.try_assign("world");
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(s.size(), 5);
  EXPECT_EQ(s.view(), "world");
  EXPECT_EQ(s.unsafe_c_str()[5], '\0');

  // Try assigning overflow
  auto res_fail = s.try_assign("12345678901234567"); // 17 chars
  ASSERT_FALSE(res_fail.has_value());
  EXPECT_EQ(res_fail.error(), error::out_of_bounds);

  // Original string should remain untouched on failure
  EXPECT_EQ(s.view(), "world");
}

TEST(InlineStringTest, TryAppendAndPushBack) {
  test_string s;

  ASSERT_TRUE(s.try_push_back('A').has_value());
  EXPECT_EQ(s.size(), 1);
  EXPECT_EQ(s.view(), "A");

  ASSERT_TRUE(s.try_append("BCDEF").has_value());
  EXPECT_EQ(s.size(), 6);
  EXPECT_EQ(s.view(), "ABCDEF");

  // 6 + 10 = 16 (Exact capacity)
  ASSERT_TRUE(s.try_append("0123456789").has_value());
  EXPECT_EQ(s.size(), 16);
  EXPECT_EQ(s.unsafe_c_str()[16], '\0'); // Still null terminated

  // 16 + 1 = 17 (Overflow)
  auto res_fail = s.try_push_back('Z');
  ASSERT_FALSE(res_fail.has_value());
  EXPECT_EQ(res_fail.error(), error::out_of_bounds);
}

TEST(InlineStringTest, TryInsertAndErase) {
  auto s = test_string::try_create("ACD").value();

  // Insert in middle
  ASSERT_TRUE(s.try_insert(1, "B").has_value());
  EXPECT_EQ(s.view(), "ABCD");

  // Erase from middle
  ASSERT_TRUE(s.try_erase(1, 2).has_value()); // Erase 'B', 'C'
  EXPECT_EQ(s.view(), "AD");

  // Erase out of bounds
  EXPECT_FALSE(s.try_erase(5, 1).has_value());
}

TEST(InlineStringTest, TryResize) {
  test_string s;
  ASSERT_TRUE(s.try_resize(5, 'X').has_value());
  EXPECT_EQ(s.view(), "XXXXX");
  EXPECT_EQ(s.unsafe_c_str()[5], '\0');

  ASSERT_TRUE(s.try_resize(3).has_value());
  EXPECT_EQ(s.view(), "XXX");
  EXPECT_EQ(s.unsafe_c_str()[3], '\0');

  EXPECT_FALSE(s.try_resize(17, 'Y').has_value());
}

TEST(InlineStringTest, ElementAccessTiers) {
  auto s = test_string::try_create("test").value();

  // Checked tier (requires death tests if you want to test assertions natively,
  // but we can test valid access here)
  EXPECT_EQ(s.at(0), 't');
  EXPECT_EQ(s.front(), 't');
  EXPECT_EQ(s.back(), 't');

  // Fallible tier
  EXPECT_TRUE(s.try_at(1).has_value());
  EXPECT_EQ(s.try_at(1).value().get(), 'e');

  EXPECT_FALSE(s.try_at(4).has_value());
  EXPECT_EQ(s.try_at(4).error(), error::out_of_bounds);

  // Unsafe tier
  EXPECT_EQ(s.unsafe_at(2), 's');
  EXPECT_EQ(s.unsafe_front(), 't');
  EXPECT_EQ(s.unsafe_back(), 't');
}

TEST(InlineStringTest, PopBackAndClear) {
  auto s = test_string::try_create("ab").value();

  ASSERT_TRUE(s.try_pop_back().has_value());
  EXPECT_EQ(s.view(), "a");
  EXPECT_EQ(s.unsafe_c_str()[1], '\0');

  ASSERT_TRUE(s.try_pop_back().has_value());
  EXPECT_TRUE(s.empty());

  EXPECT_FALSE(s.try_pop_back().has_value()); // Empty

  std::ignore = s.try_append("repopulate");
  s.clear();
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.unsafe_c_str()[0], '\0');
}

TEST(InlineStringTest, SelfAliasingSafety) {
  auto s = test_string::try_create("123").value();

  // Append the string to itself (s.view() points directly into s.data())
  auto res = s.try_append(s.view());
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(s.view(), "123123");

  // Assign a substring of itself to itself
  auto res2 = s.try_assign(s.view().substr(1, 4)); // "2312"
  ASSERT_TRUE(res2.has_value());
  EXPECT_EQ(s.view(), "2312");

  // Insert itself into itself
  auto res3 = s.try_insert(2, s.view()); // Insert "2312" at index 2
  ASSERT_TRUE(res3.has_value());
  EXPECT_EQ(s.view(), "23231212");
}

TEST(InlineStringTest, SearchAndCompare) {
  // "needle in a hay" is 15 characters, safely fitting in inline_string<16>
  auto s = test_string::try_create("needle in a hay").value();

  EXPECT_TRUE(s.starts_with("needle"));
  EXPECT_TRUE(s.contains("hay"));
  EXPECT_EQ(s.find("in"), 7);
  EXPECT_EQ(s.find('z'), test_string::npos);

  auto s2 = test_string::try_create("needle in a hay").value();
  auto s3 = test_string::try_create("zebra").value();

  EXPECT_TRUE(s == s2);
  EXPECT_TRUE(s != s3);
  EXPECT_TRUE(s < s3);

  EXPECT_TRUE(s == "needle in a hay");
  EXPECT_TRUE(s != "needle in a");
}

TEST(InlineStringTest, TrivialRelocatable) {
  // Verifies the type trait we injected is correctly evaluated
  EXPECT_TRUE(is_trivially_relocatable<test_string>::value);
}

TEST(InlineStringTest, TryToStringClonesIntoHeapString) {
  auto s_res = test_string::try_create("hello");
  ASSERT_TRUE(s_res.has_value());
  auto &s = *s_res;

  auto str_res = s.try_to_string();
  ASSERT_TRUE(str_res.has_value());
  EXPECT_EQ(str_res->view(), "hello");

  // Original is untouched.
  EXPECT_EQ(s.view(), "hello");
}

TEST(InlineStringTest, TryToStringWithExplicitAllocator) {
  auto s_res = test_string::try_create("world");
  ASSERT_TRUE(s_res.has_value());

  auto str_res = s_res->try_to_string(default_allocator());
  ASSERT_TRUE(str_res.has_value());
  EXPECT_EQ(str_res->view(), "world");
}

TEST(InlineStringTest, TryToStringEmptySourceProducesEmptyString) {
  test_string s;
  auto str_res = s.try_to_string();
  ASSERT_TRUE(str_res.has_value());
  EXPECT_TRUE(str_res->empty());
}

} // namespace
} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
