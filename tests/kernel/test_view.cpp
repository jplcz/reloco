// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/array.hpp>
#include <reloco/kernel/kernel.hpp>
#include <reloco/lifetime.hpp>
#include <reloco/span.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

// EXPECT_DEATH expands to gtest-internal fprintf() calls that -Wunsafe-buffer-usage-in-libc-call flags.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

namespace kernel = reloco::kernel;
using reloco::error;

constexpr std::size_t max_size = std::numeric_limits<std::size_t>::max();

using dyn = kernel::dynamic_stride;
template <std::size_t N> using stat = kernel::static_stride<N>;

// ---- layout and types ----------------------------------------------------------------------------------------

static_assert(sizeof(kernel::strides<stat<1>>) == 1, "a fully static stride list stores nothing");
static_assert(sizeof(kernel::strides<dyn, stat<1>>) == sizeof(std::size_t), "only dynamic strides take space");
static_assert(sizeof(kernel::strides<dyn, dyn>) == 2 * sizeof(std::size_t));
static_assert(kernel::strides<dyn, stat<1>>::dynamic_count == 1);
static_assert(kernel::strides<stat<4>, stat<1>>{}.get<0>() == 4);
static_assert(kernel::strides<dyn, stat<1>>(7).get<0>() == 7);
static_assert(kernel::strides<dyn, stat<1>>(7).get<1>() == 1);
static_assert(kernel::strides<dyn, stat<1>>(7)[1] == 1);

static_assert(std::is_trivially_copyable_v<kernel::tensor_view<int, 2>>);
static_assert(std::is_convertible_v<kernel::tensor_view<int, 2>, kernel::tensor_view<const int, 2>>);
static_assert(!std::is_convertible_v<kernel::tensor_view<const int, 2>, kernel::tensor_view<int, 2>>);
static_assert(kernel::tensor_view<int, 3>::rank == 3);

// The only way to get a view over data is the validating factory: there is no constructor taking storage.
static_assert(!std::is_constructible_v<kernel::tensor_view<int, 1>, reloco::span<int>, kernel::shape<1>,
                                       kernel::dynamic_strides<1>>);

static_assert(kernel::shape<2>{3, 4}[1] == 4);
static_assert(kernel::shape<2>{3, 0}.empty());
static_assert(kernel::shape<3>{2, 3, 4}.try_elements().value() == 24);

// ---- shape ---------------------------------------------------------------------------------------------------

TEST(KernelShapeTest, ElementsDetectsOverflow) {
  EXPECT_EQ((kernel::shape<2>{max_size, 2}.try_elements().error()), error::integer_overflow);
  const kernel::shape<2> wide{max_size, 1};
  EXPECT_EQ(wide.try_elements().value(), max_size);
}

TEST(KernelShapeTest, EmptyShapeHasZeroElementsEvenIfOtherExtentsAreHuge) {
  auto n = kernel::shape<3>{max_size, max_size, 0}.try_elements();
  ASSERT_TRUE(n.has_value());
  EXPECT_EQ(*n, 0u);
}

// ---- try_make_tensor: accepted layouts -----------------------------------------------------------------------

TEST(KernelTensorTest, ContiguousMatrixMapsRowMajor) {
  std::vector<int> data{0, 1, 2, 3, 4, 5};
  auto m = kernel::try_make_contiguous_tensor<int, 2>(reloco::span<int>(data), {2, 3});
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->extent(0), 2u);
  EXPECT_EQ(m->extent(1), 3u);
  EXPECT_EQ(m->stride(0), 3u);
  EXPECT_EQ(m->stride(1), 1u);
  for (std::size_t r = 0; r < 2; ++r)
    for (std::size_t c = 0; c < 3; ++c)
      EXPECT_EQ(m->at(r, c), static_cast<int>(r * 3 + c));
}

TEST(KernelTensorTest, PaddedRowsWithStaticUnitStride) {
  // 3 rows of 2 used elements, rows 4 apart: the last row needs offset 2*4 + 1 = 9, so 10 elements.
  std::vector<int> data(10, 0);
  using padded = kernel::strides<dyn, stat<1>>;
  auto m = kernel::try_make_tensor<int, 2, padded>(reloco::span<int>(data), {3, 2}, padded(4));
  ASSERT_TRUE(m.has_value());
  m->at(2, 1) = 42;
  EXPECT_EQ(data[9], 42);
  EXPECT_EQ(m->stride(1), 1u);
}

TEST(KernelTensorTest, DefaultStridesAcceptBraces) {
  std::vector<int> data(10, 0);
  auto m = kernel::try_make_tensor<int, 2>(reloco::span<int>(data), {3, 2}, {4, 1});
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->stride(0), 4u);
}

TEST(KernelTensorTest, ColumnMajorIsAcceptedForMutableViews) {
  std::vector<int> data(6, 0);
  auto m = kernel::try_make_tensor<int, 2>(reloco::span<int>(data), {2, 3}, {1, 2});
  ASSERT_TRUE(m.has_value());
  m->at(1, 2) = 7;
  EXPECT_EQ(data[5], 7);
}

TEST(KernelTensorTest, RankOneAndRankFour) {
  std::vector<int> data(24, 0);
  auto v = kernel::try_make_contiguous_tensor<int, 1>(reloco::span<int>(data), {24});
  ASSERT_TRUE(v.has_value());
  v->at(23) = 1;
  EXPECT_EQ(data[23], 1);

  auto t = kernel::try_make_contiguous_tensor<int, 4>(reloco::span<int>(data), {2, 3, 2, 2});
  ASSERT_TRUE(t.has_value());
  t->at(1, 2, 1, 1) = 9;
  EXPECT_EQ(data[23], 9);
}

TEST(KernelTensorTest, ExtraStorageIsFine) {
  std::vector<int> data(100, 0);
  auto m = kernel::try_make_contiguous_tensor<int, 2>(reloco::span<int>(data), {3, 3});
  EXPECT_TRUE(m.has_value());
}

TEST(KernelTensorTest, EmptyShapeIsValidEvenOverNoStorage) {
  auto m = kernel::try_make_tensor<int, 2>(reloco::span<int>(), {0, 5}, {max_size, max_size});
  ASSERT_TRUE(m.has_value());
  EXPECT_TRUE(m->empty());
  EXPECT_EQ(m->try_at(0, 0).error(), error::out_of_bounds);
}

TEST(KernelTensorTest, DefaultViewIsEmpty) {
  const kernel::tensor_view<int, 2> m;
  EXPECT_TRUE(m.empty());
  EXPECT_FALSE(m.try_at(0, 0).has_value());
}

// ---- try_make_tensor: rejected layouts -----------------------------------------------------------------------

TEST(KernelTensorTest, StorageTooSmallIsOutOfBounds) {
  std::vector<int> data(5, 0);
  auto m = kernel::try_make_contiguous_tensor<int, 2>(reloco::span<int>(data), {2, 3});
  ASSERT_FALSE(m.has_value());
  EXPECT_EQ(m.error(), error::out_of_bounds);
}

TEST(KernelTensorTest, OneElementTooFewIsRejectedAndExactFitAccepted) {
  std::vector<int> data(10, 0);
  auto m = kernel::try_make_tensor<int, 2>(reloco::span<int>(data), {3, 2}, {4, 1});
  EXPECT_TRUE(m.has_value());
  EXPECT_EQ((kernel::try_make_tensor<int, 2>(reloco::span<int>(data).subspan(0, 9), {3, 2}, {4, 1}).error()),
            error::out_of_bounds);
}

TEST(KernelTensorTest, OffsetOverflowIsReportedNotWrapped) {
  std::vector<int> data(4, 0);
  auto big_stride = kernel::try_make_tensor<const int, 2>(reloco::span<const int>(data), {3, 2}, {max_size, 1});
  ASSERT_FALSE(big_stride.has_value());
  EXPECT_EQ(big_stride.error(), error::integer_overflow);

  auto sum_overflow =
      kernel::try_make_tensor<const int, 2>(reloco::span<const int>(data), {2, 2}, {max_size, max_size});
  ASSERT_FALSE(sum_overflow.has_value());
  EXPECT_EQ(sum_overflow.error(), error::integer_overflow);
}

TEST(KernelTensorTest, MutableOverlapIsRejectedButReadOnlyIsAllowed) {
  std::vector<int> data(8, 0);
  // Stride 0 broadcasts one row: every row aliases the same elements.
  auto mutable_view = kernel::try_make_tensor<int, 2>(reloco::span<int>(data), {4, 2}, {0, 1});
  ASSERT_FALSE(mutable_view.has_value());
  EXPECT_EQ(mutable_view.error(), error::invalid_argument);

  auto read_only = kernel::try_make_tensor<const int, 2>(reloco::span<const int>(data), {4, 2}, {0, 1});
  EXPECT_TRUE(read_only.has_value());

  // Rows closer together than their length overlap too.
  EXPECT_EQ((kernel::try_make_tensor<int, 2>(reloco::span<int>(data), {3, 3}, {2, 1}).error()),
            error::invalid_argument);
}

TEST(KernelTensorTest, MutableOverlapOfExtentOneDimensionsDoesNotMatter) {
  std::vector<int> data(4, 0);
  auto m = kernel::try_make_tensor<int, 2>(reloco::span<int>(data), {1, 4}, {0, 1});
  EXPECT_TRUE(m.has_value());
}

// ---- soundness: accepted views are really in range and (if mutable) disjoint ---------------------------------

class lcg {
public:
  explicit lcg(std::uint64_t seed) : state_(seed) {}
  std::size_t next(std::size_t bound) {
    state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<std::size_t>((state_ >> 33) % bound);
  }

private:
  std::uint64_t state_;
};

template <std::size_t Rank> void check_random_layouts(std::uint64_t seed) {
  lcg rng(seed);
  for (int iter = 0; iter < 2000; ++iter) {
    reloco::array<std::size_t, Rank> ext{};
    reloco::array<std::size_t, Rank> str{};
    std::size_t total = 1;
    for (std::size_t d = 0; d < Rank; ++d) {
      ext[d] = rng.next(4) + 1;
      str[d] = rng.next(7);
      total *= ext[d];
    }
    const std::size_t size = rng.next(40);
    std::vector<int> storage(size, 0);

    // Brute force: enumerate every index tuple and record the offsets it touches.
    std::vector<unsigned> hits(size + 64, 0);
    bool in_range = true;
    for (std::size_t flat = 0; flat < total; ++flat) {
      std::size_t rest = flat;
      std::size_t offset = 0;
      for (std::size_t d = Rank; d-- > 0;) {
        offset += (rest % ext[d]) * str[d];
        rest /= ext[d];
      }
      if (offset >= size)
        in_range = false;
      else
        ++hits[offset];
    }
    bool disjoint = true;
    for (unsigned h : hits)
      disjoint = disjoint && h <= 1;

    kernel::shape<Rank> shape_value;
    if constexpr (Rank == 1)
      shape_value = kernel::shape<1>(ext[0]);
    else if constexpr (Rank == 2)
      shape_value = kernel::shape<2>(ext[0], ext[1]);
    else
      shape_value = kernel::shape<3>(ext[0], ext[1], ext[2]);
    kernel::dynamic_strides<Rank> strides_value = kernel::dynamic_strides<Rank>(str);

    auto read_only =
        kernel::try_make_tensor<const int, Rank>(reloco::span<const int>(storage), shape_value, strides_value);
    ASSERT_EQ(read_only.has_value(), in_range) << "iter " << iter;

    auto writable = kernel::try_make_tensor<int, Rank>(reloco::span<int>(storage), shape_value, strides_value);
    if (writable.has_value()) {
      // Soundness: a mutable view is never accepted if it is out of range or aliases elements.
      EXPECT_TRUE(in_range) << "iter " << iter;
      EXPECT_TRUE(disjoint) << "iter " << iter;
    }
    // The converse is deliberately not asserted: the disjointness test is conservative and rejects exotic
    // interleavings (e.g. extents {2, 2} with strides {2, 3}) that happen not to collide.
  }
}

TEST(KernelTensorPropertyTest, RankOne) { check_random_layouts<1>(1); }
TEST(KernelTensorPropertyTest, RankTwo) { check_random_layouts<2>(2); }
TEST(KernelTensorPropertyTest, RankThree) { check_random_layouts<3>(3); }

// ---- accessors -----------------------------------------------------------------------------------------------

TEST(KernelTensorTest, TryAtReportsEachOutOfRangeIndex) {
  std::vector<int> data(6, 0);
  auto made = kernel::try_make_contiguous_tensor<int, 2>(reloco::span<int>(data), {2, 3});
  ASSERT_TRUE(made.has_value());
  auto m = made.value();
  EXPECT_TRUE(m.try_at(1, 2).has_value());
  EXPECT_EQ(m.try_at(2, 0).error(), error::out_of_bounds);
  EXPECT_EQ(m.try_at(0, 3).error(), error::out_of_bounds);
  EXPECT_EQ(m.try_at(-1, 0).error(), error::out_of_bounds);
  auto cell = m.try_at(1, 1);
  ASSERT_TRUE(cell.has_value());
  cell->get() = 5;
  EXPECT_EQ(data[4], 5);
}

TEST(KernelTensorTest, UnsafeAtMatchesAt) {
  std::vector<int> data{0, 1, 2, 3, 4, 5};
  auto made = kernel::try_make_contiguous_tensor<int, 2>(reloco::span<int>(data), {2, 3});
  ASSERT_TRUE(made.has_value());
  auto m = made.value();
  EXPECT_EQ(m.unsafe_at(1, 2), m.at(1, 2));
}

TEST(KernelTensorTest, ReadOnlyConversionSharesStorage) {
  std::vector<int> data{1, 2, 3, 4};
  auto made = kernel::try_make_contiguous_tensor<int, 2>(reloco::span<int>(data), {2, 2});
  ASSERT_TRUE(made.has_value());
  auto m = made.value();
  const kernel::tensor_view<const int, 2> ro = m;
  m.at(0, 1) = 9;
  EXPECT_EQ(ro.at(0, 1), 9);
  const auto storage = ro.storage();
  EXPECT_EQ(storage.data(), data.data());
}

TEST(KernelTensorTest, ElementReferenceOutlivesTemporaryView) {
  std::vector<int> data{1, 2, 3, 4};
  int &ref = kernel::try_make_contiguous_tensor<int, 1>(reloco::span<int>(data), {4}).value().at(2);
  EXPECT_EQ(ref, 3);
}

TEST(KernelTensorDeathTest, AtTrapsOutOfRange) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  std::vector<int> data(6, 0);
  auto made = kernel::try_make_contiguous_tensor<int, 2>(reloco::span<int>(data), {2, 3});
  ASSERT_TRUE(made.has_value());
  auto m = made.value();
  EXPECT_DEATH({ [[maybe_unused]] int &x = m.at(0, 3); }, "");
}

// ---- lane_traits ---------------------------------------------------------------------------------------------

TEST(KernelLaneTraitsTest, DefaultLaneRoundTrips) {
  std::vector<int> src{1, 2, 3, 4};
  std::vector<int> dst(4, 0);
  using lanes = kernel::lane_traits<int, 4>;
  static_assert(std::is_same_v<lanes::type, reloco::array<int, 4>>);
  auto lane = lanes::load(reloco::span<const int>(src));
  EXPECT_EQ(lane[2], 3);
  lanes::store(reloco::span<int>(dst), lane);
  EXPECT_EQ(dst, src);
}

} // namespace

RELOCO_END_UNSAFE_BUFFER_USAGE
