// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/sso_vector.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>

#include <string>
#include <utility>

using reloco::error;
using reloco::sso_vector;

namespace {

// Move-only, non-trivially-relocatable/copyable stand-in for user types that
// only support nothrow move construction (exercises the manual move/destroy
// shift paths, not the memcpy/memmove fast paths). Mirrors test_vector.cpp's
// / test_inline_vector.cpp's helper of the same name/shape.
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

TEST(SsoVectorTest, DefaultConstructedIsEmptyAndInline) {
  sso_vector<int, 4> v;
  EXPECT_TRUE(v.empty());
  EXPECT_EQ(v.size(), 0u);
  EXPECT_EQ(v.capacity(), 4u);
  EXPECT_TRUE(v.is_inline());
}

TEST(SsoVectorTest, StaysInlineWithinInlineCapacity) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));
  ASSERT_TRUE(v.try_push_back(4));
  EXPECT_TRUE(v.is_inline());
  EXPECT_EQ(v.capacity(), 4u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[3], 4);
}

TEST(SsoVectorTest, GrowingPastInlineCapacityPromotesToHeap) {
  sso_vector<int, 2> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  EXPECT_TRUE(v.is_inline());

  auto res = v.try_push_back(3);
  ASSERT_TRUE(res);
  EXPECT_FALSE(v.is_inline());
  EXPECT_GT(v.capacity(), 2u);
  EXPECT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 2);
  EXPECT_EQ(v[2], 3);
}

TEST(SsoVectorTest, ShrinkToFitDemotesBackToInline) {
  sso_vector<int, 2> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));
  ASSERT_FALSE(v.is_inline());

  ASSERT_TRUE(v.try_pop_back());
  auto res = v.shrink_to_fit();
  ASSERT_TRUE(res);
  EXPECT_TRUE(v.is_inline());
  EXPECT_EQ(v.capacity(), 2u);
  EXPECT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 2);
}

TEST(SsoVectorTest, ShrinkToFitWhileInlineIsANoOp) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  auto res = v.shrink_to_fit();
  ASSERT_TRUE(res);
  EXPECT_TRUE(v.is_inline());
  EXPECT_EQ(v.capacity(), 4u);
}

TEST(SsoVectorTest, ShrinkToFitShrinksHeapBufferWithoutDemoting) {
  sso_vector<int, 2> v;
  for (int i = 0; i < 8; ++i)
    ASSERT_TRUE(v.try_push_back(i));
  ASSERT_FALSE(v.is_inline());
  const auto grown_cap = v.capacity();

  ASSERT_TRUE(v.try_pop_back());
  auto res = v.shrink_to_fit();
  ASSERT_TRUE(res);
  EXPECT_FALSE(v.is_inline());
  EXPECT_EQ(v.capacity(), v.size());
  EXPECT_LT(v.capacity(), grown_cap);
}

TEST(SsoVectorTest, TryReserveGrowsWithoutChangingSize) {
  sso_vector<int, 2> v;
  ASSERT_TRUE(v.try_push_back(1));
  auto res = v.try_reserve(16);
  ASSERT_TRUE(res);
  EXPECT_FALSE(v.is_inline());
  EXPECT_GE(v.capacity(), 16u);
  EXPECT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], 1);
}

TEST(SsoVectorTest, TryReserveWithinInlineCapacityStaysInline) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  auto res = v.try_reserve(2);
  ASSERT_TRUE(res);
  EXPECT_TRUE(v.is_inline());
  EXPECT_EQ(v.capacity(), 4u);
}

TEST(SsoVectorTest, TryEmplaceBackConstructsInPlace) {
  sso_vector<reloco::string, 2> v;
  auto res = v.try_emplace_back(reloco::string_view("xxx"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->get().view(), "xxx");
  EXPECT_EQ(v[0].view(), "xxx");
}

TEST(SsoVectorTest, TryPushBackFailsPropagateFromAllocationFailure) {
  // Sanity check only: with default_allocator, growth should always
  // succeed for reasonably small sizes. This is a smoke test that the
  // fallible signature is exercised, not an OOM simulation.
  sso_vector<int, 1> v;
  auto res = v.try_push_back(1);
  ASSERT_TRUE(res);
}

TEST(SsoVectorTest, TryPopBackRemovesLastElement) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_pop_back());
  EXPECT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], 1);
}

TEST(SsoVectorTest, TryPopBackFailsOnEmpty) {
  sso_vector<int, 4> v;
  auto res = v.try_pop_back();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::container_empty);
}

TEST(SsoVectorTest, ClearDestroysElementsAndResetsSizeWhileInline) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  bool destroyed[3] = {false, false, false};
  sso_vector<move_only, 4> v;
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

TEST(SsoVectorTest, ClearDestroysElementsAndResetsSizeWhileHeap) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  bool destroyed[3] = {false, false, false};
  sso_vector<move_only, 1> v;
  ASSERT_TRUE(v.try_emplace_back(1, &destroyed[0]));
  ASSERT_TRUE(v.try_emplace_back(2, &destroyed[1]));
  ASSERT_TRUE(v.try_emplace_back(3, &destroyed[2]));
  ASSERT_FALSE(v.is_inline());
  v.clear();
  EXPECT_TRUE(v.empty());
  EXPECT_FALSE(v.is_inline());
  EXPECT_TRUE(destroyed[0]);
  EXPECT_TRUE(destroyed[1]);
  EXPECT_TRUE(destroyed[2]);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(SsoVectorTest, TryInsertAtShiftsElementsRight) {
  sso_vector<int, 4> v;
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

TEST(SsoVectorTest, TryInsertAtGrowsPastInlineCapacity) {
  sso_vector<int, 2> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));

  auto res = v.try_insert_at(1, 99);
  ASSERT_TRUE(res);
  EXPECT_FALSE(v.is_inline());
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 99);
  EXPECT_EQ(v[2], 2);
}

TEST(SsoVectorTest, TryInsertAtOutOfBoundsFails) {
  sso_vector<int, 4> v;
  auto res = v.try_insert_at(1, 42);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::out_of_bounds);
}

TEST(SsoVectorTest, TryInsertAtNonRelocatableMovesElements) {
  sso_vector<move_only, 4> v;
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

TEST(SsoVectorTest, TryEraseAtShiftsElementsLeft) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));

  ASSERT_TRUE(v.try_erase_at(1));
  EXPECT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 3);
}

TEST(SsoVectorTest, TryEraseAtOutOfBoundsFails) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  auto res = v.try_erase_at(5);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::out_of_bounds);
}

TEST(SsoVectorTest, TryEraseAtWhileHeapBackedShiftsElements) {
  sso_vector<reloco::string, 2> v;
  for (int i = 0; i < 5; ++i)
    ASSERT_TRUE(v.try_emplace_back(reloco::string_view("x")));
  ASSERT_FALSE(v.is_inline());

  ASSERT_TRUE(v.try_erase_at(0));
  EXPECT_EQ(v.size(), 4u);
}

TEST(SsoVectorTest, TryAtChecksBounds) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(42));
  auto res = v.try_at(0);
  ASSERT_TRUE(res);
  EXPECT_EQ(res->get(), 42);
  auto bad = v.try_at(1);
  ASSERT_FALSE(bad);
  EXPECT_EQ(bad.error(), error::out_of_bounds);
}

TEST(SsoVectorTest, FrontAndBack) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));
  EXPECT_EQ(v.front(), 1);
  EXPECT_EQ(v.back(), 3);
}

TEST(SsoVectorTest, TryFrontAndTryBackFailOnEmpty) {
  sso_vector<int, 4> v;
  auto front_res = v.try_front();
  ASSERT_FALSE(front_res);
  EXPECT_EQ(front_res.error(), error::container_empty);
  auto back_res = v.try_back();
  ASSERT_FALSE(back_res);
  EXPECT_EQ(back_res.error(), error::container_empty);
}

TEST(SsoVectorTest, TryDataFailsOnEmpty) {
  sso_vector<int, 4> v;
  auto res = v.try_data();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::container_empty);
}

TEST(SsoVectorTest, UnsafeAtAndUnsafeDataBypassChecks) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(7));
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  EXPECT_EQ(v.unsafe_at(0), 7);
  EXPECT_EQ(*v.unsafe_data(), 7);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(SsoVectorTest, IteratesInOrder) {
  sso_vector<int, 2> v;
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

TEST(SsoVectorTest, ReverseIteratesInOrder) {
  sso_vector<int, 4> v;
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

TEST(SsoVectorTest, MoveConstructionFromInlineCopiesBuffer) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.is_inline());

  sso_vector<int, 4> moved(std::move(v));
  EXPECT_TRUE(moved.is_inline());
  EXPECT_EQ(moved.size(), 2u);
  EXPECT_EQ(moved[0], 1);
  EXPECT_EQ(moved[1], 2);
  EXPECT_TRUE(v.empty());     // NOLINT(bugprone-use-after-move)
  EXPECT_TRUE(v.is_inline()); // NOLINT(bugprone-use-after-move)
}

TEST(SsoVectorTest, MoveConstructionFromHeapStealsPointer) {
  sso_vector<int, 2> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));
  ASSERT_TRUE(v.try_push_back(3));
  ASSERT_FALSE(v.is_inline());
  const int *original_data = v.data();

  sso_vector<int, 2> moved(std::move(v));
  EXPECT_FALSE(moved.is_inline());
  EXPECT_EQ(moved.data(), original_data);
  EXPECT_EQ(moved.size(), 3u);
  EXPECT_TRUE(v.empty());     // NOLINT(bugprone-use-after-move)
  EXPECT_TRUE(v.is_inline()); // NOLINT(bugprone-use-after-move)
}

TEST(SsoVectorTest, MoveConstructionNonRelocatableMovesElements) {
  bool destroyed[2] = {false, false};
  sso_vector<move_only, 4> v;
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  ASSERT_TRUE(v.try_emplace_back(1, &destroyed[0]));
  ASSERT_TRUE(v.try_emplace_back(2, &destroyed[1]));
  RELOCO_END_UNSAFE_BUFFER_USAGE;

  sso_vector<move_only, 4> moved(std::move(v));
  EXPECT_EQ(moved.size(), 2u);
  EXPECT_EQ(moved[0].value, 1);
  EXPECT_EQ(moved[1].value, 2);
  EXPECT_TRUE(v.empty()); // NOLINT(bugprone-use-after-move)
}

TEST(SsoVectorTest, MoveAssignmentReleasesPreviousElements) {
  sso_vector<int, 4> a;
  ASSERT_TRUE(a.try_push_back(1));

  sso_vector<int, 4> b;
  ASSERT_TRUE(b.try_push_back(2));
  ASSERT_TRUE(b.try_push_back(3));

  a = std::move(b);
  EXPECT_EQ(a.size(), 2u);
  EXPECT_EQ(a[0], 2);
  EXPECT_EQ(a[1], 3);
  EXPECT_TRUE(b.empty()); // NOLINT(bugprone-use-after-move)
}

TEST(SsoVectorTest, MoveAssignmentFromHeapBackedReleasesInlineTarget) {
  sso_vector<int, 2> a;
  ASSERT_TRUE(a.try_push_back(1));

  sso_vector<int, 2> b;
  for (int i = 0; i < 5; ++i)
    ASSERT_TRUE(b.try_push_back(i));
  ASSERT_FALSE(b.is_inline());

  a = std::move(b);
  EXPECT_FALSE(a.is_inline());
  EXPECT_EQ(a.size(), 5u);
  EXPECT_TRUE(b.empty());     // NOLINT(bugprone-use-after-move)
  EXPECT_TRUE(b.is_inline()); // NOLINT(bugprone-use-after-move)
}

TEST(SsoVectorTest, TryCloneWithTrivialTypeUsesMemcpyPath) {
  sso_vector<int, 4> v;
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

TEST(SsoVectorTest, TryCloneWithNonTrivialTypeClonesPerElement) {
  sso_vector<reloco::string, 4> v;
  ASSERT_TRUE(v.try_emplace_back(reloco::string_view("hello")));
  ASSERT_TRUE(v.try_emplace_back(reloco::string_view("world")));

  auto clone = v.try_clone();
  ASSERT_TRUE(clone);
  EXPECT_EQ((*clone)[0].view(), "hello");
  EXPECT_EQ((*clone)[1].view(), "world");
}

TEST(SsoVectorTest, TryCloneOfHeapBackedSourceReservesUpFront) {
  sso_vector<int, 2> v;
  for (int i = 0; i < 5; ++i)
    ASSERT_TRUE(v.try_push_back(i));
  ASSERT_FALSE(v.is_inline());

  auto clone = v.try_clone();
  ASSERT_TRUE(clone);
  EXPECT_FALSE(clone->is_inline());
  EXPECT_EQ(clone->size(), 5u);
  for (int i = 0; i < 5; ++i)
    EXPECT_EQ((*clone)[static_cast<std::size_t>(i)], i);
}

TEST(SsoVectorTest, TryCloneWithExplicitAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));

  auto clone = v.try_clone(heap);
  ASSERT_TRUE(clone);
  EXPECT_EQ((*clone)[0], 1);
}

TEST(SsoVectorTest, TryCloneAtWritesIntoUninitializedStorage) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  ASSERT_TRUE(v.try_push_back(2));

  alignas(sso_vector<int, 4>) std::byte storage[sizeof(sso_vector<int, 4>)];
  auto res =
      sso_vector<int, 4>::try_clone_at(reloco::default_allocator(), reinterpret_cast<sso_vector<int, 4> *>(storage), v);
  ASSERT_TRUE(res);
  auto *cloned = reinterpret_cast<sso_vector<int, 4> *>(storage);
  EXPECT_EQ(cloned->size(), 2u);
  EXPECT_EQ((*cloned)[0], 1);
  EXPECT_EQ((*cloned)[1], 2);
  cloned->~sso_vector();
}

TEST(SsoVectorTest, TryAllocateReservesUpFrontPastInlineCapacity) {
  auto v = sso_vector<int, 2>::try_allocate(reloco::default_allocator(), 16);
  ASSERT_TRUE(v);
  EXPECT_FALSE(v->is_inline());
  EXPECT_GE(v->capacity(), 16u);
  EXPECT_TRUE(v->empty());
}

TEST(SsoVectorTest, TryCreateWithinInlineCapacityStaysInline) {
  auto v = sso_vector<int, 4>::try_create(2);
  ASSERT_TRUE(v);
  EXPECT_TRUE(v->is_inline());
  EXPECT_EQ(v->capacity(), 4u);
}

TEST(SsoVectorTest, AdviseAndAdviseUnusedAreNoOpsWhileInline) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));
  v.advise(reloco::usage_hint::dont_need);
  v.advise_unused();
  EXPECT_TRUE(v.is_inline());
  EXPECT_EQ(v[0], 1);
}

TEST(SsoVectorTest, IsNotTriviallyRelocatable) {
  using int_sso_vec = sso_vector<int, 4>;
  using string_sso_vec = sso_vector<reloco::string, 4>;
  EXPECT_FALSE(reloco::is_trivially_relocatable_v<int_sso_vec>);
  EXPECT_FALSE(reloco::is_trivially_relocatable_v<string_sso_vec>);
}

TEST(SsoVectorTest, TryResizeGrowsWithDefaultValueWhileInline) {
  sso_vector<int, 4> v;
  ASSERT_TRUE(v.try_push_back(1));

  ASSERT_TRUE(v.try_resize(4));
  EXPECT_EQ(v.size(), 4u);
  EXPECT_TRUE(v.is_inline());
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 0);
  EXPECT_EQ(v[2], 0);
  EXPECT_EQ(v[3], 0);
}

TEST(SsoVectorTest, TryResizeGrowsWithFillValueAndPromotesToHeap) {
  sso_vector<int, 2> v;
  ASSERT_TRUE(v.try_push_back(1));

  ASSERT_TRUE(v.try_resize(5, 7));
  EXPECT_EQ(v.size(), 5u);
  EXPECT_FALSE(v.is_inline());
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 7);
  EXPECT_EQ(v[2], 7);
  EXPECT_EQ(v[3], 7);
  EXPECT_EQ(v[4], 7);
}

TEST(SsoVectorTest, TryResizeShrinksAndDestroysTrailingElements) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  bool destroyed[4] = {false, false, false, false};
  sso_vector<move_only, 4> v;
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
