#include <gtest/gtest.h>
#include <reloco/map.hpp>

TEST(RelocoMapTest, RangeBasedLoop) {
  reloco::map<int, int> m;
  ASSERT_TRUE(m.try_insert(1, 10));
  ASSERT_TRUE(m.try_insert(2, 20));

  int sum_keys = 0;
  for (auto &node : m) {
    sum_keys += node.key;
  }
  EXPECT_EQ(sum_keys, 3);
}

TEST(RelocoMapTest, LowerBound) {
  reloco::map<int, int> m;
  ASSERT_TRUE(m.try_insert(10, 1));
  ASSERT_TRUE(m.try_insert(20, 2));

  auto it = m.lower_bound(15);
  ASSERT_NE(it, m.end());
  EXPECT_EQ(it->key, 20);
}

TEST(RelocoMapTest, TryCloneSuccess) {
  reloco::map<int, int> original;
  ASSERT_TRUE(original.try_insert(1, 10));
  ASSERT_TRUE(original.try_insert(2, 20));

  auto clone_res = original.try_clone();
  ASSERT_TRUE(clone_res);
  EXPECT_EQ(clone_res->size(), 2);
  EXPECT_EQ((*clone_res->find(1)).value, 10);
}
