// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Kept as its own translation unit/executable (like test_value_ref.cpp/
// test_checked_value.cpp) so RELOCO_IMPLICIT_TYPEID only ever applies here,
// not to the rest of the test suite. Requires RTTI enabled (the default for
// this target; see CMakeLists.txt) -- RELOCO_IMPLICIT_TYPEID alone does
// nothing without it.
#define RELOCO_IMPLICIT_TYPEID

#include <gtest/gtest.h>
#include <reloco/type_id.hpp>

namespace {

struct unnamed_widget {};
struct named_widget {};

} // namespace

RELOCO_TYPE_ID_NAME(named_widget, "named_widget");

TEST(TypeIdImplicitTypeIdTest, UnregisteredTypeFallsBackToTypeidName) {
  const char *name = reloco::type_id::of<unnamed_widget>().name();
  ASSERT_NE(name, nullptr);
  EXPECT_STREQ(name, typeid(unnamed_widget).name());
}

TEST(TypeIdImplicitTypeIdTest, ExplicitRegistrationStillTakesPriority) {
  EXPECT_STREQ(reloco::type_id::of<named_widget>().name(), "named_widget");
  EXPECT_STRNE(reloco::type_id::of<named_widget>().name(), typeid(named_widget).name());
}

TEST(TypeIdImplicitTypeIdTest, SentinelStillHasNoName) { EXPECT_EQ(reloco::type_id().name(), nullptr); }

TEST(TypeIdImplicitTypeIdTest, FallbackNameDoesNotAffectEquality) {
  EXPECT_EQ(reloco::type_id::of<unnamed_widget>(), reloco::type_id::of<unnamed_widget>());
  EXPECT_NE(reloco::type_id::of<unnamed_widget>(), reloco::type_id::of<named_widget>());
}
