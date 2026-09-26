// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/fmt.hpp>

#include <string>

namespace {

struct point {
  int x;
  int y;
};

struct undecorated {};

void append_sink(void *ctx, reloco::string_view sv) noexcept {
  static_cast<std::string *>(ctx)->append(sv.data(), sv.size());
}

reloco::sink make_string_sink(std::string &out) noexcept { return reloco::sink{&out, &append_sink}; }

} // namespace

template <> struct reloco::Display<point> {
  static void format(const point &p, const reloco::sink &out) noexcept {
    out.write("(");
    out.put(static_cast<char>('0' + p.x));
    out.write(", ");
    out.put(static_cast<char>('0' + p.y));
    out.write(")");
  }
};

template <> struct reloco::Debug<point> {
  static void format(const point &p, const reloco::sink &out) noexcept {
    out.write("point { x: ");
    out.put(static_cast<char>('0' + p.x));
    out.write(", y: ");
    out.put(static_cast<char>('0' + p.y));
    out.write(" }");
  }
};

TEST(FmtTest, HasDisplayDetectsSpecialization) {
  EXPECT_TRUE(reloco::has_display_v<point>);
  EXPECT_FALSE(reloco::has_display_v<undecorated>);
  EXPECT_FALSE(reloco::has_display_v<int>);
}

TEST(FmtTest, HasDebugDetectsSpecialization) {
  EXPECT_TRUE(reloco::has_debug_v<point>);
  EXPECT_FALSE(reloco::has_debug_v<undecorated>);
  EXPECT_FALSE(reloco::has_debug_v<int>);
}

TEST(FmtTest, DisplayFormatsThroughSink) {
  std::string out;
  reloco::sink s = make_string_sink(out);
  reloco::Display<point>::format(point{1, 2}, s);
  EXPECT_EQ(out, "(1, 2)");
}

TEST(FmtTest, DebugFormatsThroughSink) {
  std::string out;
  reloco::sink s = make_string_sink(out);
  reloco::Debug<point>::format(point{1, 2}, s);
  EXPECT_EQ(out, "point { x: 1, y: 2 }");
}

TEST(FmtTest, SinkWriteIsNoOpWithoutWriteFn) {
  reloco::sink s;
  s.write("hello"); // Must not crash: write_fn is null.
  s.put('x');
  SUCCEED();
}

TEST(FmtTest, SinkWriteIgnoresEmptyView) {
  bool called = false;
  reloco::sink s;
  s.ctx = &called;
  s.write_fn = [](void *ctx, reloco::string_view) noexcept { *static_cast<bool *>(ctx) = true; };
  s.write(reloco::string_view{});
  EXPECT_FALSE(called);
}

TEST(FmtTest, SinkPushBackSupportsBackInserterStyleUsage) {
  std::string out;
  reloco::sink s = make_string_sink(out);
  s.push_back('a');
  s.push_back('b');
  EXPECT_EQ(out, "ab");
}
