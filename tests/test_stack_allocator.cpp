
#include <array>
#include <gtest/gtest.h>
#include <reloco/stack_allocator.hpp>

class StackAllocatorTest : public ::testing::Test {
protected:
  static constexpr std::size_t kBufferSize = 1024;
  alignas(64) std::byte buffer[kBufferSize]{};
  reloco::stack_allocator alloc{buffer, kBufferSize};

  void SetUp() override { alloc.reset(); }
};

TEST_F(StackAllocatorTest, BasicAllocation) {
  const auto res = alloc.allocate(128, 8);
  ASSERT_TRUE(res.has_value());
  EXPECT_NE(res->ptr, nullptr);
  EXPECT_EQ(res->size, 128);

  // Pointer should be inside our buffer
  EXPECT_GE(static_cast<std::byte *>(res->ptr), buffer);
  EXPECT_LT(static_cast<std::byte *>(res->ptr), buffer + kBufferSize);
}

TEST_F(StackAllocatorTest, RespectsAlignment) {
  ASSERT_TRUE(alloc.allocate(1, 1));

  // Request highly aligned memory
  auto res = alloc.allocate(64, 64);
  ASSERT_TRUE(res.has_value());

  const uintptr_t addr = reinterpret_cast<uintptr_t>(res->ptr);
  EXPECT_EQ(addr % 64, 0);
}

TEST_F(StackAllocatorTest, ReturnsErrorOnOOM) {
  // Fill the allocator
  auto res1 = alloc.allocate(kBufferSize, 1);
  ASSERT_TRUE(res1.has_value());

  // Next one should fail gracefully
  auto res2 = alloc.allocate(1, 1);
  EXPECT_FALSE(res2.has_value());
}

TEST_F(StackAllocatorTest, ExpandInPlaceSuccess) {
  auto res = alloc.allocate(100, 8);
  void *original_ptr = res->ptr;

  auto expand_res = alloc.expand_in_place(original_ptr, 100, 200);
  ASSERT_TRUE(expand_res.has_value());
  EXPECT_EQ(*expand_res, 200);

  // Verify that the next allocation starts after the expanded size
  auto next = alloc.allocate(1, 1);
  EXPECT_EQ(static_cast<std::byte *>(next->ptr),
            static_cast<std::byte *>(original_ptr) + 200);
}
