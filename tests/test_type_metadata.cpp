// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/detail/type_metadata.hpp>
#include <reloco/relocatable.hpp>

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

using reloco::detail::has_capability;
using reloco::detail::metadata_for;
using reloco::detail::type_capability;
using reloco::detail::type_metadata;

namespace {

// Trivially destructible/copyable/relocatable/default-constructible: every
// capability bit should be set.
struct trivial_pod {
  int a;
  double b;
};

// Has a user-provided destructor: not trivially destructible, and (since
// trivial copyability requires trivial destructibility) not trivially
// copyable either. Still default-constructible, and not opted into
// `is_trivially_relocatable` so it stays non-relocatable too.
struct has_dtor {
  int value = 0;
  ~has_dtor() noexcept {}
};

// No default constructor, but otherwise entirely trivial.
struct no_default_ctor {
  int value;
  explicit no_default_ctor(int v) noexcept : value(v) {}
};

// Non-trivially copyable/destructible (user-provided special members), but
// explicitly opted into `is_trivially_relocatable` -- exercises that
// `type_metadata` can report `is_trivially_relocatable() == true` while
// `is_trivially_copyable() == false` for the same type, exactly like
// `reloco::is_trivially_relocatable`'s own opt-in customization point.
struct opted_in_relocatable {
  opted_in_relocatable() noexcept = default;
  opted_in_relocatable(const opted_in_relocatable &) noexcept {}
  opted_in_relocatable &operator=(const opted_in_relocatable &) noexcept { return *this; }
  ~opted_in_relocatable() noexcept {}
};

} // namespace

template <> struct reloco::is_trivially_relocatable<opted_in_relocatable> : std::true_type {};

TEST(TypeCapabilityTest, BitwiseOrCombinesFlags) {
  const auto combined = type_capability::trivially_destructible | type_capability::trivially_copyable;
  EXPECT_TRUE(has_capability(combined, type_capability::trivially_destructible));
  EXPECT_TRUE(has_capability(combined, type_capability::trivially_copyable));
  EXPECT_FALSE(has_capability(combined, type_capability::trivially_relocatable));
  EXPECT_FALSE(has_capability(combined, type_capability::default_constructible));
}

TEST(TypeCapabilityTest, BitwiseAndIntersectsFlags) {
  const auto lhs = type_capability::trivially_destructible | type_capability::trivially_copyable;
  const auto rhs = type_capability::trivially_copyable | type_capability::default_constructible;
  const auto intersection = lhs & rhs;
  EXPECT_TRUE(has_capability(intersection, type_capability::trivially_copyable));
  EXPECT_FALSE(has_capability(intersection, type_capability::trivially_destructible));
  EXPECT_FALSE(has_capability(intersection, type_capability::default_constructible));
}

TEST(TypeCapabilityTest, NoneHasNoCapabilitiesSet) {
  EXPECT_FALSE(has_capability(type_capability::none, type_capability::trivially_destructible));
  EXPECT_FALSE(has_capability(type_capability::none, type_capability::trivially_relocatable));
  EXPECT_FALSE(has_capability(type_capability::none, type_capability::trivially_copyable));
  EXPECT_FALSE(has_capability(type_capability::none, type_capability::default_constructible));
}

TEST(TypeCapabilityTest, HasCapabilityRequiresEveryBitInFlagToBeSet) {
  // `flag` itself may be a combination of bits; `has_capability` only
  // returns true when *all* of them are present in `flags`, not just any.
  const auto flags = type_capability::trivially_destructible;
  const auto flag = type_capability::trivially_destructible | type_capability::trivially_copyable;
  EXPECT_FALSE(has_capability(flags, flag));
}

TEST(TypeMetadataTest, ElementSizeAndAlignmentMatchSizeofAlignof) {
  EXPECT_EQ(metadata_for<int>.element_size, sizeof(int));
  EXPECT_EQ(metadata_for<int>.element_alignment, alignof(int));
  EXPECT_EQ(metadata_for<trivial_pod>.element_size, sizeof(trivial_pod));
  EXPECT_EQ(metadata_for<trivial_pod>.element_alignment, alignof(trivial_pod));
}

TEST(TypeMetadataTest, FullyTrivialTypeHasEveryCapability) {
  const type_metadata &meta = metadata_for<trivial_pod>;
  EXPECT_TRUE(meta.is_trivially_destructible());
  EXPECT_TRUE(meta.is_trivially_relocatable());
  EXPECT_TRUE(meta.is_trivially_copyable());
  EXPECT_TRUE(meta.is_default_constructible());
}

TEST(TypeMetadataTest, UserProvidedDestructorClearsDestructibleCopyableAndRelocatable) {
  const type_metadata &meta = metadata_for<has_dtor>;
  EXPECT_FALSE(meta.is_trivially_destructible());
  EXPECT_FALSE(meta.is_trivially_copyable());
  EXPECT_FALSE(meta.is_trivially_relocatable());
  EXPECT_TRUE(meta.is_default_constructible());
}

TEST(TypeMetadataTest, MissingDefaultConstructorOnlyClearsThatOneFlag) {
  const type_metadata &meta = metadata_for<no_default_ctor>;
  EXPECT_TRUE(meta.is_trivially_destructible());
  EXPECT_TRUE(meta.is_trivially_relocatable());
  EXPECT_TRUE(meta.is_trivially_copyable());
  EXPECT_FALSE(meta.is_default_constructible());
}

TEST(TypeMetadataTest, RelocatableOptInIsIndependentOfCopyable) {
  const type_metadata &meta = metadata_for<opted_in_relocatable>;
  EXPECT_FALSE(meta.is_trivially_copyable());
  EXPECT_FALSE(meta.is_trivially_destructible());
  EXPECT_TRUE(meta.is_trivially_relocatable());
}

TEST(TypeMetadataTest, NonTrivialStdTypeHasNoTrivialCapabilities) {
  const type_metadata &meta = metadata_for<std::string>;
  EXPECT_FALSE(meta.is_trivially_destructible());
  EXPECT_FALSE(meta.is_trivially_copyable());
  EXPECT_FALSE(meta.is_trivially_relocatable());
  EXPECT_TRUE(meta.is_default_constructible());
}

TEST(TypeMetadataTest, SameSizeAlignmentAndCapabilitiesShareOneInstance) {
  // int and unsigned int are both 4-byte/4-aligned and fully trivial, so
  // metadata_for<T> should intern to the exact same object.
  EXPECT_EQ(&metadata_for<int>, &metadata_for<unsigned int>);
}

TEST(TypeMetadataTest, DifferentSizeNeverSharesAnInstance) {
  static_assert(sizeof(float) != sizeof(double));
  EXPECT_NE(&metadata_for<float>, &metadata_for<double>);
}

TEST(TypeMetadataTest, DifferentCapabilitiesNeverShareAnInstanceEvenAtSameSizeAndAlignment) {
  static_assert(sizeof(no_default_ctor) == sizeof(int) && alignof(no_default_ctor) == alignof(int));
  EXPECT_NE(&metadata_for<int>, &metadata_for<no_default_ctor>);
}

TEST(TypeMetadataTest, RepeatedLookupsForSameTypeReturnTheSameInstance) {
  EXPECT_EQ(&metadata_for<trivial_pod>, &metadata_for<trivial_pod>);
}
