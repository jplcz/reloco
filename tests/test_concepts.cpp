// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/concepts.hpp>
#include <reloco/heap_allocator.hpp>

#include <new>

namespace {

struct plain_widget {};

struct creatable_widget {
  int value;
  static reloco::result<creatable_widget> try_create(int v) noexcept {
    if (v < 0)
      return reloco::unexpected(reloco::error::invalid_argument);
    return creatable_widget{v};
  }
};

struct allocating_widget {
  int value;
  static reloco::result<allocating_widget> try_allocate(reloco::allocator_ref, int v) noexcept {
    if (v < 0)
      return reloco::unexpected(reloco::error::invalid_argument);
    return allocating_widget{v};
  }
};

struct constructible_widget {
  int value = 0;
  reloco::result<void> try_construct(int v) noexcept {
    if (v < 0)
      return reloco::unexpected(reloco::error::invalid_argument);
    value = v;
    return {};
  }
};

struct allocator_aware_clonable_widget {
  int value;
  reloco::result<allocator_aware_clonable_widget> try_clone(reloco::allocator_ref) const noexcept {
    return allocator_aware_clonable_widget{value};
  }
};

struct self_contained_clonable_widget {
  int value;
  reloco::result<self_contained_clonable_widget> try_clone() const noexcept {
    return self_contained_clonable_widget{value};
  }
};

struct clonable_at_widget {
  int value;
  static reloco::result<void> try_clone_at(reloco::allocator_ref, clonable_at_widget *storage,
                                           const clonable_at_widget &source) noexcept {
    new (storage) clonable_at_widget{source.value};
    return {};
  }
};

} // namespace

TEST(ConceptsTest, HasTryCreateDetectsMatchingStaticFactory) {
  EXPECT_TRUE((reloco::has_try_create_v<creatable_widget, int>));
  EXPECT_FALSE((reloco::has_try_create_v<plain_widget, int>));
  EXPECT_FALSE((reloco::has_try_create_v<allocating_widget, int>));
}

TEST(ConceptsTest, HasTryAllocateDetectsMatchingStaticFactory) {
  EXPECT_TRUE((reloco::has_try_allocate_v<allocating_widget, int>));
  EXPECT_FALSE((reloco::has_try_allocate_v<plain_widget, int>));
  EXPECT_FALSE((reloco::has_try_allocate_v<creatable_widget, int>));
}

TEST(ConceptsTest, HasTryConstructDetectsInPlaceFallibleInit) {
  EXPECT_TRUE((reloco::has_try_construct_v<constructible_widget, int>));
  EXPECT_FALSE((reloco::has_try_construct_v<plain_widget, int>));
}

TEST(ConceptsTest, HasTryCloneDetectsBothAllocatorAwareAndSelfContainedShapes) {
  EXPECT_TRUE(reloco::has_try_clone_v<allocator_aware_clonable_widget>);
  EXPECT_TRUE(reloco::has_try_clone_v<self_contained_clonable_widget>);
  EXPECT_FALSE(reloco::has_try_clone_v<plain_widget>);
}

TEST(ConceptsTest, HasTryCloneAtDetectsInPlaceClone) {
  EXPECT_TRUE(reloco::has_try_clone_at_v<clonable_at_widget>);
  EXPECT_FALSE(reloco::has_try_clone_at_v<plain_widget>);
  EXPECT_FALSE(reloco::has_try_clone_at_v<allocator_aware_clonable_widget>);
}

TEST(ConceptsTest, DetectionActuallyExercisesTheMatchedOperations) {
  auto created = creatable_widget::try_create(7);
  ASSERT_TRUE(created);
  EXPECT_EQ(created->value, 7);

  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto allocated = allocating_widget::try_allocate(heap, 9);
  ASSERT_TRUE(allocated);
  EXPECT_EQ(allocated->value, 9);

  constructible_widget shell;
  auto constructed = shell.try_construct(5);
  ASSERT_TRUE(constructed);
  EXPECT_EQ(shell.value, 5);

  allocator_aware_clonable_widget aware{3};
  auto cloned_aware = aware.try_clone(heap);
  ASSERT_TRUE(cloned_aware);
  EXPECT_EQ(cloned_aware->value, 3);

  self_contained_clonable_widget self{4};
  auto cloned_self = self.try_clone();
  ASSERT_TRUE(cloned_self);
  EXPECT_EQ(cloned_self->value, 4);

  alignas(clonable_at_widget) unsigned char storage[sizeof(clonable_at_widget)];
  clonable_at_widget source{6};
  auto clone_at_result =
      clonable_at_widget::try_clone_at(heap, reinterpret_cast<clonable_at_widget *>(storage), source);
  ASSERT_TRUE(clone_at_result);
  EXPECT_EQ(reinterpret_cast<clonable_at_widget *>(storage)->value, 6);
}
