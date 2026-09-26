// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/sso_vec_deque.hpp>

using namespace reloco;

TEST(SsoVecDequeTest, StaysInlineUntilCapacityExceeded) {
  sso_vec_deque<int, 4> d;
  EXPECT_TRUE(d.is_inline());

  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  ASSERT_TRUE(d.try_push_back(4).has_value());
  EXPECT_TRUE(d.is_inline());
  EXPECT_EQ(d.size(), 4);

  // Growing past InlineCapacity promotes to the heap.
  ASSERT_TRUE(d.try_push_back(5).has_value());
  EXPECT_FALSE(d.is_inline());
  EXPECT_EQ(d.size(), 5);
  for (int i = 0; i < 5; ++i)
    EXPECT_EQ(d[static_cast<std::size_t>(i)], i + 1);
}

TEST(SsoVecDequeTest, PromotionPreservesWrappedOrder) {
  sso_vec_deque<int, 4> d;
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  ASSERT_TRUE(d.try_push_back(4).has_value());

  // Wrap the inline buffer: pop 2 from front, push 2 to back.
  ASSERT_TRUE(d.try_pop_front().has_value());
  ASSERT_TRUE(d.try_pop_front().has_value());
  ASSERT_TRUE(d.try_push_back(5).has_value());
  ASSERT_TRUE(d.try_push_back(6).has_value());
  // Logically: [3, 4, 5, 6], physically wrapped.

  // Force promotion to heap while wrapped.
  ASSERT_TRUE(d.try_push_back(7).has_value());
  EXPECT_FALSE(d.is_inline());
  EXPECT_EQ(d.size(), 5);
  EXPECT_EQ(d[0], 3);
  EXPECT_EQ(d[1], 4);
  EXPECT_EQ(d[2], 5);
  EXPECT_EQ(d[3], 6);
  EXPECT_EQ(d[4], 7);
}

TEST(SsoVecDequeTest, TryCloneDeepCopiesAcrossInlineAndHeap) {
  auto d_res = sso_vec_deque<int, 2>::try_create();
  ASSERT_TRUE(d_res.has_value());
  auto &d = *d_res;

  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value()); // promotes to heap

  auto clone_res = d.try_clone();
  ASSERT_TRUE(clone_res.has_value());
  auto &clone = *clone_res;
  EXPECT_EQ(clone.size(), 3);
  EXPECT_EQ(clone[0], 1);
  EXPECT_EQ(clone[1], 2);
  EXPECT_EQ(clone[2], 3);

  ASSERT_TRUE(clone.try_push_back(99).has_value());
  EXPECT_EQ(d.size(), 3); // original untouched
}

TEST(SsoVecDequeTest, InsertAtAndSwapRemove) {
  sso_vec_deque<int, 4> d;
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(4).has_value());

  ASSERT_TRUE(d.try_insert_at(2, 3).has_value());
  EXPECT_EQ(d.size(), 4);
  EXPECT_EQ(d[0], 1);
  EXPECT_EQ(d[1], 2);
  EXPECT_EQ(d[2], 3);
  EXPECT_EQ(d[3], 4);

  ASSERT_TRUE(d.try_swap_remove_front(2).has_value());
  EXPECT_EQ(d.size(), 3);
}

TEST(SsoVecDequeTest, MoveConstructAndAssign) {
  sso_vec_deque<int, 4> d;
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  ASSERT_TRUE(d.try_push_back(4).has_value());
  ASSERT_TRUE(d.try_push_back(5).has_value()); // heap-backed

  sso_vec_deque<int, 4> moved(std::move(d));
  EXPECT_EQ(moved.size(), 5);
  EXPECT_EQ(moved[0], 1);
  EXPECT_EQ(moved[4], 5);

  sso_vec_deque<int, 4> other;
  ASSERT_TRUE(other.try_push_back(42).has_value());
  other = std::move(moved);
  EXPECT_EQ(other.size(), 5);
  EXPECT_EQ(other[0], 1);
}
