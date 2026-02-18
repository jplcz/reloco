#include <gtest/gtest.h>
#include <reloco/vector.hpp>

struct HeavyType {
  std::string data;
  HeavyType(std::string s) : data(std::move(s)) {}
  HeavyType(const HeavyType &) = delete;
  HeavyType(HeavyType &&other) noexcept : data(std::move(other.data)) {}
  HeavyType &operator=(HeavyType &&other) noexcept {
    data = std::move(other.data);
    return *this;
  }
};

struct MockClonable {
  int value;
  bool fail_on_clone = false;

  [[nodiscard]] reloco::result<MockClonable> try_clone() const noexcept {
    if (fail_on_clone) {
      return std::unexpected(reloco::error::allocation_failed);
    }
    return MockClonable{value, false};
  }
};

static_assert(!reloco::is_relocatable<HeavyType>::value,
              "HeavyType should not be relocatable");

static_assert(reloco::has_try_clone<MockClonable>,
              "Trait should detect try_clone method");

TEST(RelocoVectorTest, EmplaceAndIterate) {
  auto vec = reloco::vector<int>::try_create(2).value();

  ASSERT_TRUE(vec.try_emplace_back(10));
  ASSERT_TRUE(vec.try_emplace_back(20));

  int sum = 0;
  for (int x : vec)
    sum += x;
  EXPECT_EQ(sum, 30);
}

TEST(RelocoVectorTest, FalliblePop) {
  auto vec = reloco::vector<int>::try_create().value();
  auto res = vec.try_pop_back();
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), reloco::error::out_of_range);
}

TEST(RelocoVectorTest, EmplaceBackReturnsPointer) {
  auto vec = reloco::vector<std::string>::try_create(1).value();

  auto res = vec.try_emplace_back("hello hardware");
  ASSERT_TRUE(res.has_value());

  // Check that we can modify the element via the returned pointer
  auto ptr = *res;
  EXPECT_EQ(*ptr, "hello hardware");

  ptr->append(" honest");
  EXPECT_EQ(vec[0], "hello hardware honest");
}

TEST(RelocoVectorTest, InsertAtVariousPositions) {
  reloco::vector<int> vec;

  // Insert into empty
  auto res1 = vec.try_insert(0, 10);
  ASSERT_TRUE(res1);
  EXPECT_EQ(vec.size(), 1);
  EXPECT_EQ(vec[0], 10);

  // Insert at back (pos == size)
  auto res2 = vec.try_insert(1, 30);
  ASSERT_TRUE(res2);
  EXPECT_EQ(vec[1], 30);

  // Insert in middle
  auto res3 = vec.try_insert(1, 20);
  ASSERT_TRUE(res3);

  // Final state should be [10, 20, 30]
  ASSERT_EQ(vec.size(), 3);
  EXPECT_EQ(vec[0], 10);
  EXPECT_EQ(vec[1], 20);
  EXPECT_EQ(vec[2], 30);
}

TEST(RelocoVectorTest, ReturnsErrorOnOutOfBounds) {
  reloco::vector<int> vec;
  auto res = vec.try_insert(1, 99);
  EXPECT_FALSE(res);
  EXPECT_EQ(res.error(), reloco::error::out_of_range);
}

TEST(RelocoVectorComplexTest, RelocatableTypeShifting) {
  reloco::vector<size_t> v;
  for (size_t i = 0; i < 5; ++i)
    std::ignore = v.try_emplace_back(i);

  // [0, 1, 2, 3, 4] -> Insert 99 at index 2
  auto res = v.try_insert(2, 99);
  ASSERT_TRUE(res);
  EXPECT_EQ(v[2], 99);
  EXPECT_EQ(v[3], 2); // Verify shift
  EXPECT_EQ(v.size(), 6);
}

TEST(RelocoVectorComplexTest, NonRelocatableTypeShifting) {
  reloco::vector<HeavyType> v;
  std::ignore = v.try_emplace_back("first");
  std::ignore = v.try_emplace_back("third");

  // Insert "second" in the middle
  auto res = v.try_insert(1, "second");
  ASSERT_TRUE(res);

  ASSERT_EQ(v.size(), 3);
  EXPECT_EQ(v[0].data, "first");
  EXPECT_EQ(v[1].data, "second");
  EXPECT_EQ(v[2].data, "third");
}

TEST(RelocoVectorTest, TriggersReallocation) {
  reloco::vector<int> vec;

  // Force a small capacity
  std::ignore = vec.try_reserve(2);
  std::ignore = vec.try_emplace_back(1);
  std::ignore = vec.try_emplace_back(2);

  ASSERT_EQ(vec.capacity(), 2);

  // This should trigger try_reserve(4)
  auto res = vec.try_insert(1, 100);

  ASSERT_TRUE(res);
  EXPECT_GT(vec.capacity(), 2);
  EXPECT_EQ(vec[0], 1);
  EXPECT_EQ(vec[1], 100);
  EXPECT_EQ(vec[2], 2);
}

TEST(RelocoVectorTest, SuccessStandardTypeForCline) {
  reloco::vector<std::string> original;
  ASSERT_TRUE(original.try_push_back("hello"));
  ASSERT_TRUE(original.try_push_back("world"));

  auto clone_res = original.try_clone();
  ASSERT_TRUE(clone_res) << "Clone failed for standard types";

  auto &clone = *clone_res;
  EXPECT_EQ(clone.size(), 2);
  EXPECT_EQ(clone[0], "hello");
  EXPECT_EQ(clone[1], "world");

  // Verify it's a deep copy of the container (different addresses)
  EXPECT_NE(&original[0], &clone[0]);
}

TEST(RelocoVectorTest, SuccessRecursiveClone) {
  reloco::vector<MockClonable> original;
  ASSERT_TRUE(original.try_push_back({42, false}));
  ASSERT_TRUE(original.try_push_back({100, false}));

  auto clone_res = original.try_clone();
  ASSERT_TRUE(clone_res);
  EXPECT_EQ((*clone_res)[0].value, 42);
  EXPECT_EQ((*clone_res)[1].value, 100);
}

TEST(RelocoVectorTest, FailsWhenElementCloneFails) {
  reloco::vector<MockClonable> original;
  ASSERT_TRUE(original.try_push_back({1, false}));
  ASSERT_TRUE(
      original.try_push_back({2, true})); // This one will trigger an error

  auto clone_res = original.try_clone();

  // The entire vector clone should fail because the second element failed
  EXPECT_FALSE(clone_res);
  EXPECT_EQ(clone_res.error(), reloco::error::allocation_failed);
}

TEST(RelocoVectorTest, CloneEmptyVector) {
  reloco::vector<int> original;
  auto clone_res = original.try_clone();

  ASSERT_TRUE(clone_res);
  EXPECT_EQ(clone_res->size(), 0);
  EXPECT_EQ(clone_res->capacity(), 0);
}

TEST(RelocoVectorEmplace, HandlesTryCreate) {
  struct FallibleWidget {
    int id;
    static reloco::result<FallibleWidget> try_create(int id) noexcept {
      if (id < 0)
        return std::unexpected(reloco::error::invalid_argument);
      return FallibleWidget{id};
    }
  };

  reloco::vector<FallibleWidget> v;

  // Test Success
  auto res_ok = v.try_emplace_back(42);
  ASSERT_TRUE(res_ok);
  EXPECT_EQ(v.size(), 1);
  EXPECT_EQ(v[0].id, 42);

  // Test Failure propagation
  auto res_fail = v.try_emplace_back(-1);
  EXPECT_FALSE(res_fail);
  EXPECT_EQ(res_fail.error(), reloco::error::invalid_argument);
  EXPECT_EQ(v.size(), 1); // Size should not have incremented
}
