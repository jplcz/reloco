// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/span.hpp>

#include <cstddef>
#include <string>
#include <tuple>
#include <vector>

namespace {

using ints = std::vector<int>;

// Results of hardened accessors cannot be dereferenced as temporaries; this names the temporary for one expression.
ints vec(reloco::span<int> s) { return ints(s.begin(), s.end()); }

template <typename R> R &lv(R &&value) { return value; }

template <typename Range> std::vector<ints> pieces(const Range &range) {
  std::vector<ints> out;
  for (auto piece : range) {
    out.emplace_back(piece.begin(), piece.end());
  }
  return out;
}

TEST(SpanSliceOps, SplitFirstAndLast) {
  int data[] = {1, 2, 3};
  reloco::span<int> s(data);
  auto [first, rest] = s.split_first();
  EXPECT_EQ(first, 1);
  EXPECT_EQ(rest.size(), 2u);
  auto [last, init] = s.split_last();
  EXPECT_EQ(last, 3);
  EXPECT_EQ(init.size(), 2u);
  first = 10; // references into the viewed data
  EXPECT_EQ(data[0], 10);

  EXPECT_EQ(lv(s.try_split_first())->first.get(), 10);
  EXPECT_EQ(lv(s.try_split_last())->second.size(), 2u);
  reloco::span<int> empty;
  EXPECT_EQ(empty.try_split_first().error(), reloco::error::container_empty);
  EXPECT_EQ(empty.try_split_last().error(), reloco::error::container_empty);
}

TEST(SpanSliceOps, SplitOffAdvancesTheView) {
  int data[] = {1, 2, 3, 4, 5};
  reloco::span<int> s(data);
  auto head = s.split_off_first(2);
  EXPECT_EQ(head.size(), 2u);
  EXPECT_EQ(head[1], 2);
  EXPECT_EQ(s.size(), 3u);
  EXPECT_EQ(s[0], 3);
  auto tail = s.split_off_last(1);
  EXPECT_EQ(tail[0], 5);
  EXPECT_EQ(s.size(), 2u);
  EXPECT_EQ(s.try_split_off_first(3).error(), reloco::error::out_of_bounds);
  EXPECT_EQ(s.try_split_off_last(3).error(), reloco::error::out_of_bounds);
  EXPECT_EQ(s.size(), 2u);
  ASSERT_TRUE(s.try_split_off_first(2).has_value());
  EXPECT_TRUE(s.empty());
}

TEST(SpanSliceOps, ChunksExactAndRchunks) {
  int data[] = {1, 2, 3, 4, 5, 6, 7};
  reloco::span<int> s(data);
  EXPECT_EQ(pieces(s.chunks_exact(3)), (std::vector<ints>{{1, 2, 3}, {4, 5, 6}}));
  EXPECT_EQ(s.chunks_exact_remainder(3).size(), 1u);
  EXPECT_EQ(lv(s.chunks_exact_remainder(3))[0], 7);
  EXPECT_EQ(pieces(s.rchunks(3)), (std::vector<ints>{{5, 6, 7}, {2, 3, 4}, {1}}));
  EXPECT_EQ(s.rchunks(3).size(), 3u);
  EXPECT_TRUE(reloco::span<int>().rchunks(2).empty());
}

TEST(SpanSliceOps, SplitFamily) {
  int data[] = {1, 0, 2, 3, 0, 4};
  reloco::span<int> s(data);
  auto is_zero = [](int v) { return v == 0; };
  EXPECT_EQ(pieces(s.split(is_zero)), (std::vector<ints>{{1}, {2, 3}, {4}}));
  EXPECT_EQ(pieces(s.rsplit(is_zero)), (std::vector<ints>{{4}, {2, 3}, {1}}));
  EXPECT_EQ(pieces(s.splitn(2, is_zero)), (std::vector<ints>{{1}, {2, 3, 0, 4}}));
  EXPECT_TRUE(pieces(s.splitn(0, is_zero)).empty());

  int edges[] = {0, 5, 0};
  EXPECT_EQ(pieces(reloco::span<int>(edges).split(is_zero)), (std::vector<ints>{{}, {5}, {}}));
  EXPECT_EQ(pieces(reloco::span<int>().split(is_zero)), (std::vector<ints>{{}})); // one empty piece
}

TEST(SpanSliceOps, PrefixSuffix) {
  int data[] = {1, 2, 3, 4};
  reloco::span<int> s(data);
  int pre[] = {1, 2};
  int suf[] = {3, 4};
  int bad[] = {2, 3};
  EXPECT_TRUE(s.starts_with(pre));
  EXPECT_FALSE(s.starts_with(bad));
  EXPECT_TRUE(s.ends_with(suf));
  EXPECT_FALSE(s.ends_with(pre));
  auto stripped = s.strip_prefix(pre);
  ASSERT_TRUE(stripped.has_value());
  EXPECT_EQ(stripped->size(), 2u);
  EXPECT_EQ((*stripped)[0], 3);
  EXPECT_FALSE(s.strip_prefix(bad).has_value());
  auto stripped_suffix = s.strip_suffix(suf);
  ASSERT_TRUE(stripped_suffix.has_value());
  EXPECT_EQ(stripped_suffix->size(), 2u);
  EXPECT_FALSE(s.strip_suffix(bad).has_value());
  EXPECT_TRUE(s.starts_with(reloco::span<const int>())); // empty prefix always matches
}

TEST(SpanSliceOps, Searching) {
  int sorted[] = {1, 2, 2, 2, 5, 8};
  reloco::span<int> s(sorted);
  EXPECT_EQ(s.partition_point([](int v) { return v < 5; }), 4u);
  EXPECT_EQ(s.lower_bound(2), 1u);
  EXPECT_EQ(s.upper_bound(2), 4u);
  EXPECT_EQ(s.lower_bound(100), 6u);
  EXPECT_EQ(*s.rposition(2), 3u);
  EXPECT_FALSE(s.rposition(7).has_value());
  EXPECT_EQ(*s.rposition_if([](int v) { return v < 3; }), 3u);

  EXPECT_EQ(lv(s.try_find(5))->get(), 5);
  EXPECT_EQ(s.try_find(7).error(), reloco::error::not_found);
  lv(s.try_find_if([](int v) { return v > 4; }))->get() = 50; // reference into the data
  EXPECT_EQ(s[4], 50);

  EXPECT_TRUE(s.any([](int v) { return v == 8; }));
  EXPECT_FALSE(s.all([](int v) { return v < 8; }));
  EXPECT_TRUE(reloco::span<int>().all([](int) { return false; }));
  EXPECT_EQ(s.count(2), 3u);
  EXPECT_EQ(s.count_if([](int v) { return v > 1; }), 5u);
}

TEST(SpanSliceOps, MinMaxFamily) {
  struct item {
    int key;
    int tag;
  };
  item data[] = {{3, 0}, {1, 1}, {3, 2}, {1, 3}};
  reloco::span<item> s(data);
  EXPECT_EQ(lv(s.try_max_by_key([](const item &i) { return i.key; }))->get().tag, 2); // last maximum
  EXPECT_EQ(lv(s.try_min_by_key([](const item &i) { return i.key; }))->get().tag, 1); // first minimum
  auto by_tag = [](const item &a, const item &b) { return a.tag < b.tag; };
  EXPECT_EQ(lv(s.try_max_by(by_tag))->get().tag, 3);
  EXPECT_EQ(lv(s.try_min_by(by_tag))->get().tag, 0);

  int values[] = {4, 9, 2, 9, 2};
  reloco::span<int> v(values);
  EXPECT_EQ(lv(v.try_max())->get(), 9);
  EXPECT_EQ(&lv(v.try_max())->get(), &v[3]);
  EXPECT_EQ(&lv(v.try_min())->get(), &v[2]);
  auto mm = v.try_minmax();
  ASSERT_TRUE(mm.has_value());
  EXPECT_EQ(mm->first.get(), 2);
  EXPECT_EQ(mm->second.get(), 9);
  EXPECT_EQ(reloco::span<int>().try_min().error(), reloco::error::container_empty);
  EXPECT_EQ(reloco::span<int>().try_minmax().error(), reloco::error::container_empty);
}

TEST(SpanSliceOps, SortedChecksAndSelectNth) {
  int sorted[] = {1, 2, 2, 5};
  int unsorted[] = {3, 1, 2};
  EXPECT_TRUE(reloco::span<int>(sorted).is_sorted());
  EXPECT_FALSE(reloco::span<int>(unsorted).is_sorted());
  EXPECT_TRUE(reloco::span<int>(unsorted).is_sorted_by([](int a, int b) { return a > b; }) == false);
  EXPECT_TRUE(reloco::span<int>(sorted).is_sorted_by_key([](int v) { return v / 2; }));
  EXPECT_TRUE(reloco::span<int>().is_sorted());

  int data[] = {9, 1, 8, 2, 7, 3};
  reloco::span<int> s(data);
  auto [before, nth, after] = s.select_nth_unstable(2);
  EXPECT_EQ(nth, 3);
  EXPECT_EQ(before.size(), 2u);
  EXPECT_EQ(after.size(), 3u);
  for (int v : before)
    EXPECT_LE(v, 3);
  for (int v : after)
    EXPECT_GE(v, 3);
  EXPECT_EQ(s.try_select_nth_unstable(6).error(), reloco::error::out_of_bounds);
  EXPECT_EQ(std::get<1>(*lv(s.try_select_nth_unstable(0))).get(), 1);
}

TEST(SpanSliceOps, ReverseRotateFillSwap) {
  int data[] = {1, 2, 3, 4, 5};
  reloco::span<int> s(data);
  s.reverse();
  EXPECT_EQ(ints(s.begin(), s.end()), (ints{5, 4, 3, 2, 1}));
  s.rotate_left(2);
  EXPECT_EQ(ints(s.begin(), s.end()), (ints{3, 2, 1, 5, 4}));
  s.rotate_right(1);
  EXPECT_EQ(ints(s.begin(), s.end()), (ints{4, 3, 2, 1, 5}));
  EXPECT_EQ(s.try_rotate_left(6).error(), reloco::error::out_of_bounds);
  EXPECT_EQ(s.try_rotate_right(6).error(), reloco::error::out_of_bounds);
  EXPECT_TRUE(s.try_rotate_left(5).has_value());

  s.swap_at(0, 4);
  EXPECT_EQ(s[0], 5);
  EXPECT_EQ(s[4], 4);
  EXPECT_EQ(s.try_swap_at(0, 5).error(), reloco::error::out_of_bounds);

  s.fill(7);
  EXPECT_TRUE(s.all([](int v) { return v == 7; }));
  int next = 0;
  s.fill_with([&next] { return next++; });
  EXPECT_EQ(ints(s.begin(), s.end()), (ints{0, 1, 2, 3, 4}));
}

TEST(SpanSliceOps, CopyAndSwapSpans) {
  int a[] = {1, 2, 3};
  int b[] = {7, 8, 9};
  reloco::span<int> sa(a);
  reloco::span<int> sb(b);
  sa.swap_with_span(sb);
  EXPECT_EQ(a[0], 7);
  EXPECT_EQ(sb[2], 3);
  sa.copy_from(reloco::span<const int>(b));
  EXPECT_EQ(vec(reloco::span<int>(a)), (ints{1, 2, 3}));

  int shorter[] = {1, 2};
  EXPECT_EQ(sa.try_copy_from(reloco::span<const int>(shorter)).error(), reloco::error::invalid_argument);
  EXPECT_EQ(sa.try_swap_with_span(reloco::span<int>(shorter)).error(), reloco::error::invalid_argument);
  EXPECT_EQ(sa.try_copy_from(sa).error(), reloco::error::invalid_argument); // overlap
  EXPECT_TRUE(sa.overlaps(sa.subspan(1)));
  EXPECT_FALSE(sa.first(1).overlaps(sa.subspan(1)));
}

TEST(SpanSliceOps, CopyWithin) {
  int data[] = {1, 2, 3, 4, 5};
  reloco::span<int> s(data);
  s.copy_within(0, 3, 2); // overlapping, moving right
  EXPECT_EQ(vec(reloco::span<int>(data)), (ints{1, 2, 1, 2, 3}));
  s.copy_within(2, 3, 0); // overlapping, moving left
  EXPECT_EQ(vec(reloco::span<int>(data)), (ints{1, 2, 3, 2, 3}));
  EXPECT_EQ(s.try_copy_within(3, 3, 0).error(), reloco::error::out_of_bounds);
  EXPECT_EQ(s.try_copy_within(0, 3, 3).error(), reloco::error::out_of_bounds);
  EXPECT_TRUE(s.try_copy_within(0, 0, 5).has_value());
}

TEST(SpanSliceOps, PartitionForEachFold) {
  int data[] = {1, 2, 3, 4, 5, 6};
  reloco::span<int> s(data);
  auto even = [](int v) { return v % 2 == 0; };
  EXPECT_EQ(s.stable_partition(even), 3u);
  EXPECT_EQ(vec(reloco::span<int>(data)), (ints{2, 4, 6, 1, 3, 5}));
  EXPECT_EQ(s.partition([](int v) { return v > 3; }), 3u);
  EXPECT_TRUE(s.first(3).all([](int v) { return v > 3; }));

  int sum = 0;
  s.for_each([&sum](int v) { sum += v; });
  EXPECT_EQ(sum, 21);
  EXPECT_EQ(s.fold(0, [](int acc, int v) { return acc + v; }), 21);
  EXPECT_EQ(s.fold(std::size_t{0}, [](std::size_t acc, int) { return acc + 1; }), 6u);
}

TEST(SpanSliceOps, Comparison) {
  int a[] = {1, 2, 3};
  int b[] = {1, 2, 4};
  int c[] = {1, 2};
  reloco::span<int> sa(a);
  EXPECT_TRUE(sa.equals(reloco::span<const int>(a)));
  EXPECT_FALSE(sa.equals(reloco::span<const int>(c)));
  EXPECT_LT(sa.compare(reloco::span<const int>(b)), 0);
  EXPECT_GT(reloco::span<int>(b).compare(reloco::span<const int>(a)), 0);
  EXPECT_GT(sa.compare(reloco::span<const int>(c)), 0); // prefix is smaller
  EXPECT_EQ(sa.compare(reloco::span<const int>(a)), 0);
}

TEST(SpanSliceOps, AsciiHelpers) {
  char text[] = "Hello, World";
  reloco::span<char> s(text, 12);
  EXPECT_TRUE(s.is_ascii());
  char upper[] = "HELLO, WORLD";
  EXPECT_TRUE(s.eq_ignore_ascii_case(reloco::span<const char>(upper, 12)));
  s.make_ascii_uppercase();
  EXPECT_EQ(std::string(text, 12), "HELLO, WORLD");
  s.make_ascii_lowercase();
  EXPECT_EQ(std::string(text, 12), "hello, world");

  unsigned char high[] = {0x41, 0xC3, 0xA9};
  EXPECT_FALSE(reloco::span<unsigned char>(high).is_ascii());
  std::byte raw[] = {std::byte{'a'}};
  reloco::span<std::byte> rs(raw);
  rs.make_ascii_uppercase();
  EXPECT_EQ(raw[0], std::byte{'A'});
}

} // namespace
