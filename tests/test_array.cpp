// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <reloco/array.hpp>

#include <type_traits>
#include <utility>

namespace {

template <typename Array, typename = void> struct can_index_temporary_array : std::false_type {};

template <typename Array>
struct can_index_temporary_array<Array, std::void_t<decltype(std::declval<Array &&>()[0])>> : std::true_type {};

template <typename Array, typename = void> struct can_borrow_temporary_array_data : std::false_type {};

template <typename Array>
struct can_borrow_temporary_array_data<Array, std::void_t<decltype(std::declval<Array &&>().data())>> : std::true_type {
};

template <typename Array, typename = void> struct can_iterate_temporary_array : std::false_type {};

template <typename Array>
struct can_iterate_temporary_array<Array, std::void_t<decltype(std::declval<Array &&>().begin())>> : std::true_type {};

template <typename Array, typename = void> struct can_get_temporary_array_element : std::false_type {};

template <typename Array>
struct can_get_temporary_array_element<Array, std::void_t<decltype(reloco::get<0>(std::declval<Array &&>()))>>
    : std::true_type {};

template <typename T, typename U, typename = void> struct can_deduce_mixed_array : std::false_type {};

template <typename T, typename U>
struct can_deduce_mixed_array<T, U, std::void_t<decltype(reloco::array{std::declval<T>(), std::declval<U>()})>>
    : std::true_type {};

static_assert(!can_index_temporary_array<reloco::array<int, 2>>::value);
static_assert(!can_borrow_temporary_array_data<reloco::array<int, 2>>::value);
static_assert(!can_iterate_temporary_array<reloco::array<int, 2>>::value);
static_assert(!can_get_temporary_array_element<reloco::array<int, 2>>::value);
static_assert(!can_deduce_mixed_array<int, short>::value);
static_assert(std::tuple_size<reloco::array<int, 3>>::value == 3);

} // namespace

TEST(ArrayTest, SupportsAggregateAccessAndStructuredBindings) {
  reloco::array values{1, 2, 3};

  EXPECT_EQ(values.size(), 3u);
  EXPECT_FALSE(values.empty());
  EXPECT_EQ(values.front(), 1);
  EXPECT_EQ(values.back(), 3);

  values[1] = 7;
  ASSERT_TRUE(values.try_at(1).has_value());
  EXPECT_EQ(values.try_at(1).value().get(), 7);
  EXPECT_FALSE(values.try_at(values.size()).has_value());

  const auto [first, second, third] = values;
  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 7);
  EXPECT_EQ(third, 3);
}

TEST(ArrayTest, ProvidesSpanSubviewsAndMapping) {
  reloco::array<int, 4> values{1, 2, 3, 4};

  const auto view = values.as_span();
  EXPECT_EQ(view.data(), values.data());
  EXPECT_EQ(view.size(), values.size());

  const auto middle = values.static_subspan<1, 2>();
  EXPECT_EQ(middle.size(), 2u);
  EXPECT_EQ(middle.front(), 2);
  EXPECT_EQ(middle.back(), 3);

  const auto doubled = values.map([](int value) noexcept { return value * 2; });
  EXPECT_EQ(doubled[0], 2);
  EXPECT_EQ(doubled[3], 8);
}

TEST(ArrayTest, SupportsFillSwapComparisonAndZeroSize) {
  reloco::array<int, 3> lhs{};
  reloco::array<int, 3> rhs{};
  lhs.fill(4);
  rhs.fill(9);

  EXPECT_LT(lhs, rhs);
  lhs.swap(rhs);
  EXPECT_EQ(lhs.front(), 9);
  EXPECT_EQ(rhs.front(), 4);

  reloco::array<int, 0> empty{};
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty.size(), 0u);
  EXPECT_EQ(empty.data(), nullptr);
  EXPECT_EQ(empty.begin(), empty.end());
  EXPECT_TRUE(empty.as_span().empty());
  EXPECT_FALSE(empty.try_at(0).has_value());
  EXPECT_FALSE(empty.try_front().has_value());
  EXPECT_FALSE(empty.try_back().has_value());
  static_assert(std::is_same_v<decltype(std::declval<reloco::array<int, 0> &>().unsafe_at(0)), int &>);

  const auto mapped = empty.map([](int value) noexcept { return static_cast<long>(value); });
  static_assert(std::is_same_v<decltype(mapped), const reloco::array<long, 0>>);
  EXPECT_TRUE(mapped.empty());
}
