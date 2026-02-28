#include <gtest/gtest.h>
#include <reloco/unique_ptr.hpp>

class MakeUniqueTest : public ::testing::Test {
protected:
  reloco::core_allocator alloc;
};

TEST_F(MakeUniqueTest, StandardTypeCreation) {
  auto res = reloco::unique_ptr<int>::try_create(42);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(**res, 42);
}

struct FallibleType {
  int value;
  static inline thread_local bool try_create_called = false;

  // The Fallible Protocol
  static reloco::result<FallibleType> try_create(int val) noexcept {
    try_create_called = true;
    if (val < 0)
      return reloco::unexpected(reloco::error::allocation_failed);
    return FallibleType{val};
  }
};

TEST_F(MakeUniqueTest, DispatchesToTryCreate) {
  FallibleType::try_create_called = false;

  auto res = reloco::unique_ptr<FallibleType>::try_create(100);

  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(FallibleType::try_create_called);
  EXPECT_EQ((*res)->value, 100);
}

TEST_F(MakeUniqueTest, HandlesTryCreateFailure) {
  FallibleType::try_create_called = false;

  auto res = reloco::unique_ptr<FallibleType>::try_create(
      -1); // Should trigger failure

  EXPECT_FALSE(res.has_value());
}

// Test Alignment and Deleter Integration
TEST_F(MakeUniqueTest, RespectsExtendedAlignment) {
  struct alignas(64) AlignedType {
    float data[16];
  };

  auto res = reloco::unique_ptr<AlignedType>::try_create();

  ASSERT_TRUE(res.has_value());
  auto ptr_val = reinterpret_cast<std::uintptr_t>(res->unsafe_get());
  EXPECT_EQ(ptr_val % 64, 0);
}

// Test Resource Leakage/Cleanup
TEST_F(MakeUniqueTest, DestructorAndDeallocatorCalled) {
  static bool destructor_called = false;
  struct Tracker {
    ~Tracker() { destructor_called = true; }
  };

  {
    auto res = reloco::unique_ptr<Tracker>::try_create();
    ASSERT_TRUE(res.has_value());
    destructor_called = false;
  } // unique_ptr goes out of scope here

  EXPECT_TRUE(destructor_called);
}

TEST_F(MakeUniqueTest, UniquePtrIsRelocatable) {
  EXPECT_TRUE(reloco::is_relocatable<reloco::unique_ptr<int>>::value);
  EXPECT_TRUE(reloco::is_relocatable<int>::value);
}
