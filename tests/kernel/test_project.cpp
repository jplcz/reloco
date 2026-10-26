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

// A 3x4 matrix stored with a row stride of 5 (one padding element per row); element (r, c) holds 10 * r + c.
struct padded_matrix {
  std::vector<int> data = std::vector<int>(15, -1);
  padded_matrix() {
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c)
        data[static_cast<std::size_t>(r * 5 + c)] = 10 * r + c;
  }
  auto view() { return kernel::try_make_tensor<int, 2>(reloco::span<int>(data), {3, 4}, {5, 1}).value(); }
};

TEST(KernelProjectTest, SliceKeepsRankAndMovesTheOrigin) {
  padded_matrix m;
  auto v = m.view();
  auto s = kernel::try_slice<0>(v, 1, 2);
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s.value().extent(0), 2U);
  EXPECT_EQ(s.value().extent(1), 4U);
  EXPECT_EQ(s.value().at(0, 0), 10);
  EXPECT_EQ(s.value().at(1, 3), 23);
}

TEST(KernelProjectTest, SliceRejectsRangesOutsideTheExtent) {
  padded_matrix m;
  auto v = m.view();
  EXPECT_EQ(kernel::try_slice<0>(v, 2, 2).error(), reloco::error::out_of_bounds);
  EXPECT_EQ(kernel::try_slice<1>(v, 5, 0).error(), reloco::error::out_of_bounds);
  EXPECT_EQ(kernel::try_slice<0>(v, 0, SIZE_MAX).error(), reloco::error::out_of_bounds);
  EXPECT_EQ(kernel::try_slice<0>(v, SIZE_MAX, 1).error(), reloco::error::out_of_bounds);
}

TEST(KernelProjectTest, EmptySliceAtTheEndIsLegal) {
  padded_matrix m;
  auto s = kernel::try_slice<0>(m.view(), 3, 0);
  ASSERT_TRUE(s.has_value());
  EXPECT_TRUE(s.value().empty());
}

TEST(KernelProjectTest, RowAndColumn) {
  padded_matrix m;
  auto v = m.view();
  auto row = kernel::try_row(v, 2);
  ASSERT_TRUE(row.has_value());
  EXPECT_EQ(row.value().extent(0), 4U);
  EXPECT_EQ(row.value().at(3), 23);
  auto col = kernel::try_column(v, 1);
  ASSERT_TRUE(col.has_value());
  EXPECT_EQ(col.value().extent(0), 3U);
  EXPECT_EQ(col.value().stride(0), 5U);
  EXPECT_EQ(col.value().at(2), 21);
  EXPECT_EQ(kernel::try_row(v, 3).error(), reloco::error::out_of_bounds);
  EXPECT_EQ(kernel::try_column(v, 4).error(), reloco::error::out_of_bounds);
}

TEST(KernelProjectTest, ProjectionOfMutableViewWrites) {
  padded_matrix m;
  auto v = m.view();
  auto col = kernel::try_column(v, 3);
  ASSERT_TRUE(col.has_value());
  auto c = col.value();
  for (std::size_t i = 0; i < c.extent(0); ++i)
    c.at(i) = 100;
  EXPECT_EQ(m.data[3], 100);
  EXPECT_EQ(m.data[8], 100);
  EXPECT_EQ(m.data[13], 100);
  EXPECT_EQ(m.data[4], -1);
}

TEST(KernelProjectTest, TileSelectsABlock) {
  padded_matrix m;
  auto t = kernel::try_tile(m.view(), kernel::shape<2>{1, 1}, kernel::shape<2>{2, 3});
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t.value().at(0, 0), 11);
  EXPECT_EQ(t.value().at(1, 2), 23);
  EXPECT_EQ(kernel::try_tile(m.view(), kernel::shape<2>{2, 0}, kernel::shape<2>{2, 1}).error(),
            reloco::error::out_of_bounds);
  EXPECT_EQ(kernel::try_tile(m.view(), kernel::shape<2>{0, 2}, kernel::shape<2>{1, 3}).error(),
            reloco::error::out_of_bounds);
}

TEST(KernelProjectTest, TransposeSwapsIndexes) {
  padded_matrix m;
  auto v = m.view();
  auto t = kernel::transpose(v);
  EXPECT_EQ(t.extent(0), 4U);
  EXPECT_EQ(t.extent(1), 3U);
  for (std::size_t r = 0; r < 3; ++r)
    for (std::size_t c = 0; c < 4; ++c)
      EXPECT_EQ(&t.at(c, r), &v.at(r, c));
  EXPECT_EQ(&kernel::transpose(t).at(1, 2), &v.at(1, 2));
}

TEST(KernelProjectTest, ProjectionsChain) {
  padded_matrix m;
  auto tile = kernel::try_tile(m.view(), kernel::shape<2>{1, 0}, kernel::shape<2>{2, 4});
  ASSERT_TRUE(tile.has_value());
  auto col = kernel::try_column(kernel::transpose(tile.value()), 1); // transposed: 4x2, column 1 is a 4-long vector
  ASSERT_TRUE(col.has_value());
  EXPECT_EQ(col.value().extent(0), 4U);
  EXPECT_EQ(col.value().at(2), 22);
}

TEST(KernelProjectTest, Rank3Index) {
  std::vector<int> data(24);
  std::iota(data.begin(), data.end(), 0);
  auto v = kernel::try_make_contiguous_tensor<int, 3>(reloco::span<int>(data), {2, 3, 4}).value();
  auto plane = kernel::try_index<0>(v, 1);
  ASSERT_TRUE(plane.has_value());
  EXPECT_EQ(plane.value().at(2, 3), 23);
  auto mid = kernel::try_index<1>(v, 2);
  ASSERT_TRUE(mid.has_value());
  EXPECT_EQ(mid.value().extent(0), 2U);
  EXPECT_EQ(mid.value().at(1, 1), 12 + 8 + 1);
  auto sw = kernel::transpose<0, 2>(v);
  EXPECT_EQ(&sw.at(3, 2, 1), &v.at(1, 2, 3));
}

TEST(KernelProjectTest, ProjectionsOfRandomLayoutsStayInsideAndMatchTheParent) {
  std::mt19937 rng(7);
  for (int iter = 0; iter < 2000; ++iter) {
    const std::size_t rows = rng() % 6;
    const std::size_t cols = rng() % 6;
    const std::size_t stride = cols + rng() % 3;
    const std::size_t need = rows == 0 || cols == 0 ? 0 : (rows - 1) * stride + cols;
    std::vector<int> data(need + rng() % 3);
    auto parent = kernel::try_make_tensor<int, 2>(reloco::span<int>(data), {rows, cols}, {stride, std::size_t{1}});
    ASSERT_TRUE(parent.has_value());
    const std::size_t r0 = rng() % (rows + 2);
    const std::size_t c0 = rng() % (cols + 2);
    const std::size_t nr = rng() % (rows + 2);
    const std::size_t nc = rng() % (cols + 2);
    auto tile = kernel::try_tile(parent.value(), kernel::shape<2>{r0, c0}, kernel::shape<2>{nr, nc});
    const bool fits = r0 <= rows && nr <= rows - r0 && c0 <= cols && nc <= cols - c0;
    ASSERT_EQ(tile.has_value(), fits);
    if (!fits)
      continue;
    for (std::size_t r = 0; r < nr; ++r)
      for (std::size_t c = 0; c < nc; ++c)
        ASSERT_EQ(&tile.value().at(r, c), &parent.value().at(r0 + r, c0 + c));
  }
}

} // namespace

RELOCO_END_UNSAFE_BUFFER_USAGE
