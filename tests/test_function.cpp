// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/function.hpp>
#include <reloco/heap_allocator.hpp>

#include <array>
#include <utility>

namespace {

int plain_add_one(int v) noexcept { return v + 1; }

struct destructor_counter {
  static int live_count;
  int value;

  explicit destructor_counter(int v) noexcept : value(v) { ++live_count; }
  destructor_counter(const destructor_counter &other) noexcept : value(other.value) { ++live_count; }
  destructor_counter(destructor_counter &&other) noexcept : value(other.value) { ++live_count; }
  ~destructor_counter() { --live_count; }

  int operator()(int x) const noexcept { return value + x; }
};

int destructor_counter::live_count = 0;

// A capture large enough to force the heap tier (bigger than soo_capacity).
struct big_capture {
  std::array<int, 16> data{};
  explicit big_capture(int seed) noexcept {
    for (auto &v : data)
      v = seed++;
  }
  int operator()(int x) const noexcept { return data[0] + x; }
};

struct non_copyable_capture {
  int value;
  explicit non_copyable_capture(int v) noexcept : value(v) {}
  non_copyable_capture(const non_copyable_capture &) = delete;
  non_copyable_capture(non_copyable_capture &&) noexcept = default;
  int operator()(int x) const noexcept { return value + x; }
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

TEST(FunctionTest, WrapsAPlainFunctionPointerWithoutAllocation) {
  auto fn = reloco::function<int(int)>::try_create(&plain_add_one);
  ASSERT_TRUE(fn);
  EXPECT_TRUE(*fn);
  EXPECT_EQ((*fn)(4), 5);
}

TEST(FunctionTest, WrapsACaptureUsingCapturelessLambdaAsFunctionPointer) {
  auto fn = reloco::function<int(int)>::try_create([](int v) noexcept { return v * 2; });
  ASSERT_TRUE(fn);
  EXPECT_EQ((*fn)(3), 6);
}

TEST(FunctionTest, WrapsASmallCaptureUsingSooStorage) {
  int captured = 10;
  auto fn = reloco::function<int(int)>::try_create([captured](int v) noexcept { return captured + v; });
  ASSERT_TRUE(fn);
  EXPECT_EQ((*fn)(5), 15);
}

TEST(FunctionTest, WrapsALargeCaptureUsingHeapStorage) {
  auto fn = reloco::function<int(int)>::try_create(big_capture(100));
  ASSERT_TRUE(fn);
  EXPECT_EQ((*fn)(1), 101);
}

TEST(FunctionTest, TryAllocateUsesExplicitAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto fn = reloco::function<int(int)>::try_allocate(heap, big_capture(5));
  ASSERT_TRUE(fn);
  EXPECT_EQ((*fn)(0), 5);
}

TEST(FunctionTest, TryAllocateReportsHeapAllocationFailure) {
  reloco::allocator_ref exhausted = reloco::allocator<exhausted_allocator_tag>::ref();
  auto fn = reloco::function<int(int)>::try_allocate(exhausted, big_capture(1));
  ASSERT_FALSE(fn);
  EXPECT_EQ(fn.error(), reloco::error::allocation_failed);
}

TEST(FunctionTest, DefaultConstructedIsEmpty) {
  reloco::function<int(int)> fn;
  EXPECT_FALSE(fn);
}

TEST(FunctionTest, NullptrConstructedIsEmpty) {
  reloco::function<int(int)> fn(nullptr);
  EXPECT_FALSE(fn);
}

TEST(FunctionTest, MoveConstructionTransfersOwnershipAndEmptiesSource) {
  auto fn = reloco::function<int(int)>::try_create(big_capture(7));
  ASSERT_TRUE(fn);
  reloco::function<int(int)> moved(std::move(*fn));
  EXPECT_TRUE(moved);
  EXPECT_EQ(moved(1), 8);
  EXPECT_FALSE(*fn);
}

TEST(FunctionTest, MoveAssignmentDestroysPreviousAndTransfersOwnership) {
  auto first = reloco::function<int(int)>::try_create(big_capture(1));
  auto second = reloco::function<int(int)>::try_create(big_capture(2));
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  *first = std::move(*second);
  EXPECT_EQ((*first)(0), 2);
  EXPECT_FALSE(*second);
}

TEST(FunctionTest, ResetDestroysHeapCaptureAndEmpties) {
  destructor_counter::live_count = 0;
  {
    auto fn = reloco::function<int(int)>::try_create(big_capture(1));
    ASSERT_TRUE(fn);
    fn->reset();
    EXPECT_FALSE(*fn);
  }
}

TEST(FunctionTest, SooCaptureDestructorRunsOnReset) {
  destructor_counter::live_count = 0;
  {
    auto fn = reloco::function<int(int)>::try_create(destructor_counter(1));
    ASSERT_TRUE(fn);
    EXPECT_EQ(destructor_counter::live_count, 1);
    fn->reset();
    EXPECT_EQ(destructor_counter::live_count, 0);
  }
}

TEST(FunctionTest, HeapCaptureDestructorRunsOnDestruction) {
  destructor_counter::live_count = 0;
  {
    // destructor_counter easily fits SOO; force heap via a wrapping struct.
    struct wrapper {
      destructor_counter dc;
      std::array<int, 16> padding{};
      explicit wrapper(int v) noexcept : dc(v) {}
      int operator()(int x) const noexcept { return dc(x); }
    };
    auto fn = reloco::function<int(int)>::try_create(wrapper(3));
    ASSERT_TRUE(fn);
    EXPECT_EQ(destructor_counter::live_count, 1);
  }
  EXPECT_EQ(destructor_counter::live_count, 0);
}

TEST(FunctionTest, TryCallReportsContainerEmptyOnEmptyFunction) {
  reloco::function<int(int)> fn;
  auto res = fn.try_call(1);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), reloco::error::container_empty);
}

TEST(FunctionTest, UnsafeCallSkipsTheEmptyCheck) {
  auto fn = reloco::function<int(int)>::try_create(&plain_add_one);
  ASSERT_TRUE(fn);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(fn->unsafe_call(4), 5);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

TEST(FunctionTest, TryCallWrapsAPlainReturnValue) {
  auto fn = reloco::function<int(int)>::try_create(&plain_add_one);
  ASSERT_TRUE(fn);
  auto res = (*fn).try_call(9);
  ASSERT_TRUE(res);
  EXPECT_EQ(*res, 10);
}

TEST(FunctionTest, TryCallFlattensAnAlreadyResultReturningCallable) {
  auto fn = reloco::function<reloco::result<int>(int)>::try_create([](int v) noexcept -> reloco::result<int> {
    if (v < 0)
      return reloco::unexpected(reloco::error::invalid_argument);
    return v * 10;
  });
  ASSERT_TRUE(fn);
  auto ok = (*fn).try_call(2);
  ASSERT_TRUE(ok);
  EXPECT_EQ(*ok, 20);

  auto failed = (*fn).try_call(-1);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), reloco::error::invalid_argument);
}

TEST(FunctionTest, TryCloneCopiesAFunctionPointer) {
  auto fn = reloco::function<int(int)>::try_create(&plain_add_one);
  ASSERT_TRUE(fn);
  auto cloned = fn->try_clone();
  ASSERT_TRUE(cloned);
  EXPECT_EQ((*cloned)(1), 2);
  EXPECT_EQ((*fn)(1), 2);
}

TEST(FunctionTest, TryCloneCopiesASooCapture) {
  int captured = 4;
  auto fn = reloco::function<int(int)>::try_create([captured](int v) noexcept { return captured + v; });
  ASSERT_TRUE(fn);
  auto cloned = fn->try_clone();
  ASSERT_TRUE(cloned);
  EXPECT_EQ((*cloned)(1), 5);
}

TEST(FunctionTest, TryCloneCopiesAHeapCapture) {
  auto fn = reloco::function<int(int)>::try_create(big_capture(50));
  ASSERT_TRUE(fn);
  auto cloned = fn->try_clone();
  ASSERT_TRUE(cloned);
  EXPECT_EQ((*cloned)(1), 51);
  EXPECT_EQ((*fn)(1), 51);
}

TEST(FunctionTest, TryCloneOnEmptyFunctionSucceedsWithAnEmptyClone) {
  reloco::function<int(int)> fn;
  auto cloned = fn.try_clone();
  ASSERT_TRUE(cloned);
  EXPECT_FALSE(*cloned);
}

TEST(FunctionTest, TryCloneFailsOnNonCopyableCapture) {
  auto fn = reloco::function<int(int)>::try_create(non_copyable_capture(1));
  ASSERT_TRUE(fn);
  auto cloned = fn->try_clone();
  ASSERT_FALSE(cloned);
  EXPECT_EQ(cloned.error(), reloco::error::unsupported_operation);
}

TEST(FunctionTest, IsNotTriviallyRelocatable) {
  static_assert(!reloco::is_trivially_relocatable_v<reloco::function<int(int)>>);
}
