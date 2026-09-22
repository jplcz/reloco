// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/inline_vector.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>
#include <reloco/vector.hpp>

#include <string>
#include <utility>

using reloco::error;
using reloco::inline_vector;

namespace {

// Move-only, non-trivially-relocatable/copyable stand-in for user types that
// only support nothrow move construction (exercises the manual move/destroy
// shift paths, not the memcpy/memmove fast paths). Mirrors test_vector.cpp's
// helper of the same name/shape.
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

} // namespace

TEST(InlineVectorTest, DefaultConstructedIsEmpty) {
  inline_vector<int, 4> v;
  EXPECT_TRUE(v.empty());
  EXPECT_EQ(v.size(), 0u);
  EXPECT_EQ(v.capacity(), 4u);
  EXPECT_FALSE(v.full());
}

TEST(InlineVectorTest, TryPushBackAppendsUntilFull) {
  inline_vector<int, 3> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));
  EXPECT_EQ(v.size(), 3u);
  EXPECT_TRUE(v.full());
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 2);
  EXPECT_EQ(v[2], 3);
}

TEST(InlineVectorTest, TryPushBackFailsWhenAtCapacity) {
  inline_vector<int, 2> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  auto res = v.try_push_back(3);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::capacity_exceeded);
  EXPECT_EQ(v.size(), 2u);
}

TEST(InlineVectorTest, TryEmplaceBackConstructsInPlace) {
  inline_vector<reloco::string, 2> v;
  auto res = v.try_emplace_back(reloco::string_view("xxx"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->get().view(), "xxx");
  EXPECT_EQ(v[0].view(), "xxx");
}

TEST(InlineVectorTest, TryPopBackRemovesLastElement) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_pop_back());
  EXPECT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], 1);
}

TEST(InlineVectorTest, TryPopBackFailsOnEmpty) {
  inline_vector<int, 4> v;
  auto res = v.try_pop_back();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::container_empty);
}

TEST(InlineVectorTest, ClearDestroysElementsAndResetsSize) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  bool destroyed[3] = {false, false, false};
  inline_vector<move_only, 4> v;
  ASSERT_TRUE(v.try_emplace_back(1, &destroyed[0]));
  ASSERT_TRUE(v.try_emplace_back(2, &destroyed[1]));
  ASSERT_TRUE(v.try_emplace_back(3, &destroyed[2]));
  v.clear();
  EXPECT_TRUE(v.empty());
  EXPECT_TRUE(destroyed[0]);
  EXPECT_TRUE(destroyed[1]);
  EXPECT_TRUE(destroyed[2]);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(InlineVectorTest, TryInsertAtShiftsElementsRight) {
  inline_vector<int, 4> v;
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

TEST(InlineVectorTest, TryInsertAtBeginningAndEnd) {
  inline_vector<int, 3> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_insert_at(0, 0));
  ASSERT_TRUE(v.try_insert_at(v.size(), 2));
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], 0);
  EXPECT_EQ(v[1], 1);
  EXPECT_EQ(v[2], 2);
}

TEST(InlineVectorTest, TryInsertAtOutOfBoundsFails) {
  inline_vector<int, 4> v;
  auto res = v.try_insert_at(1, 42);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::out_of_bounds);
}

TEST(InlineVectorTest, TryInsertAtFailsWhenAtCapacity) {
  inline_vector<int, 2> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  auto res = v.try_insert_at(0, 3);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::capacity_exceeded);
  EXPECT_EQ(v.size(), 2u);
}

TEST(InlineVectorTest, TryInsertAtNonRelocatableMovesElements) {
  inline_vector<move_only, 4> v;
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

TEST(InlineVectorTest, TryEraseAtShiftsElementsLeft) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));

  ASSERT_TRUE(v.try_erase_at(1));
  EXPECT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 3);
}

TEST(InlineVectorTest, TryEraseAtOutOfBoundsFails) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  auto res = v.try_erase_at(5);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::out_of_bounds);
}

TEST(InlineVectorTest, TryEraseAtNonRelocatableDestroysAndMoves) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  bool destroyed[3] = {false, false, false};
  inline_vector<move_only, 4> v;
  ASSERT_TRUE(v.try_emplace_back(1, &destroyed[0]));
  ASSERT_TRUE(v.try_emplace_back(2, &destroyed[1]));
  ASSERT_TRUE(v.try_emplace_back(3, &destroyed[2]));

  ASSERT_TRUE(v.try_erase_at(0));
  EXPECT_TRUE(destroyed[0]);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0].value, 2);
  EXPECT_EQ(v[1].value, 3);
}

TEST(InlineVectorTest, TryAtChecksBounds) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(42));
  auto res = v.try_at(0);
  ASSERT_TRUE(res);
  EXPECT_EQ(res->get(), 42);
  auto bad = v.try_at(1);
  ASSERT_FALSE(bad);
  EXPECT_EQ(bad.error(), error::out_of_bounds);
}

TEST(InlineVectorTest, FrontAndBack) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));
  EXPECT_EQ(v.front(), 1);
  EXPECT_EQ(v.back(), 3);
}

TEST(InlineVectorTest, TryFrontAndTryBackFailOnEmpty) {
  inline_vector<int, 4> v;
  auto front_res = v.try_front();
  ASSERT_FALSE(front_res);
  EXPECT_EQ(front_res.error(), error::container_empty);
  auto back_res = v.try_back();
  ASSERT_FALSE(back_res);
  EXPECT_EQ(back_res.error(), error::container_empty);
}

TEST(InlineVectorTest, TryDataFailsOnEmpty) {
  inline_vector<int, 4> v;
  auto res = v.try_data();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::container_empty);
}

TEST(InlineVectorTest, UnsafeAtAndUnsafeDataBypassChecks) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(7));
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  EXPECT_EQ(v.unsafe_at(0), 7);
  EXPECT_EQ(*v.unsafe_data(), 7);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(InlineVectorTest, IteratesInOrder) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));

  int expected = 1;
  for (int x : v) {
    EXPECT_EQ(x, expected);
    ++expected;
  }
  EXPECT_EQ(expected, 4);
}

TEST(InlineVectorTest, ReverseIteratesInOrder) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));

  int expected = 3;
  for (auto it = v.rbegin(); it != v.rend(); ++it) {
    EXPECT_EQ(*it, expected);
    --expected;
  }
  EXPECT_EQ(expected, 0);
}

TEST(InlineVectorTest, MoveConstructionTransfersOwnership) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));

  inline_vector<int, 4> moved(std::move(v));
  EXPECT_EQ(moved.size(), 2u);
  EXPECT_EQ(moved[0], 1);
  EXPECT_EQ(moved[1], 2);
  EXPECT_TRUE(v.empty()); // NOLINT(bugprone-use-after-move)
}

TEST(InlineVectorTest, MoveConstructionNonRelocatableMovesElements) {
  bool destroyed[2] = {false, false};
  inline_vector<move_only, 4> v;
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  ASSERT_TRUE(v.try_emplace_back(1, &destroyed[0]));
  ASSERT_TRUE(v.try_emplace_back(2, &destroyed[1]));
  RELOCO_END_UNSAFE_BUFFER_USAGE;

  inline_vector<move_only, 4> moved(std::move(v));
  EXPECT_EQ(moved.size(), 2u);
  EXPECT_EQ(moved[0].value, 1);
  EXPECT_EQ(moved[1].value, 2);
  EXPECT_TRUE(v.empty()); // NOLINT(bugprone-use-after-move)
}

TEST(InlineVectorTest, MoveAssignmentReleasesPreviousElements) {
  inline_vector<int, 4> a;
  ASSERT_TRUE(a.try_push_back(1));

  inline_vector<int, 4> b;
  ASSERT_TRUE(b.try_push_back(2));
  ASSERT_TRUE(b.try_push_back(3));

  a = std::move(b);
  EXPECT_EQ(a.size(), 2u);
  EXPECT_EQ(a[0], 2);
  EXPECT_EQ(a[1], 3);
  EXPECT_TRUE(b.empty()); // NOLINT(bugprone-use-after-move)
}

TEST(InlineVectorTest, TryCloneWithTrivialTypeUsesMemcpyPath) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));

  auto clone = v.try_clone();
  ASSERT_TRUE(clone);
  EXPECT_EQ(clone->size(), v.size());
  EXPECT_EQ((*clone)[0], 1);
  EXPECT_EQ((*clone)[1], 2);
  EXPECT_EQ((*clone)[2], 3);

  // Independent storage: mutating the clone must not affect the original.
  ASSERT_TRUE(clone->try_push_back(4));
  EXPECT_EQ(v.size(), 3u);
}

TEST(InlineVectorTest, TryCloneWithNonTrivialTypeClonesPerElement) {
  inline_vector<reloco::string, 4> v;
  ASSERT_TRUE(v.try_emplace_back(reloco::string_view("hello")));
  ASSERT_TRUE(v.try_emplace_back(reloco::string_view("world")));

  auto clone = v.try_clone();
  ASSERT_TRUE(clone);
  EXPECT_EQ((*clone)[0].view(), "hello");
  EXPECT_EQ((*clone)[1].view(), "world");
}

TEST(InlineVectorTest, TryCloneWithExplicitAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));

  auto clone = v.try_clone(heap);
  ASSERT_TRUE(clone);
  EXPECT_EQ((*clone)[0], 1);
}

TEST(InlineVectorTest, TryCloneAtWritesIntoUninitializedStorage) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));

  alignas(inline_vector<int, 4>) std::byte storage[sizeof(inline_vector<int, 4>)];
  auto res = inline_vector<int, 4>::try_clone_at(reloco::default_allocator(),
                                                 reinterpret_cast<inline_vector<int, 4> *>(storage), v);
  ASSERT_TRUE(res);
  auto *cloned = reinterpret_cast<inline_vector<int, 4> *>(storage);
  EXPECT_EQ(cloned->size(), 2u);
  EXPECT_EQ((*cloned)[0], 1);
  EXPECT_EQ((*cloned)[1], 2);
  cloned->~inline_vector();
}

TEST(InlineVectorTest, TryToVectorClonesWithoutConsumingSource) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));

  auto vec_res = v.try_to_vector();
  ASSERT_TRUE(vec_res);
  EXPECT_EQ(vec_res->size(), 2u);
  EXPECT_EQ((*vec_res)[0], 1);
  EXPECT_EQ((*vec_res)[1], 2);

  // Source untouched: still has its own two elements.
  EXPECT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 2);

  // Independent storage: mutating the vector must not affect the source.
  ASSERT_TRUE(vec_res->try_push_back(3));
  EXPECT_EQ(v.size(), 2u);
}

TEST(InlineVectorTest, TryToVectorMoveConsumesSource) {
  inline_vector<reloco::string, 4> v;
  ASSERT_TRUE(v.try_emplace_back(reloco::string_view("hello")));
  ASSERT_TRUE(v.try_emplace_back(reloco::string_view("world")));

  auto vec_res = std::move(v).try_to_vector();
  ASSERT_TRUE(vec_res);
  EXPECT_EQ(vec_res->size(), 2u);
  EXPECT_EQ((*vec_res)[0].view(), "hello");
  EXPECT_EQ((*vec_res)[1].view(), "world");
  EXPECT_TRUE(v.empty()); // NOLINT(bugprone-use-after-move)
}

TEST(InlineVectorTest, TryToVectorWithExplicitAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));

  auto vec_res = v.try_to_vector(heap);
  ASSERT_TRUE(vec_res);
  EXPECT_EQ((*vec_res)[0], 1);
}

TEST(InlineVectorTest, TryToVectorEmptySourceProducesEmptyVector) {
  inline_vector<int, 4> v;
  auto vec_res = v.try_to_vector();
  ASSERT_TRUE(vec_res);
  EXPECT_TRUE(vec_res->empty());
}

TEST(InlineVectorTest, IsTriviallyRelocatableDependsOnElementType) {
  using int_vec = inline_vector<int, 4>;
  using string_vec = inline_vector<reloco::string, 4>;
  using std_string_vec = inline_vector<std::string, 4>;
  using move_only_vec = inline_vector<move_only, 4>;
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<int_vec>);
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<string_vec>);
  EXPECT_FALSE(reloco::is_trivially_relocatable_v<std_string_vec>);
  EXPECT_FALSE(reloco::is_trivially_relocatable_v<move_only_vec>);
}

TEST(InlineVectorTest, TryResizeGrowsWithDefaultValue) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));

  ASSERT_TRUE(v.try_resize(4));
  EXPECT_EQ(v.size(), 4u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 0);
  EXPECT_EQ(v[2], 0);
  EXPECT_EQ(v[3], 0);
}

TEST(InlineVectorTest, TryResizeGrowsWithFillValue) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));

  ASSERT_TRUE(v.try_resize(4, 7));
  EXPECT_EQ(v.size(), 4u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 7);
  EXPECT_EQ(v[2], 7);
  EXPECT_EQ(v[3], 7);
}

TEST(InlineVectorTest, TryResizeFailsWithCapacityExceeded) {
  inline_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));

  auto res = v.try_resize(5);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), reloco::error::capacity_exceeded);
  EXPECT_EQ(v.size(), 1u);
}

TEST(InlineVectorTest, TryResizeShrinksAndDestroysTrailingElements) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  bool destroyed[4] = {false, false, false, false};
  inline_vector<move_only, 4> v;
  ASSERT_TRUE(v.try_push_back(move_only(0, &destroyed[0])));
  ASSERT_TRUE(v.try_push_back(move_only(1, &destroyed[1])));
  ASSERT_TRUE(v.try_push_back(move_only(2, &destroyed[2])));
  ASSERT_TRUE(v.try_push_back(move_only(3, &destroyed[3])));

  ASSERT_TRUE(v.try_resize(2));
  EXPECT_EQ(v.size(), 2u);
  EXPECT_FALSE(destroyed[0]);
  EXPECT_FALSE(destroyed[1]);
  EXPECT_TRUE(destroyed[2]);
  EXPECT_TRUE(destroyed[3]);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
