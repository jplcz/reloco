// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/inline_vec_deque.hpp>

using namespace reloco;

TEST(InlineVecDequeTest, BasicPushAndPop) {
  inline_vec_deque<int, 4> d;

  EXPECT_TRUE(d.empty());
  EXPECT_EQ(d.size(), 0);
  EXPECT_EQ(d.capacity(), 4);

  ASSERT_TRUE(d.try_push_back(10).has_value());
  ASSERT_TRUE(d.try_push_back(20).has_value());
  ASSERT_TRUE(d.try_push_front(5).has_value());
  EXPECT_EQ(d.size(), 3);
  EXPECT_EQ(d[0], 5);
  EXPECT_EQ(d[1], 10);
  EXPECT_EQ(d[2], 20);

  ASSERT_TRUE(d.try_pop_back().has_value());
  EXPECT_EQ(d.back(), 10);
  ASSERT_TRUE(d.try_pop_front().has_value());
  EXPECT_EQ(d.front(), 10);
  EXPECT_EQ(d.size(), 1);
}

TEST(InlineVecDequeTest, CapacityExceededFails) {
  inline_vec_deque<int, 2> d;
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  EXPECT_TRUE(d.full());

  auto res = d.try_push_back(3);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::capacity_exceeded);

  auto res2 = d.try_push_front(3);
  ASSERT_FALSE(res2.has_value());
  EXPECT_EQ(res2.error(), error::capacity_exceeded);
}

TEST(InlineVecDequeTest, WrapAroundAndInsertRotate) {
  inline_vec_deque<int, 4> d;
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  ASSERT_TRUE(d.try_push_back(4).has_value());

  // Pop 2 from front, push 2 to back -> physically wrapped, logically [3, 4, 5, 6]
  ASSERT_TRUE(d.try_pop_front().has_value());
  ASSERT_TRUE(d.try_pop_front().has_value());
  ASSERT_TRUE(d.try_push_back(5).has_value());
  ASSERT_TRUE(d.try_push_back(6).has_value());

  auto [s1, s2] = d.as_slices();
  EXPECT_FALSE(s2.empty()); // confirms wrap-around

  EXPECT_TRUE(d.contains(5));
  EXPECT_FALSE(d.contains(1));

  ASSERT_TRUE(d.try_erase_at(1).has_value()); // remove '4'
  EXPECT_EQ(d.size(), 3);
  EXPECT_EQ(d[0], 3);
  EXPECT_EQ(d[1], 5);
  EXPECT_EQ(d[2], 6);
}

TEST(InlineVecDequeTest, TryCloneDeepCopies) {
  inline_vec_deque<int, 4> d;
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_pop_front().has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  ASSERT_TRUE(d.try_push_back(4).has_value());
  // Logically: [2, 3, 4]

  auto clone_res = d.try_clone();
  ASSERT_TRUE(clone_res.has_value());
  auto &clone = *clone_res;
  EXPECT_EQ(clone.size(), 3);
  EXPECT_EQ(clone[0], 2);
  EXPECT_EQ(clone[1], 3);
  EXPECT_EQ(clone[2], 4);

  // Mutating the clone must not affect the original.
  ASSERT_TRUE(clone.try_push_back(99).has_value());
  EXPECT_EQ(d.size(), 3);
}

TEST(InlineVecDequeTest, MoveConstructAndAssign) {
  inline_vec_deque<int, 4> d;
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());

  inline_vec_deque<int, 4> moved(std::move(d));
  EXPECT_EQ(moved.size(), 2);
  EXPECT_EQ(moved[0], 1);
  EXPECT_EQ(moved[1], 2);

  inline_vec_deque<int, 4> other;
  ASSERT_TRUE(other.try_push_back(42).has_value());
  other = std::move(moved);
  EXPECT_EQ(other.size(), 2);
  EXPECT_EQ(other[0], 1);
}

TEST(InlineVecDequeTest, TryToVecDequeClonesAndMoves) {
  inline_vec_deque<int, 4> d;
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());

  auto cloned_res = d.try_to_vec_deque();
  ASSERT_TRUE(cloned_res.has_value());
  EXPECT_EQ(cloned_res->size(), 3);
  EXPECT_EQ(d.size(), 3); // untouched

  auto moved_res = std::move(d).try_to_vec_deque();
  ASSERT_TRUE(moved_res.has_value());
  EXPECT_EQ(moved_res->size(), 3);
  EXPECT_EQ((*moved_res)[0], 1);
  EXPECT_TRUE(d.empty()); // consumed
}
