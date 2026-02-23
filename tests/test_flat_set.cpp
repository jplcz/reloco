#include <gtest/gtest.h>
#include <reloco/flat_set.hpp>

TEST(FlatSetTest, SortingAndUniqueness) {
  auto set = std::move(*reloco::flat_set<int>::try_create());

  std::ignore = set.try_insert(50);
  std::ignore = set.try_insert(10);
  std::ignore = set.try_insert(30);

  // Duplicate check
  auto dup_res = set.try_insert(30);
  EXPECT_FALSE(dup_res.has_value());
  EXPECT_EQ(dup_res.error(), reloco::error::already_exists);

  // Sorted verification via view
  auto view = set.as_view().value();
  ASSERT_EQ(view.size(), 3);
  EXPECT_EQ(view.at(0), 10);
  EXPECT_EQ(view.at(1), 30);
  EXPECT_EQ(view.at(2), 50);
}

TEST(FlatSetTest, FlatSetSatisfiesCreationPatterns) {
  using SetType = reloco::flat_set<int>;

  static_assert(reloco::has_try_allocate<SetType, size_t>);
  static_assert(reloco::has_try_create<SetType, size_t>);

  // Test Allocate
  auto s1 = SetType::try_allocate(reloco::get_default_allocator(), 10);
  ASSERT_TRUE(s1.has_value());

  // Test Create
  auto s2 = SetType::try_create(5);
  ASSERT_TRUE(s2.has_value());
}

namespace {
struct MoveOnlyType {
  int id;
  MoveOnlyType(int i) : id(i) {}
  MoveOnlyType(const MoveOnlyType &) = delete;
  MoveOnlyType(MoveOnlyType &&) noexcept = default;

  bool operator<(const MoveOnlyType &other) const { return id < other.id; }
};
} // namespace

TEST(FlatSetTest, HandlesMoveOnlyTypes) {
  auto set = std::move(*reloco::flat_set<MoveOnlyType>::try_create());

  MoveOnlyType m(50);
  auto res = set.try_insert(std::move(m));

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(set.size(), 1);
}
