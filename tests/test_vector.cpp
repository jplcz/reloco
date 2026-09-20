// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>
#include <reloco/vector.hpp>

#include <cstddef>
#include <string>
#include <utility>

using reloco::error;
using reloco::vector;

namespace {

// Move-only, non-trivially-relocatable/copyable stand-in for user types that
// only support nothrow move construction (exercises the manual move/destroy
// growth and shift paths, not the memcpy/memmove fast paths).
struct move_only {
  int value;
  bool *destroyed_flag = nullptr;

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

TEST(VectorTest, DefaultConstructedIsEmpty) {
  vector<int> v;
  EXPECT_TRUE(v.empty());
  EXPECT_EQ(v.size(), 0u);
  EXPECT_EQ(v.capacity(), 0u);
}

TEST(VectorTest, TryCreateReservesCapacity) {
  auto v = vector<int>::try_create(8);
  ASSERT_TRUE(v);
  EXPECT_TRUE(v->empty());
  EXPECT_GE(v->capacity(), 8u);
}

TEST(VectorTest, TryAllocateUsesExplicitAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto v = vector<int>::try_allocate(heap, 4);
  ASSERT_TRUE(v);
  EXPECT_GE(v->capacity(), 4u);
}

TEST(VectorTest, TryPushBackGrowsAndAppends) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_push_back(2));
  ASSERT_TRUE(v->try_push_back(3));
  EXPECT_EQ(v->size(), 3u);
  EXPECT_EQ((*v)[0], 1);
  EXPECT_EQ((*v)[1], 2);
  EXPECT_EQ((*v)[2], 3);
}

TEST(VectorTest, TryEmplaceBackConstructsInPlace) {
  auto v = vector<reloco::string>::try_create();
  ASSERT_TRUE(v);
  auto res = v->try_emplace_back(reloco::string_view("xxx"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->get().view(), "xxx");
  EXPECT_EQ((*v)[0].view(), "xxx");
}

TEST(VectorTest, TryPopBackRemovesLastElement) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_push_back(2));
  ASSERT_TRUE(v->try_pop_back());
  EXPECT_EQ(v->size(), 1u);
  EXPECT_EQ((*v)[0], 1);
}

TEST(VectorTest, TryPopBackFailsOnEmpty) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  auto res = v->try_pop_back();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::container_empty);
}

TEST(VectorTest, ClearDestroysElementsAndResetsSize) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  bool destroyed[3] = {false, false, false};
  auto v = vector<move_only>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_emplace_back(1, &destroyed[0]));
  ASSERT_TRUE(v->try_emplace_back(2, &destroyed[1]));
  ASSERT_TRUE(v->try_emplace_back(3, &destroyed[2]));
  v->clear();
  EXPECT_TRUE(v->empty());
  EXPECT_TRUE(destroyed[0]);
  EXPECT_TRUE(destroyed[1]);
  EXPECT_TRUE(destroyed[2]);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(VectorTest, TryInsertAtShiftsElementsRight) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_push_back(2));
  ASSERT_TRUE(v->try_push_back(3));

  auto res = v->try_insert_at(1, 99);
  ASSERT_TRUE(res);
  EXPECT_EQ(v->size(), 4u);
  EXPECT_EQ((*v)[0], 1);
  EXPECT_EQ((*v)[1], 99);
  EXPECT_EQ((*v)[2], 2);
  EXPECT_EQ((*v)[3], 3);
}

TEST(VectorTest, TryInsertAtBeginningAndEnd) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_insert_at(0, 0));
  ASSERT_TRUE(v->try_insert_at(v->size(), 2));
  ASSERT_EQ(v->size(), 3u);
  EXPECT_EQ((*v)[0], 0);
  EXPECT_EQ((*v)[1], 1);
  EXPECT_EQ((*v)[2], 2);
}

TEST(VectorTest, TryInsertAtOutOfBoundsFails) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  auto res = v->try_insert_at(1, 42);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::out_of_bounds);
}

TEST(VectorTest, TryInsertAtNonRelocatableMovesElements) {
  auto v = vector<move_only>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_emplace_back(1));
  ASSERT_TRUE(v->try_emplace_back(2));
  ASSERT_TRUE(v->try_emplace_back(3));

  auto res = v->try_insert_at(1, 99);
  ASSERT_TRUE(res);
  ASSERT_EQ(v->size(), 4u);
  EXPECT_EQ((*v)[0].value, 1);
  EXPECT_EQ((*v)[1].value, 99);
  EXPECT_EQ((*v)[2].value, 2);
  EXPECT_EQ((*v)[3].value, 3);
}

TEST(VectorTest, TryEraseAtShiftsElementsLeft) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_push_back(2));
  ASSERT_TRUE(v->try_push_back(3));

  ASSERT_TRUE(v->try_erase_at(1));
  EXPECT_EQ(v->size(), 2u);
  EXPECT_EQ((*v)[0], 1);
  EXPECT_EQ((*v)[1], 3);
}

TEST(VectorTest, TryEraseAtOutOfBoundsFails) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  auto res = v->try_erase_at(5);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::out_of_bounds);
}

TEST(VectorTest, TryEraseAtNonRelocatableDestroysAndMoves) {
  bool destroyed[3] = {false, false, false};
  auto v = vector<move_only>::try_create();
  ASSERT_TRUE(v);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  ASSERT_TRUE(v->try_emplace_back(1, &destroyed[0]));
  ASSERT_TRUE(v->try_emplace_back(2, &destroyed[1]));
  ASSERT_TRUE(v->try_emplace_back(3, &destroyed[2]));
  RELOCO_END_UNSAFE_BUFFER_USAGE;

  ASSERT_TRUE(v->try_erase_at(0));
  EXPECT_TRUE(destroyed[0]);
  ASSERT_EQ(v->size(), 2u);
  EXPECT_EQ((*v)[0].value, 2);
  EXPECT_EQ((*v)[1].value, 3);
}

TEST(VectorTest, TryReserveGrowsCapacityWithoutChangingSize) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_reserve(64));
  EXPECT_GE(v->capacity(), 64u);
  EXPECT_EQ(v->size(), 1u);
  EXPECT_EQ((*v)[0], 1);
}

TEST(VectorTest, TryReserveSmallerThanCapacityIsNoop) {
  auto v = vector<int>::try_create(16);
  ASSERT_TRUE(v);
  const auto cap = v->capacity();
  ASSERT_TRUE(v->try_reserve(4));
  EXPECT_EQ(v->capacity(), cap);
}

TEST(VectorTest, ShrinkToFitReleasesUnusedCapacity) {
  auto v = vector<int>::try_create(64);
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_push_back(2));
  ASSERT_TRUE(v->shrink_to_fit());
  EXPECT_EQ(v->capacity(), 2u);
  EXPECT_EQ((*v)[0], 1);
  EXPECT_EQ((*v)[1], 2);
}

TEST(VectorTest, ShrinkToFitOnEmptyVectorReleasesStorage) {
  auto v = vector<int>::try_create(16);
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->shrink_to_fit());
  EXPECT_EQ(v->capacity(), 0u);
}

TEST(VectorTest, TryAtChecksBounds) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(42));
  auto res = v->try_at(0);
  ASSERT_TRUE(res);
  EXPECT_EQ(res->get(), 42);
  auto bad = v->try_at(1);
  ASSERT_FALSE(bad);
  EXPECT_EQ(bad.error(), error::out_of_bounds);
}

TEST(VectorTest, FrontAndBack) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_push_back(2));
  ASSERT_TRUE(v->try_push_back(3));
  EXPECT_EQ(v->front(), 1);
  EXPECT_EQ(v->back(), 3);
}

TEST(VectorTest, TryFrontAndTryBackFailOnEmpty) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  auto front_res = v->try_front();
  ASSERT_FALSE(front_res);
  EXPECT_EQ(front_res.error(), error::container_empty);
  auto back_res = v->try_back();
  ASSERT_FALSE(back_res);
  EXPECT_EQ(back_res.error(), error::container_empty);
}

TEST(VectorTest, TryDataFailsOnEmpty) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  auto res = v->try_data();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::container_empty);
}

TEST(VectorTest, UnsafeAtAndUnsafeDataBypassChecks) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(7));
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  EXPECT_EQ(v->unsafe_at(0), 7);
  EXPECT_EQ(*v->unsafe_data(), 7);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(VectorTest, IteratesInOrder) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_push_back(2));
  ASSERT_TRUE(v->try_push_back(3));

  int expected = 1;
  for (int x : *v) {
    EXPECT_EQ(x, expected);
    ++expected;
  }
  EXPECT_EQ(expected, 4);
}

TEST(VectorTest, ReverseIteratesInOrder) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_push_back(2));
  ASSERT_TRUE(v->try_push_back(3));

  int expected = 3;
  for (auto it = v->rbegin(); it != v->rend(); ++it) {
    EXPECT_EQ(*it, expected);
    --expected;
  }
  EXPECT_EQ(expected, 0);
}

TEST(VectorTest, MoveConstructionTransfersOwnership) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_push_back(2));

  vector<int> moved(std::move(*v));
  EXPECT_EQ(moved.size(), 2u);
  EXPECT_TRUE(v->empty());      // NOLINT(bugprone-use-after-move)
  EXPECT_EQ(v->capacity(), 0u); // NOLINT(bugprone-use-after-move)
}

TEST(VectorTest, MoveAssignmentReleasesPreviousStorage) {
  auto a = vector<int>::try_create();
  ASSERT_TRUE(a);
  ASSERT_TRUE(a->try_push_back(1));

  auto b = vector<int>::try_create();
  ASSERT_TRUE(b);
  ASSERT_TRUE(b->try_push_back(2));
  ASSERT_TRUE(b->try_push_back(3));

  *a = std::move(*b);
  EXPECT_EQ(a->size(), 2u);
  EXPECT_EQ((*a)[0], 2);
  EXPECT_EQ((*a)[1], 3);
}

TEST(VectorTest, TryCloneWithTrivialTypeUsesMemcpyPath) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_push_back(2));
  ASSERT_TRUE(v->try_push_back(3));

  auto clone = v->try_clone();
  ASSERT_TRUE(clone);
  EXPECT_EQ(clone->size(), v->size());
  EXPECT_EQ((*clone)[0], 1);
  EXPECT_EQ((*clone)[1], 2);
  EXPECT_EQ((*clone)[2], 3);

  // Independent storage: mutating the clone must not affect the original.
  ASSERT_TRUE(clone->try_push_back(4));
  EXPECT_EQ(v->size(), 3u);
}

TEST(VectorTest, TryCloneWithNonTrivialTypeClonesPerElement) {
  auto v = vector<reloco::string>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_emplace_back(reloco::string_view("hello")));
  ASSERT_TRUE(v->try_emplace_back(reloco::string_view("world")));

  auto clone = v->try_clone();
  ASSERT_TRUE(clone);
  EXPECT_EQ((*clone)[0].view(), "hello");
  EXPECT_EQ((*clone)[1].view(), "world");
}

TEST(VectorTest, TryCloneWithExplicitAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));

  auto clone = v->try_clone(heap);
  ASSERT_TRUE(clone);
  EXPECT_EQ((*clone)[0], 1);
}

TEST(VectorTest, TryCloneAtWritesIntoUninitializedStorage) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_push_back(2));

  alignas(vector<int>) std::byte storage[sizeof(vector<int>)];
  auto res = vector<int>::try_clone_at(reloco::default_allocator(), reinterpret_cast<vector<int> *>(storage), *v);
  ASSERT_TRUE(res);
  auto *cloned = reinterpret_cast<vector<int> *>(storage);
  EXPECT_EQ(cloned->size(), 2u);
  EXPECT_EQ((*cloned)[0], 1);
  EXPECT_EQ((*cloned)[1], 2);
  cloned->~vector();
}

TEST(VectorTest, IsTriviallyRelocatable) {
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<vector<int>>);
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<vector<move_only>>);
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<vector<std::string>>);
}

TEST(VectorTest, GetAllocatorReturnsUsableAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto v = vector<int>::try_allocate(heap);
  ASSERT_TRUE(v);
  auto clone = v->try_clone(v->get_allocator());
  ASSERT_TRUE(clone);
}
