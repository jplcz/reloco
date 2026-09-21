// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/alignment.hpp>
#include <reloco/array.hpp>
#include <reloco/inline_vector.hpp>
#include <reloco/vector.hpp>

#include <cstddef>
#include <cstdint>

namespace {

struct plain_thing {
  float x = 0.0F;
};

// A type that requests SIMD-friendly over-alignment via the customization
// point, without itself being declared alignas(...).
struct wide_thing {
  float x = 0.0F;
  float y = 0.0F;
};

} // namespace

template <> struct reloco::alignment_of<wide_thing> : std::integral_constant<std::size_t, 32> {};

namespace {

[[nodiscard]] bool is_aligned(const void *ptr, std::size_t alignment) noexcept {
  return (reinterpret_cast<std::uintptr_t>(ptr) % alignment) == 0;
}

} // namespace

TEST(AlignmentTest, DefaultsToAlignofT) {
  static_assert(reloco::alignment_of_v<plain_thing> == alignof(plain_thing));
  static_assert(reloco::effective_alignment_v<plain_thing> == alignof(plain_thing));
}

TEST(AlignmentTest, SpecializationOverridesDefault) {
  static_assert(reloco::alignment_of_v<wide_thing> == 32);
  static_assert(reloco::effective_alignment_v<wide_thing> == 32);
}

TEST(AlignmentTest, NeverWeakerThanNaturalAlignment) {
  // A type whose alignment_of<T> is deliberately left at the default
  // (alignof(T)) must never be treated as *less* aligned than that.
  static_assert(reloco::effective_alignment_v<plain_thing> >= alignof(plain_thing));
}

TEST(AlignmentTest, ArrayStorageHonorsOverride) {
  reloco::array<wide_thing, 4> a{};
  EXPECT_TRUE(is_aligned(a.data(), 32));
  EXPECT_TRUE(is_aligned(&a, 32));
}

TEST(AlignmentTest, InlineVectorStorageHonorsOverride) {
  reloco::inline_vector<wide_thing, 4> v;
  ASSERT_TRUE(v.try_push_back(wide_thing{}));
  EXPECT_TRUE(is_aligned(v.data(), 32));
  EXPECT_TRUE(is_aligned(&v, 32));
}

TEST(AlignmentTest, VectorAllocationHonorsOverride) {
  auto v = reloco::vector<wide_thing>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(wide_thing{}));
  EXPECT_TRUE(is_aligned(v->data(), 32));

  // Force growth past the initial allocation to exercise reallocate() too.
  for (int i = 0; i < 64; ++i)
    ASSERT_TRUE(v->try_push_back(wide_thing{}));
  EXPECT_TRUE(is_aligned(v->data(), 32));
}

TEST(AlignmentTest, VectorShrinkToFitHonorsOverride) {
  auto v = reloco::vector<wide_thing>::try_create();
  ASSERT_TRUE(v);
  for (int i = 0; i < 16; ++i)
    ASSERT_TRUE(v->try_push_back(wide_thing{}));
  ASSERT_TRUE(v->try_reserve(64));
  ASSERT_TRUE(v->shrink_to_fit());
  EXPECT_TRUE(is_aligned(v->data(), 32));
}

TEST(AlignmentTest, PlainTypesUnaffected) {
  reloco::array<plain_thing, 4> a{};
  EXPECT_TRUE(is_aligned(a.data(), alignof(plain_thing)));

  auto v = reloco::vector<plain_thing>::try_create();
  ASSERT_TRUE(v);
  ASSERT_TRUE(v->try_push_back(plain_thing{}));
  EXPECT_TRUE(is_aligned(v->data(), alignof(plain_thing)));
}
