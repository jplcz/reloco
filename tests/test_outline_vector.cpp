// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/alignment.hpp>
#include <reloco/outline_vector.hpp>
#include <reloco/span.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>

using reloco::error;
using reloco::outline_vector;
using reloco::span;

namespace {

// Move-only, non-trivially-relocatable/copyable stand-in for user types that
// only support nothrow move construction (exercises the manual move/destroy
// shift paths, not the memcpy/memmove fast paths). Mirrors
// test_inline_vector.cpp's helper of the same name/shape.
struct move_only {
  int value = 0;
  bool *destroyed_flag = nullptr;

  move_only() noexcept = default;
  explicit move_only(int v) noexcept : value(v) {}
  move_only(int v, bool *flag) noexcept : value(v), destroyed_flag(flag) {}
  move_only(const move_only &) = delete;
  move_only &operator=(const move_only &) = delete;

  move_only(move_only &&other) noexcept : value(other.value), destroyed_flag(other.destroyed_flag) {
    other.destroyed_flag = nullptr;
  }

  move_only &operator=(move_only &&other) noexcept {
    if (this != &other) {
      value = other.value;
      destroyed_flag = other.destroyed_flag;
      other.destroyed_flag = nullptr;
    }
    return *this;
  }

  ~move_only() {
    if (destroyed_flag)
      *destroyed_flag = true;
  }
};

template <typename T, std::size_t Capacity> struct backing_storage {
  alignas(reloco::effective_alignment_v<T>) std::byte bytes[sizeof(T) * Capacity];

  span<std::byte> as_span() noexcept { return span<std::byte>(bytes); }
};

} // namespace

TEST(OutlineVectorTest, ConstructedFromSpanIsEmptyWithSpanCapacity) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  EXPECT_TRUE(v.empty());
  EXPECT_EQ(v.size(), 0u);
  EXPECT_EQ(v.capacity(), 4u);
  EXPECT_FALSE(v.full());
}

TEST(OutlineVectorTest, TryPushBackAppendsUntilFull) {
  backing_storage<int, 3> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));
  EXPECT_EQ(v.size(), 3u);
  EXPECT_TRUE(v.full());
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 2);
  EXPECT_EQ(v[2], 3);
}

TEST(OutlineVectorTest, TryPushBackFailsWhenAtCapacity) {
  backing_storage<int, 2> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  auto res = v.try_push_back(3);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::capacity_exceeded);
  EXPECT_EQ(v.size(), 2u);
}

TEST(OutlineVectorTest, TryEmplaceBackConstructsInPlace) {
  backing_storage<reloco::string, 2> storage;
  outline_vector<reloco::string> v(storage.as_span());
  auto res = v.try_emplace_back(reloco::string_view("xxx"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->get(), "xxx");
}

TEST(OutlineVectorTest, TryPopBackRemovesLastElement) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_pop_back());
  EXPECT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], 1);
}

TEST(OutlineVectorTest, TryPopBackFailsOnEmpty) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  auto res = v.try_pop_back();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::container_empty);
}

TEST(OutlineVectorTest, ClearDestroysElementsAndResetsSize) {
  bool destroyed[2] = {false, false};
  backing_storage<move_only, 4> storage;
  outline_vector<move_only> v(storage.as_span());
  ASSERT_TRUE(v.try_emplace_back(1, &destroyed[0]));
  ASSERT_TRUE(v.try_emplace_back(2, &destroyed[1]));

  v.clear();
  EXPECT_TRUE(destroyed[0]);
  EXPECT_TRUE(destroyed[1]);
  EXPECT_EQ(v.size(), 0u);
  EXPECT_TRUE(v.empty());
}

TEST(OutlineVectorTest, TryInsertAtShiftsElementsRight) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));

  auto res = v.try_insert_at(1, 99);
  ASSERT_TRUE(res);
  EXPECT_EQ(v.size(), 4u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 99);
  EXPECT_EQ(v[2], 2);
  EXPECT_EQ(v[3], 3);
}

TEST(OutlineVectorTest, TryInsertAtOutOfBoundsFails) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  auto res = v.try_insert_at(1, 42);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::out_of_bounds);
}

TEST(OutlineVectorTest, TryInsertAtFailsWhenAtCapacity) {
  backing_storage<int, 2> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  auto res = v.try_insert_at(0, 3);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::capacity_exceeded);
  EXPECT_EQ(v.size(), 2u);
}

TEST(OutlineVectorTest, TryInsertAtNonRelocatableMovesElements) {
  backing_storage<move_only, 4> storage;
  outline_vector<move_only> v(storage.as_span());
  ASSERT_TRUE(v.try_emplace_back(1));
  ASSERT_TRUE(v.try_emplace_back(2));
  ASSERT_TRUE(v.try_emplace_back(3));

  auto res = v.try_insert_at(1, 99);
  ASSERT_TRUE(res);
  ASSERT_EQ(v.size(), 4u);
  EXPECT_EQ(v[0].value, 1);
  EXPECT_EQ(v[1].value, 99);
  EXPECT_EQ(v[2].value, 2);
  EXPECT_EQ(v[3].value, 3);
}

TEST(OutlineVectorTest, TryEraseAtShiftsElementsLeft) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));

  ASSERT_TRUE(v.try_erase_at(1));
  EXPECT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 3);
}

TEST(OutlineVectorTest, TryEraseAtOutOfBoundsFails) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(1));
  auto res = v.try_erase_at(5);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::out_of_bounds);
}

TEST(OutlineVectorTest, TryEraseAtNonRelocatableDestroysAndMoves) {
  bool destroyed[3] = {false, false, false};
  backing_storage<move_only, 4> storage;
  outline_vector<move_only> v(storage.as_span());
  ASSERT_TRUE(v.try_emplace_back(1, &destroyed[0]));
  ASSERT_TRUE(v.try_emplace_back(2, &destroyed[1]));
  ASSERT_TRUE(v.try_emplace_back(3, &destroyed[2]));

  ASSERT_TRUE(v.try_erase_at(0));
  EXPECT_TRUE(destroyed[0]);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0].value, 2);
  EXPECT_EQ(v[1].value, 3);
}

TEST(OutlineVectorTest, TryAtChecksBounds) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(42));
  auto res = v.try_at(0);
  ASSERT_TRUE(res);
  EXPECT_EQ(res->get(), 42);
  auto oob = v.try_at(5);
  ASSERT_FALSE(oob);
  EXPECT_EQ(oob.error(), error::out_of_bounds);
}

TEST(OutlineVectorTest, FrontAndBack) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));
  EXPECT_EQ(v.front(), 1);
  EXPECT_EQ(v.back(), 3);
}

TEST(OutlineVectorTest, TryFrontAndTryBackFailOnEmpty) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  auto front_res = v.try_front();
  ASSERT_FALSE(front_res);
  EXPECT_EQ(front_res.error(), error::container_empty);
  auto back_res = v.try_back();
  ASSERT_FALSE(back_res);
  EXPECT_EQ(back_res.error(), error::container_empty);
}

TEST(OutlineVectorTest, TryDataFailsOnEmpty) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  auto res = v.try_data();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::container_empty);
}

TEST(OutlineVectorTest, UnsafeAtAndUnsafeDataBypassChecks) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(7));
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  EXPECT_EQ(v.unsafe_at(0), 7);
  EXPECT_EQ(*v.unsafe_data(), 7);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(OutlineVectorTest, IteratesInOrder) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));

  int expected = 1;
  for (int x : v) {
    EXPECT_EQ(x, expected++);
  }
  EXPECT_EQ(expected, 4);
}

TEST(OutlineVectorTest, ReverseIteratesInOrder) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));

  int expected = 3;
  for (auto it = v.rbegin(); it != v.rend(); ++it) {
    EXPECT_EQ(*it, expected--);
  }
  EXPECT_EQ(expected, 0);
}

TEST(OutlineVectorTest, TryResizeGrowsWithDefaultValue) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_resize(3));
  EXPECT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 0);
  EXPECT_EQ(v[2], 0);
}

TEST(OutlineVectorTest, TryResizeGrowsWithFillValue) {
  backing_storage<int, 4> storage;
  outline_vector<int> v(storage.as_span());
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_resize(3, 9));
  EXPECT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 9);
  EXPECT_EQ(v[2], 9);
}

TEST(OutlineVectorTest, TryResizeFailsWithCapacityExceeded) {
  backing_storage<int, 2> storage;
  outline_vector<int> v(storage.as_span());
  auto res = v.try_resize(3);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::capacity_exceeded);
}

TEST(OutlineVectorTest, TryResizeShrinksAndDestroysTrailingElements) {
  bool destroyed[3] = {false, false, false};
  backing_storage<move_only, 4> storage;
  outline_vector<move_only> v(storage.as_span());
  ASSERT_TRUE(v.try_emplace_back(1, &destroyed[0]));
  ASSERT_TRUE(v.try_emplace_back(2, &destroyed[1]));
  ASSERT_TRUE(v.try_emplace_back(3, &destroyed[2]));

  ASSERT_TRUE(v.try_resize(1));
  EXPECT_EQ(v.size(), 1u);
  EXPECT_TRUE(destroyed[1]);
  EXPECT_TRUE(destroyed[2]);
  EXPECT_FALSE(destroyed[0]);
}

TEST(OutlineVectorTest, DestructorDestroysElementsButNotStorage) {
  bool destroyed[2] = {false, false};
  backing_storage<move_only, 4> storage;
  {
    outline_vector<move_only> v(storage.as_span());
    ASSERT_TRUE(v.try_emplace_back(1, &destroyed[0]));
    ASSERT_TRUE(v.try_emplace_back(2, &destroyed[1]));
  }
  EXPECT_TRUE(destroyed[0]);
  EXPECT_TRUE(destroyed[1]);

  // The backing storage itself is still ours and reusable after the outline_vector's
  // destructor has run.
  outline_vector<move_only> v2(storage.as_span());
  EXPECT_TRUE(v2.empty());
  EXPECT_EQ(v2.capacity(), 4u);
}

TEST(OutlineVectorTest, IsNeitherCopyableNorMovable) {
  EXPECT_FALSE(std::is_copy_constructible_v<outline_vector<int>>);
  EXPECT_FALSE(std::is_move_constructible_v<outline_vector<int>>);
  EXPECT_FALSE(std::is_copy_assignable_v<outline_vector<int>>);
  EXPECT_FALSE(std::is_move_assignable_v<outline_vector<int>>);
}

TEST(OutlineVectorTest, IsNotTriviallyRelocatable) {
  EXPECT_FALSE(reloco::is_trivially_relocatable_v<outline_vector<int>>);
}

TEST(OutlineVectorTest, MultipleOutlineVectorsOverIndependentStorageDoNotInterfere) {
  backing_storage<int, 4> storage_a;
  backing_storage<int, 4> storage_b;
  outline_vector<int> a(storage_a.as_span());
  outline_vector<int> b(storage_b.as_span());
  ASSERT_TRUE(a.try_push_back(1));
  ASSERT_TRUE(b.try_push_back(2));
  EXPECT_EQ(a[0], 1);
  EXPECT_EQ(b[0], 2);
}
