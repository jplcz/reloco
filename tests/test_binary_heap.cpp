// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/binary_heap.hpp>

#include <functional>
#include <utility>

using reloco::binary_heap;
using reloco::error;
using reloco::vector;

TEST(BinaryHeapTest, TryCreateStartsEmpty) {
  auto h = binary_heap<int>::try_create();
  ASSERT_TRUE(h.has_value());
  EXPECT_TRUE(h->empty());
  EXPECT_EQ(h->size(), 0u);
}

TEST(BinaryHeapTest, PushAndPeekTracksMax) {
  auto h_res = binary_heap<int>::try_create();
  binary_heap<int> h = std::move(h_res.value());

  EXPECT_TRUE(h.try_push(5).has_value());
  EXPECT_EQ(h.peek(), 5);
  EXPECT_TRUE(h.try_push(9).has_value());
  EXPECT_EQ(h.peek(), 9);
  EXPECT_TRUE(h.try_push(1).has_value());
  EXPECT_EQ(h.peek(), 9);
  EXPECT_EQ(h.size(), 3u);
}

TEST(BinaryHeapTest, PopReturnsElementsInDescendingOrder) {
  auto h_res = binary_heap<int>::try_create();
  binary_heap<int> h = std::move(h_res.value());
  for (int v : {5, 1, 9, 3, 7}) {
    ASSERT_TRUE(h.try_push(v).has_value());
  }

  const int expected[] = {9, 7, 5, 3, 1};
  for (int e : expected) {
    auto popped = h.try_pop();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(popped.value(), e);
  }
  EXPECT_TRUE(h.empty());
}

TEST(BinaryHeapTest, TryPopFailsWhenEmpty) {
  auto h_res = binary_heap<int>::try_create();
  binary_heap<int> h = std::move(h_res.value());
  auto popped = h.try_pop();
  ASSERT_FALSE(popped.has_value());
  EXPECT_EQ(popped.error(), error::container_empty);
}

TEST(BinaryHeapTest, TryPeekFailsWhenEmpty) {
  auto h_res = binary_heap<int>::try_create();
  binary_heap<int> h = std::move(h_res.value());
  EXPECT_FALSE(h.try_peek().has_value());
}

TEST(BinaryHeapTest, MinHeapViaGreaterComparator) {
  auto h_res = binary_heap<int, std::greater<int>>::try_create();
  binary_heap<int, std::greater<int>> h = std::move(h_res.value());
  for (int v : {5, 1, 9, 3}) {
    ASSERT_TRUE(h.try_push(v).has_value());
  }
  EXPECT_EQ(h.peek(), 1);
}

TEST(BinaryHeapTest, IntoSortedVecConsumesTheHeap) {
  auto h_res = binary_heap<int>::try_create();
  binary_heap<int> h = std::move(h_res.value());
  for (int v : {5, 1, 9, 3, 7}) {
    ASSERT_TRUE(h.try_push(v).has_value());
  }

  vector<int> sorted = std::move(h).into_sorted_vec();
  ASSERT_EQ(sorted.size(), 5u);
  const int expected[] = {1, 3, 5, 7, 9};
  std::size_t i = 0;
  for (int e : expected) {
    EXPECT_EQ(sorted[i++], e);
  }
}

TEST(BinaryHeapTest, TryCloneProducesAnIndependentHeap) {
  auto h_res = binary_heap<int>::try_create();
  binary_heap<int> h = std::move(h_res.value());
  ASSERT_TRUE(h.try_push(42).has_value());

  auto clone_res = h.try_clone();
  ASSERT_TRUE(clone_res.has_value());
  EXPECT_EQ(clone_res->peek(), 42);

  ASSERT_TRUE(h.try_push(100).has_value());
  EXPECT_EQ(h.peek(), 100);
  EXPECT_EQ(clone_res->peek(), 42); // clone is unaffected
}

TEST(BinaryHeapTest, ClearRemovesAllElements) {
  auto h_res = binary_heap<int>::try_create();
  binary_heap<int> h = std::move(h_res.value());
  for (int v : {1, 2, 3}) {
    ASSERT_TRUE(h.try_push(v).has_value());
  }
  h.clear();
  EXPECT_TRUE(h.empty());
}

TEST(BinaryHeapTest, IterationVisitsEveryElement) {
  auto h_res = binary_heap<int>::try_create();
  binary_heap<int> h = std::move(h_res.value());
  for (int v : {1, 2, 3, 4}) {
    ASSERT_TRUE(h.try_push(v).has_value());
  }

  int sum = 0;
  for (int v : h) {
    sum += v;
  }
  EXPECT_EQ(sum, 10);
}

TEST(BinaryHeapTest, IsTriviallyRelocatable) {
  static_assert(reloco::is_trivially_relocatable<binary_heap<int>>::value,
                "binary_heap<int> should be trivially relocatable");
}
