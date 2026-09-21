// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <reloco/array.hpp>
#include <reloco/collection_view.hpp>
#include <reloco/lifetime.hpp>

#include <array>
#include <type_traits>
#include <utility>

using reloco::collection_view;
using reloco::mutable_collection_view;

namespace {

template <typename Container, typename = void> struct can_bind_mutable_view : std::false_type {};

template <typename Container>
struct can_bind_mutable_view<Container,
                             std::void_t<decltype(mutable_collection_view<int>(std::declval<Container &>()))>>
    : std::true_type {};

} // namespace

TEST(CollectionViewTest, ReadOnlyViewOverRelocoArrayReportsSizeAndAccess) {
  reloco::array<int, 3> a{1, 2, 3};

  collection_view<int> cv(a);
  static_assert(std::is_same_v<decltype(cv.at(0)), const int &>);
  static_assert(std::is_same_v<decltype(cv.data()), const int *>);

  EXPECT_EQ(cv.size(), 3u);
  EXPECT_FALSE(cv.empty());
  EXPECT_TRUE(cv.is_random_access());
  EXPECT_TRUE(cv.supports_direct_access());
  EXPECT_EQ(cv.at(0), 1);
  EXPECT_EQ(cv.at(2), 3);
  ASSERT_TRUE(cv.data() != nullptr);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(cv.data()[1], 2);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

TEST(CollectionViewTest, DefaultConstructedViewIsEmptyAndUnbound) {
  collection_view<int> cv;
  EXPECT_EQ(cv.size(), 0u);
  EXPECT_TRUE(cv.empty());
  EXPECT_FALSE(cv.is_random_access());
  EXPECT_FALSE(cv.supports_direct_access());
  EXPECT_FALSE(cv.try_at(0).has_value());
  EXPECT_FALSE(cv.try_data().has_value());
}

TEST(CollectionViewTest, TryAtReportsOutOfBounds) {
  reloco::array<int, 2> a{10, 20};
  collection_view<int> cv(a);

  auto ok = cv.try_at(1);
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok->get(), 20);

  auto oob = cv.try_at(2);
  ASSERT_FALSE(oob.has_value());
  EXPECT_EQ(oob.error(), reloco::error::out_of_bounds);
}

TEST(CollectionViewTest, ForEachVisitsEveryElementInOrderReadOnly) {
  reloco::array<int, 4> a{1, 2, 3, 4};
  collection_view<int> cv(a);

  int sum = 0;
  std::size_t count = 0;
  cv.for_each([&](const int &v) {
    sum += v;
    ++count;
  });
  EXPECT_EQ(sum, 10);
  EXPECT_EQ(count, 4u);
}

TEST(CollectionViewTest, BindsToConstRelocoArray) {
  const reloco::array<int, 3> a{7, 8, 9};
  collection_view<int> cv(a);
  EXPECT_EQ(cv.at(0), 7);
  EXPECT_EQ(cv.at(2), 9);
}

TEST(CollectionViewTest, ReadOnlyViewOverStdArrayReportsSizeAndAccess) {
  std::array<int, 3> a{4, 5, 6};

  collection_view<int> cv(a);
  EXPECT_EQ(cv.size(), 3u);
  EXPECT_TRUE(cv.is_random_access());
  EXPECT_TRUE(cv.supports_direct_access());
  EXPECT_EQ(cv.at(1), 5);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(cv.data()[2], 6);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

TEST(CollectionViewTest, MutableViewOverRelocoArrayMutatesThroughAt) {
  reloco::array<int, 3> a{1, 2, 3};
  mutable_collection_view<int> mcv(a);

  static_assert(std::is_same_v<decltype(mcv.at(0)), int &>);
  static_assert(std::is_same_v<decltype(mcv.data()), int *>);

  mcv.at(0) = 100;
  EXPECT_EQ(a[0], 100);

  auto at_result = mcv.try_at(1);
  ASSERT_TRUE(at_result.has_value());
  at_result->get() = 200;
  EXPECT_EQ(a[1], 200);
}

TEST(CollectionViewTest, MutableViewOverRelocoArrayMutatesThroughData) {
  reloco::array<int, 3> a{1, 2, 3};
  mutable_collection_view<int> mcv(a);

  ASSERT_TRUE(mcv.data() != nullptr);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  mcv.data()[2] = 42;
  RELOCO_END_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(a[2], 42);

  auto data_result = mcv.try_data();
  ASSERT_TRUE(data_result.has_value());
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE(*data_result)[0] = 7;
  RELOCO_END_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(a[0], 7);
}

TEST(CollectionViewTest, MutableViewForEachMutatesEveryElement) {
  reloco::array<int, 3> a{1, 2, 3};
  mutable_collection_view<int> mcv(a);

  mcv.for_each([](int &v) { v *= 10; });
  EXPECT_EQ(a[0], 10);
  EXPECT_EQ(a[1], 20);
  EXPECT_EQ(a[2], 30);
}

TEST(CollectionViewTest, MutableViewInheritsReadOnlyAccessors) {
  reloco::array<int, 3> a{1, 2, 3};
  mutable_collection_view<int> mcv(a);

  // The read-only accessors from collection_view<T> remain reachable via
  // `using base::...`, alongside the mutable overloads.
  const mutable_collection_view<int> &const_ref = mcv;
  EXPECT_EQ(const_ref.at(0), 1);

  int sum = 0;
  const_ref.for_each([&](const int &v) { sum += v; });
  EXPECT_EQ(sum, 6);
}

TEST(CollectionViewTest, MutableViewOverStdArrayMutatesThroughAt) {
  std::array<int, 2> a{5, 6};
  mutable_collection_view<int> mcv(a);

  mcv.at(0) = 50;
  EXPECT_EQ(a[0], 50);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  mcv.data()[1] = 60;
  RELOCO_END_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(a[1], 60);
}

TEST(CollectionViewTest, MutableViewRejectsConstRelocoArray) {
  EXPECT_FALSE((can_bind_mutable_view<const reloco::array<int, 3>>::value));
  EXPECT_TRUE((can_bind_mutable_view<reloco::array<int, 3>>::value));
}

TEST(CollectionViewTest, MutableViewRejectsConstStdArray) {
  EXPECT_FALSE((can_bind_mutable_view<const std::array<int, 3>>::value));
  EXPECT_TRUE((can_bind_mutable_view<std::array<int, 3>>::value));
}

TEST(CollectionViewTest, ConvertingConstructorIsExplicit) {
  EXPECT_FALSE((std::is_convertible_v<reloco::array<int, 3> &, collection_view<int>>));
  EXPECT_TRUE((std::is_constructible_v<collection_view<int>, reloco::array<int, 3> &>));
}
