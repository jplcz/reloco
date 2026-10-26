// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/array.hpp>
#include <reloco/kernel/kernel.hpp>
#include <reloco/lifetime.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

// EXPECT_DEATH expands to gtest-internal fprintf() calls that -Wunsafe-buffer-usage-in-libc-call flags; see
// tests/test_boost_intrusive_safety.cpp for the same pattern.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

namespace kernel = reloco::kernel;

constexpr std::size_t max_size = std::numeric_limits<std::size_t>::max();

// ---- the proof type cannot be forged -------------------------------------------------------------------------

using index_t = kernel::proven_index<kernel::unbranded>;

static_assert(!std::is_constructible_v<index_t, std::size_t>,
              "a proven_index must not be constructible from an integer");
static_assert(!std::is_convertible_v<index_t, std::size_t>, "a proven_index must not convert implicitly to an integer");
static_assert(!std::is_default_constructible_v<index_t>, "a proven_index must not be default constructible");
static_assert(std::is_trivially_copyable_v<index_t>);
static_assert(sizeof(index_t) == sizeof(std::size_t), "layout must not depend on RELOCO_KERNEL_CHECKS");
static_assert(sizeof(kernel::block<8>) == 2 * sizeof(std::size_t));
static_assert(sizeof(kernel::window<>) == 3 * sizeof(std::size_t));

static_assert(kernel::checks_level == RELOCO_KERNEL_CHECKS);

// Indexes of different brands are different types and cannot be mixed.
struct brand_a {};
struct brand_b {};
static_assert(!std::is_same_v<kernel::proven_index<brand_a>, kernel::proven_index<brand_b>>);
static_assert(!std::is_convertible_v<kernel::proven_index<brand_a>, kernel::proven_index<brand_b>>);

// ---- constexpr use -------------------------------------------------------------------------------------------

constexpr std::size_t sum_domain(std::size_t n) {
  std::size_t total = 0;
  for (auto i : kernel::domain(n))
    total += i.value();
  return total;
}

static_assert(sum_domain(5) == 10);
static_assert(sum_domain(0) == 0);

// ---- domain / range ------------------------------------------------------------------------------------------

TEST(KernelIndexTest, DomainYieldsEveryIndexBelowN) {
  std::size_t expected = 0;
  for (auto i : kernel::domain(7)) {
    EXPECT_EQ(i.value(), expected);
    ++expected;
  }
  EXPECT_EQ(expected, 7U);
  EXPECT_EQ(kernel::domain(7).size(), 7U);
}

TEST(KernelIndexTest, EmptyDomainHasNoIndexes) {
  const auto d = kernel::domain(0);
  EXPECT_TRUE(d.empty());
  EXPECT_EQ(d.begin(), d.end());
}

TEST(KernelIndexTest, ProvenIndexesCompare) {
  auto d = kernel::domain(3);
  auto it = d.begin();
  const auto a = *it;
  ++it;
  const auto b = *it;
  EXPECT_LT(a, b);
  EXPECT_NE(a, b);
  EXPECT_EQ(a, *d.begin());
  EXPECT_EQ(static_cast<std::size_t>(b), 1U);
}

TEST(KernelIndexTest, PostIncrementReturnsPreviousPosition) {
  auto d = kernel::domain(3);
  auto it = d.begin();
  const auto old = it++;
  EXPECT_EQ((*old).value(), 0U);
  EXPECT_EQ((*it).value(), 1U);
}

// ---- blocks / tail -------------------------------------------------------------------------------------------

template <std::size_t B> void expect_blocks_cover_exactly_once(std::size_t n) {
  reloco::array<unsigned, 64> seen{};
  std::size_t block_count = 0;

  const auto chunks = kernel::blocks<B>(n);
  for (auto blk : chunks) {
    ++block_count;
    ASSERT_LE(blk.base() + B, n) << "n=" << n;
    blk.for_each_lane([&](auto i) { ++seen[i.value()]; });
  }
  for (auto i : chunks.tail())
    ++seen[i.value()];

  EXPECT_EQ(block_count, n / B) << "n=" << n;
  EXPECT_EQ(chunks.size(), n / B);
  EXPECT_EQ(chunks.tail().size(), n % B);
  EXPECT_EQ(kernel::tail<B>(n).size(), n % B);
  for (std::size_t i = 0; i < 64; ++i)
    EXPECT_EQ(seen[i], i < n ? 1U : 0U) << "n=" << n << " i=" << i;
}

TEST(KernelIndexTest, BlocksAndTailCoverTheExtentExactlyOnce) {
  for (std::size_t n = 0; n <= 40; ++n) {
    expect_blocks_cover_exactly_once<1>(n);
    expect_blocks_cover_exactly_once<2>(n);
    expect_blocks_cover_exactly_once<4>(n);
    expect_blocks_cover_exactly_once<16>(n);
  }
}

TEST(KernelIndexTest, ExtentSmallerThanOneBlockHasOnlyATail) {
  const auto chunks = kernel::blocks<16>(5);
  EXPECT_TRUE(chunks.empty());
  EXPECT_EQ(chunks.begin(), chunks.end());
  EXPECT_EQ(chunks.tail().first(), 0U);
  EXPECT_EQ(chunks.tail().size(), 5U);
}

TEST(KernelIndexTest, BlockLanesAreConsecutive) {
  for (auto blk : kernel::blocks<4>(9)) {
    EXPECT_EQ(blk.lane<0>().value(), blk.base());
    EXPECT_EQ(blk.lane<3>().value(), blk.base() + 3);
    std::size_t expected = blk.base();
    for (auto i : blk.lanes())
      EXPECT_EQ(i.value(), expected++);
    EXPECT_EQ(expected, blk.base() + 4);
  }
}

TEST(KernelIndexTest, PairsAreBlocksOfTwo) {
  std::size_t pairs_seen = 0;
  for (auto p : kernel::pairs(7)) {
    EXPECT_EQ(p.lane<0>().value(), 2 * pairs_seen);
    EXPECT_EQ(p.lane<1>().value(), 2 * pairs_seen + 1);
    ++pairs_seen;
  }
  EXPECT_EQ(pairs_seen, 3U);
  EXPECT_EQ(kernel::pairs(7).tail().size(), 1U);
}

TEST(KernelIndexTest, BlocksDoNotOverflowNearTheLargestSize) {
  const auto chunks = kernel::blocks<16>(max_size);
  EXPECT_EQ(chunks.size(), max_size / 16);
  EXPECT_EQ(chunks.tail().first(), max_size - 15);
  EXPECT_EQ(chunks.tail().size(), 15U);
  EXPECT_EQ(kernel::tail<16>(max_size).size(), 15U);
}

// ---- windows -------------------------------------------------------------------------------------------------

TEST(KernelIndexTest, WindowsAllFitInsideTheExtent) {
  for (std::size_t n = 0; n <= 12; ++n) {
    for (std::size_t w = 0; w <= 14; ++w) {
      const auto ws = kernel::windows(n, w);
      const std::size_t expected = (w == 0 || w > n) ? 0 : n - w + 1;
      EXPECT_EQ(ws.size(), expected) << "n=" << n << " w=" << w;

      std::size_t count = 0;
      for (auto win : ws) {
        ASSERT_EQ(win.start(), count);
        ASSERT_EQ(win.size(), w);
        ASSERT_LE(win.start() + w, n);
        EXPECT_EQ(win.first().value(), win.start());
        EXPECT_EQ(win.last().value(), win.start() + w - 1);
        std::size_t inside = 0;
        for (auto i : win.indices()) {
          EXPECT_EQ(i.value(), win.start() + inside);
          EXPECT_LT(i.value(), n);
          ++inside;
        }
        EXPECT_EQ(inside, w);
        ++count;
      }
      EXPECT_EQ(count, expected);
    }
  }
}

TEST(KernelIndexTest, WindowsDoNotOverflowNearTheLargestSize) {
  EXPECT_EQ(kernel::windows(max_size, max_size).size(), 1U);
  EXPECT_EQ(kernel::windows(max_size, 1).size(), max_size);
  EXPECT_TRUE(kernel::windows(3, max_size).empty());
}

// ---- strided -------------------------------------------------------------------------------------------------

TEST(KernelIndexTest, StridedProducesEveryStepThIndex) {
  for (std::size_t n = 0; n <= 20; ++n) {
    for (std::size_t step = 0; step <= 22; ++step) {
      std::size_t count = 0;
      for (auto i : kernel::strided(n, step)) {
        ASSERT_EQ(i.value(), count * step);
        ASSERT_LT(i.value(), n);
        ++count;
      }
      std::size_t expected = 0;
      if (step != 0)
        for (std::size_t v = 0; v < n; v += step)
          ++expected;
      EXPECT_EQ(count, expected) << "n=" << n << " step=" << step;
      EXPECT_EQ(kernel::strided(n, step).size(), expected);
    }
  }
}

TEST(KernelIndexTest, StridedDoesNotOverflowNearTheLargestSize) {
  const std::size_t step = max_size / 2;
  std::size_t count = 0;
  std::size_t last = 0;
  for (auto i : kernel::strided(max_size, step)) {
    last = i.value();
    ++count;
  }
  EXPECT_EQ(count, 3U);
  EXPECT_LT(last, max_size);
  EXPECT_EQ(kernel::strided(max_size, max_size).size(), 1U);
}

// ---- inner-loop checks (only meaningful at RELOCO_KERNEL_CHECKS >= 1) -----------------------------------------

#if RELOCO_KERNEL_CHECKS >= 1
TEST(KernelIndexDeathTest, DereferencingEndAssertsWhenChecksAreEnabled) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  const auto d = kernel::domain(2);
  EXPECT_DEATH({ [[maybe_unused]] auto i = *d.end(); }, "");
}
#endif

TEST(KernelIndexTest, SubBlocksSplitABlockInOrder) {
  std::vector<std::size_t> bases;
  for (auto blk : kernel::blocks<6>(13)) // blocks at 0 and 6
    blk.for_each_sub<3>([&](auto sub) { bases.push_back(sub.base()); });
  EXPECT_EQ(bases, (std::vector<std::size_t>{0, 3, 6, 9}));
}

} // namespace

RELOCO_END_UNSAFE_BUFFER_USAGE
