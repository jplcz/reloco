// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/error.hpp>
#include <reloco/ordering.hpp>
#include <reloco/sso_string.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>
#include <reloco/type_id.hpp>

#include <cstring>
#include <unordered_set>

namespace {

struct alpha {};
struct beta {};
struct unnamed_type {};

} // namespace

RELOCO_TYPE_ID_NAME(alpha, "alpha");

TEST(TypeIdTest, SameTypeCompareEqual) {
  EXPECT_EQ(reloco::type_id::of<alpha>(), reloco::type_id::of<alpha>());
  EXPECT_EQ(reloco::type_id_of<alpha>(), reloco::type_id_of<alpha>());
}

TEST(TypeIdTest, DifferentTypesCompareUnequal) { EXPECT_NE(reloco::type_id::of<alpha>(), reloco::type_id::of<beta>()); }

TEST(TypeIdTest, CvAndReferenceQualificationAreDistinctIdentities) {
  EXPECT_NE(reloco::type_id::of<alpha>(), reloco::type_id::of<const alpha>());
  EXPECT_NE(reloco::type_id::of<alpha>(), reloco::type_id::of<alpha &>());
}

TEST(TypeIdTest, DefaultConstructedIsTheNoTypeSentinel) {
  reloco::type_id id;
  EXPECT_FALSE(id);
  EXPECT_NE(id, reloco::type_id::of<alpha>());
  EXPECT_EQ(id, reloco::type_id());
}

TEST(TypeIdTest, OfProducesATruthyValidIdentity) { EXPECT_TRUE(static_cast<bool>(reloco::type_id::of<alpha>())); }

TEST(TypeIdTest, TotalOrderingIsConsistentAndIrreflexive) {
  auto a = reloco::type_id::of<alpha>();
  auto b = reloco::type_id::of<beta>();
  EXPECT_FALSE(a < a);
  EXPECT_TRUE((a < b) != (b < a) || a == b);
  EXPECT_EQ(a <= a, true);
  EXPECT_EQ(a >= a, true);
  EXPECT_EQ(a < b, !(a >= b) || a == b);
}

TEST(TypeIdTest, HashIsConsistentForEqualIdentities) {
  EXPECT_EQ(reloco::type_id::of<alpha>().hash(), reloco::type_id::of<alpha>().hash());
}

TEST(TypeIdTest, UsableAsAnUnorderedSetKeyViaStdHashSpecialization) {
  std::unordered_set<reloco::type_id> ids;
  ids.insert(reloco::type_id::of<alpha>());
  ids.insert(reloco::type_id::of<alpha>());
  ids.insert(reloco::type_id::of<beta>());
  EXPECT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids.count(reloco::type_id::of<alpha>()), 1u);
  EXPECT_EQ(ids.count(reloco::type_id::of<beta>()), 1u);
}

TEST(TypeIdTest, NameIsNullptrForATypeWithNoRegisteredName) {
  EXPECT_EQ(reloco::type_id::of<unnamed_type>().name(), nullptr);
}

TEST(TypeIdTest, NameIsNullptrForTheNoTypeSentinel) { EXPECT_EQ(reloco::type_id().name(), nullptr); }

TEST(TypeIdTest, RelocoTypeIdNameRegistersACustomDebugName) {
  const char *name = reloco::type_id::of<alpha>().name();
  ASSERT_NE(name, nullptr);
  EXPECT_STREQ(name, "alpha");
}

TEST(TypeIdTest, NameDoesNotAffectEqualityOrOrdering) {
  // `alpha` (named) and `beta` (unnamed) still compare exactly like any
  // other pair of distinct types; a registered name is a pure add-on.
  EXPECT_NE(reloco::type_id::of<alpha>(), reloco::type_id::of<beta>());
  EXPECT_EQ(reloco::type_id::of<alpha>(), reloco::type_id::of<alpha>());
}

TEST(TypeIdTest, FundamentalTypesHaveRegisteredNames) {
  EXPECT_STREQ(reloco::type_id::of<bool>().name(), "bool");
  EXPECT_STREQ(reloco::type_id::of<int>().name(), "int");
  EXPECT_STREQ(reloco::type_id::of<unsigned int>().name(), "unsigned int");
  EXPECT_STREQ(reloco::type_id::of<long long>().name(), "long long");
  EXPECT_STREQ(reloco::type_id::of<float>().name(), "float");
  EXPECT_STREQ(reloco::type_id::of<double>().name(), "double");
  EXPECT_STREQ(reloco::type_id::of<std::nullptr_t>().name(), "std::nullptr_t");
}

TEST(TypeIdTest, ClassicRelocoTypesHaveRegisteredNames) {
  EXPECT_STREQ(reloco::type_id::of<reloco::type_id>().name(), "reloco::type_id");
  EXPECT_STREQ(reloco::type_id::of<reloco::error>().name(), "reloco::error");
  EXPECT_STREQ(reloco::type_id::of<reloco::ordering>().name(), "reloco::ordering");
  EXPECT_STREQ(reloco::type_id::of<reloco::string>().name(), "reloco::string");
  EXPECT_STREQ(reloco::type_id::of<reloco::string_view>().name(), "reloco::string_view");
  EXPECT_STREQ(reloco::type_id::of<reloco::sso_string>().name(), "reloco::sso_string");
}
