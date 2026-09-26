// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/alignment.hpp>
#include <reloco/outline_vec_deque.hpp>
#include <reloco/span.hpp>

#include <cstddef>
#include <type_traits>

using reloco::error;
using reloco::outline_vec_deque;
using reloco::span;

namespace {

template <typename T, std::size_t Capacity> struct backing_storage {
  alignas(reloco::effective_alignment_v<T>) std::byte bytes[sizeof(T) * Capacity];

  span<std::byte> as_span() noexcept { return span<std::byte>(bytes); }
};

} // namespace

TEST(OutlineVecDequeTest, ConstructedFromSpanIsEmptyWithSpanCapacity) {
  backing_storage<int, 4> storage;
  outline_vec_deque<int> d(storage.as_span());
  EXPECT_TRUE(d.empty());
  EXPECT_EQ(d.size(), 0u);
  EXPECT_EQ(d.capacity(), 4u);
  EXPECT_FALSE(d.full());
}

TEST(OutlineVecDequeTest, PushFrontAndBackUntilFull) {
  backing_storage<int, 3> storage;
  outline_vec_deque<int> d(storage.as_span());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_front(1).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  EXPECT_EQ(d.size(), 3u);
  EXPECT_TRUE(d.full());
  EXPECT_EQ(d[0], 1);
  EXPECT_EQ(d[1], 2);
  EXPECT_EQ(d[2], 3);
}

TEST(OutlineVecDequeTest, PushFailsAtCapacity) {
  backing_storage<int, 2> storage;
  outline_vec_deque<int> d(storage.as_span());
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());

  auto res = d.try_push_back(3);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::capacity_exceeded);

  auto res2 = d.try_push_front(3);
  ASSERT_FALSE(res2.has_value());
  EXPECT_EQ(res2.error(), error::capacity_exceeded);
}

TEST(OutlineVecDequeTest, WrapAroundEraseAndRotate) {
  backing_storage<int, 4> storage;
  outline_vec_deque<int> d(storage.as_span());
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  ASSERT_TRUE(d.try_push_back(4).has_value());

  ASSERT_TRUE(d.try_pop_front().has_value());
  ASSERT_TRUE(d.try_pop_front().has_value());
  ASSERT_TRUE(d.try_push_back(5).has_value());
  ASSERT_TRUE(d.try_push_back(6).has_value());
  // Logically: [3, 4, 5, 6], physically wrapped.

  auto [s1, s2] = d.as_slices();
  EXPECT_FALSE(s2.empty());

  d.rotate_left(1);
  // Logically: [4, 5, 6, 3]
  EXPECT_EQ(d[0], 4);
  EXPECT_EQ(d[3], 3);

  ASSERT_TRUE(d.try_erase_at(0).has_value());
  EXPECT_EQ(d.size(), 3u);
  EXPECT_EQ(d[0], 5);
}

TEST(OutlineVecDequeTest, NoMoveOrCopy) {
  EXPECT_FALSE(std::is_copy_constructible_v<outline_vec_deque<int>>);
  EXPECT_FALSE(std::is_move_constructible_v<outline_vec_deque<int>>);
}
