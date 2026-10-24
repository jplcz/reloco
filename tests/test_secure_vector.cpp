// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/secure_vector.hpp>
#include <reloco/secure_wipe.hpp>
#include <reloco/vector.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace {

// An allocator that checks what a block contains at the moment it is freed:
// a secure container must have wiped it first.
struct spy_context {
  std::size_t allocations = 0;
  std::size_t frees = 0;
  std::size_t frees_not_zero = 0;
};

struct spy_allocator_tag {};

} // namespace

template <> struct reloco::allocator_traits<spy_allocator_tag> {
  using context_type = spy_context;

  static reloco::result<reloco::mem_block> allocate(reloco::value_ref<context_type> ctx, std::size_t bytes,
                                                    std::size_t alignment) noexcept {
    if (alignment < alignof(void *))
      alignment = alignof(void *);
    const std::size_t rounded = (bytes + alignment - 1) / alignment * alignment;
    void *ptr = std::aligned_alloc(alignment, rounded);
    if (ptr == nullptr)
      return reloco::unexpected(reloco::error::allocation_failed);
    ++ctx->allocations;
    return reloco::mem_block{ptr, bytes};
  }

  static void deallocate(reloco::value_ref<context_type> ctx, void *ptr, std::size_t bytes) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    const auto *raw = static_cast<const unsigned char *>(ptr);
    bool zero = true;
    for (std::size_t i = 0; i < bytes; ++i) {
      if (raw[i] != 0)
        zero = false;
    }
    RELOCO_END_UNSAFE_BUFFER_USAGE
    ++ctx->frees;
    if (!zero)
      ++ctx->frees_not_zero;
    std::free(ptr);
  }
};

namespace {

using reloco::error;
using reloco::secure_vector;

struct key_block {
  std::uint32_t words[4];
};

struct spy_fixture : ::testing::Test {
  spy_context ctx;
  reloco::allocator_ref alloc{spy_allocator_tag{}, ctx};
};

// ---- secure_wipe -----------------------------------------------------------

TEST(SecureWipeTest, ZeroesABuffer) {
  unsigned char buf[16];
  for (auto &b : buf)
    b = 0xA5;
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  reloco::secure_wipe(buf, sizeof(buf));
  RELOCO_END_UNSAFE_BUFFER_USAGE
  for (auto b : buf)
    EXPECT_EQ(b, 0);
}

TEST(SecureWipeTest, ZeroesATrivialObjectAndAnArray) {
  key_block key{{1, 2, 3, 4}};
  reloco::secure_wipe(key);
  for (auto w : key.words)
    EXPECT_EQ(w, 0u);

  std::uint64_t arr[3] = {7, 8, 9};
  reloco::secure_wipe(arr);
  for (auto w : arr)
    EXPECT_EQ(w, 0u);
}

TEST(SecureWipeTest, NullAndEmptyAreNoOps) {
  unsigned char buf[4] = {1, 2, 3, 4};
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  reloco::secure_wipe(nullptr, 8);
  reloco::secure_wipe(buf, 0);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(buf[0], 1);
  EXPECT_EQ(buf[3], 4);
}

// ---- secure_vector: contract ----------------------------------------------

static_assert(!std::is_copy_constructible_v<secure_vector<std::uint8_t>>);
static_assert(!std::is_copy_assignable_v<secure_vector<std::uint8_t>>);
static_assert(std::is_nothrow_move_constructible_v<secure_vector<std::uint8_t>>);
static_assert(reloco::is_trivially_relocatable_v<secure_vector<std::uint8_t>>);
static_assert(!std::is_convertible_v<secure_vector<std::uint8_t>, reloco::vector<std::uint8_t>>);
static_assert(!std::is_convertible_v<reloco::vector<std::uint8_t>, secure_vector<std::uint8_t>>);

// ---- secure_vector: the wipe ------------------------------------------------

TEST(SecureVectorFromSpanTest, TryCreateFromCopiesSpan) {
  const std::uint8_t src[] = {1, 2, 3};
  auto v = secure_vector<std::uint8_t>::try_create_from(reloco::span<const std::uint8_t>(src, 3));
  ASSERT_TRUE(v);
  EXPECT_EQ(v->size(), 3u);
}

TEST_F(spy_fixture, PlainVectorFreesSecretsUnwiped) {
  // Control: proves the spy detects an unwiped free.
  {
    auto v = reloco::vector<std::uint8_t>::try_allocate(alloc, 16);
    ASSERT_TRUE(v);
    for (int i = 0; i < 8; ++i)
      ASSERT_TRUE(v->try_push_back(0x5A));
  }
  EXPECT_EQ(ctx.frees, 1u);
  EXPECT_EQ(ctx.frees_not_zero, 1u);
}

TEST_F(spy_fixture, DestructorWipesTheWholeCapacity) {
  {
    auto v = secure_vector<std::uint8_t>::try_allocate(alloc, 32);
    ASSERT_TRUE(v);
    for (int i = 0; i < 8; ++i)
      ASSERT_TRUE(v->try_push_back(0x5A));
    EXPECT_GE(v->capacity(), 32u);
  }
  EXPECT_EQ(ctx.frees, 1u);
  EXPECT_EQ(ctx.frees_not_zero, 0u);
}

TEST_F(spy_fixture, GrowthWipesTheOldBlockAndKeepsTheContents) {
  secure_vector<std::uint8_t> v(alloc);
  for (int i = 0; i < 100; ++i)
    ASSERT_TRUE(v.try_push_back(static_cast<std::uint8_t>(i + 1)));
  ASSERT_EQ(v.size(), 100u);
  for (std::size_t i = 0; i < v.size(); ++i)
    EXPECT_EQ(v[i], static_cast<std::uint8_t>(i + 1));
  EXPECT_GT(ctx.frees, 1u); // several growth steps, each freed its old block
  EXPECT_EQ(ctx.frees_not_zero, 0u);
}

TEST_F(spy_fixture, ReserveWipesTheOldBlock) {
  secure_vector<std::uint8_t> v(alloc);
  ASSERT_TRUE(v.try_push_back(1));
  const auto before = ctx.frees;
  ASSERT_TRUE(v.try_reserve(1024));
  EXPECT_GE(v.capacity(), 1024u);
  EXPECT_EQ(ctx.frees, before + 1);
  EXPECT_EQ(ctx.frees_not_zero, 0u);
  EXPECT_EQ(v[0], 1);
  ASSERT_TRUE(v.try_reserve(8)); // never shrinks
  EXPECT_GE(v.capacity(), 1024u);
}

TEST_F(spy_fixture, ShrinkToFitWipesTheOldBlock) {
  secure_vector<std::uint8_t> v(alloc);
  ASSERT_TRUE(v.try_reserve(256));
  ASSERT_TRUE(v.try_push_back(9));
  ASSERT_TRUE(v.try_shrink_to_fit());
  EXPECT_LT(v.capacity(), 256u);
  EXPECT_EQ(v[0], 9);
  EXPECT_EQ(ctx.frees_not_zero, 0u);
}

TEST_F(spy_fixture, PopBackWipesTheVacatedSlot) {
  secure_vector<std::uint8_t> v(alloc);
  ASSERT_TRUE(v.try_push_back(0x11));
  ASSERT_TRUE(v.try_push_back(0x22));
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  const std::uint8_t *raw = v.unsafe_data();
  v.pop_back();
  EXPECT_EQ(raw[1], 0);
  EXPECT_EQ(raw[0], 0x11);
  ASSERT_TRUE(v.try_pop_back());
  EXPECT_EQ(raw[0], 0);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  EXPECT_TRUE(v.empty());
  EXPECT_EQ(v.try_pop_back().error(), error::container_empty);
}

TEST_F(spy_fixture, ClearWipesTheLiveElementsAndKeepsTheBlock) {
  secure_vector<std::uint8_t> v(alloc);
  for (int i = 0; i < 6; ++i)
    ASSERT_TRUE(v.try_push_back(0x77));
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  const std::uint8_t *raw = v.unsafe_data();
  const auto cap = v.capacity();
  v.clear();
  for (std::size_t i = 0; i < 6; ++i)
    EXPECT_EQ(raw[i], 0) << i;
  EXPECT_EQ(v.capacity(), cap);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  EXPECT_TRUE(v.empty());
}

TEST_F(spy_fixture, WipeErasesTheSpareCapacityToo) {
  secure_vector<std::uint8_t> v(alloc);
  ASSERT_TRUE(v.try_reserve(32));
  for (int i = 0; i < 20; ++i)
    ASSERT_TRUE(v.try_push_back(0x33));
  ASSERT_TRUE(v.try_resize(4)); // slots 4..19 wiped by the shrink
  ASSERT_TRUE(v.try_resize(20)); // default-constructs the new elements
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  const std::uint8_t *raw = v.unsafe_data();
  v.wipe();
  for (std::size_t i = 0; i < v.capacity(); ++i)
    EXPECT_EQ(raw[i], 0) << i;
  RELOCO_END_UNSAFE_BUFFER_USAGE
  EXPECT_TRUE(v.empty());
  EXPECT_GT(v.capacity(), 0u);
}

TEST_F(spy_fixture, EraseAtShiftsAndWipesTheVacatedTail) {
  secure_vector<std::uint8_t> v(alloc);
  for (const std::uint8_t b : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}})
    ASSERT_TRUE(v.try_push_back(b));
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  const std::uint8_t *raw = v.unsafe_data();
  ASSERT_TRUE(v.try_erase_at(1));
  EXPECT_EQ(v.size(), 3u);
  EXPECT_EQ(raw[0], 1);
  EXPECT_EQ(raw[1], 3);
  EXPECT_EQ(raw[2], 4);
  EXPECT_EQ(raw[3], 0); // the stale duplicate of the last element is gone
  RELOCO_END_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(v.try_erase_at(9).error(), error::out_of_bounds);
}

TEST_F(spy_fixture, InsertAtShiftsUpAndGrowsSafely) {
  secure_vector<std::uint8_t> v(alloc);
  for (const std::uint8_t b : {std::uint8_t{1}, std::uint8_t{3}})
    ASSERT_TRUE(v.try_push_back(b));
  ASSERT_TRUE(v.try_insert_at(1, 2));
  ASSERT_TRUE(v.try_insert_at(3, 4));
  ASSERT_EQ(v.size(), 4u);
  for (std::size_t i = 0; i < 4; ++i)
    EXPECT_EQ(v[i], static_cast<std::uint8_t>(i + 1));
  EXPECT_EQ(v.try_insert_at(9, 0).error(), error::out_of_bounds);
  EXPECT_EQ(ctx.frees_not_zero, 0u);
}

TEST_F(spy_fixture, ResizeShrinkWipesAndGrowthKeepsTheContents) {
  secure_vector<std::uint8_t> v(alloc);
  ASSERT_TRUE(v.try_resize(8, 0x44));
  ASSERT_EQ(v.size(), 8u);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  const std::uint8_t *raw = v.unsafe_data();
  ASSERT_TRUE(v.try_resize(3));
  for (std::size_t i = 3; i < 8; ++i)
    EXPECT_EQ(raw[i], 0) << i;
  RELOCO_END_UNSAFE_BUFFER_USAGE
  for (std::size_t i = 0; i < 3; ++i)
    EXPECT_EQ(v[i], 0x44);
  ASSERT_TRUE(v.try_resize(500, 0x55)); // grows by relocation
  EXPECT_EQ(v[2], 0x44);
  EXPECT_EQ(v[499], 0x55);
  EXPECT_EQ(ctx.frees_not_zero, 0u);
}

TEST_F(spy_fixture, PushingAnElementOfTheSameBufferSurvivesGrowth) {
  secure_vector<std::uint8_t> v(alloc);
  ASSERT_TRUE(v.try_push_back(0x66));
  while (v.size() < v.capacity())
    ASSERT_TRUE(v.try_push_back(0x66));
  ASSERT_TRUE(v.try_push_back(v[0])); // the argument is a copy, so growth wiping the old block is harmless
  EXPECT_EQ(v.back(), 0x66);
}

TEST_F(spy_fixture, MoveAssignmentWipesTheReplacedBuffer) {
  secure_vector<std::uint8_t> a(alloc);
  secure_vector<std::uint8_t> b(alloc);
  for (int i = 0; i < 8; ++i)
    ASSERT_TRUE(a.try_push_back(0x12));
  ASSERT_TRUE(b.try_push_back(0x34));
  const auto before = ctx.frees;
  a = std::move(b);
  EXPECT_EQ(ctx.frees, before + 1);
  EXPECT_EQ(ctx.frees_not_zero, 0u);
  EXPECT_EQ(a.size(), 1u);
  EXPECT_EQ(a[0], 0x34);
  EXPECT_EQ(b.size(), 0u);
}

TEST_F(spy_fixture, MoveConstructionTransfersOwnershipWithoutFreeing) {
  secure_vector<std::uint8_t> a(alloc);
  ASSERT_TRUE(a.try_push_back(0x12));
  const auto before = ctx.frees;
  secure_vector<std::uint8_t> b(std::move(a));
  EXPECT_EQ(ctx.frees, before);
  EXPECT_EQ(b.size(), 1u);
  EXPECT_EQ(a.capacity(), 0u);
}

TEST_F(spy_fixture, CloneIsIndependentAndAlsoWiped) {
  {
    secure_vector<std::uint8_t> a(alloc);
    for (const std::uint8_t b : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}})
      ASSERT_TRUE(a.try_push_back(b));
    auto c = a.try_clone();
    ASSERT_TRUE(c);
    ASSERT_EQ(c->size(), 3u);
    EXPECT_EQ((*c)[2], 3);
    a[0] = 9;
    EXPECT_EQ((*c)[0], 1);
  }
  EXPECT_EQ(ctx.frees, ctx.allocations);
  EXPECT_EQ(ctx.frees_not_zero, 0u);
}

TEST_F(spy_fixture, UnwrapUnwipedGivesUpTheGuarantee) {
  {
    secure_vector<std::uint8_t> a(alloc);
    ASSERT_TRUE(a.try_push_back(0x5A));
    reloco::vector<std::uint8_t> plain = std::move(a).unwrap_unwiped();
    EXPECT_EQ(plain.size(), 1u);
    EXPECT_EQ(plain[0], 0x5A);
  }
  EXPECT_EQ(ctx.frees_not_zero, 1u); // the escape hatch frees unwiped, as documented
}

TEST_F(spy_fixture, TrivialStructElementsAreWiped) {
  {
    secure_vector<key_block> v(alloc);
    ASSERT_TRUE(v.try_push_back(key_block{{1, 2, 3, 4}}));
    ASSERT_TRUE(v.try_push_back(key_block{{5, 6, 7, 8}}));
    EXPECT_EQ(v[1].words[3], 8u);
  }
  EXPECT_EQ(ctx.frees_not_zero, 0u);
}

// ---- secure_vector: the access tiers ----------------------------------------

TEST_F(spy_fixture, CheckedAndFallibleAccessors) {
  secure_vector<std::uint8_t> v(alloc);
  EXPECT_EQ(v.try_at(0).error(), error::out_of_bounds);
  EXPECT_EQ(v.try_front().error(), error::container_empty);
  EXPECT_EQ(v.try_back().error(), error::container_empty);
  EXPECT_EQ(v.try_data().error(), error::container_empty);
  ASSERT_TRUE(v.try_push_back(7));
  ASSERT_TRUE(v.try_push_back(8));
  EXPECT_EQ(v.front(), 7);
  EXPECT_EQ(v.back(), 8);
  auto second = v.try_at(1);
  ASSERT_TRUE(second);
  EXPECT_EQ(second->get(), 8);
  EXPECT_EQ(v.data()[0], 7);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(v.unsafe_at(1), 8);
  EXPECT_EQ(v.unsafe_data()[0], 7);
  RELOCO_END_UNSAFE_BUFFER_USAGE

  int sum = 0;
  for (auto x : v)
    sum += x;
  EXPECT_EQ(sum, 15);
  const auto &cv = v;
  sum = 0;
  for (auto it = cv.cbegin(); it != cv.cend(); ++it)
    sum += *it;
  EXPECT_EQ(sum, 15);
}

TEST_F(spy_fixture, AllocationFailureLeavesTheVectorIntact) {
  secure_vector<std::uint8_t> v(alloc);
  ASSERT_TRUE(v.try_push_back(1));
  EXPECT_EQ(v.try_reserve(std::size_t(-1) / 2).error(), error::allocation_failed);
  EXPECT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], 1);
}

} // namespace
