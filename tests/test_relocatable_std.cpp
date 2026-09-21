// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/relocatable_std.hpp>
#include <reloco/string.hpp>
#include <reloco/unique_ptr.hpp>

#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace {
struct widget {
  int value;
};
} // namespace

// ---- std::pair ----

TEST(RelocatableStdTest, PairOfTriviallyCopyableTypesIsRelocatable) {
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<std::pair<int, double>>));
}

TEST(RelocatableStdTest, PairContainingRelocoStringIsRelocatable) {
  static_assert(!std::is_trivially_copyable_v<std::pair<reloco::string, int>>);
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<std::pair<reloco::string, int>>));
}

TEST(RelocatableStdTest, PairContainingNonRelocatableTypeIsNotRelocatable) {
  EXPECT_FALSE((reloco::is_trivially_relocatable_v<std::pair<std::string, int>>));
}

// ---- std::tuple ----

TEST(RelocatableStdTest, TupleOfTriviallyCopyableTypesIsRelocatable) {
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<std::tuple<int, double, char>>));
}

TEST(RelocatableStdTest, TupleContainingRelocoTypesIsRelocatable) {
  using tuple_type = std::tuple<reloco::string, reloco::unique_ptr<widget>, int>;
  static_assert(!std::is_trivially_copyable_v<tuple_type>);
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<tuple_type>);
}

TEST(RelocatableStdTest, TupleContainingNonRelocatableTypeIsNotRelocatable) {
  EXPECT_FALSE((reloco::is_trivially_relocatable_v<std::tuple<reloco::string, std::string>>));
}

TEST(RelocatableStdTest, EmptyTupleIsRelocatable) { EXPECT_TRUE(reloco::is_trivially_relocatable_v<std::tuple<>>); }

// ---- std::optional ----

TEST(RelocatableStdTest, OptionalOfTriviallyCopyableTypeIsRelocatable) {
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<std::optional<int>>);
}

TEST(RelocatableStdTest, OptionalOfRelocoStringIsRelocatable) {
  static_assert(!std::is_trivially_copyable_v<std::optional<reloco::string>>);
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<std::optional<reloco::string>>);
}

TEST(RelocatableStdTest, OptionalOfNonRelocatableTypeIsNotRelocatable) {
  EXPECT_FALSE(reloco::is_trivially_relocatable_v<std::optional<std::string>>);
}

// ---- std::variant ----

TEST(RelocatableStdTest, VariantOfTriviallyCopyableTypesIsRelocatable) {
  EXPECT_TRUE((reloco::is_trivially_relocatable_v<std::variant<int, double>>));
}

TEST(RelocatableStdTest, VariantContainingRelocoTypesIsRelocatable) {
  using variant_type = std::variant<reloco::string, reloco::unique_ptr<widget>, int>;
  static_assert(!std::is_trivially_copyable_v<variant_type>);
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<variant_type>);
}

TEST(RelocatableStdTest, VariantContainingNonRelocatableTypeIsNotRelocatable) {
  EXPECT_FALSE((reloco::is_trivially_relocatable_v<std::variant<reloco::string, std::string>>));
}
