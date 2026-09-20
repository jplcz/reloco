// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/checked_value.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/string.hpp>
#include <reloco/unique_ptr.hpp>

#include <string>

namespace {

struct trivial_pod {
  int a;
  double b;
};

struct non_trivial_but_relocatable {
  non_trivial_but_relocatable() noexcept = default;
  non_trivial_but_relocatable(const non_trivial_but_relocatable &) noexcept {}
  non_trivial_but_relocatable &operator=(const non_trivial_but_relocatable &) noexcept { return *this; }
  ~non_trivial_but_relocatable() noexcept {}
};

struct widget {
  int value;
};

} // namespace

// Opt in explicitly: a user-defined copy constructor/destructor makes this
// type non-trivially-copyable, so it does not qualify by default even
// though it happens to hold no self-references.
template <> struct reloco::is_trivially_relocatable<non_trivial_but_relocatable> : std::true_type {};

TEST(RelocatableTest, TriviallyCopyableTypesAreTriviallyRelocatableByDefault) {
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<int>);
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<trivial_pod>);
}

TEST(RelocatableTest, NonTrivialTypesAreNotRelocatableUnlessOptedIn) {
  EXPECT_FALSE(reloco::is_trivially_relocatable_v<std::string>);
}

TEST(RelocatableTest, UserOptInSpecializationIsHonored) {
  EXPECT_FALSE(std::is_trivially_copyable_v<non_trivial_but_relocatable>);
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<non_trivial_but_relocatable>);
}

TEST(RelocatableTest, UniquePtrIsAlwaysTriviallyRelocatable) {
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<reloco::unique_ptr<widget>>);
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<reloco::unique_ptr<std::string>>);
}

TEST(RelocatableTest, StringIsAlwaysTriviallyRelocatable) {
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<reloco::string>);
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<reloco::wstring>);
}

TEST(RelocatableTest, CheckedValueForwardsToItsHeldType) {
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<reloco::checked_value<int>>);
  EXPECT_FALSE(reloco::is_trivially_relocatable_v<reloco::checked_value<std::string>>);
}

TEST(RelocatableTest, CheckedValuePointerSpecializationIsAlwaysRelocatable) {
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<reloco::checked_value<widget *>>));
}

#if RELOCO_CXX20
TEST(RelocatableTest, ConceptMatchesTrait) {
  static_assert(reloco::trivially_relocatable<int>);
  static_assert(!reloco::trivially_relocatable<std::string>);
  static_assert(reloco::trivially_relocatable<reloco::unique_ptr<widget>>);
}
#endif
