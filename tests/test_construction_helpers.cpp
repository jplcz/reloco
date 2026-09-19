// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/construction_helpers.hpp>
#include <reloco/heap_allocator.hpp>

#include <new>

namespace {

using reloco::construction_helpers;

enum class widget_error { bad };

reloco::allocator_ref heap() { return reloco::allocator<reloco::heap_allocator_tag>::ref(); }

// Tier 1: has_try_construct_v.
struct constructible_widget {
  int value = 0;
  reloco::expected<void, widget_error> try_construct(int v) noexcept {
    if (v < 0)
      return reloco::unexpected(widget_error::bad);
    value = v;
    return {};
  }
};

// Tier 2: has_try_allocate_v.
struct allocating_widget {
  int value;
  static reloco::expected<allocating_widget, widget_error> try_allocate(reloco::allocator_ref,
                                                                        int v) noexcept {
    if (v < 0)
      return reloco::unexpected(widget_error::bad);
    return allocating_widget{v};
  }
};

// Tier 3: has_try_create_v.
struct creatable_widget {
  int value;
  static reloco::expected<creatable_widget, widget_error> try_create(int v) noexcept {
    if (v < 0)
      return reloco::unexpected(widget_error::bad);
    return creatable_widget{v};
  }
};

// Tier 4: plain nothrow constructible, no fallible protocol.
struct plain_widget {
  int value;
  explicit plain_widget(int v) noexcept : value(v) {}
};

// try_clone tier 1: allocator-aware.
struct allocator_aware_clonable_widget {
  int value;
  reloco::expected<allocator_aware_clonable_widget, widget_error>
  try_clone(reloco::allocator_ref) const noexcept {
    return allocator_aware_clonable_widget{value};
  }
};

// try_clone tier 2: self-contained.
struct self_contained_clonable_widget {
  int value;
  reloco::expected<self_contained_clonable_widget, widget_error> try_clone() const noexcept {
    return self_contained_clonable_widget{value};
  }
};

// try_clone_at tier 1: direct in-place clone.
struct clonable_at_widget {
  int value;
  static reloco::expected<void, widget_error>
  try_clone_at(reloco::allocator_ref, clonable_at_widget *storage,
              const clonable_at_widget &source) noexcept {
    new (storage) clonable_at_widget{source.value};
    return {};
  }
};

// try_clone_at tier 2 (fallback): plain, nothrow copy constructible, no
// try_clone/try_clone_at of its own.
struct plain_clonable_widget {
  int value;
};

} // namespace

TEST(ConstructionHelpersTest, TryConstructUsesInPlaceFallibleTier) {
  alignas(constructible_widget) unsigned char storage[sizeof(constructible_widget)];
  auto *ptr = reinterpret_cast<constructible_widget *>(storage);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  auto ok = construction_helpers::try_construct(heap(), ptr, 5);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ptr->value, 5);

  auto failed = construction_helpers::try_construct(heap(), ptr, -1);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), widget_error::bad);
}

TEST(ConstructionHelpersTest, TryConstructUsesAllocateTier) {
  alignas(allocating_widget) unsigned char storage[sizeof(allocating_widget)];
  auto *ptr = reinterpret_cast<allocating_widget *>(storage);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  auto ok = construction_helpers::try_construct(heap(), ptr, 6);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ptr->value, 6);

  auto failed = construction_helpers::try_construct(heap(), ptr, -1);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), widget_error::bad);
}

TEST(ConstructionHelpersTest, TryConstructUsesCreateTier) {
  alignas(creatable_widget) unsigned char storage[sizeof(creatable_widget)];
  auto *ptr = reinterpret_cast<creatable_widget *>(storage);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  auto ok = construction_helpers::try_construct(heap(), ptr, 7);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ptr->value, 7);

  auto failed = construction_helpers::try_construct(heap(), ptr, -1);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), widget_error::bad);
}

TEST(ConstructionHelpersTest, TryConstructUsesNothrowFallbackTier) {
  alignas(plain_widget) unsigned char storage[sizeof(plain_widget)];
  auto *ptr = reinterpret_cast<plain_widget *>(storage);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  auto ok = construction_helpers::try_construct(heap(), ptr, 8);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  ASSERT_TRUE(ok);
  EXPECT_EQ(ptr->value, 8);
}

TEST(ConstructionHelpersTest, TryAllocateUsesAllocateTier) {
  auto ok = construction_helpers::try_allocate<allocating_widget>(heap(), 9);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->value, 9);

  auto failed = construction_helpers::try_allocate<allocating_widget>(heap(), -1);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), widget_error::bad);
}

TEST(ConstructionHelpersTest, TryAllocateUsesCreateTier) {
  auto ok = construction_helpers::try_allocate<creatable_widget>(heap(), 10);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->value, 10);
}

TEST(ConstructionHelpersTest, TryAllocateUsesConstructTier) {
  auto ok = construction_helpers::try_allocate<constructible_widget>(heap(), 11);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->value, 11);

  auto failed = construction_helpers::try_allocate<constructible_widget>(heap(), -1);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), widget_error::bad);
}

TEST(ConstructionHelpersTest, TryAllocateUsesNothrowFallbackTier) {
  auto ok = construction_helpers::try_allocate<plain_widget>(heap(), 12);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->value, 12);
}

TEST(ConstructionHelpersTest, TryCloneUsesAllocatorAwareTier) {
  allocator_aware_clonable_widget source{13};
  auto ok = construction_helpers::try_clone(heap(), source);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->value, 13);
}

TEST(ConstructionHelpersTest, TryCloneUsesSelfContainedTier) {
  self_contained_clonable_widget source{14};
  auto ok = construction_helpers::try_clone(heap(), source);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->value, 14);
}

TEST(ConstructionHelpersTest, TryCloneUsesAllocateTier) {
  allocating_widget source{15};
  auto ok = construction_helpers::try_clone(heap(), source);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->value, 15);
}

TEST(ConstructionHelpersTest, TryCloneUsesCreateTier) {
  creatable_widget source{16};
  auto ok = construction_helpers::try_clone(heap(), source);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->value, 16);
}

TEST(ConstructionHelpersTest, TryCloneUsesNothrowFallbackTier) {
  plain_clonable_widget source{17};
  auto ok = construction_helpers::try_clone(heap(), source);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->value, 17);
}

TEST(ConstructionHelpersTest, TryCloneAtUsesDirectTier) {
  alignas(clonable_at_widget) unsigned char storage[sizeof(clonable_at_widget)];
  auto *ptr = reinterpret_cast<clonable_at_widget *>(storage);
  clonable_at_widget source{18};
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  auto ok = construction_helpers::try_clone_at(heap(), ptr, source);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  ASSERT_TRUE(ok);
  EXPECT_EQ(ptr->value, 18);
}

TEST(ConstructionHelpersTest, TryCloneAtUsesFallbackTier) {
  alignas(plain_clonable_widget) unsigned char storage[sizeof(plain_clonable_widget)];
  auto *ptr = reinterpret_cast<plain_clonable_widget *>(storage);
  plain_clonable_widget source{19};
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  auto ok = construction_helpers::try_clone_at(heap(), ptr, source);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  ASSERT_TRUE(ok);
  EXPECT_EQ(ptr->value, 19);
}
