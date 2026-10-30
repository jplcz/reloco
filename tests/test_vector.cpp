// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/boxed_slice.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/inline_vector.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/sso_vector.hpp>
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

TEST(VectorTest, TryCreateSizedValueInitializesElements) {
  auto v = vector<int>::try_create_sized(5);
  ASSERT_TRUE(v);
  EXPECT_EQ(v->size(), 5u);
  for (const int x : *v) {
    EXPECT_EQ(x, 0);
  }
  auto empty = vector<int>::try_create_sized(0);
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->empty());
}

TEST(VectorTest, TryAllocateSizedUsesExplicitAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto v = vector<int>::try_allocate_sized(heap, 3);
  ASSERT_TRUE(v);
  EXPECT_EQ(v->size(), 3u);
}

TEST(VectorTest, TryCreateFromCopiesSpan) {
  const int src[] = {1, 2, 3};
  auto v = vector<int>::try_create_from(reloco::span<const int>(src, 3));
  ASSERT_TRUE(v);
  ASSERT_EQ(v->size(), 3u);
  EXPECT_EQ((*v)[2], 3);
}

TEST(VectorTest, SpanTryToVectorCopies) {
  const int src[] = {4, 5};
  auto v = reloco::span<const int>(src, 2).try_to_vector();
  ASSERT_TRUE(v);
  EXPECT_EQ(v->size(), 2u);
  EXPECT_EQ((*v)[0], 4);
}

TEST(VectorTest, TryAppendSpanAndAssign) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  const int src[] = {1, 2, 3};
  ASSERT_TRUE(v->try_append(reloco::span<const int>(src, 3)));
  ASSERT_TRUE(v->try_append(reloco::span<const int>(src, 2)));
  EXPECT_EQ(v->size(), 5u);
  EXPECT_EQ((*v)[3], 1);
  ASSERT_TRUE(v->try_assign(reloco::span<const int>(src, 1)));
  EXPECT_EQ(v->size(), 1u);
}

TEST(VectorTest, TryAppendAndAssignFromOwnStorage) {
  auto v = vector<int>::try_create(1); // tiny capacity so appending must reallocate
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(7));
  ASSERT_TRUE(v->try_push_back(8));
  ASSERT_TRUE(v->try_append(reloco::span<const int>(v->data(), v->size())));
  ASSERT_EQ(v->size(), 4u);
  EXPECT_EQ((*v)[2], 7);
  EXPECT_EQ((*v)[3], 8);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  ASSERT_TRUE(v->try_assign(reloco::span<const int>(v->data() + 1, 2)));
  RELOCO_END_UNSAFE_BUFFER_USAGE
  ASSERT_EQ(v->size(), 2u);
  EXPECT_EQ((*v)[0], 8);
  EXPECT_EQ((*v)[1], 7);
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
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  bool destroyed[3] = {false, false, false};
  auto v = vector<move_only>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_emplace_back(1, &destroyed[0]));
  ASSERT_TRUE(v->try_emplace_back(2, &destroyed[1]));
  ASSERT_TRUE(v->try_emplace_back(3, &destroyed[2]));

  ASSERT_TRUE(v->try_erase_at(0));
  EXPECT_TRUE(destroyed[0]);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
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

TEST(VectorTest, TryResizeGrowsWithDefaultValue) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));

  ASSERT_TRUE(v->try_resize(4));
  EXPECT_EQ(v->size(), 4u);
  EXPECT_EQ((*v)[0], 1);
  EXPECT_EQ((*v)[1], 0);
  EXPECT_EQ((*v)[2], 0);
  EXPECT_EQ((*v)[3], 0);
}

TEST(VectorTest, TryResizeGrowsWithFillValue) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));

  ASSERT_TRUE(v->try_resize(4, 7));
  EXPECT_EQ(v->size(), 4u);
  EXPECT_EQ((*v)[0], 1);
  EXPECT_EQ((*v)[1], 7);
  EXPECT_EQ((*v)[2], 7);
  EXPECT_EQ((*v)[3], 7);
}

TEST(VectorTest, TryResizeShrinksAndDestroysTrailingElements) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  bool destroyed[4] = {false, false, false, false};
  auto v = vector<move_only>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(move_only(0, &destroyed[0])));
  ASSERT_TRUE(v->try_push_back(move_only(1, &destroyed[1])));
  ASSERT_TRUE(v->try_push_back(move_only(2, &destroyed[2])));
  ASSERT_TRUE(v->try_push_back(move_only(3, &destroyed[3])));

  ASSERT_TRUE(v->try_resize(2));
  EXPECT_EQ(v->size(), 2u);
  EXPECT_FALSE(destroyed[0]);
  EXPECT_FALSE(destroyed[1]);
  EXPECT_TRUE(destroyed[2]);
  EXPECT_TRUE(destroyed[3]);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(VectorTest, TryResizeToSameSizeIsNoop) {
  auto v = vector<int>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(1));
  ASSERT_TRUE(v->try_push_back(2));

  ASSERT_TRUE(v->try_resize(2));
  EXPECT_EQ(v->size(), 2u);
  EXPECT_EQ((*v)[0], 1);
  EXPECT_EQ((*v)[1], 2);
}

TEST(VectorTest, TryConvertToOtherVectorFlavors) {
  vector<int> v;
  for (int i : {1, 2, 3})
    ASSERT_TRUE(v.try_push_back(i));
  auto sso = v.try_convert_to<reloco::sso_vector<int, 2>>();
  ASSERT_TRUE(sso);
  EXPECT_EQ(sso->size(), 3u);
  auto fixed = v.try_convert_to<reloco::inline_vector<int, 3>>();
  ASSERT_TRUE(fixed);
  auto tiny = v.try_convert_to<reloco::inline_vector<int, 2>>();
  EXPECT_EQ(tiny.error(), error::capacity_exceeded);
  auto boxed = fixed->try_convert_to<reloco::boxed_slice<int>>();
  ASSERT_TRUE(boxed);
  auto again = boxed->try_convert_to<vector<int>>();
  ASSERT_TRUE(again);
  EXPECT_EQ((*again)[2], 3);
}

TEST(VectorTest, RetainAndDedupCompileAndWork) {
  vector<int> v;
  for (int i : {1, 1, 2, 3, 3, 3, 4, 5, 6})
    ASSERT_TRUE(v.try_push_back(i));
  v.dedup();
  ASSERT_EQ(v.size(), 6u);
  v.retain([](const int &x) { return x % 2 == 0; });
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], 2);
  EXPECT_EQ(v[2], 6);
  v.dedup_by([](const int &a, const int &b) { return b - a == 2; });
  EXPECT_EQ(v.size(), 2u); // {2,4,6}: 4 is 2 apart from 2 (dropped), 6 is 4 apart from 2 (kept)
}

TEST(VectorTest, TryInsertSpanInsertsInMiddleAndHandlesAliasing) {
  vector<int> v;
  for (int i : {1, 2, 5})
    ASSERT_TRUE(v.try_push_back(i));
  const int mid[] = {3, 4};
  ASSERT_TRUE(v.try_insert_span(2, reloco::span<const int>(mid, 2)));
  ASSERT_EQ(v.size(), 5u);
  for (std::size_t i = 0; i < 5; ++i)
    EXPECT_EQ(v[i], static_cast<int>(i) + 1);
  // Alias own storage: insert [1,2] at the front (storage reallocates during the reserve).
  ASSERT_TRUE(v.try_insert_span(0, reloco::span<const int>(v.data(), 2)));
  ASSERT_EQ(v.size(), 7u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 2);
  EXPECT_EQ(v[2], 1);
  EXPECT_EQ(v[6], 5);
  EXPECT_EQ(v.try_insert_span(99, reloco::span<const int>(mid, 2)).error(), error::out_of_bounds);
}

TEST(VectorTest, TryEraseRangeSwapRemoveTruncate) {
  vector<int> v;
  for (int i = 0; i < 8; ++i)
    ASSERT_TRUE(v.try_push_back(i));
  ASSERT_TRUE(v.try_erase_range(2, 3)); // 0 1 5 6 7
  ASSERT_EQ(v.size(), 5u);
  EXPECT_EQ(v[2], 5);
  EXPECT_EQ(v.try_erase_range(4, 2).error(), error::out_of_bounds);
  ASSERT_TRUE(v.try_swap_remove(0)); // 7 1 5 6
  ASSERT_EQ(v.size(), 4u);
  EXPECT_EQ(v[0], 7);
  EXPECT_EQ(v.try_swap_remove(4).error(), error::out_of_bounds);
  v.truncate(2);
  ASSERT_EQ(v.size(), 2u);
  v.truncate(10);
  EXPECT_EQ(v.size(), 2u);
}

TEST(VectorTest, TrySplitOffMovesTail) {
  vector<int> v;
  for (int i = 0; i < 5; ++i)
    ASSERT_TRUE(v.try_push_back(i));
  auto tail = v.try_split_off(3);
  ASSERT_TRUE(tail);
  EXPECT_EQ(v.size(), 3u);
  ASSERT_EQ(tail->size(), 2u);
  EXPECT_EQ((*tail)[0], 3);
  EXPECT_EQ(v.try_split_off(9).error(), error::out_of_bounds);
}

TEST(VectorTest, SpanPositionAndTryMapTo) {
  const int a[] = {4, 8, 15};
  reloco::span<const int> sp(a, 3);
  EXPECT_EQ(sp.position(8), 1u);
  EXPECT_FALSE(sp.position(99).has_value());
  EXPECT_EQ(sp.position_if([](int x) { return x > 10; }), 2u);
  auto doubled = sp.try_map_to([](int x) { return x * 2; });
  ASSERT_TRUE(doubled);
  ASSERT_EQ(doubled->size(), 3u);
  EXPECT_EQ((*doubled)[2], 30);
}

TEST(VectorTest, MapVector) {
  auto v = vector<int>::try_create().unwrap();
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));
  ASSERT_TRUE(v.try_push_back(4));
  auto result = v.iter().map([](int x) { return x * 2; });
  ASSERT_EQ(result.next().unwrap(), 2);
  ASSERT_EQ(result.next().unwrap(), 4);
  ASSERT_EQ(result.next().unwrap(), 6);
  ASSERT_EQ(result.next().unwrap(), 8);
  ASSERT_FALSE(result.next().has_value());
}

namespace {
struct pinned_item {
  int value = 0;
  pinned_item() = default;
  ~pinned_item() {}
  pinned_item(const pinned_item &) = delete;
  pinned_item &operator=(const pinned_item &) = delete;
};
} // namespace

TEST(VectorTest, PinnedElementsCompileAndReportUnsupportedRelocation) {
  auto v = vector<pinned_item>::try_create_sized(2);
  ASSERT_TRUE(v.has_value());
  (*v)[1].value = 5;
  EXPECT_EQ((*v)[1].value, 5);

  EXPECT_EQ(v->try_erase_at(0).error(), error::unsupported_operation);
  EXPECT_TRUE(v->try_erase_at(1).has_value());
  EXPECT_EQ(v->size(), 1u);
}
