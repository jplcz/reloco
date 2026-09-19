// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/span.hpp>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

template <typename Span, typename = void> struct can_index_temporary_span : std::false_type {};

template <typename Span>
struct can_index_temporary_span<Span, std::void_t<decltype(std::declval<Span &&>()[0])>> : std::true_type {};

template <typename Span, typename = void> struct can_borrow_temporary_span_data : std::false_type {};

template <typename Span>
struct can_borrow_temporary_span_data<Span, std::void_t<decltype(std::declval<Span &&>().data())>> : std::true_type {};

template <typename Span, typename = void> struct can_iterate_temporary_span : std::false_type {};

template <typename Span>
struct can_iterate_temporary_span<Span, std::void_t<decltype(std::declval<Span &&>().begin())>> : std::true_type {};

template <typename Span, typename = void> struct can_borrow_temporary_span_unsafe_front : std::false_type {};

template <typename Span>
struct can_borrow_temporary_span_unsafe_front<Span, std::void_t<decltype(std::declval<Span &&>().unsafe_front())>>
    : std::true_type {};

template <typename Span, typename = void> struct can_borrow_temporary_span_unsafe_back : std::false_type {};

template <typename Span>
struct can_borrow_temporary_span_unsafe_back<Span, std::void_t<decltype(std::declval<Span &&>().unsafe_back())>>
    : std::true_type {};

static_assert(!can_index_temporary_span<reloco::span<int>>::value);
static_assert(!can_borrow_temporary_span_data<reloco::span<int>>::value);
static_assert(!can_iterate_temporary_span<reloco::span<int>>::value);
static_assert(!can_borrow_temporary_span_unsafe_front<reloco::span<int>>::value);
static_assert(!can_borrow_temporary_span_unsafe_back<reloco::span<int>>::value);

} // namespace

TEST(SpanTest, DefaultAndPointerConstruction) {
  reloco::span<char> empty_span;
  EXPECT_TRUE(empty_span.empty());
  EXPECT_EQ(empty_span.size(), 0u);
  EXPECT_EQ(empty_span.data(), nullptr);

  constexpr char raw_arr[] = "0123456789abcdef";
  reloco::span<const char> arr_span(raw_arr, sizeof(raw_arr) - 1);
  EXPECT_FALSE(arr_span.empty());
  EXPECT_EQ(arr_span.size(), 16u);
  EXPECT_EQ(arr_span.data(), raw_arr);
  EXPECT_EQ(arr_span[0], '0');
  EXPECT_EQ(arr_span[15], 'f');

  reloco::span<const char> null_with_size(nullptr, 16);
  EXPECT_TRUE(null_with_size.empty());
  EXPECT_EQ(null_with_size.data(), nullptr);
}

TEST(SpanTest, ArrayDeductionAndIterators) {
  int nums[] = {10, 20, 30, 40};
  reloco::span<int> s(nums);

  EXPECT_EQ(s.size(), 4u);
  EXPECT_EQ(*s.begin(), 10);

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

  EXPECT_EQ(*(s.end() - 1), 40);

  RELOCO_END_UNSAFE_BUFFER_USAGE

  size_t count = 0;
  for (int n : s) {
    count += static_cast<size_t>(n);
  }
  EXPECT_EQ(count, 100u);

  const reloco::span<int> const_view(nums);
  *const_view.begin() = 11;
  EXPECT_EQ(nums[0], 11);
  EXPECT_EQ(*const_view.cbegin(), 11);
}

TEST(SpanTest, ProvidesCheckedAndFallibleAccess) {
  int values[] = {10, 20, 30, 40};
  reloco::span<int> view(values);

  EXPECT_EQ(view.front(), 10);
  EXPECT_EQ(view.back(), 40);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(view.unsafe_at(2), 30);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(*view.rbegin(), 40);

  ASSERT_TRUE(view.try_at(1).has_value());
  EXPECT_EQ(view.try_at(1).value().get(), 20);
  EXPECT_FALSE(view.try_at(view.size()).has_value());
  EXPECT_EQ(view.try_at(view.size()).error(), reloco::span_error::out_of_bounds);

  ASSERT_TRUE(view.try_front().has_value());
  EXPECT_EQ(view.try_front().value().get(), 10);
  ASSERT_TRUE(view.try_back().has_value());
  EXPECT_EQ(view.try_back().value().get(), 40);
  ASSERT_TRUE(view.try_data().has_value());
  EXPECT_EQ(view.try_data().value(), values);

  reloco::span<int> empty;
  EXPECT_FALSE(empty.try_data().has_value());
  EXPECT_EQ(empty.try_data().error(), reloco::span_error::container_empty);
  EXPECT_FALSE(empty.try_front().has_value());
  EXPECT_FALSE(empty.try_back().has_value());
}

TEST(SpanTest, ProvidesCheckedAndFallibleSubviews) {
  uint32_t values[] = {0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00};
  reloco::span<uint32_t> view(values);

  const auto first = view.first(2);
  EXPECT_EQ(first.size(), 2u);
  EXPECT_EQ(first.front(), values[0]);

  const auto last = view.last(2);
  EXPECT_EQ(last.size(), 2u);
  EXPECT_EQ(last.front(), values[2]);

  ASSERT_TRUE(view.try_subspan(1, 2).has_value());
  const auto middle = view.try_subspan(1, 2).value();
  EXPECT_EQ(middle.size(), 2u);
  EXPECT_EQ(middle.front(), values[1]);
  EXPECT_FALSE(view.try_subspan(3, 2).has_value());
  EXPECT_FALSE(view.try_first(5).has_value());
  EXPECT_FALSE(view.try_last(5).has_value());

  const auto bytes = view.as_bytes();
  EXPECT_EQ(bytes.size(), sizeof(values));
  EXPECT_EQ(bytes.data(), reinterpret_cast<const std::byte *>(values));

  reloco::span<const uint32_t> const_view = view;
  EXPECT_EQ(const_view.data(), values);
  EXPECT_EQ(const_view.size_bytes(), sizeof(values));
}

#if RELOCO_HAS_STD_SPAN
TEST(SpanTest, StdSpanInteroperability) {
  char data[] = "interop_test";
  std::span<char> std_s(data, sizeof(data));

  // Implicit construct from std::span
  reloco::span<char> custom_s(std_s);
  EXPECT_EQ(custom_s.size(), std_s.size());
  EXPECT_EQ(custom_s.data(), std_s.data());

  // Conversion to std::span
  std::span<char> roundtrip = custom_s.operator std::span<char>();
  EXPECT_EQ(roundtrip.data(), data);

  static_assert(!std::is_convertible_v<reloco::span<char> &&, std::span<char>>);
}
#endif
