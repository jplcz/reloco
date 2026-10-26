// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/kernel/kernel.hpp>
#include <reloco/span.hpp>

#include <cstddef>
#include <numeric>
#include <vector>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

namespace kernel = reloco::kernel;
struct brand_a {};
struct brand_b {};

TEST(KernelBindTest, ShortSpanIsRejected) {
  std::vector<int> a(8);
  std::vector<int> b(7);
  auto r = kernel::try_bind<brand_a>(8, reloco::span<int>(a), reloco::span<const int>(b));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::out_of_bounds);
}

TEST(KernelBindTest, LongerSpansAreAccepted) {
  std::vector<int> a(10);
  std::vector<int> b(8);
  auto r = kernel::try_bind<brand_a>(8, reloco::span<int>(a), reloco::span<const int>(b));
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value().size(), 8U);
}

TEST(KernelBindTest, OverlappingMutableSpanIsRejected) {
  std::vector<int> a(8);
  auto r = kernel::try_bind<brand_a>(4, reloco::span<int>(a).subspan(0, 6), reloco::span<const int>(a).subspan(2, 6));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::invalid_argument);
}

TEST(KernelBindTest, ReadOnlyOverlapIsAccepted) {
  std::vector<int> a(8);
  auto r = kernel::try_bind<brand_a>(4, reloco::span<const int>(a), reloco::span<const int>(a));
  EXPECT_TRUE(r.has_value());
}

TEST(KernelBindTest, DisjointMutableSpansAreAccepted) {
  std::vector<int> a(8);
  auto r = kernel::try_bind<brand_a>(4, reloco::span<int>(a).subspan(0, 4), reloco::span<int>(a).subspan(4, 4));
  EXPECT_TRUE(r.has_value());
}

TEST(KernelBindTest, ExactAliasNeedsOptIn) {
  std::vector<int> a(8);
  auto strict = kernel::try_bind<brand_a>(8, reloco::span<int>(a), reloco::span<const int>(a));
  EXPECT_FALSE(strict.has_value());
  auto loose = kernel::try_bind<brand_a>(kernel::allow_alias, 8, reloco::span<int>(a), reloco::span<const int>(a));
  EXPECT_TRUE(loose.has_value());
}

TEST(KernelBindTest, PartialAliasIsRejectedEvenWithOptIn) {
  std::vector<int> a(8);
  auto r = kernel::try_bind<brand_a>(kernel::allow_alias, 4, reloco::span<int>(a).subspan(0, 6),
                                     reloco::span<const int>(a).subspan(2, 6));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::invalid_argument);
}

TEST(KernelBindTest, DifferentElementTypeAliasIsRejectedEvenWithOptIn) {
  std::vector<int> a(8);
  reloco::span<int> ints(a);
  auto r = kernel::try_bind<brand_a>(kernel::allow_alias, 4, ints,
                                     reloco::span<const unsigned>(reinterpret_cast<const unsigned *>(a.data()), 8));
  ASSERT_FALSE(r.has_value());
}

TEST(KernelBindTest, EmptyBindingWorks) {
  std::vector<int> a;
  auto r = kernel::try_bind<brand_a>(0, reloco::span<int>(a));
  ASSERT_TRUE(r.has_value());
  std::size_t visited = 0;
  for (auto i : r.value().domain()) {
    (void)i;
    ++visited;
  }
  EXPECT_EQ(visited, 0U);
}

TEST(KernelBindTest, DomainIndexesAddressViews) {
  std::vector<int> in{1, 2, 3, 4, 5, 6, 7};
  std::vector<int> out(7, 0);
  auto r = kernel::try_bind<brand_a>(7, reloco::span<int>(out), reloco::span<const int>(in));
  ASSERT_TRUE(r.has_value());
  auto k = r.value();
  auto dst = k.view<0>();
  auto src = k.view<1>();
  for (auto i : k.domain())
    dst(i) = src(i) * 2;
  for (std::size_t i = 0; i < 7; ++i)
    EXPECT_EQ(out[i], in[i] * 2);
}

TEST(KernelBindTest, BlocksAndTailCoverEverything) {
  std::vector<int> in{1, 2, 3, 4, 5, 6, 7};
  std::vector<int> out(7, 0);
  auto r = kernel::try_bind<brand_a>(7, reloco::span<int>(out), reloco::span<const int>(in));
  ASSERT_TRUE(r.has_value());
  auto k = r.value();
  auto dst = k.view<0>();
  auto src = k.view<1>();
  for (auto blk : k.blocks<4>())
    dst.store(blk, src.load(blk));
  for (auto i : k.tail<4>())
    dst(i) = src(i);
  EXPECT_EQ(out, in);
}

TEST(KernelBindTest, SubBlocksCoverTheirBlockInOrder) {
  std::vector<int> in(20);
  std::iota(in.begin(), in.end(), 0);
  std::vector<int> out(20, -1);
  auto r = kernel::try_bind<brand_b>(20, reloco::span<int>(out), reloco::span<const int>(in));
  ASSERT_TRUE(r.has_value());
  auto dst = r->view<0>();
  auto src = r->view<1>();
  for (auto blk : r->blocks<8>()) // two blocks of 8, split into pairs
    blk.for_each_sub<2>([&](auto pair) { dst.store(pair, src.load(pair)); });
  for (auto i : r->tail<8>())
    dst(i) = src(i);
  EXPECT_EQ(out, in);
}

TEST(KernelBindTest, WindowsGiveSubspans) {
  std::vector<int> in{1, 2, 3, 4, 5};
  std::vector<int> out(5, 0);
  auto r = kernel::try_bind<brand_a>(5, reloco::span<int>(out), reloco::span<const int>(in));
  ASSERT_TRUE(r.has_value());
  auto k = r.value();
  auto src = k.view<1>();
  std::size_t n = 0;
  for (auto w : k.windows(3)) {
    auto s = src.subspan(w);
    ASSERT_EQ(s.size(), 3U);
    EXPECT_EQ(s.unsafe_at(0), in[n]);
    ++n;
  }
  EXPECT_EQ(n, 3U);
}

TEST(KernelBindTest, MacroMakesIndependentBrands) {
  std::vector<int> a(4);
  auto r1 = RELOCO_KERNEL_BIND(4, reloco::span<int>(a));
  auto r2 = RELOCO_KERNEL_BIND(4, reloco::span<int>(a));
  static_assert(!std::is_same_v<decltype(r1), decltype(r2)>);
  EXPECT_TRUE(r1.has_value());
  EXPECT_TRUE(r2.has_value());
}

TEST(KernelBindTest, BrandsDoNotMix) {
  using view_a = kernel::bound_span<int, brand_a>;
  static_assert(std::is_invocable_v<view_a &, kernel::proven_index<brand_a>>);
  static_assert(!std::is_invocable_v<view_a &, kernel::proven_index<brand_b>>);
  static_assert(!std::is_invocable_v<view_a &, kernel::proven_index<kernel::unbranded>>);
}

TEST(KernelBindTest, WithBoundRunsAndReturns) {
  std::vector<int> in{1, 2, 3, 4};
  std::vector<int> out(4, 0);
  auto r = kernel::with_bound(
      4,
      [](auto &k) {
        auto dst = k.template view<0>();
        auto src = k.template view<1>();
        int sum = 0;
        for (auto i : k.domain()) {
          dst(i) = src(i);
          sum += src(i);
        }
        return sum;
      },
      reloco::span<int>(out), reloco::span<const int>(in));
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value(), 10);
  EXPECT_EQ(out, in);
}

TEST(KernelBindTest, WithBoundReportsErrorsWithoutRunning) {
  std::vector<int> a(2);
  bool ran = false;
  auto r = kernel::with_bound(4, [&ran](auto &) { ran = true; }, reloco::span<int>(a));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), reloco::error::out_of_bounds);
  EXPECT_FALSE(ran);
}

#if RELOCO_KERNEL_CHECKS >= 1
TEST(KernelBindTest, BlockwiseTableNeedsOneEntryPerFullBlock) {
  std::vector<int> data(10), small(2), exact(2), big(9);
  auto bound = RELOCO_KERNEL_BIND(data.size(), reloco::span<int>(data));
  ASSERT_TRUE(bound.has_value());
  // 10 elements hold 2 full blocks of 4.
  EXPECT_EQ(bound->try_blockwise<4>(reloco::span<int>(small).first(1)).error(), reloco::error::out_of_bounds);
  EXPECT_TRUE(bound->try_blockwise<4>(reloco::span<int>(exact)).has_value());
  EXPECT_TRUE(bound->try_blockwise<2>(reloco::span<int>(big)).has_value());
  EXPECT_EQ(bound->try_blockwise<2>(reloco::span<int>(small)).error(), reloco::error::out_of_bounds);
}

TEST(KernelBindTest, BlockwiseTableIsIndexedByBlockNumber) {
  std::vector<int> data(9, 0);
  const std::vector<int> table{10, 20, 30};
  auto bound = RELOCO_KERNEL_BIND(data.size(), reloco::span<int>(data));
  ASSERT_TRUE(bound.has_value());
  auto t = bound->try_blockwise<3>(reloco::span<const int>(table));
  ASSERT_TRUE(t.has_value());
  auto out = bound->view<0>();
  for (auto blk : bound->blocks<3>())
    blk.for_each_lane([&](auto i) { out(i) = t.value()(blk); });
  EXPECT_EQ(data, (std::vector<int>{10, 10, 10, 20, 20, 20, 30, 30, 30}));
}

TEST(KernelBindTest, BlockwiseTableMustNotOverlapAWritableSpan) {
  std::vector<int> data(8);
  auto bound = RELOCO_KERNEL_BIND(data.size(), reloco::span<int>(data));
  ASSERT_TRUE(bound.has_value());
  EXPECT_EQ(bound->try_blockwise<2>(reloco::span<const int>(data).first(4)).error(), reloco::error::invalid_argument);
  EXPECT_EQ(bound->try_blockwise<2>(reloco::span<int>(data).first(4)).error(), reloco::error::invalid_argument);
}

TEST(KernelBindTest, BlockwiseReadOnlyOverlapIsAccepted) {
  const std::vector<int> data(8, 1);
  auto bound = RELOCO_KERNEL_BIND(data.size(), reloco::span<const int>(data));
  ASSERT_TRUE(bound.has_value());
  EXPECT_TRUE(bound->try_blockwise<2>(reloco::span<const int>(data).first(4)).has_value());
}

TEST(KernelBindDeathTest, IndexFromALargerRunIsCaughtWhenChecked) {
  std::vector<int> big(8);
  std::vector<int> small(2);
  auto binder = [](std::vector<int> &v, std::size_t n) { return kernel::try_bind<brand_a>(n, reloco::span<int>(v)); };
  auto rb = binder(big, 8);
  auto rs = binder(small, 2);
  ASSERT_TRUE(rb.has_value());
  ASSERT_TRUE(rs.has_value());
  std::vector<kernel::proven_index<brand_a>> indexes;
  for (auto i : rb.value().domain())
    indexes.push_back(i);
  auto small_view = rs.value().view<0>();
  EXPECT_DEATH(small_view(indexes.back()) = 1, "");
}
#endif

} // namespace

RELOCO_END_UNSAFE_BUFFER_USAGE
