#include <cstdint>
#include <gtest/gtest.h>
#include <reloco/allocator.hpp>

class PosixAllocatorTest : public ::testing::Test {
protected:
  reloco::core_allocator alloc;
};

// Verify that allocation actually respects the alignment boundary
TEST_F(PosixAllocatorTest, RespectsAlignment) {
  const std::size_t alignment = 64;
  auto result = alloc.allocate(1024, alignment);

  ASSERT_TRUE(result.has_value());
  EXPECT_NE(result->ptr, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(result->ptr) % alignment, 0);

  alloc.deallocate(result->ptr, 1024);
}

// Verify that reallocate maintains alignment even if the OS moves the block
TEST_F(PosixAllocatorTest, ReallocateMaintainsAlignment) {
  const std::size_t alignment = 4096; // Page alignment
  auto initial = alloc.allocate(128, alignment);
  ASSERT_TRUE(initial.has_value());

  // Force a relocation by asking for a much larger size
  auto resized = alloc.reallocate(initial->ptr, 128, 1024 * 1024, alignment);

  ASSERT_TRUE(resized.has_value());
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(resized->ptr) % alignment, 0);

  alloc.deallocate(resized->ptr, 1024 * 1024);
}

// Verify the fallible interface when asking for impossible memory
TEST_F(PosixAllocatorTest, HandlesAllocationFailure) {
  // Attempt to allocate an absurd amount of memory (e.g., 2^60 bytes)
  auto result = alloc.allocate(static_cast<std::size_t>(1) << 60, 16);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), reloco::error::allocation_failed);
}

// Check the expand_in_place behavior
TEST_F(PosixAllocatorTest, ExpandInPlaceAPI) {
  auto block = alloc.allocate(4096, 16);
  ASSERT_TRUE(block.has_value());

  auto res = alloc.expand_in_place(block->ptr, 4096, 8192);

  // We don't ASSERT_TRUE here because the OS might genuinely not be able to
  // grow in place Instead, we verify the return type is handled correctly
  if (res) {
    EXPECT_EQ(*res, 8192);
  } else {
    EXPECT_EQ(res.error(), reloco::error::in_place_growth_failed);
  }

  alloc.deallocate(block->ptr, 4096);
}

// Test the generic advise hints (verify they don't crash)
TEST_F(PosixAllocatorTest, AdviseDoesNotCrash) {
  auto block = alloc.allocate(4096, 16);
  ASSERT_TRUE(block.has_value());

  // These are hints; we just ensure the internal syscalls are valid
  alloc.advise(block->ptr, 4096, reloco::usage_hint::sequential);
  alloc.advise(block->ptr, 4096, reloco::usage_hint::will_need);
  alloc.advise(block->ptr, 4096, reloco::usage_hint::dont_need);

  alloc.deallocate(block->ptr, 4096);
}
