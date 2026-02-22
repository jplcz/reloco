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

namespace {

int global_adder(int a, int b) noexcept { return a + b; }
} // namespace

TEST(CFunctionWrapperTest, SkinnyVersionIsSizeOptimized) {
  using SkinnyFunc = reloco::function<int (*)(int, int)>;

  static_assert(sizeof(SkinnyFunc) == sizeof(void *),
                "Skinny specialization should only store the raw pointer");

  EXPECT_EQ(sizeof(SkinnyFunc), sizeof(uintptr_t));
}

TEST(CFunctionWrapperTest, WrapsAndCallsCFunction) {
  auto func_res = reloco::function<int (*)(int, int)>::try_create(global_adder);
  ASSERT_TRUE(func_res.has_value());

  auto &func = *func_res;
  EXPECT_EQ(func(5, 7), 12);
}

TEST(CFunctionWrapperTest, CloneIsTrivialAndSafe) {
  auto func_res = reloco::function<int (*)(int, int)>::try_create(global_adder);
  auto &original = *func_res;

  // Cloning a skinny function should be a simple bitwise copy of the pointer
  auto clone_res = original.try_clone();
  ASSERT_TRUE(clone_res.has_value());

  EXPECT_EQ((*clone_res)(10, 20), 30);
}

TEST(CFunctionWrapperTest, MoveZeroesSource) {
  auto f1 =
      std::move(*reloco::function<int (*)(int, int)>::try_create(global_adder));

  auto f2 = std::move(f1);

  EXPECT_EQ(f2(1, 1), 2);
  EXPECT_DEATH({ f1(1, 1); }, "");
}
