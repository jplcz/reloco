#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/allocator_helper.hpp>

namespace {
struct LifecycleMock {
  static thread_local inline int active_instances = 0;
  int id;
  LifecycleMock(int val) noexcept : id(val) { active_instances++; }
  ~LifecycleMock() { active_instances--; }
  LifecycleMock(const LifecycleMock &) = delete;
  LifecycleMock &operator=(const LifecycleMock &) = delete;
};

struct ExplodingMock {
  static thread_local inline int active_instances = 0;
  static thread_local inline int fail_at_id = -1;
  static thread_local inline int current_id_counter = 0;

  ExplodingMock() noexcept {
    printf("ExplodingMock(so far %d, fail_at_id=%d)\n", active_instances,
           fail_at_id);

    if (current_id_counter == fail_at_id) {
      printf("id counter fail\n");
      // Simulate a failure
      return;
    }
    active_instances++;
    current_id_counter++;
    printf("ExplodingMock(%d)\n", active_instances);
  }

  // Two-phase init version for try_construct
  reloco::result<void> try_construct() noexcept {
    printf("try_construct(current_id=%d, fail_at=%d)\n", current_id_counter,
           fail_at_id);

    if (current_id_counter == fail_at_id) {
      printf("!!! Simulating Failure at index %d !!!\n", current_id_counter);
      return reloco::unexpected(reloco::error::invalid_argument);
    }
    return {};
  }

  ~ExplodingMock() {
    printf("~ExplodingMock(%d)\n", active_instances);
    active_instances--;
  }

  ExplodingMock(const ExplodingMock &) = delete;
  ExplodingMock &operator=(const ExplodingMock &) = delete;
};

} // namespace

class FallibleArrayPtrTest : public ::testing::Test {
protected:
  reloco::allocator_helper helper{reloco::get_default_allocator()};

  void SetUp() override { LifecycleMock::active_instances = 0; }
};

TEST_F(FallibleArrayPtrTest, CleanupOnScopeExit) {
  {
    auto res = helper.allocate_array<LifecycleMock>(5, 100);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(LifecycleMock::active_instances, 5);
  }
  EXPECT_EQ(LifecycleMock::active_instances, 0);
}

TEST_F(FallibleArrayPtrTest, UnsafeReleaseTransfersOwnership) {
  LifecycleMock *raw_ptr = nullptr;

  {
    auto res = helper.allocate_array<LifecycleMock>(3, 42);
    raw_ptr = res->unsafe_release();
    EXPECT_EQ(res->size(), 0);
    EXPECT_EQ(res->unsafe_get(), nullptr);
  }

  // Scope ended, but we released: instances must still exist
  EXPECT_EQ(LifecycleMock::active_instances, 3);

  // Manual cleanup since we took ownership
  for (size_t i = 0; i < 3; ++i)
    raw_ptr[i].~LifecycleMock();

  helper.get_allocator().deallocate(raw_ptr, 3 * sizeof(LifecycleMock));
}

TEST_F(FallibleArrayPtrTest, AccessorLogic) {
  auto res = helper.allocate_array<int>(5, 10); // Array of 10, 10, 10, 10, 10
  auto &array = *res;

  EXPECT_EQ(array[0], 10);

  auto at_res = array.at(2);
  ASSERT_TRUE(at_res.has_value());
  EXPECT_EQ(at_res->get(), 10);

  auto at_fail = array.at(5);
  ASSERT_FALSE(at_fail.has_value());
  EXPECT_EQ(at_fail.error(), reloco::error::out_of_bounds);

  EXPECT_EQ(array.unsafe_at(4), 10);
}

TEST_F(FallibleArrayPtrTest, MoveSemantics) {
  auto res = helper.allocate_array<LifecycleMock>(2, 1);
  reloco::fallible_array_ptr<LifecycleMock> ptr1 = std::move(*res);

  EXPECT_EQ(ptr1.size(), 2);

  reloco::fallible_array_ptr<LifecycleMock> ptr2 = std::move(ptr1);
  EXPECT_EQ(ptr2.size(), 2);
  EXPECT_EQ(ptr1.size(), 0);
  EXPECT_EQ(LifecycleMock::active_instances, 2);
}

TEST_F(FallibleArrayPtrTest, IntegrityCheck) {
  // This should fail to compile if uncommented:
  //   reloco::fallible_array_ptr<int> spoof(nullptr, 100,
  //   &helper.get_allocator());

  // Default constructor is allowed (Empty state)
  reloco::fallible_array_ptr<int> empty;
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty.unsafe_get(), nullptr);
}

TEST_F(FallibleArrayPtrTest, PartialConstructionRollback) {
  ExplodingMock::active_instances = 0;
  ExplodingMock::current_id_counter = 0;
  ExplodingMock::fail_at_id = 3; // The 4th element will fail

  auto res = helper.allocate_array<ExplodingMock>(5);

  // The result must be an error
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), reloco::error::invalid_argument);

  // active_instances must be 0.
  // Elements 0, 1, 2 were built then rolled back.
  EXPECT_EQ(ExplodingMock::active_instances, 0);
}

TEST_F(FallibleArrayPtrTest, PreciseRollbackCount) {
  ExplodingMock::active_instances = 0;
  ExplodingMock::current_id_counter = 0;
  ExplodingMock::fail_at_id = 2; // Fail at index 2 (0, 1 succeed)

  auto res = helper.allocate_array<ExplodingMock>(10);

  ASSERT_FALSE(res.has_value());
  // Only index 0 and 1 were fully "Constructed"
  // Therefore, exactly 2 destructors should have been called during rollback.
  EXPECT_EQ(ExplodingMock::active_instances, 0);
}

TEST_F(FallibleArrayPtrTest, ZeroSizeAllocation) {
  auto res = helper.allocate_array<int>(0);

  ASSERT_TRUE(!res.has_value());
}

TEST_F(FallibleArrayPtrTest, MassiveAllocationOverflow) {
  size_t massive_count = std::numeric_limits<size_t>::max() / sizeof(int) + 1;

  auto res = helper.allocate_array<int>(massive_count);

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), reloco::error::integer_overflow);
}

TEST_F(FallibleArrayPtrTest, MoveAssignmentCleansUpOldData) {
  {
    auto res1 = helper.allocate_array<LifecycleMock>(2, 10);
    auto res2 = helper.allocate_array<LifecycleMock>(3, 20);

    reloco::fallible_array_ptr<LifecycleMock> ptr1 = std::move(*res1);
    reloco::fallible_array_ptr<LifecycleMock> ptr2 = std::move(*res2);

    EXPECT_EQ(LifecycleMock::active_instances, 5);

    // Overwrite ptr1 with ptr2
    ptr1 = std::move(ptr2);

    // The original 2 elements in ptr1 should be destroyed now
    EXPECT_EQ(LifecycleMock::active_instances, 3);
    EXPECT_EQ(ptr1.size(), 3);
    EXPECT_EQ(ptr2.size(), 0);
  }
  EXPECT_EQ(LifecycleMock::active_instances, 0);
}

TEST_F(FallibleArrayPtrTest, ReferenceWrapperMutation) {
  auto res = helper.allocate_array<int>(1, 500);
  auto &array = *res;

  auto ref_res = array.at(0);
  ASSERT_TRUE(ref_res.has_value());

  // Modify via reference_wrapper
  ref_res->get() = 999;

  EXPECT_EQ(array[0], 999);
  EXPECT_EQ(array.unsafe_at(0), 999);
}

TEST_F(FallibleArrayPtrTest, LvalueStability) {
  auto res = helper.allocate_array<int>(5, 10);
  auto &array = *res;

  // This works because 'array' is an lvalue
  int &val = array[0];
  val = 20;
  EXPECT_EQ(array[0], 20);

  // The following would fail to compile:
  // int& dangling = std::move(array)[0];
}
