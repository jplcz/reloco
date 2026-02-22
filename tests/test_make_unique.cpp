#include <gtest/gtest.h>
#include <reloco/unique_ptr.hpp>

class MakeUniqueTest : public ::testing::Test {
protected:
  reloco::posix_allocator alloc;
};

TEST_F(MakeUniqueTest, StandardTypeCreation) {
  auto res = reloco::make_unique<int>(42);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(**res, 42);
}

struct FallibleType {
  int value;
  static bool try_create_called;

  // The Fallible Protocol
  static reloco::result<FallibleType> try_create(int val) noexcept {
    try_create_called = true;
    if (val < 0)
      return reloco::unexpected(reloco::error::allocation_failed);
    return FallibleType{val};
  }
};

bool FallibleType::try_create_called = false;

TEST_F(MakeUniqueTest, DispatchesToTryCreate) {
  FallibleType::try_create_called = false;

  auto res = reloco::make_unique<FallibleType>(100);

  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(FallibleType::try_create_called);
  EXPECT_EQ((*res)->value, 100);
}

TEST_F(MakeUniqueTest, HandlesTryCreateFailure) {
  auto res = reloco::make_unique<FallibleType>(-1); // Should trigger failure

  EXPECT_FALSE(res.has_value());
}

// Test Alignment and Deleter Integration
TEST_F(MakeUniqueTest, RespectsExtendedAlignment) {
  struct alignas(64) AlignedType {
    float data[16];
  };

  auto res = reloco::make_unique<AlignedType>();

  ASSERT_TRUE(res.has_value());
  auto ptr_val = reinterpret_cast<std::uintptr_t>(res->get());
  EXPECT_EQ(ptr_val % 64, 0);
}

// Test Resource Leakage/Cleanup
TEST_F(MakeUniqueTest, DestructorAndDeallocatorCalled) {
  static bool destructor_called = false;
  struct Tracker {
    ~Tracker() { destructor_called = true; }
  };

  {
    auto res = reloco::make_unique<Tracker>();
    ASSERT_TRUE(res.has_value());
    destructor_called = false;
  } // unique_ptr goes out of scope here

  EXPECT_TRUE(destructor_called);
}

TEST_F(MakeUniqueTest, UsesCoreAllocatorByDefault) {
  auto res = reloco::make_unique<double>(3.14);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(**res, 3.14);

  // Check if the type of the deleter matches our core_allocator
  static_assert(
      std::is_same_v<typename reloco::unique_ptr<double>::deleter_type,
                     reloco::allocator_deleter<double>>);
}

TEST_F(MakeUniqueTest, UniquePtrIsRelocatable) {
  // Verify our alias
  EXPECT_TRUE(reloco::is_relocatable<reloco::unique_ptr<int>>::value);

  // Verify standard unique_ptr
  EXPECT_TRUE(reloco::is_relocatable<std::unique_ptr<int>>::value);

  // Counter-test: std::string might not be (depending on implementation)
  // but a raw int always is.
  EXPECT_TRUE(reloco::is_relocatable<int>::value);
}
