// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/detail/type_metadata.hpp>
#include <reloco/detail/type_operations.hpp>
#include <reloco/error.hpp>
#include <reloco/heap_allocator.hpp>

#include <cstddef>
#include <new>
#include <string>
#include <utility>

using reloco::detail::get_type_operations_for;
using reloco::detail::metadata_for;
using reloco::detail::operations_for_trivial_element;
using reloco::detail::type_operations;

namespace {

reloco::allocator_ref heap() { return reloco::allocator<reloco::heap_allocator_tag>::ref(); }

// Non-trivial in every way (user-provided copy/move/destructor), so it
// always resolves to a per-type `type_operations_for<T>` table rather than
// `operations_for_trivial_element`. `destroy_count` is incremented by every
// destructor call that still holds a live pointer, letting tests
// distinguish "destroyed the already-moved-from shell" (no-op) from
// "destroyed the live, relocated-to copy" (counts).
struct tracked {
  int value = 0;
  int *destroy_count = nullptr;
  static inline int copy_count = 0;

  tracked() noexcept = default;
  explicit tracked(int v) noexcept : value(v) {}
  tracked(int v, int *count) noexcept : value(v), destroy_count(count) {}

  tracked(const tracked &other) noexcept : value(other.value), destroy_count(other.destroy_count) { ++copy_count; }
  tracked &operator=(const tracked &) = delete;

  tracked(tracked &&other) noexcept : value(other.value), destroy_count(other.destroy_count) {
    other.destroy_count = nullptr;
  }
  tracked &operator=(tracked &&) = delete;

  ~tracked() noexcept {
    if (destroy_count)
      ++(*destroy_count);
  }
};

// Non-trivial (user-provided destructor), not copy-constructible, and no
// custom clone/try_clone protocol -- exercises `copy_construct_one`'s
// "unsupported" path and `clone_one`'s "not cloneable" (nullptr) path.
struct move_only_non_copyable {
  int value = 0;

  move_only_non_copyable() noexcept = default;
  explicit move_only_non_copyable(int v) noexcept : value(v) {}
  move_only_non_copyable(const move_only_non_copyable &) = delete;
  move_only_non_copyable &operator=(const move_only_non_copyable &) = delete;
  move_only_non_copyable(move_only_non_copyable &&) noexcept = default;
  move_only_non_copyable &operator=(move_only_non_copyable &&) noexcept = default;
  ~move_only_non_copyable() noexcept {}
};

// Non-trivial (user-provided destructor) and not default-constructible --
// exercises `copy_construct_one`'s default-construction "unsupported" path.
struct no_default_ctor {
  int value;
  explicit no_default_ctor(int v) noexcept : value(v) {}
  no_default_ctor(const no_default_ctor &) noexcept = default;
  ~no_default_ctor() noexcept {}
};

// Uninitialized, correctly-aligned storage for exactly one `T`.
template <typename T> struct raw_slot {
  alignas(alignof(T)) std::byte bytes[sizeof(T)];

  void *ptr() noexcept { return static_cast<void *>(bytes); }
  T &as() noexcept { return *std::launder(reinterpret_cast<T *>(bytes)); }
};

} // namespace

TEST(TypeOperationsTest, TrivialTypeSharesTheGlobalTrivialTable) {
  EXPECT_EQ(get_type_operations_for<int>(), &operations_for_trivial_element);
  EXPECT_EQ(get_type_operations_for<int>(), get_type_operations_for<unsigned int>());
}

TEST(TypeOperationsTest, TrivialPairSharesTheGlobalTrivialTable) {
  EXPECT_EQ((get_type_operations_for<std::pair<int, int>>()), &operations_for_trivial_element);
}

TEST(TypeOperationsTest, PairWithANonTrivialMemberDoesNotShareTheTrivialTable) {
  EXPECT_NE((get_type_operations_for<std::pair<int, std::string>>()), &operations_for_trivial_element);
}

TEST(TypeOperationsTest, TriviallyDestructibleTypeHasNullDestroyOne) {
  EXPECT_EQ(get_type_operations_for<int>()->destroy_one, nullptr);
}

TEST(TypeOperationsTest, TrivialCloneOneCopiesBytes) {
  const int src = 42;
  raw_slot<int> slot;
  auto res = get_type_operations_for<int>()->clone_one(metadata_for<int>, heap(), slot.ptr(), &src);
  ASSERT_TRUE(res);
  EXPECT_EQ(slot.as(), 42);
}

TEST(TypeOperationsTest, TrivialCopyConstructOneDefaultConstructsWhenValuePtrIsNull) {
  raw_slot<int> slot;
  auto res = get_type_operations_for<int>()->copy_construct_one(metadata_for<int>, heap(), slot.ptr(), nullptr);
  ASSERT_TRUE(res);
  EXPECT_EQ(slot.as(), 0);
}

TEST(TypeOperationsTest, TrivialCopyConstructOneCopiesGivenValue) {
  const int value = 7;
  raw_slot<int> slot;
  auto res = get_type_operations_for<int>()->copy_construct_one(metadata_for<int>, heap(), slot.ptr(), &value);
  ASSERT_TRUE(res);
  EXPECT_EQ(slot.as(), 7);
}

TEST(TypeOperationsTest, TrivialRelocateOneCopiesBytes) {
  int src = 13;
  raw_slot<int> dest;
  get_type_operations_for<int>()->relocate_one(metadata_for<int>, dest.ptr(), &src);
  EXPECT_EQ(dest.as(), 13);
}

TEST(TypeOperationsTest, NonTrivialTypeGetsItsOwnPerTypeTable) {
  const type_operations *ops = get_type_operations_for<tracked>();
  EXPECT_NE(ops, &operations_for_trivial_element);
  EXPECT_NE(ops->destroy_one, nullptr);
}

TEST(TypeOperationsTest, NonTrivialTypeTableIsSingleton) {
  EXPECT_EQ(get_type_operations_for<tracked>(), get_type_operations_for<tracked>());
}

TEST(TypeOperationsTest, DestroyOneInvokesDestructor) {
  int destroy_count = 0;
  raw_slot<tracked> slot;
  new (slot.ptr()) tracked(9, &destroy_count);
  get_type_operations_for<tracked>()->destroy_one(metadata_for<tracked>, slot.ptr());
  EXPECT_EQ(destroy_count, 1);
}

TEST(TypeOperationsTest, CopyConstructOneDefaultConstructsWhenValuePtrIsNull) {
  raw_slot<tracked> slot;
  auto res =
      get_type_operations_for<tracked>()->copy_construct_one(metadata_for<tracked>, heap(), slot.ptr(), nullptr);
  ASSERT_TRUE(res);
  EXPECT_EQ(slot.as().value, 0);
  slot.as().~tracked();
}

TEST(TypeOperationsTest, CopyConstructOneCopiesGivenValueViaCopyConstructor) {
  tracked::copy_count = 0;
  const tracked source(42);
  raw_slot<tracked> slot;
  auto res =
      get_type_operations_for<tracked>()->copy_construct_one(metadata_for<tracked>, heap(), slot.ptr(), &source);
  ASSERT_TRUE(res);
  EXPECT_EQ(slot.as().value, 42);
  EXPECT_EQ(tracked::copy_count, 1);
  slot.as().~tracked();
}

TEST(TypeOperationsTest, CloneOneUsesCopyConstructor) {
  tracked::copy_count = 0;
  const tracked source(99);
  raw_slot<tracked> slot;
  auto res = get_type_operations_for<tracked>()->clone_one(metadata_for<tracked>, heap(), slot.ptr(), &source);
  ASSERT_TRUE(res);
  EXPECT_EQ(slot.as().value, 99);
  EXPECT_EQ(tracked::copy_count, 1);
  slot.as().~tracked();
}

TEST(TypeOperationsTest, RelocateOneMovesValueAndDestroysExactlyOnceOverall) {
  int destroy_count = 0;
  raw_slot<tracked> src_slot;
  raw_slot<tracked> dest_slot;
  new (src_slot.ptr()) tracked(55, &destroy_count);

  get_type_operations_for<tracked>()->relocate_one(metadata_for<tracked>, dest_slot.ptr(), src_slot.ptr());

  EXPECT_EQ(dest_slot.as().value, 55);
  // relocate_one destroyed only the moved-from source shell so far, which
  // no longer holds the counter pointer -- the real side effect hasn't
  // fired yet.
  EXPECT_EQ(destroy_count, 0);

  dest_slot.as().~tracked();
  // Destroying the live, relocated-to copy runs the real destructor
  // exactly once -- proving relocate_one didn't double-destroy anything.
  EXPECT_EQ(destroy_count, 1);
}

TEST(TypeOperationsTest, ClonableTypeWithoutCopySemanticsHasNullCloneOne) {
  EXPECT_EQ(get_type_operations_for<move_only_non_copyable>()->clone_one, nullptr);
}

TEST(TypeOperationsTest, CopyConstructOneReturnsUnsupportedForNonCopyableType) {
  raw_slot<move_only_non_copyable> slot;
  move_only_non_copyable value(3);
  auto res = get_type_operations_for<move_only_non_copyable>()->copy_construct_one(
      metadata_for<move_only_non_copyable>, heap(), slot.ptr(), &value);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), reloco::error::unsupported_operation);
}

TEST(TypeOperationsTest, CopyConstructOneReturnsInvalidArgumentWhenDefaultConstructionUnsupported) {
  raw_slot<no_default_ctor> slot;
  auto res = get_type_operations_for<no_default_ctor>()->copy_construct_one(metadata_for<no_default_ctor>, heap(),
                                                                            slot.ptr(), nullptr);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), reloco::error::invalid_argument);
}
