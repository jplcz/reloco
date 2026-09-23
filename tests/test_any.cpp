// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/any.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>

#include <array>
#include <string>
#include <utility>

namespace {

struct destructor_counter {
  static int live_count;
  int value;

  explicit destructor_counter(int v) noexcept : value(v) { ++live_count; }
  destructor_counter(const destructor_counter &other) noexcept : value(other.value) { ++live_count; }
  destructor_counter(destructor_counter &&other) noexcept : value(other.value) { ++live_count; }
  ~destructor_counter() { --live_count; }
};

int destructor_counter::live_count = 0;

// Large enough to force the heap tier (bigger than soo_capacity).
struct big_value {
  std::array<int, 16> data{};
  explicit big_value(int seed) noexcept {
    for (auto &v : data)
      v = seed++;
  }
};

struct non_copyable_value {
  int value;
  explicit non_copyable_value(int v) noexcept : value(v) {}
  non_copyable_value(const non_copyable_value &) = delete;
  non_copyable_value(non_copyable_value &&) noexcept = default;
};

// Move-only (deleted copy ctor), but implements a self-contained try_clone,
// so any::try_clone() should still succeed by dispatching through
// construction_helpers::try_clone_at instead of requiring a nothrow copy
// constructor.
struct custom_cloneable_value {
  int value;
  explicit custom_cloneable_value(int v) noexcept : value(v) {}
  custom_cloneable_value(const custom_cloneable_value &) = delete;
  custom_cloneable_value(custom_cloneable_value &&) noexcept = default;

  [[nodiscard]] reloco::result<custom_cloneable_value> try_clone() const noexcept {
    return custom_cloneable_value(value);
  }
};

struct exhausted_allocator_tag {};

} // namespace

template <> struct reloco::allocator_traits<exhausted_allocator_tag> {
  using context_type = void;

  static reloco::result<reloco::mem_block> allocate(std::size_t, std::size_t) noexcept {
    return reloco::unexpected(reloco::error::allocation_failed);
  }

  static void deallocate(void *, std::size_t) noexcept {}
};

TEST(AnyTest, WrapsASmallValueUsingSooStorage) {
  auto a = reloco::any::try_create(42);
  ASSERT_TRUE(a);
  EXPECT_TRUE(*a);
  ASSERT_TRUE(a->template is<int>());
  EXPECT_EQ(a->template get<int>(), 42);
}

TEST(AnyTest, WrapsALargeValueUsingHeapStorage) {
  auto a = reloco::any::try_create(big_value(100));
  ASSERT_TRUE(a);
  ASSERT_TRUE(a->template is<big_value>());
  EXPECT_EQ(a->template get<big_value>().data[0], 100);
}

TEST(AnyTest, TryAllocateUsesExplicitAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto a = reloco::any::try_allocate(heap, big_value(5));
  ASSERT_TRUE(a);
  EXPECT_EQ(a->template get<big_value>().data[0], 5);
}

TEST(AnyTest, TryAllocateReportsHeapAllocationFailure) {
  reloco::allocator_ref exhausted = reloco::allocator<exhausted_allocator_tag>::ref();
  auto a = reloco::any::try_allocate(exhausted, big_value(1));
  ASSERT_FALSE(a);
  EXPECT_EQ(a.error(), reloco::error::allocation_failed);
}

TEST(AnyTest, TryCreateInPlaceConstructsWithoutAnExtraMove) {
  auto a = reloco::any::try_create(std::in_place_type<std::string>, "hello", 3u);
  ASSERT_TRUE(a);
  ASSERT_TRUE(a->template is<std::string>());
  EXPECT_EQ(a->template get<std::string>(), "hel");
}

TEST(AnyTest, DefaultConstructedIsEmpty) {
  reloco::any a;
  EXPECT_FALSE(a);
  EXPECT_FALSE(a.has_value());
  EXPECT_FALSE(a.is<int>());
}

TEST(AnyTest, NullptrConstructedIsEmpty) {
  reloco::any a(nullptr);
  EXPECT_FALSE(a);
}

TEST(AnyTest, IsReportsFalseOnTypeMismatch) {
  auto a = reloco::any::try_create(42);
  ASSERT_TRUE(a);
  EXPECT_TRUE(a->template is<int>());
  EXPECT_FALSE(a->template is<double>());
  EXPECT_FALSE(a->template is<long>());
}

TEST(AnyTest, MoveConstructionTransfersOwnershipAndEmptiesSource) {
  auto a = reloco::any::try_create(big_value(7));
  ASSERT_TRUE(a);
  reloco::any moved(std::move(*a));
  EXPECT_TRUE(moved);
  EXPECT_EQ(moved.get<big_value>().data[0], 7);
  EXPECT_FALSE(*a);
}

TEST(AnyTest, MoveAssignmentDestroysPreviousAndTransfersOwnership) {
  auto first = reloco::any::try_create(big_value(1));
  auto second = reloco::any::try_create(big_value(2));
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  *first = std::move(*second);
  EXPECT_EQ(first->get<big_value>().data[0], 2);
  EXPECT_FALSE(*second);
}

TEST(AnyTest, ResetDestroysHeapValueAndEmpties) {
  auto a = reloco::any::try_create(big_value(1));
  ASSERT_TRUE(a);
  a->reset();
  EXPECT_FALSE(*a);
}

TEST(AnyTest, SooValueDestructorRunsOnReset) {
  destructor_counter::live_count = 0;
  {
    auto a = reloco::any::try_create(destructor_counter(1));
    ASSERT_TRUE(a);
    EXPECT_EQ(destructor_counter::live_count, 1);
    a->reset();
    EXPECT_EQ(destructor_counter::live_count, 0);
  }
}

TEST(AnyTest, HeapValueDestructorRunsOnDestruction) {
  destructor_counter::live_count = 0;
  {
    struct wrapper {
      destructor_counter dc;
      std::array<int, 16> padding{};
      explicit wrapper(int v) noexcept : dc(v) {}
    };
    auto a = reloco::any::try_create(wrapper(3));
    ASSERT_TRUE(a);
    EXPECT_EQ(destructor_counter::live_count, 1);
  }
  EXPECT_EQ(destructor_counter::live_count, 0);
}

TEST(AnyTest, TryGetReportsContainerEmptyOnEmptyInstance) {
  reloco::any a;
  auto res = a.try_get<int>();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), reloco::error::container_empty);
}

TEST(AnyTest, TryGetReportsInvalidArgumentOnTypeMismatch) {
  auto a = reloco::any::try_create(42);
  ASSERT_TRUE(a);
  auto res = a->template try_get<double>();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), reloco::error::invalid_argument);
}

TEST(AnyTest, TryGetReturnsAWritableReference) {
  auto a = reloco::any::try_create(42);
  ASSERT_TRUE(a);
  auto res = a->template try_get<int>();
  ASSERT_TRUE(res);
  res->get() = 7;
  EXPECT_EQ(a->template get<int>(), 7);
}

TEST(AnyTest, ConstTryGetReturnsAReadOnlyReference) {
  auto created = reloco::any::try_create(42);
  ASSERT_TRUE(created);
  const reloco::any a = std::move(*created);
  auto res = a.try_get<int>();
  ASSERT_TRUE(res);
  EXPECT_EQ(res->get(), 42);
}

TEST(AnyTest, UnsafeGetSkipsTheCheck) {
  auto a = reloco::any::try_create(42);
  ASSERT_TRUE(a);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(a->template unsafe_get<int>(), 42);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

TEST(AnyTest, RvalueGetMovesTheValueOut) {
  auto a = reloco::any::try_create(std::string("movable"));
  ASSERT_TRUE(a);
  std::string moved = std::move(*a).get<std::string>();
  EXPECT_EQ(moved, "movable");
}

TEST(AnyTest, TryCloneCopiesASooValue) {
  auto a = reloco::any::try_create(42);
  ASSERT_TRUE(a);
  auto cloned = a->try_clone();
  ASSERT_TRUE(cloned);
  EXPECT_EQ(cloned->get<int>(), 42);
  EXPECT_EQ(a->get<int>(), 42);
}

TEST(AnyTest, TryCloneCopiesAHeapValue) {
  auto a = reloco::any::try_create(big_value(50));
  ASSERT_TRUE(a);
  auto cloned = a->try_clone();
  ASSERT_TRUE(cloned);
  EXPECT_EQ(cloned->get<big_value>().data[0], 50);
  EXPECT_EQ(a->get<big_value>().data[0], 50);
}

TEST(AnyTest, TryCloneOnEmptyInstanceSucceedsWithAnEmptyClone) {
  reloco::any a;
  auto cloned = a.try_clone();
  ASSERT_TRUE(cloned);
  EXPECT_FALSE(*cloned);
}

TEST(AnyTest, TryCloneFailsOnANonCopyableStoredType) {
  auto a = reloco::any::try_create(non_copyable_value(1));
  ASSERT_TRUE(a);
  auto cloned = a->try_clone();
  ASSERT_FALSE(cloned);
  EXPECT_EQ(cloned.error(), reloco::error::unsupported_operation);
}

TEST(AnyTest, TryCloneDispatchesToACustomTryCloneViaConstructionHelpers) {
  auto a = reloco::any::try_create(custom_cloneable_value(9));
  ASSERT_TRUE(a);
  auto cloned = a->try_clone();
  ASSERT_TRUE(cloned);
  EXPECT_EQ(cloned->get<custom_cloneable_value>().value, 9);
  EXPECT_EQ(a->get<custom_cloneable_value>().value, 9);
}

TEST(AnyTest, TypeIdReturnsTheHeldTypesIdentity) {
  auto a = reloco::any::try_create(42);
  ASSERT_TRUE(a);
  EXPECT_EQ(a->type_id(), reloco::type_id::of<int>());
  EXPECT_NE(a->type_id(), reloco::type_id::of<double>());
}

TEST(AnyTest, TypeIdOnAnEmptyInstanceIsTheNoTypeSentinel) {
  reloco::any a;
  EXPECT_FALSE(a.type_id());
  EXPECT_EQ(a.type_id(), reloco::type_id());
}

TEST(AnyTest, TypeIdNameSurfacesTheHeldTypesRegisteredDebugName) {
  auto a = reloco::any::try_create(42);
  ASSERT_TRUE(a);
  EXPECT_STREQ(a->type_id().name(), "int");
}

TEST(AnyTest, DowncastMutReturnsAWritablePointerOnMatch) {
  auto a = reloco::any::try_create(42);
  ASSERT_TRUE(a);
  int *ptr = a->template downcast_mut<int>();
  ASSERT_NE(ptr, nullptr);
  *ptr = 7;
  EXPECT_EQ(a->template get<int>(), 7);
}

TEST(AnyTest, DowncastMutReturnsNullptrOnTypeMismatchOrEmpty) {
  auto a = reloco::any::try_create(42);
  ASSERT_TRUE(a);
  EXPECT_EQ(a->template downcast_mut<double>(), nullptr);

  reloco::any empty;
  EXPECT_EQ(empty.downcast_mut<int>(), nullptr);
}

TEST(AnyTest, DowncastRefReturnsAReadOnlyPointerOnMatch) {
  auto created = reloco::any::try_create(42);
  ASSERT_TRUE(created);
  const reloco::any a = std::move(*created);
  const int *ptr = a.downcast_ref<int>();
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 42);
}

TEST(AnyTest, DowncastRefReturnsNullptrOnTypeMismatchOrEmpty) {
  auto created = reloco::any::try_create(42);
  ASSERT_TRUE(created);
  const reloco::any a = std::move(*created);
  EXPECT_EQ(a.downcast_ref<double>(), nullptr);

  const reloco::any empty;
  EXPECT_EQ(empty.downcast_ref<int>(), nullptr);
}

TEST(AnyTest, DowncastMovesTheValueOutOnMatch) {
  auto a = reloco::any::try_create(std::string("movable"));
  ASSERT_TRUE(a);
  auto res = std::move(*a).downcast<std::string>();
  ASSERT_TRUE(res);
  EXPECT_EQ(*res, "movable");
}

TEST(AnyTest, DowncastFailsWithContainerEmptyOnAnEmptyInstance) {
  reloco::any a;
  auto res = std::move(a).downcast<int>();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), reloco::error::container_empty);
}

TEST(AnyTest, DowncastFailsWithInvalidArgumentOnTypeMismatch) {
  auto a = reloco::any::try_create(42);
  ASSERT_TRUE(a);
  auto res = std::move(*a).downcast<double>();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), reloco::error::invalid_argument);
}

TEST(AnyTest, IsNotTriviallyRelocatable) { static_assert(!reloco::is_trivially_relocatable_v<reloco::any>); }
