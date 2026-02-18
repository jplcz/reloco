#include <boost/intrusive_ptr.hpp>
#include <gtest/gtest.h>
#include <map>
#include <reloco/intrusive_ptr.hpp>

class TrackingAllocator : public reloco::fallible_allocator {
public:
  std::map<void *, std::size_t> allocated_map;
  size_t deallocate_calls = 0;

  [[nodiscard]] reloco::result<reloco::mem_block>
  allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    void *ptr = std::aligned_alloc(alignment, bytes);
    if (!ptr)
      return std::unexpected(reloco::error::allocation_failed);
    allocated_map[ptr] = bytes;
    return reloco::mem_block{ptr, bytes};
  }

  void deallocate(void *ptr, std::size_t bytes) noexcept override {
    deallocate_calls++;
    // Verification: The size passed to deallocate must match what we allocated
    if (allocated_map.count(ptr)) {
      EXPECT_EQ(allocated_map[ptr], bytes) << "Deallocation size mismatch!";
      allocated_map.erase(ptr);
    }
    std::free(ptr);
  }

  reloco::result<std::size_t> expand_in_place(void *, std::size_t,
                                              std::size_t) noexcept override {
    return 0;
  }

  reloco::result<reloco::mem_block> reallocate(void *, std::size_t, std::size_t,
                                               std::size_t) noexcept override {
    return std::unexpected(reloco::error::allocation_failed);
  }
};

class StaticResource : public reloco::intrusive_base<StaticResource> {
public:
  int value;
  StaticResource(int v) : value(v) {}
};

class DynamicResource : public reloco::intrusive_base_dynamic<DynamicResource> {
public:
  int id;
  DynamicResource(int i) : id(i) {}
};

class UnifiedFactoryTest : public ::testing::Test {
protected:
  TrackingAllocator alloc;
};

TEST_F(UnifiedFactoryTest, FixedSizeAllocationAndCleanup) {
  {
    auto res = reloco::try_allocate_intrusive<StaticResource>(alloc, 42);

    ASSERT_TRUE(res);
    EXPECT_EQ((*res)->value, 42);
    EXPECT_EQ(alloc.allocated_map.size(), 1);

    void *ptr = res->get();
    EXPECT_EQ(alloc.allocated_map[ptr], sizeof(StaticResource));
  }
  EXPECT_EQ(alloc.deallocate_calls, 1);
  EXPECT_EQ(alloc.allocated_map.size(), 0);
}

TEST_F(UnifiedFactoryTest, DynamicSizeAllocationAndCleanup) {
  const std::size_t requested_size = 1024;
  {
    auto res = reloco::try_allocate_intrusive_dynamic<DynamicResource>(
        alloc, requested_size, 99);

    ASSERT_TRUE(res);
    EXPECT_EQ((*res)->id, 99);

    void *ptr = res->get();
    EXPECT_EQ(alloc.allocated_map[ptr], requested_size);
  }
  EXPECT_EQ(alloc.deallocate_calls, 1);
  EXPECT_EQ(alloc.allocated_map.size(), 0);
}

struct FailResource : public reloco::intrusive_base<FailResource> {
  static reloco::result<FailResource> try_create(bool should_fail) {
    if (should_fail)
      return std::unexpected(reloco::error::invalid_argument);
    return FailResource();
  }
};

TEST_F(UnifiedFactoryTest, HandlesConstructionFailure) {
  auto res = reloco::try_allocate_intrusive<FailResource>(alloc, true);

  EXPECT_FALSE(res);
  EXPECT_EQ(res.error(), reloco::error::invalid_argument);

  // Memory should have been allocated and then immediately deallocated
  EXPECT_EQ(alloc.deallocate_calls, 1);
  EXPECT_EQ(alloc.allocated_map.size(), 0);
}
