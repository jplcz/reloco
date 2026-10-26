// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/kernel/kernel.hpp>
#include <reloco/span.hpp>

#include <cstddef>
#include <numeric>
#include <random>
#include <vector>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

namespace kernel = reloco::kernel;
using reloco::span;

std::vector<int> iota_vector(std::size_t n) {
  std::vector<int> v(n);
  std::iota(v.begin(), v.end(), 0);
  return v;
}

TEST(KernelProjectMoreTest, PermuteReordersDimensions) {
  auto data = iota_vector(24);
  auto v = kernel::try_make_contiguous_tensor<int, 3>(span<int>(data), {2, 3, 4}).value();
  auto p = kernel::permute<2, 0, 1>(v);
  EXPECT_EQ(p.extent(0), 4U);
  EXPECT_EQ(p.extent(1), 2U);
  EXPECT_EQ(p.extent(2), 3U);
  for (std::size_t i = 0; i < 4; ++i)
    for (std::size_t j = 0; j < 2; ++j)
      for (std::size_t k = 0; k < 3; ++k)
        EXPECT_EQ(p.at(i, j, k), v.at(j, k, i));
}

TEST(KernelProjectMoreTest, PermuteOneZeroIsTranspose) {
  auto data = iota_vector(6);
  auto v = kernel::try_make_contiguous_tensor<int, 2>(span<int>(data), {2, 3}).value();
  auto p = kernel::permute<1, 0>(v);
  auto t = kernel::transpose(v);
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 2; ++j)
      EXPECT_EQ(p.at(i, j), t.at(i, j));
}

TEST(KernelProjectMoreTest, PermutedMutableViewStaysWritable) {
  auto data = iota_vector(6);
  auto v = kernel::try_make_contiguous_tensor<int, 2>(span<int>(data), {2, 3}).value();
  kernel::permute<1, 0>(v).at(2, 1) = 99; // element (1, 2) of the parent
  EXPECT_EQ(data[5], 99);
}

TEST(KernelProjectMoreTest, DiagonalOfAMatrix) {
  auto data = iota_vector(15);
  auto v = kernel::try_make_tensor<int, 2>(span<int>(data), {3, 4}, {5, 1}).value();
  auto d = kernel::diagonal<0, 1>(v);
  ASSERT_EQ(d.extent(0), 3U);
  for (std::size_t i = 0; i < 3; ++i)
    EXPECT_EQ(d.at(i), v.at(i, i));
  d.at(2) = -1;
  EXPECT_EQ(v.at(2, 2), -1);
}

TEST(KernelProjectMoreTest, DiagonalOfARank3ViewKeepsTheOtherDimension) {
  auto data = iota_vector(24);
  auto v = kernel::try_make_contiguous_tensor<int, 3>(span<int>(data), {2, 3, 4}).value();
  auto d = kernel::diagonal<1, 2>(v); // shape {2, 3}
  ASSERT_EQ(d.extent(0), 2U);
  ASSERT_EQ(d.extent(1), 3U);
  for (std::size_t a = 0; a < 2; ++a)
    for (std::size_t i = 0; i < 3; ++i)
      EXPECT_EQ(d.at(a, i), v.at(a, i, i));
}

TEST(KernelProjectMoreTest, DiagonalOfEmptyAndSingleStepViews) {
  std::vector<int> data(4, 7);
  auto one = kernel::try_make_contiguous_tensor<int, 2>(span<int>(data), {1, 4}).value();
  auto d = kernel::diagonal<0, 1>(one);
  ASSERT_EQ(d.extent(0), 1U);
  EXPECT_EQ(d.at(0), 7);
  auto none = kernel::try_make_contiguous_tensor<int, 2>(span<int>(data), {0, 4}).value();
  const auto nd = kernel::diagonal<0, 1>(none);
  EXPECT_TRUE(nd.empty());
}

TEST(KernelProjectMoreTest, ReshapeKeepsMemoryOrder) {
  auto data = iota_vector(12);
  auto v = kernel::try_make_contiguous_tensor<int, 2>(span<int>(data), {3, 4}).value();
  auto r = kernel::try_reshape<3>(v, {2, 3, 2});
  ASSERT_TRUE(r.has_value());
  for (std::size_t i = 0; i < 2; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      for (std::size_t k = 0; k < 2; ++k)
        EXPECT_EQ(r.value().at(i, j, k), static_cast<int>(6 * i + 2 * j + k));
  auto flat = kernel::try_reshape<1>(v, {12});
  ASSERT_TRUE(flat.has_value());
  EXPECT_EQ(flat.value().at(11), 11);
}

TEST(KernelProjectMoreTest, ReshapeRejectsWrongCountPaddingAndTranspose) {
  auto data = iota_vector(15);
  auto padded = kernel::try_make_tensor<int, 2>(span<int>(data), {3, 4}, {5, 1}).value();
  EXPECT_EQ(kernel::try_reshape<1>(padded, {12}).error(), reloco::error::invalid_argument);
  auto dense = kernel::try_make_contiguous_tensor<int, 2>(span<int>(data), {3, 4}).value();
  EXPECT_EQ(kernel::try_reshape<1>(dense, {11}).error(), reloco::error::invalid_argument);
  EXPECT_EQ(kernel::try_reshape<2>(kernel::transpose(dense), {4, 3}).error(), reloco::error::invalid_argument);
}

TEST(KernelProjectMoreTest, ReshapeIgnoresStridesOfSizeOneDimensions) {
  auto data = iota_vector(8);
  auto v = kernel::try_make_tensor<int, 3>(span<int>(data), {1, 2, 4}, {0, 4, 1}).value();
  EXPECT_TRUE(kernel::try_reshape<1>(v, {8}).has_value());
}

TEST(KernelProjectMoreTest, ReshapeOfEmptyViewsNeedsAnEmptyShape) {
  std::vector<int> data(4);
  auto e = kernel::try_make_contiguous_tensor<int, 2>(span<int>(data), {0, 4}).value();
  EXPECT_TRUE(kernel::try_reshape<1>(e, {0}).has_value());
  EXPECT_FALSE(kernel::try_reshape<1>(e, {4}).has_value());
}

TEST(KernelProjectMoreTest, BroadcastAddsAndRepeatsDimensions) {
  auto data = iota_vector(3);
  auto row = kernel::try_make_contiguous_tensor<int, 1>(span<int>(data), {3}).value();
  auto b = kernel::try_broadcast<2>(row, {4, 3});
  ASSERT_TRUE(b.has_value());
  for (std::size_t r = 0; r < 4; ++r)
    for (std::size_t c = 0; c < 3; ++c)
      EXPECT_EQ(b.value().at(r, c), static_cast<int>(c));

  auto col = kernel::try_make_contiguous_tensor<int, 2>(span<int>(data), {3, 1}).value();
  auto wide = kernel::try_broadcast<2>(col, {3, 5});
  ASSERT_TRUE(wide.has_value());
  for (std::size_t r = 0; r < 3; ++r)
    for (std::size_t c = 0; c < 5; ++c)
      EXPECT_EQ(wide.value().at(r, c), static_cast<int>(r));
}

TEST(KernelProjectMoreTest, BroadcastIsReadOnlyAndRejectsMismatches) {
  auto data = iota_vector(3);
  auto row = kernel::try_make_contiguous_tensor<int, 1>(span<int>(data), {3}).value();
  auto b = kernel::try_broadcast<2>(row, {2, 3}).value();
  static_assert(std::is_const_v<std::remove_reference_t<decltype(b.at(0, 0))>>);
  EXPECT_EQ(kernel::try_broadcast<2>(row, {2, 4}).error(), reloco::error::invalid_argument);
  EXPECT_EQ(kernel::try_broadcast<1>(row, {2}).error(), reloco::error::invalid_argument);
}

TEST(KernelProjectMoreTest, RandomLayoutsMatchNaiveIndexing) {
  std::mt19937 rng(31);
  for (int iter = 0; iter < 300; ++iter) {
    const std::size_t e0 = 1 + rng() % 4, e1 = 1 + rng() % 4, e2 = 1 + rng() % 4;
    const std::size_t s2 = 1 + rng() % 2, s1 = e2 * s2 + rng() % 3, s0 = e1 * s1 + rng() % 3;
    auto data = iota_vector((e0 - 1) * s0 + (e1 - 1) * s1 + (e2 - 1) * s2 + 1 + rng() % 3);
    auto v = kernel::try_make_tensor<int, 3>(span<int>(data), {e0, e1, e2}, {s0, s1, s2}).value();
    auto p = kernel::permute<1, 2, 0>(v);
    auto d = kernel::diagonal<0, 2>(v);
    for (std::size_t i = 0; i < e1; ++i)
      for (std::size_t j = 0; j < e2; ++j)
        for (std::size_t k = 0; k < e0; ++k)
          ASSERT_EQ(p.at(i, j, k), v.at(k, i, j));
    for (std::size_t a = 0; a < e1; ++a)
      for (std::size_t i = 0; i < std::min(e0, e2); ++i)
        ASSERT_EQ(d.at(a, i), v.at(i, a, i));
    auto b = kernel::try_broadcast<4>(v, {2, e0, e1, e2}).value();
    for (std::size_t n = 0; n < 2; ++n)
      ASSERT_EQ(b.at(n, e0 - 1, e1 - 1, e2 - 1), v.at(e0 - 1, e1 - 1, e2 - 1));
  }
}

} // namespace

RELOCO_END_UNSAFE_BUFFER_USAGE
