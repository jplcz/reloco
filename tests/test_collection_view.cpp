#include <gtest/gtest.h>
#include <reloco/collection_view.hpp>
#include <reloco/vector.hpp>

class CollectionViewTest : public ::testing::Test {
protected:
  // Helper to create a populated vector
  static reloco::vector<int> create_vec() {
    auto vec_res = reloco::vector<int>::try_create(10);
    auto &vec = *vec_res;
    std::ignore = vec.try_push_back(10);
    std::ignore = vec.try_push_back(20);
    return std::move(vec);
  }
};

TEST_F(CollectionViewTest, SharedOwnerIsCopyable) {
  auto shared_ptr = reloco::try_make_shared<reloco::vector<int>>(create_vec());

  reloco::mutable_collection_view<reloco::vector<int>,
                                  reloco::policy::shared_owner>
      s_view(shared_ptr.value());

  // Verify standard copy operator works (ref-count increment)
  auto s_view_copy = s_view;

  EXPECT_EQ(s_view.size(), 2);
  EXPECT_EQ(s_view_copy.size(), 2);

  // Modify via copy, reflect in original
  std::ignore = s_view_copy.try_push_back(30);
  EXPECT_EQ(s_view.size(), 3);
  EXPECT_EQ(s_view.at(2), 30);
}

TEST_F(CollectionViewTest, MoveOwnerIsMoveOnly) {
  auto vec = create_vec();
  reloco::mutable_collection_view<reloco::vector<int>,
                                  reloco::policy::move_owner>
      m_view(std::move(vec));

  // Verify move works
  auto m_view_moved = std::move(m_view);
  EXPECT_EQ(m_view_moved.size(), 2);

  // Verify copy is deleted at compile time
  static_assert(!std::is_copy_constructible_v<decltype(m_view)>,
                "Move owner must not be copyable!");
}

TEST_F(CollectionViewTest, MutableInterfaceSupportsModification) {
  auto vec = create_vec();
  reloco::mutable_collection_view<reloco::vector<int>,
                                  reloco::policy::move_owner>
      m_view(std::move(vec));

  // Test fallible API
  auto res = m_view.try_push_back(99);
  EXPECT_TRUE(res.has_value());
  EXPECT_EQ(m_view.at(2), 99);

  // Test unsafe/asserted API
  m_view.at(0) = 11;
  EXPECT_EQ(m_view.unsafe_at(0), 11);

  m_view.clear();
  EXPECT_TRUE(m_view.empty());
}
TEST_F(CollectionViewTest, ReadOnlyViewPreventsModification) {
  auto vec = create_vec();
  // A read-only view
  reloco::collection_view<reloco::vector<int>, reloco::policy::move_owner>
      ro_view(std::move(vec));

  EXPECT_EQ(ro_view.at(0), 10);

  // Verify compile-time concept compliance
  static_assert(reloco::is_fallible_collection_view<decltype(ro_view), int>,
                "Must satisfy Read-Only concept");
  static_assert(
      !reloco::is_mutable_fallible_collection_view<decltype(ro_view), int>,
      "Read-only view must NOT satisfy Mutable concept");

  // Note: Attempting ro_view.try_push_back(5) would fail to compile here.
}

TEST_F(CollectionViewTest, NonOwningPointerView) {
  auto vec = create_vec();
  {
    // View that does not own the data (policy::non_owner)
    reloco::mutable_collection_view<reloco::vector<int>,
                                    reloco::policy::non_owner>
        view(&vec);

    view.at(0) = 55;
    EXPECT_EQ(vec.at(0), 55);
  }
  // vec is still valid here because view didn't own it
  EXPECT_EQ(vec.at(0), 55);
}

class AnyViewTest : public ::testing::Test {
protected:
  reloco::core_allocator alloc;

  auto create_populated_vec(size_t size) {
    auto vec_res = reloco::vector<int>::try_create(size);
    RELOCO_ASSERT(vec_res.has_value());
    auto &vec = *vec_res;
    for (int i = 0; i < size; ++i) {
      std::ignore = vec.try_push_back(i * 10);
    }
    return std::move(vec);
  }
};

TEST_F(AnyViewTest, TypeErasureMaintainsDataIntegrity) {
  auto vec = create_populated_vec(3); // [0, 10, 20]
  reloco::collection_view<reloco::vector<int>, reloco::policy::move_owner>
      concrete_view(std::move(vec));

  // Create erased view
  auto erased_res =
      reloco::any_view<int>::try_create(std::move(concrete_view), alloc);
  ASSERT_TRUE(erased_res.has_value());
  auto erased = std::move(*erased_res);

  EXPECT_EQ(erased.size(), 3);
  EXPECT_FALSE(erased.empty());

  auto val = erased.try_at(1);
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(val->get(), 10);
}

TEST_F(AnyViewTest, MoveConstructionZerosSource) {
  auto vec = create_populated_vec(1);
  auto erased_res = reloco::any_view<int>::try_create(
      reloco::collection_view<reloco::vector<int>, reloco::policy::move_owner>(
          std::move(vec)),
      alloc);

  auto source = std::move(*erased_res);
  auto destination = std::move(source);

  // Destination should be valid
  EXPECT_EQ(destination.size(), 1);
  EXPECT_TRUE(source.empty());
}
TEST_F(AnyViewTest, MoveAssignmentCleansUpExistingContext) {
  auto vec1 = create_populated_vec(1);
  auto vec2 = create_populated_vec(5);

  auto v1 = std::move(*reloco::any_view<int>::try_create(
      reloco::collection_view<reloco::vector<int>, reloco::policy::move_owner>(
          std::move(vec1)),
      alloc));
  auto v2 = std::move(*reloco::any_view<int>::try_create(
      reloco::collection_view<reloco::vector<int>, reloco::policy::move_owner>(
          std::move(vec2)),
      alloc));

  v1 = std::move(v2); // Should trigger destroy on v1's original context

  EXPECT_EQ(v1.size(), 5);
}

TEST_F(AnyViewTest, NonOwningPointerErasesCorrectly) {
  auto vec = create_populated_vec(2);
  {
    // View that points to local stack vec
    reloco::collection_view<reloco::vector<int>, reloco::policy::non_owner>
        pointer_view(&vec);
    auto erased_res =
        reloco::any_view<int>::try_create(std::move(pointer_view), alloc);

    ASSERT_TRUE(erased_res.has_value());
    EXPECT_EQ((*erased_res).at(0), 0);
  }
  // vec is still alive here; any_view destructor shouldn't have affected it
  EXPECT_EQ(vec.at(1), 10);
}

TEST_F(AnyViewTest, DeepCloneSupportedForMoveOwner) {
  auto vec = create_populated_vec(5);
  auto erased = std::move(*reloco::any_view<int>::try_create(
      reloco::collection_view<reloco::vector<int>, reloco::policy::move_owner>(
          std::move(vec)),
      alloc));

  auto cloned_res = erased.try_clone();
  ASSERT_TRUE(cloned_res.has_value()) << "Error is " << (int)cloned_res.error();
  EXPECT_NE(&erased.try_at(0).value().get(),
            &(*cloned_res).try_at(0).value().get());
}
