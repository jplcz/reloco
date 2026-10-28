// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/inline_string.hpp>
#include <reloco/span.hpp>
#include <reloco/sso_string.hpp>
#include <reloco/string.hpp>

#include <array>
#include <cstdint>
#include <string>

namespace {

using reloco::error;

template <typename S> S make(reloco::string_view text) {
  auto r = S::try_create(text);
  EXPECT_TRUE(r.has_value());
  return std::move(*r);
}

template <typename S> std::string str(const S &s) { return std::string(s.view().data(), s.view().size()); }

template <typename T> class StringOps : public ::testing::Test {};
using StringTypes = ::testing::Types<reloco::string, reloco::sso_string, reloco::inline_string<256>>;
TYPED_TEST_SUITE(StringOps, StringTypes, ::testing::internal::DefaultNameGenerator);

reloco::span<const std::uint8_t> bytes_of(const std::uint8_t *p, std::size_t n) {
  return reloco::span<const std::uint8_t>(p, n);
}

} // namespace

TYPED_TEST(StringOps, ForwardedViewOps) {
  auto s = make<TypeParam>("  a,b,,c  ");
  EXPECT_EQ(s.rfind(','), 6u);
  EXPECT_TRUE(s.contains("b,"));
  EXPECT_EQ(s.trim(), reloco::string_view("a,b,,c"));
  EXPECT_EQ(s.count(','), 3u);
  EXPECT_EQ(s.find(','), 3u);
  EXPECT_TRUE(s.starts_with(' '));
  EXPECT_FALSE(s.template try_parse<int>().has_value());
  auto n = make<TypeParam>("42");
  auto parsed = n.template try_parse<int>();
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, 42);
  std::size_t pieces = 0;
  for (auto p : s.trim().split(',')) {
    (void)p;
    ++pieces;
  }
  EXPECT_EQ(pieces, 4u);
}

TYPED_TEST(StringOps, InPlaceTrim) {
  auto s = make<TypeParam>(" \t hello world \n");
  ASSERT_TRUE(s.trim_in_place());
  EXPECT_EQ(str(s), "hello world");
  auto t = make<TypeParam>("xxabcxx");
  ASSERT_TRUE(t.trim_matches_in_place('x'));
  EXPECT_EQ(str(t), "abc");
  auto u = make<TypeParam>("prefix-body-suffix");
  EXPECT_TRUE(u.strip_prefix_in_place(reloco::string_view("prefix-")));
  EXPECT_TRUE(u.strip_suffix_in_place(reloco::string_view("-suffix")));
  EXPECT_FALSE(u.strip_suffix_in_place(reloco::string_view("nope")));
  EXPECT_EQ(str(u), "body");
  auto v = make<TypeParam>("   ");
  ASSERT_TRUE(v.trim_in_place());
  EXPECT_EQ(str(v), "");
}

TYPED_TEST(StringOps, ReplaceRange) {
  auto s = make<TypeParam>("hello world");
  ASSERT_TRUE(s.try_replace_range(6, 5, "there, friend"));
  EXPECT_EQ(str(s), "hello there, friend");
  ASSERT_TRUE(s.try_replace_range(0, 5, "yo"));
  EXPECT_EQ(str(s), "yo there, friend");
  ASSERT_TRUE(s.try_replace_range(2, 100, ""));
  EXPECT_EQ(str(s), "yo");
  EXPECT_EQ(s.try_replace_range(5, 1, "x").error(), error::out_of_bounds);
  EXPECT_EQ(str(s), "yo");
  // Replacement that aliases the string itself.
  auto a = make<TypeParam>("abcdef");
  ASSERT_TRUE(a.try_replace_range(1, 1, a.view().substr(3, 3)));
  EXPECT_EQ(str(a), "adefcdef");
  ASSERT_TRUE(a.try_prepend("<"));
  EXPECT_EQ(str(a), "<adefcdef");
}

TYPED_TEST(StringOps, ReplaceAll) {
  auto grow = make<TypeParam>("a-b-c");
  ASSERT_TRUE(grow.try_replace_all("-", "<->"));
  EXPECT_EQ(str(grow), "a<->b<->c");
  auto shrink = make<TypeParam>("a<->b<->c");
  ASSERT_TRUE(shrink.try_replace_all("<->", "+"));
  EXPECT_EQ(str(shrink), "a+b+c");
  auto same = make<TypeParam>("aXbXc");
  ASSERT_TRUE(same.try_replace_all("X", "Y"));
  EXPECT_EQ(str(same), "aYbYc");
  auto del = make<TypeParam>("xaxbx");
  ASSERT_TRUE(del.try_replace_all("x", ""));
  EXPECT_EQ(str(del), "ab");
  auto lim = make<TypeParam>("aaaa");
  ASSERT_TRUE(lim.try_replacen("a", "bb", 2));
  EXPECT_EQ(str(lim), "bbbbaa");
  auto none = make<TypeParam>("abc");
  ASSERT_TRUE(none.try_replace_all("z", "q"));
  EXPECT_EQ(str(none), "abc");
  EXPECT_EQ(none.try_replace_all(none.view().substr(0, 1), "z").error(), error::invalid_argument);
  auto overlap = make<TypeParam>("aaa");
  ASSERT_TRUE(overlap.try_replace_all("aa", "b"));
  EXPECT_EQ(str(overlap), "ba");
}

TYPED_TEST(StringOps, TruncateSplitDrain) {
  auto s = make<TypeParam>("hello world");
  auto tail = s.try_split_off(5);
  ASSERT_TRUE(tail);
  EXPECT_EQ(str(s), "hello");
  EXPECT_EQ(str(*tail), " world");
  auto mid = make<TypeParam>("abcdef");
  auto drained = mid.try_drain(1, 3);
  ASSERT_TRUE(drained);
  EXPECT_EQ(str(*drained), "bcd");
  EXPECT_EQ(str(mid), "aef");
  EXPECT_EQ(mid.try_drain(9, 1).error(), error::out_of_bounds);
  mid.truncate(1);
  EXPECT_EQ(str(mid), "a");
  mid.truncate(10);
  EXPECT_EQ(str(mid), "a");
}

TYPED_TEST(StringOps, RetainDedupCase) {
  auto s = make<TypeParam>("a1b22c333");
  s.retain([](char c) noexcept { return c < '0' || c > '9'; });
  EXPECT_EQ(str(s), "abc");
  auto d = make<TypeParam>("aabbbcdd");
  d.dedup_chars();
  EXPECT_EQ(str(d), "abcd");
  auto c = make<TypeParam>("hello WORLD 1x");
  c.make_ascii_uppercase();
  EXPECT_EQ(str(c), "HELLO WORLD 1X");
  c.make_ascii_lowercase();
  EXPECT_EQ(str(c), "hello world 1x");
  c.make_ascii_titlecase();
  EXPECT_EQ(str(c), "Hello World 1x");
}

TYPED_TEST(StringOps, BuildingAndPadding) {
  auto s = make<TypeParam>("ab");
  ASSERT_TRUE(s.try_append_repeat("xy", 3));
  EXPECT_EQ(str(s), "abxyxyxy");
  ASSERT_TRUE(s.try_assign_repeat('z', 3));
  EXPECT_EQ(str(s), "zzz");
  ASSERT_TRUE(s.try_assign_repeat('q', 1));
  EXPECT_EQ(str(s), "q");
  auto p = make<TypeParam>("ab");
  ASSERT_TRUE(p.try_pad_left(5, '.'));
  EXPECT_EQ(str(p), "...ab");
  ASSERT_TRUE(p.try_pad_right(7, '_'));
  EXPECT_EQ(str(p), "...ab__");
  auto c = make<TypeParam>("ab");
  ASSERT_TRUE(c.try_center(7, '*'));
  EXPECT_EQ(str(c), "**ab***");
  ASSERT_TRUE(c.try_center(3));
  EXPECT_EQ(str(c), "**ab***");
  auto e = make<TypeParam>("");
  const std::array<char, 3> src{'x', 'y', 'z'};
  ASSERT_TRUE(e.try_extend_from(src));
  EXPECT_EQ(str(e), "xyz");
}

TYPED_TEST(StringOps, AppendInt) {
  auto s = make<TypeParam>("");
  ASSERT_TRUE(s.try_append_int(-12345));
  ASSERT_TRUE(s.try_append_int(255u, 16, true));
  ASSERT_TRUE(s.try_append_int(5, 2));
  ASSERT_TRUE(s.try_append_int(INT64_MIN));
  ASSERT_TRUE(s.try_append_int(UINT64_MAX));
  ASSERT_TRUE(s.try_append_int(0));
  EXPECT_EQ(str(s), "-12345FF101-922337203685477580818446744073709551615"
                    "0");
  EXPECT_EQ(s.try_append_int(1, 1).error(), error::invalid_argument);
}

TYPED_TEST(StringOps, HexBase64Url) {
  const std::array<std::uint8_t, 4> bytes{0xDE, 0xAD, 0xBE, 0xEF};
  auto h = make<TypeParam>("");
  ASSERT_TRUE(h.try_append_hex(bytes_of(bytes.data(), bytes.size())));
  EXPECT_EQ(str(h), "deadbeef");
  auto back = make<TypeParam>("");
  ASSERT_TRUE(back.try_append_from_hex("DEADbeef"));
  EXPECT_EQ(back.view().size(), 4u);
  EXPECT_EQ(static_cast<std::uint8_t>(back.view()[0]), 0xDE);
  EXPECT_EQ(back.try_append_from_hex("abc").error(), error::invalid_argument);
  EXPECT_EQ(back.try_append_from_hex("zz").error(), error::invalid_argument);
  EXPECT_EQ(back.view().size(), 4u);

  const std::array<std::uint8_t, 4> text{'f', 'o', 'o', 'b'};
  auto b = make<TypeParam>("");
  ASSERT_TRUE(b.try_append_base64(bytes_of(text.data(), 1)));
  ASSERT_TRUE(b.try_append_base64(bytes_of(text.data(), 2)));
  ASSERT_TRUE(b.try_append_base64(bytes_of(text.data(), 3)));
  ASSERT_TRUE(b.try_append_base64(bytes_of(text.data(), 4), false, false));
  EXPECT_EQ(str(b), "Zg==Zm8=Zm9vZm9vYg");
  const std::array<std::uint8_t, 2> hi{0xFB, 0xFF};
  auto u = make<TypeParam>("");
  ASSERT_TRUE(u.try_append_base64(bytes_of(hi.data(), 2), true));
  EXPECT_EQ(str(u), "-_8=");
  auto d = make<TypeParam>("");
  ASSERT_TRUE(d.try_append_from_base64("Zm9vYg=="));
  ASSERT_TRUE(d.try_append_from_base64("Zm9vYg"));
  EXPECT_EQ(str(d), "foobfoob");
  EXPECT_EQ(d.try_append_from_base64("Z").error(), error::invalid_argument);
  EXPECT_EQ(d.try_append_from_base64("Zm$v").error(), error::invalid_argument);
  EXPECT_EQ(str(d), "foobfoob");

  auto e = make<TypeParam>("");
  ASSERT_TRUE(e.try_append_url_encoded("a b&c=d/\xC3\xA9~"));
  EXPECT_EQ(str(e), "a%20b%26c%3Dd%2F%C3%A9~");
  auto dec = make<TypeParam>("");
  ASSERT_TRUE(dec.try_append_url_decoded("a%20b%26c%3Dd%2F%C3%A9~"));
  EXPECT_EQ(str(dec), "a b&c=d/\xC3\xA9~");
  EXPECT_EQ(dec.try_append_url_decoded("bad%2").error(), error::invalid_argument);
  EXPECT_EQ(dec.try_append_url_decoded("bad%zz").error(), error::invalid_argument);
  EXPECT_EQ(str(dec), "a b&c=d/\xC3\xA9~");
}

TYPED_TEST(StringOps, Unicode) {
  auto s = make<TypeParam>("");
  ASSERT_TRUE(s.try_append_char32(U'A'));
  ASSERT_TRUE(s.try_append_char32(0xE9));
  ASSERT_TRUE(s.try_append_char32(0x20AC));
  ASSERT_TRUE(s.try_append_char32(0x1F600));
  EXPECT_EQ(str(s), "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
  EXPECT_EQ(s.try_append_char32(0xD800).error(), error::invalid_argument);
  EXPECT_EQ(s.try_append_char32(0x110000).error(), error::invalid_argument);

  auto v = make<TypeParam>("ok ");
  ASSERT_TRUE(v.try_append_utf8("\xC3\xA9"));
  EXPECT_EQ(v.try_append_utf8("\xC3").error(), error::invalid_argument);
  EXPECT_EQ(str(v), "ok \xC3\xA9");

  auto l = make<TypeParam>("");
  ASSERT_TRUE(l.try_append_utf8_lossy("a\xFF"
                                      "b\xC3"
                                      "c"));
  EXPECT_EQ(str(l), "a\xEF\xBF\xBD"
                    "b\xEF\xBF\xBD"
                    "c");

  const std::array<char16_t, 4> u16{u'a', 0xD83D, 0xDE00, 0x20AC};
  auto w = make<TypeParam>("");
  ASSERT_TRUE(w.try_append_utf16(reloco::span<const char16_t>(u16.data(), u16.size())));
  EXPECT_EQ(str(w), "a\xF0\x9F\x98\x80\xE2\x82\xAC");
  const std::array<char16_t, 1> lone{0xD83D};
  EXPECT_EQ(w.try_append_utf16(reloco::span<const char16_t>(lone.data(), 1)).error(), error::invalid_argument);
  EXPECT_EQ(str(w), "a\xF0\x9F\x98\x80\xE2\x82\xAC");

  auto length = w.try_utf16_length();
  ASSERT_TRUE(length.has_value());
  EXPECT_EQ(*length, 4u);
  std::array<char16_t, 8> out{};
  auto n = w.try_to_utf16_into(reloco::span<char16_t>(out.data(), out.size()));
  ASSERT_TRUE(n);
  EXPECT_EQ(*n, 4u);
  EXPECT_EQ(out[1], 0xD83D);
  EXPECT_EQ(out[2], 0xDE00);
  EXPECT_EQ(out[3], 0x20AC);
  EXPECT_EQ(w.try_to_utf16_into(reloco::span<char16_t>(out.data(), 2)).error(), error::out_of_bounds);
}

namespace {
struct point {
  int x;
  int y;
};
} // namespace

template <> struct reloco::Display<point> {
  static void format(const point &p, const reloco::sink &out) noexcept {
    out.write("(1,2)");
    (void)p;
  }
};

TYPED_TEST(StringOps, EscapeAndDisplay) {
  auto s = make<TypeParam>("");
  ASSERT_TRUE(s.try_append_escaped(reloco::string_view("a\tb\n\"q\"\\'\x01\x7f\xC3\xA9", 13)));
  EXPECT_EQ(str(s), "a\\tb\\n\\\"q\\\"\\\\\\'\\x01\\x7f\xC3\xA9");
  auto u = make<TypeParam>("");
  ASSERT_TRUE(u.try_append_unescaped(s.view()));
  EXPECT_EQ(str(u), std::string("a\tb\n\"q\"\\'\x01\x7f\xC3\xA9"));
  EXPECT_EQ(u.try_append_unescaped("bad\\").error(), error::invalid_argument);
  EXPECT_EQ(u.try_append_unescaped("\\q").error(), error::invalid_argument);
  EXPECT_EQ(u.try_append_unescaped("\\x4").error(), error::invalid_argument);
  EXPECT_EQ(str(u), std::string("a\tb\n\"q\"\\'\x01\x7f\xC3\xA9"));
  auto d = make<TypeParam>("p=");
  ASSERT_TRUE(d.try_append_display(point{1, 2}));
  EXPECT_EQ(str(d), "p=(1,2)");
}

TEST(StringOpsLimits, InlineCapacityIsReportedAndRolledBack) {
  auto s = reloco::inline_string<8>::try_create("abcd");
  ASSERT_TRUE(s);
  EXPECT_FALSE(s->try_append_repeat("xy", 3));
  EXPECT_EQ(s->view(), reloco::string_view("abcd"));
  EXPECT_FALSE(s->try_replace_all("a", "123456789"));
  EXPECT_EQ(s->view(), reloco::string_view("abcd"));
  EXPECT_FALSE(s->try_pad_left(9));
  EXPECT_EQ(s->view(), reloco::string_view("abcd"));
  const std::array<std::uint8_t, 3> three{1, 2, 3};
  EXPECT_FALSE(s->try_append_display(point{1, 2}));
  EXPECT_EQ(s->view(), reloco::string_view("abcd"));
  EXPECT_FALSE(s->try_append_hex(bytes_of(three.data(), three.size())));
  EXPECT_EQ(s->view(), reloco::string_view("abcd"));
}

TEST(StringOpsWide, Char16AndChar32Encoders) {
  auto w16 = reloco::basic_string<char16_t>::try_create();
  ASSERT_TRUE(w16);
  ASSERT_TRUE(w16->try_append_char32(0x1F600));
  EXPECT_EQ(w16->size(), 2u);
  auto w32 = reloco::basic_string<char32_t>::try_create();
  ASSERT_TRUE(w32);
  ASSERT_TRUE(w32->try_append_char32(0x1F600));
  EXPECT_EQ(w32->size(), 1u);
  ASSERT_TRUE(w32->try_append_int(-7));
  EXPECT_EQ(w32->size(), 3u);
}
