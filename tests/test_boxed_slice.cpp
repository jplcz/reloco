// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/boxed_slice.hpp>
#include <reloco/vector.hpp>

#include <utility>

using reloco::boxed_slice;
using reloco::error;
using reloco::vector;

namespace {

// Every allocation request fails, exercising try_allocate's failure path.
struct exhausted_allocator_tag {};

} // namespace

template <> struct reloco::allocator_traits<exhausted_allocator_tag> {
  using context_type = void;

  static reloco::result<reloco::mem_block> allocate(std::size_t, std::size_t) noexcept {
    return reloco::unexpected(reloco::error::allocation_failed);
  }

  static void deallocate(void *, std::size_t) noexcept {}
};

TEST(BoxedSliceTest, DefaultConstructedIsEmpty) {
  boxed_slice<int> bs;
  EXPECT_TRUE(bs.empty());
  EXPECT_EQ(bs.size(), 0u);
  EXPECT_EQ(bs.data(), nullptr);
}

TEST(BoxedSliceTest, TryCreateDefaultConstructsEachElement) {
  auto res = boxed_slice<int>::try_create(5);
  ASSERT_TRUE(res.has_value());
  boxed_slice<int> bs = std::move(res.value());
  EXPECT_EQ(bs.size(), 5u);
  for (std::size_t i = 0; i < bs.size(); ++i) {
    EXPECT_EQ(bs[i], 0);
  }
}

TEST(BoxedSliceTest, TryCreateWithFillValue) {
  auto res = boxed_slice<int>::try_create(3, 7);
  ASSERT_TRUE(res.has_value());
  boxed_slice<int> bs = std::move(res.value());
  ASSERT_EQ(bs.size(), 3u);
  for (int v : bs) {
    EXPECT_EQ(v, 7);
  }
}

TEST(BoxedSliceTest, TryCreateZeroDoesNotAllocate) {
  auto res = boxed_slice<int>::try_create(0);
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(res->empty());
  EXPECT_EQ(res->data(), nullptr);
}

TEST(BoxedSliceTest, TryAllocateReportsAllocationFailure) {
  reloco::allocator<exhausted_allocator_tag> alloc;
  auto res = boxed_slice<int>::try_allocate(alloc.ref(), 4);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::allocation_failed);
}

TEST(BoxedSliceTest, TryFromVectorMovesElementsAndShrinksToExactSize) {
  auto v_res = vector<int>::try_create();
  ASSERT_TRUE(v_res.has_value());
  vector<int> v = std::move(v_res.value());
  for (int x : {1, 2, 3, 4}) {
    ASSERT_TRUE(v.try_push_back(x).has_value());
  }

  auto res = boxed_slice<int>::try_from_vector(std::move(v));
  ASSERT_TRUE(res.has_value());
  boxed_slice<int> bs = std::move(res.value());
  ASSERT_EQ(bs.size(), 4u);
  EXPECT_EQ(bs[0], 1);
  EXPECT_EQ(bs[3], 4);
}

TEST(BoxedSliceTest, TryFromVectorOfEmptyVectorIsEmpty) {
  auto v_res = vector<int>::try_create();
  vector<int> v = std::move(v_res.value());
  auto res = boxed_slice<int>::try_from_vector(std::move(v));
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(res->empty());
}

TEST(BoxedSliceTest, TryAtChecksBounds) {
  auto res = boxed_slice<int>::try_create(2, 9);
  boxed_slice<int> bs = std::move(res.value());
  EXPECT_TRUE(bs.try_at(0).has_value());
  auto oob = bs.try_at(5);
  ASSERT_FALSE(oob.has_value());
  EXPECT_EQ(oob.error(), error::out_of_bounds);
}

TEST(BoxedSliceTest, FrontAndBack) {
  auto res = boxed_slice<int>::try_create(3, 0);
  boxed_slice<int> bs = std::move(res.value());
  bs[0] = 10;
  bs[2] = 30;
  EXPECT_EQ(bs.front(), 10);
  EXPECT_EQ(bs.back(), 30);
}

TEST(BoxedSliceTest, MoveConstructionTransfersOwnership) {
  auto res = boxed_slice<int>::try_create(2, 5);
  boxed_slice<int> bs = std::move(res.value());
  boxed_slice<int> bs2 = std::move(bs);
  EXPECT_TRUE(bs.empty());
  EXPECT_EQ(bs2.size(), 2u);
}

TEST(BoxedSliceTest, MoveAssignmentReleasesPreviousStorage) {
  auto res1 = boxed_slice<int>::try_create(2, 1);
  auto res2 = boxed_slice<int>::try_create(3, 2);
  boxed_slice<int> bs1 = std::move(res1.value());
  boxed_slice<int> bs2 = std::move(res2.value());
  bs1 = std::move(bs2);
  EXPECT_EQ(bs1.size(), 3u);
  EXPECT_TRUE(bs2.empty());
}

TEST(BoxedSliceTest, IsTriviallyRelocatable) {
  static_assert(reloco::is_trivially_relocatable<boxed_slice<int>>::value,
                "boxed_slice<int> should be trivially relocatable");
}
