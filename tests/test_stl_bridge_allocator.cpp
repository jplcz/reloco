#include <gtest/gtest.h>
#include <reloco/stl_bridge_allocator.hpp>
#include <vector>

TEST(StlBridgeTest, ThrowsOnOOM) {
  reloco::core_allocator pa;
  reloco::stl_bridge_allocator<int> bridge(pa);

  // Attempt an impossible allocation through the bridge
  EXPECT_THROW(
      { std::ignore = bridge.allocate(static_cast<std::size_t>(1) << 60); },
      std::bad_alloc);
}

TEST(StlBridgeTest, CompatibleWithStdVector) {
  reloco::core_allocator pa;
  std::vector<int, reloco::stl_bridge_allocator<int>> vec(
      (reloco::stl_bridge_allocator<int>(pa)));

  vec.push_back(42);
  EXPECT_EQ(vec.size(), 1);
  EXPECT_EQ(vec[0], 42);
}
