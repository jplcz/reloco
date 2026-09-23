// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/type_id.hpp>

#include <unordered_set>

namespace {

struct alpha {};
struct beta {};

} // namespace

TEST(TypeIdTest, SameTypeCompareEqual) {
  EXPECT_EQ(reloco::type_id::of<alpha>(), reloco::type_id::of<alpha>());
  EXPECT_EQ(reloco::type_id_of<alpha>(), reloco::type_id_of<alpha>());
}

TEST(TypeIdTest, DifferentTypesCompareUnequal) {
  EXPECT_NE(reloco::type_id::of<alpha>(), reloco::type_id::of<beta>());
}

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

TEST(TypeIdTest, OfProducesATruthyValidIdentity) {
  EXPECT_TRUE(static_cast<bool>(reloco::type_id::of<alpha>()));
}

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
