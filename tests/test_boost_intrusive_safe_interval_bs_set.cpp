// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/boost_intrusive_safe_bs_set_hook.hpp"
#include "reloco/boost_intrusive_safe_interval_bs_set.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace {

struct vma : public reloco::boost_intrusive::safe_bs_set_hook<> {
  std::uintptr_t start;
  std::uintptr_t end;
  bool is_free;

  vma(std::uintptr_t s, std::uintptr_t e, bool free = false) : start(s), end(e), is_free(free) {}
};

[[nodiscard]] auto make_vma_set() noexcept {
  return reloco::boost_intrusive::make_safe_interval_bs_set<vma>([](const vma &v) { return v.start; },
                                                                 [](const vma &v) { return v.end; });
}

// Deterministic stand-in for a real PRNG: always returns a fixed draw, so
// tests can pin down exactly which slot `find_random_gap()` should land on.
struct fixed_rng {
  std::uintptr_t value;
  std::uintptr_t operator()() const noexcept { return value; }
};

constexpr auto vma_setter = [](vma &v, std::uintptr_t start, std::uintptr_t end) {
  v.start = start;
  v.end = end;
};

} // namespace

TEST(BoostIntrusiveSafeIntervalBsSetTest, InsertSortedKeepsAscendingOrderRegardlessOfInsertionOrder) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x3000, 0x4000), c(0x5000, 0x6000);
  ASSERT_TRUE(vmas.insert_sorted(c));
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));

  auto it = vmas.begin();
  EXPECT_EQ((it++)->start, 0x1000u);
  EXPECT_EQ((it++)->start, 0x3000u);
  EXPECT_EQ((it++)->start, 0x5000u);
  EXPECT_EQ(it, vmas.end());

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
  vmas.erase(vmas.iterator_to(c));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, InsertSortedFailsOnDuplicateStart) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000);
  vma dup(0x1000, 0x1800);
  ASSERT_TRUE(vmas.insert_sorted(a));
  EXPECT_FALSE(vmas.insert_sorted(dup));

  vmas.erase(vmas.iterator_to(a));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, TryResizeGrowsEndInPlaceWhenItFits) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x4000, 0x5000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));

  auto result = vmas.try_resize(a, 0x3000, vma_setter);
  ASSERT_TRUE(result);
  EXPECT_EQ(*result, &a);
  EXPECT_EQ(a.start, 0x1000u);
  EXPECT_EQ(a.end, 0x3000u);

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, TryResizeFailsAndLeavesNodeUntouchedWhenItWouldOverlap) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x4000, 0x5000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));

  auto result = vmas.try_resize(a, 0x4500, vma_setter);
  EXPECT_FALSE(result);
  EXPECT_EQ(a.start, 0x1000u);
  EXPECT_EQ(a.end, 0x2000u); // Untouched: resizer must not run on failure.

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, TryResizeFailsOnEmptyOrInvertedRange) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000);
  ASSERT_TRUE(vmas.insert_sorted(a));

  EXPECT_FALSE(vmas.try_resize(a, 0x3000, 0x3000, vma_setter));
  EXPECT_FALSE(vmas.try_resize(a, 0x3000, 0x2000, vma_setter));
  EXPECT_EQ(a.start, 0x1000u);
  EXPECT_EQ(a.end, 0x2000u);

  vmas.erase(vmas.iterator_to(a));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, TryResizeCanMoveStartAndReordersToKeepAscendingInvariant) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x4000, 0x5000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));

  // Move `a` entirely past `b`: must not collide with `b`, and must
  // reorder so iteration order still reflects ascending start_of().
  auto result = vmas.try_resize(a, 0x6000, 0x7000, vma_setter);
  ASSERT_TRUE(result);

  auto it = vmas.begin();
  EXPECT_EQ(it->start, 0x4000u); // `b` is now first.
  ++it;
  EXPECT_EQ(it->start, 0x6000u); // `a` is now second.
  ++it;
  EXPECT_EQ(it, vmas.end());

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, TryRebaseMovesIntervalKeepingSizeUnchanged) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x4000, 0x5000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));

  auto result = vmas.try_rebase(a, 0x6000, vma_setter);
  ASSERT_TRUE(result);
  EXPECT_EQ(*result, &a);
  EXPECT_EQ(a.start, 0x6000u);
  EXPECT_EQ(a.end, 0x7000u); // Original 0x1000 size preserved.

  auto it = vmas.begin();
  EXPECT_EQ(it->start, 0x4000u); // `b` is now first.
  ++it;
  EXPECT_EQ(it->start, 0x6000u); // `a` is now second.

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, TryRebaseFailsAndLeavesNodeUntouchedWhenItWouldOverlap) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x4000, 0x5000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));

  auto result = vmas.try_rebase(a, 0x4500, vma_setter);
  EXPECT_FALSE(result);
  EXPECT_EQ(a.start, 0x1000u); // Untouched: resizer must not run on failure.
  EXPECT_EQ(a.end, 0x2000u);

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, TryRebaseFailsOnAddressOverflow) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000);
  ASSERT_TRUE(vmas.insert_sorted(a));

  auto result = vmas.try_rebase(a, std::numeric_limits<std::uintptr_t>::max() - 10, vma_setter);
  EXPECT_FALSE(result);
  EXPECT_EQ(a.start, 0x1000u);
  EXPECT_EQ(a.end, 0x2000u);

  vmas.erase(vmas.iterator_to(a));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, FindContainingLocatesCoveringInterval) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x3000, 0x4000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));

  auto found = vmas.find_containing(0x3500);
  ASSERT_TRUE(found);
  EXPECT_EQ(*found, &b);

  EXPECT_FALSE(vmas.find_containing(0x2500));

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, FindContainingFailsBeforeFirstInterval) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000);
  ASSERT_TRUE(vmas.insert_sorted(a));

  EXPECT_FALSE(vmas.find_containing(0x500));

  vmas.erase(vmas.iterator_to(a));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, FindOverlapDetectsCandidateRangeCollision) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000);
  ASSERT_TRUE(vmas.insert_sorted(a));

  auto overlap = vmas.find_overlap(0x1800, 0x2200);
  ASSERT_TRUE(overlap);
  EXPECT_EQ(*overlap, &a);

  EXPECT_FALSE(vmas.find_overlap(0x2000, 0x3000));

  vmas.erase(vmas.iterator_to(a));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, FindGapLocatesFirstFitAlignedGapBetweenIntervals) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x3000, 0x4000), c(0x5000, 0x6000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));
  ASSERT_TRUE(vmas.insert_sorted(c));

  auto gap = vmas.find_gap(0x1500, 0x7000, 0x500, 0x1000);
  ASSERT_TRUE(gap);
  EXPECT_EQ(gap->start, 0x2000u);
  EXPECT_EQ(gap->end, 0x3000u);
  EXPECT_EQ(gap->size(), 0x1000u);

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
  vmas.erase(vmas.iterator_to(c));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, FindGapFailsWhenNoGapIsLargeEnough) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x3000, 0x4000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));

  EXPECT_FALSE(vmas.find_gap(0x1500, 0x7000, 0x5000, 0x1000));

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, FindGapCanReturnLeadingGapBeforeFirstInterval) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000);
  ASSERT_TRUE(vmas.insert_sorted(a));

  auto gap = vmas.find_gap(0x0, 0x2000, 0x500, 0x1000);
  ASSERT_TRUE(gap);
  EXPECT_EQ(gap->start, 0u);
  EXPECT_EQ(gap->end, 0x1000u);

  vmas.erase(vmas.iterator_to(a));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, ForEachGapVisitsEveryGapInAscendingOrder) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x3000, 0x4000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));

  std::vector<std::pair<std::uintptr_t, std::uintptr_t>> gaps;
  vmas.for_each_gap(0x0, 0x5000, [&](std::uintptr_t gap_start, std::uintptr_t gap_end) {
    gaps.emplace_back(gap_start, gap_end);
    return false; // Keep scanning every gap.
  });

  ASSERT_EQ(gaps.size(), 3u);
  EXPECT_EQ(gaps[0], (std::pair<std::uintptr_t, std::uintptr_t>{0x0, 0x1000}));
  EXPECT_EQ(gaps[1], (std::pair<std::uintptr_t, std::uintptr_t>{0x2000, 0x3000}));
  EXPECT_EQ(gaps[2], (std::pair<std::uintptr_t, std::uintptr_t>{0x4000, 0x5000}));

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, ForEachGapStopsEarlyWhenVisitorReturnsTrue) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x3000, 0x4000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));

  int visited = 0;
  vmas.for_each_gap(0x0, 0x5000, [&](std::uintptr_t, std::uintptr_t) {
    ++visited;
    return true; // Stop after the first gap.
  });
  EXPECT_EQ(visited, 1);

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, MergeAdjacentCoalescesChainedContiguousFreeRegions) {
  auto vmas = make_vma_set();
  auto *f1 = new vma(0x1000, 0x2000, /*free=*/true);
  auto *f2 = new vma(0x2000, 0x3000, /*free=*/true);
  auto *f3 = new vma(0x3000, 0x4000, /*free=*/true);
  auto *busy = new vma(0x4000, 0x5000, /*free=*/false);
  ASSERT_TRUE(vmas.insert_sorted(*f1));
  ASSERT_TRUE(vmas.insert_sorted(*f2));
  ASSERT_TRUE(vmas.insert_sorted(*f3));
  ASSERT_TRUE(vmas.insert_sorted(*busy));

  int disposed = 0;
  vmas.merge_adjacent([](const vma &x, const vma &y) { return x.is_free && y.is_free && x.end == y.start; },
                      [](vma &x, const vma &y) { x.end = y.end; },
                      [&](vma *y) {
                        ++disposed;
                        delete y;
                      });

  EXPECT_EQ(disposed, 2);
  ASSERT_EQ(vmas.size(), 2u);
  auto it = vmas.begin();
  EXPECT_EQ(it->start, 0x1000u);
  EXPECT_EQ(it->end, 0x4000u);
  EXPECT_TRUE(it->is_free);
  ++it;
  EXPECT_EQ(it->start, 0x4000u);
  EXPECT_EQ(it->end, 0x5000u);
  EXPECT_FALSE(it->is_free);

  vmas.erase(vmas.iterator_to(*f1));
  vmas.erase(vmas.iterator_to(*busy));
  delete f1;
  delete busy;
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, MergeAdjacentLeavesNonMergeableNeighborsIntact) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000, /*free=*/true);
  vma b(0x2000, 0x3000, /*free=*/false);
  vma c(0x3000, 0x4000, /*free=*/true);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));
  ASSERT_TRUE(vmas.insert_sorted(c));

  int disposed = 0;
  vmas.merge_adjacent([](const vma &x, const vma &y) { return x.is_free && y.is_free && x.end == y.start; },
                      [](vma &x, const vma &y) { x.end = y.end; }, [&](vma *) { ++disposed; });

  EXPECT_EQ(disposed, 0);
  EXPECT_EQ(vmas.size(), 3u);

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
  vmas.erase(vmas.iterator_to(c));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, FindRandomGapCoversEveryDistinctSlotAcrossAllGaps) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x3000, 0x4000), c(0x5000, 0x6000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));
  ASSERT_TRUE(vmas.insert_sorted(c));

  std::set<std::uintptr_t> seen_starts;
  for (std::uintptr_t pick = 0; pick < 16; ++pick) {
    auto gap = vmas.find_random_gap(0x0, 0x7000, 0x400, 0x400, fixed_rng{pick});
    ASSERT_TRUE(gap);
    EXPECT_EQ(gap->size(), 0x400u);
    seen_starts.insert(gap->start);
  }
  EXPECT_EQ(seen_starts.size(), 16u);

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
  vmas.erase(vmas.iterator_to(c));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, FindRandomGapWrapsDrawViaModulo) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x3000, 0x4000), c(0x5000, 0x6000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));
  ASSERT_TRUE(vmas.insert_sorted(c));

  auto gap_pick_0 = vmas.find_random_gap(0x0, 0x7000, 0x400, 0x400, fixed_rng{0});
  auto gap_pick_16 = vmas.find_random_gap(0x0, 0x7000, 0x400, 0x400, fixed_rng{16});
  ASSERT_TRUE(gap_pick_0);
  ASSERT_TRUE(gap_pick_16);
  EXPECT_EQ(gap_pick_0->start, gap_pick_16->start);

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
  vmas.erase(vmas.iterator_to(c));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, FindRandomGapFailsWhenNoGapIsLargeEnough) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000), b(0x3000, 0x4000);
  ASSERT_TRUE(vmas.insert_sorted(a));
  ASSERT_TRUE(vmas.insert_sorted(b));

  EXPECT_FALSE(vmas.find_random_gap(0x1500, 0x7000, 0x5000, 0x1000, fixed_rng{0}));

  vmas.erase(vmas.iterator_to(a));
  vmas.erase(vmas.iterator_to(b));
}

TEST(BoostIntrusiveSafeIntervalBsSetTest, FindRandomGapAcceptsStatelessLambdaRng) {
  auto vmas = make_vma_set();
  vma a(0x1000, 0x2000);
  ASSERT_TRUE(vmas.insert_sorted(a));

  auto gap = vmas.find_random_gap(0x0, 0x3000, 0x400, 0x400, [] { return std::uintptr_t{2}; });
  ASSERT_TRUE(gap);
  EXPECT_EQ(gap->size(), 0x400u);

  vmas.erase(vmas.iterator_to(a));
}
