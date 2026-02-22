#include <gtest/gtest.h>
#include <reloco/function.hpp>

TEST(FunctionTest, LargeCaptureTriggersAllocation) {
  // A large lambda that exceeds SOO_SIZE
  struct Large {
    char data[128];
  };
  Large l;

  auto func_res = reloco::function<int(int)>::try_create(
      [l](int x) { return x + (int)sizeof(l); });

  ASSERT_TRUE(func_res.has_value());
  EXPECT_EQ((*func_res)(10), 138);
}

TEST(FunctionTest, CloneRestriction) {
  // Move-only capture
  auto uptr = std::make_unique<int>(10);
  auto f_res = reloco::function<int()>::try_create(
      [p = std::move(uptr)]() { return *p; });

  auto f = std::move(*f_res);

  // Should fail because unique_ptr is not copyable
  auto clone_res = f.try_clone();
  EXPECT_FALSE(clone_res.has_value());
  EXPECT_EQ(clone_res.error(), reloco::error::unsupported_operation);
}
