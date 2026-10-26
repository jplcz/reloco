// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "../../demos/kernel/example_kernels.hpp"
#include "audit.hpp"

#include <gtest/gtest.h>

#include <random>
#include <vector>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

namespace ke = kernel_examples;
using reloco::span;

template <class T> std::vector<T> random_values(std::mt19937 &rng, std::size_t n, int range) {
  std::vector<T> v(n);
  for (auto &x : v)
    x = static_cast<T>(static_cast<int>(rng() % static_cast<unsigned>(2 * range + 1)) - range);
  return v;
}

TEST(KernelExamplesTest, RescaleMatchesScalar) {
  std::mt19937 rng(21);
  for (int iter = 0; iter < 300; ++iter) {
    auto in = random_values<std::int32_t>(rng, rng() % 40, 1 << 20);
    const std::size_t out_n = rng() % 4 == 0 ? rng() % 40 : in.size();
    const std::vector<std::int8_t> init(out_n, 5);
    const auto mult = static_cast<std::int32_t>(rng() % 100000);
    const unsigned shift = rng() % 70; // includes invalid shifts
    audit::expect_same(
        init,
        [&](std::vector<std::int8_t> &o) {
          return ke::rescale(span<std::int8_t>(o), span<const std::int32_t>(in), mult, shift, 3);
        },
        [&](std::vector<std::int8_t> &o) {
          return ke::scalar::rescale(span<std::int8_t>(o), span<const std::int32_t>(in), mult, shift, 3);
        });
  }
}

TEST(KernelExamplesTest, RotatePairsMatchesScalar) {
  std::mt19937 rng(22);
  for (int iter = 0; iter < 300; ++iter) {
    const std::size_t n = (rng() % 20) * 2 + (rng() % 8 == 0 ? 1 : 0);
    auto data = random_values<std::int16_t>(rng, n, 30000);
    const std::size_t tn = rng() % 6 == 0 ? rng() % 10 : n / 2;
    auto cos_q = random_values<std::int16_t>(rng, tn, 32767);
    auto sin_q = random_values<std::int16_t>(rng, tn, 32767);
    audit::expect_same(
        data,
        [&](std::vector<std::int16_t> &d) {
          return ke::rotate_pairs(span<std::int16_t>(d), span<const std::int16_t>(cos_q),
                                  span<const std::int16_t>(sin_q));
        },
        [&](std::vector<std::int16_t> &d) {
          return ke::scalar::rotate_pairs(span<std::int16_t>(d), span<const std::int16_t>(cos_q),
                                          span<const std::int16_t>(sin_q));
        });
  }
}

TEST(KernelExamplesTest, RowDotsMatchesScalarForPaddedMatrices) {
  std::mt19937 rng(23);
  for (int iter = 0; iter < 300; ++iter) {
    const std::size_t rows = rng() % 11;
    const std::size_t cols = rng() % 13;
    const std::size_t stride = cols + rng() % 4;
    const std::size_t need = rows == 0 || cols == 0 ? 0 : (rows - 1) * stride + cols;
    auto data = random_values<std::int8_t>(rng, need + rng() % 3, 127);
    auto x = random_values<std::int8_t>(rng, cols + rng() % 2, 127);
    auto m = reloco::kernel::try_make_tensor<const std::int8_t, 2>(span<const std::int8_t>(data), {rows, cols},
                                                                   {stride, std::size_t{1}});
    ASSERT_TRUE(m.has_value());
    const std::vector<std::int64_t> init(rows + rng() % 2, -1);
    audit::expect_same(
        init,
        [&](std::vector<std::int64_t> &o) {
          return ke::row_dots(m.value(), span<const std::int8_t>(x), span<std::int64_t>(o));
        },
        [&](std::vector<std::int64_t> &o) {
          return ke::scalar::row_dots(m.value(), span<const std::int8_t>(x), span<std::int64_t>(o));
        });
  }
}

TEST(KernelExamplesTest, RowDotsRejectsAShortVector) {
  std::vector<std::int8_t> data(6, 1), x(2, 1);
  std::vector<std::int64_t> out(2);
  auto m =
      reloco::kernel::try_make_contiguous_tensor<const std::int8_t, 2>(span<const std::int8_t>(data), {2, 3}).value();
  EXPECT_EQ(ke::row_dots(m, span<const std::int8_t>(x), span<std::int64_t>(out)).error(), reloco::error::out_of_bounds);
}

TEST(KernelExamplesTest, MovingSumMatchesScalar) {
  std::mt19937 rng(24);
  for (int iter = 0; iter < 300; ++iter) {
    auto in = random_values<std::int32_t>(rng, rng() % 30, 1000);
    const std::size_t w = rng() % 12;
    const std::size_t out_n = rng() % 4 == 0 ? rng() % 30 : in.size();
    const std::vector<std::int64_t> init(out_n, -7);
    audit::expect_same(
        init,
        [&](std::vector<std::int64_t> &o) {
          return ke::moving_sum(span<std::int64_t>(o), span<const std::int32_t>(in), w);
        },
        [&](std::vector<std::int64_t> &o) {
          return ke::scalar::moving_sum(span<std::int64_t>(o), span<const std::int32_t>(in), w);
        });
  }
}

TEST(KernelExamplesTest, MovingSumKnownValues) {
  std::vector<std::int32_t> in{1, 2, 3, 4, 5};
  std::vector<std::int64_t> out(5, 0);
  ASSERT_TRUE(ke::moving_sum(span<std::int64_t>(out), span<const std::int32_t>(in), 3).has_value());
  EXPECT_EQ(out[0], 6);
  EXPECT_EQ(out[1], 9);
  EXPECT_EQ(out[2], 12);
}

} // namespace

RELOCO_END_UNSAFE_BUFFER_USAGE
