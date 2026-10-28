// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/default_allocator.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using sv = reloco::string_view;
using parts = std::vector<std::string>;

template <typename Range> parts collect(const Range &range) {
  parts out;
  for (auto piece : range)
    out.emplace_back(piece.data(), piece.size());
  return out;
}

// Results of hardened accessors cannot be dereferenced as temporaries; this names the temporary.
template <typename R> R &lv(R &&value) { return value; }
template <typename R> decltype(auto) val(R &&value) { return *value; }

} // namespace

TEST(StringViewOps, Searching) {
  const sv s = "a,b,,c";
  EXPECT_EQ(s.rfind(','), 4u);
  EXPECT_EQ(s.rfind(',', 3), 3u);
  EXPECT_EQ(sv("abcabc").rfind(sv("bc")), 4u);
  EXPECT_EQ(sv("abcabc").rfind(sv("bc"), 3), 1u);
  EXPECT_EQ(sv("abc").rfind(sv("")), 3u);
  EXPECT_EQ(sv("abc").rfind(sv("abcd")), sv::npos);
  EXPECT_EQ(s.find_first_of(sv("bc")), 2u);
  EXPECT_EQ(s.find_first_not_of(sv("a,")), 2u);
  EXPECT_EQ(s.find_last_not_of(sv("c,")), 2u);
  EXPECT_EQ(s.find_if([](char c) { return c == 'c'; }), 5u);
  EXPECT_EQ(s.rfind_if([](char c) { return c == ','; }), 4u);
  EXPECT_TRUE(s.contains(','));
  EXPECT_TRUE(s.contains("b,,"));
  EXPECT_TRUE(s.contains(sv("")));
  EXPECT_FALSE(s.contains("x"));
  EXPECT_TRUE(s.contains([](char c) { return c == 'c'; }));
  EXPECT_EQ(s.count(','), 3u);
  EXPECT_EQ(sv("aaaa").count("aa"), 2u);
  EXPECT_EQ(sv("abc").count(""), 0u);
}

TEST(StringViewOps, Split) {
  EXPECT_EQ(collect(sv("a,b,,c").split(',')), (parts{"a", "b", "", "c"}));
  EXPECT_EQ(collect(sv("").split(',')), (parts{""}));
  EXPECT_EQ(collect(sv("a--b--c").split("--")), (parts{"a", "b", "c"}));
  EXPECT_EQ(collect(sv("a1b22c").split([](char c) { return c >= '0' && c <= '9'; })), (parts{"a", "b", "", "c"}));
  EXPECT_EQ(collect(sv("abc").split("")), (parts{"abc"}));
  EXPECT_EQ(collect(sv("a,b,c").rsplit(',')), (parts{"c", "b", "a"}));
  EXPECT_EQ(collect(sv("a,b,c,d").splitn(2, ',')), (parts{"a", "b,c,d"}));
  EXPECT_EQ(collect(sv("a,b,c,d").rsplitn(2, ',')), (parts{"d", "a,b,c"}));
  EXPECT_EQ(collect(sv("a,b,c").splitn(1, ',')), (parts{"a,b,c"}));
  EXPECT_EQ(collect(sv("a,b,c").splitn(0, ',')), (parts{}));
  EXPECT_EQ(collect(sv("a;b;").split_terminator(';')), (parts{"a", "b"}));
  EXPECT_EQ(collect(sv("").split_terminator(';')), (parts{}));
  EXPECT_EQ(collect(sv("a;b").split_terminator(';')), (parts{"a", "b"}));
  EXPECT_EQ(collect(sv("a\nb\n").split_inclusive('\n')), (parts{"a\n", "b\n"}));
  EXPECT_EQ(collect(sv("a\nb").split_inclusive('\n')), (parts{"a\n", "b"}));
  EXPECT_EQ(collect(sv("  a \t b\n c  ").split_whitespace()), (parts{"a", "b", "c"}));
  EXPECT_EQ(collect(sv("one\r\ntwo\nthree\n").lines()), (parts{"one", "two", "three"}));
  EXPECT_EQ(collect(sv("a\n\nb").lines()), (parts{"a", "", "b"}));
}

TEST(StringViewOps, SplitOnceAndAt) {
  auto kv = sv("key=value=x").split_once('=');
  ASSERT_TRUE(kv.has_value());
  EXPECT_EQ(lv(kv)->first, "key");
  EXPECT_EQ(lv(kv)->second, "value=x");
  auto last = sv("key=value=x").rsplit_once('=');
  ASSERT_TRUE(last.has_value());
  EXPECT_EQ(lv(last)->first, "key=value");
  EXPECT_EQ(lv(last)->second, "x");
  EXPECT_EQ(sv("abc").split_once("z").error(), reloco::error::not_found);
  EXPECT_EQ(sv("abc").rsplit_once("z").error(), reloco::error::not_found);

  const auto [head, tail] = sv("abcdef").split_at(2);
  EXPECT_EQ(head, "ab");
  EXPECT_EQ(tail, "cdef");
  EXPECT_EQ(sv("abc").try_split_at(4).error(), reloco::error::out_of_bounds);
  EXPECT_TRUE(sv("abc").try_split_at(3).has_value());
}

TEST(StringViewOps, Matches) {
  EXPECT_EQ(collect(sv("a1b22c333").matches([](char c) { return c >= '0' && c <= '9'; })),
            (parts{"1", "2", "2", "3", "3", "3"}));
  EXPECT_EQ(collect(sv("abXabXab").matches("ab")), (parts{"ab", "ab", "ab"}));
  std::vector<std::size_t> idx;
  for (auto [i, m] : sv("abXabXab").match_indices("ab")) {
    idx.push_back(i);
    EXPECT_EQ(m, "ab");
  }
  EXPECT_EQ(idx, (std::vector<std::size_t>{0, 3, 6}));
}

TEST(StringViewOps, Trimming) {
  EXPECT_EQ(sv("xxabxx").trim_matches('x'), "ab");
  EXPECT_EQ(sv("xxabxx").trim_start_matches('x'), "abxx");
  EXPECT_EQ(sv("xxabxx").trim_end_matches('x'), "xxab");
  EXPECT_EQ(sv("abababXab").trim_start_matches("ab"), "Xab");
  EXPECT_EQ(sv("123abc456").trim_matches([](char c) { return c >= '0' && c <= '9'; }), "abc");
  EXPECT_EQ(sv("aaa").trim_matches('a'), "");
  EXPECT_EQ(sv("abc").trim_matches(""), "abc");
  EXPECT_EQ(*sv("-x").strip_prefix('-'), "x");
  EXPECT_FALSE(sv("x").strip_prefix('-').has_value());
  EXPECT_EQ(*sv("x;").strip_suffix(';'), "x");
}

TEST(StringViewOps, Ascii) {
  EXPECT_TRUE(sv("plain").is_ascii());
  EXPECT_FALSE(sv("caf\xC3\xA9").is_ascii());
  EXPECT_TRUE(sv("HeLLo").eq_ignore_ascii_case("hello"));
  EXPECT_FALSE(sv("HeLLo").eq_ignore_ascii_case("hell"));
  EXPECT_LT(sv("apple").compare_ignore_ascii_case("BANANA"), 0);
  EXPECT_GT(sv("b").compare_ignore_ascii_case("A"), 0);
  EXPECT_EQ(sv("abc").compare_ignore_ascii_case("ABC"), 0);
  EXPECT_TRUE(sv("Content-Type").starts_with_ignore_ascii_case("content-"));
  EXPECT_TRUE(sv("FILE.TXT").ends_with_ignore_ascii_case(".txt"));
  EXPECT_FALSE(sv("a").ends_with_ignore_ascii_case("ab"));
  const sv utf8 = "a\xC3\xA9";
  EXPECT_TRUE(utf8.is_char_boundary(0));
  EXPECT_TRUE(utf8.is_char_boundary(1));
  EXPECT_FALSE(utf8.is_char_boundary(2));
  EXPECT_TRUE(utf8.is_char_boundary(3));
  EXPECT_FALSE(utf8.is_char_boundary(4));
}

TEST(StringViewOps, ParsesIntegers) {
  EXPECT_EQ(val(sv("1234").try_parse<int>()), 1234);
  EXPECT_EQ(val(sv("-42").try_parse<int>()), -42);
  EXPECT_EQ(val(sv("+7").try_parse<int>()), 7);
  EXPECT_EQ(val(sv("ff").try_parse<unsigned>(16)), 255u);
  EXPECT_EQ(val(sv("101").try_parse<int>(2)), 5);
  EXPECT_EQ(val(sv("255").try_parse<std::uint8_t>()), 255);
  EXPECT_EQ(val(sv("-128").try_parse<std::int8_t>()), -128);
  EXPECT_EQ(val(sv("9223372036854775807").try_parse<std::int64_t>()), INT64_MAX);
  EXPECT_EQ(val(sv("-9223372036854775808").try_parse<std::int64_t>()), INT64_MIN);
  EXPECT_EQ(val(sv("18446744073709551615").try_parse<std::uint64_t>()), UINT64_MAX);

  EXPECT_EQ(sv("256").try_parse<std::uint8_t>().error(), reloco::error::out_of_range);
  EXPECT_EQ(sv("128").try_parse<std::int8_t>().error(), reloco::error::out_of_range);
  EXPECT_EQ(sv("-129").try_parse<std::int8_t>().error(), reloco::error::out_of_range);
  EXPECT_EQ(sv("99999999999999999999999").try_parse<std::int64_t>().error(), reloco::error::out_of_range);
  EXPECT_EQ(sv("").try_parse<int>().error(), reloco::error::invalid_argument);
  EXPECT_EQ(sv("-").try_parse<int>().error(), reloco::error::invalid_argument);
  EXPECT_EQ(sv("12x").try_parse<int>().error(), reloco::error::invalid_argument);
  EXPECT_EQ(sv(" 1").try_parse<int>().error(), reloco::error::invalid_argument);
  EXPECT_EQ(sv("-1").try_parse<unsigned>().error(), reloco::error::invalid_argument);
  EXPECT_EQ(sv("12").try_parse<int>(2).error(), reloco::error::invalid_argument);

  sv text = "12,-3,x";
  EXPECT_EQ(val(text.try_consume_int<int>()), 12);
  EXPECT_EQ(text, ",-3,x");
  EXPECT_TRUE(text.try_consume_int<int>().error() == reloco::error::invalid_argument);
  text.remove_prefix(1);
  EXPECT_EQ(val(text.try_consume_int<int>()), -3);
  EXPECT_EQ(text, ",x");

  EXPECT_TRUE(val(sv("true").try_parse_bool()));
  EXPECT_FALSE(val(sv("false").try_parse_bool()));
  EXPECT_EQ(sv("True").try_parse_bool().error(), reloco::error::invalid_argument);
}

TEST(StringViewOps, Utf8) {
  const sv text = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"; // a, e-acute, euro, grinning face
  EXPECT_TRUE(text.is_valid_utf8());
  EXPECT_EQ(val(text.try_char_count()), 4u);
  std::vector<char32_t> cps;
  for (char32_t c : text.utf8_chars())
    cps.push_back(c);
  EXPECT_EQ(cps, (std::vector<char32_t>{U'a', 0xE9, 0x20AC, 0x1F600}));

  for (const char *bad : {"\xC3", "\x80", "\xC0\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80", "a\xE2\x82"}) {
    EXPECT_FALSE(sv(bad).is_valid_utf8()) << bad;
    EXPECT_EQ(sv(bad).try_char_count().error(), reloco::error::invalid_argument);
  }
  std::vector<char32_t> replaced;
  for (char32_t c : sv("a\x80z").utf8_chars())
    replaced.push_back(c);
  EXPECT_EQ(replaced, (std::vector<char32_t>{U'a', 0xFFFD, U'z'}));
}

TEST(StringViewOps, AllocatingOperations) {
  auto replaced = sv("one two one").try_replace("one", "1");
  ASSERT_TRUE(replaced.has_value());
  EXPECT_EQ(replaced->view(), "1 two 1");
  EXPECT_EQ(val(sv("abc").try_replace("", "x")).view(), "abc");
  EXPECT_EQ(val(sv("aaa").try_replace("a", "")).view(), "");

  EXPECT_EQ(val(sv("MiXed 1").try_to_ascii_uppercase()).view(), "MIXED 1");
  EXPECT_EQ(val(sv("MiXed 1").try_to_ascii_lowercase()).view(), "mixed 1");
  EXPECT_EQ(val(sv("ab").try_repeat(3)).view(), "ababab");
  EXPECT_EQ(val(sv("ab").try_repeat(0)).view(), "");
  EXPECT_EQ(sv("ab").try_repeat(SIZE_MAX).error(), reloco::error::integer_overflow);

  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  EXPECT_EQ(val(sv("x-y").try_replace(heap, "-", "+")).view(), "x+y");
  EXPECT_EQ(val(sv("Ab").try_to_ascii_uppercase(heap)).view(), "AB");
  EXPECT_EQ(val(sv("ab").try_repeat(heap, 2)).view(), "abab");
}
