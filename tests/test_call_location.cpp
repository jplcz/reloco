// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/call_location.hpp>
#include <reloco/lifetime.hpp>

#include <cstring>
#include <type_traits>

using reloco::call_location;
using reloco::call_location_ref;
using reloco::debug_call_location_ref;
using reloco::release_call_location_ref;

namespace {

/** @brief Fixture for `call_location`/`call_location_ref` tests; the types under test carry no mutable state. */
class CallLocationTest : public ::testing::Test {};

// A location-less and a location-aware overload sharing one implementation,
// mirroring call_location.hpp's @file "two kernel-API exports" example.
const char *shared_file = nullptr;
int shared_line = 0;
bool shared_had_location = false;

void record_impl(call_location_ref where) {
  shared_had_location = where.has_value();
  if (where.has_value()) {
    const call_location loc = where.value();
    shared_file = loc.file;
    shared_line = loc.line;
  } else {
    shared_file = nullptr;
    shared_line = 0;
  }
}

void record(call_location_ref where = call_location_ref::current()) { record_impl(where); }

extern "C" void record_no_location() { record_impl(call_location_ref::none()); }

} // namespace

TEST_F(CallLocationTest, DefaultConstructedRefHasNoValue) {
  call_location_ref ref;
  EXPECT_FALSE(ref.has_value());
}

TEST_F(CallLocationTest, NoneFactoryHasNoValue) { EXPECT_FALSE(call_location_ref::none().has_value()); }

TEST_F(CallLocationTest, ConstructingFromCallLocationHasValue) {
  static constexpr call_location loc{"some_file.cpp", 42};
  call_location_ref ref(loc);
  ASSERT_TRUE(ref.has_value());
  EXPECT_STREQ(ref.value().file, "some_file.cpp");
  EXPECT_EQ(ref.value().line, 42);
}

TEST_F(CallLocationTest, ConstructingFromLocationWithNullFileHasNoValue) {
  static constexpr call_location loc{nullptr, 42};
  EXPECT_FALSE(call_location_ref(loc).has_value());
}

TEST_F(CallLocationTest, CurrentCapturesThisFileAndSomeLine) {
  call_location_ref ref = call_location_ref::current();
  ASSERT_TRUE(ref.has_value());
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  EXPECT_NE(std::strstr(ref.value().file, "test_call_location.cpp"), nullptr);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
  EXPECT_GT(ref.value().line, 0);
}

#if RELOCO_CALL_LOCATION_HAS_BUILTINS

TEST_F(CallLocationTest, DefaultArgumentCapturesCallerLocation) {
  const int call_line = __LINE__ + 1;
  record();
  EXPECT_TRUE(shared_had_location);
  ASSERT_NE(shared_file, nullptr);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  EXPECT_NE(std::strstr(shared_file, "test_call_location.cpp"), nullptr);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
  EXPECT_EQ(shared_line, call_line);
}

TEST_F(CallLocationTest, DefaultArgumentCapturesDistinctCallerLines) {
  const int first_line = __LINE__ + 1;
  record();
  const int first_captured = shared_line;
  const int second_line = __LINE__ + 1;
  record();
  const int second_captured = shared_line;
  EXPECT_EQ(first_captured, first_line);
  EXPECT_EQ(second_captured, second_line);
  EXPECT_NE(first_captured, second_captured);
}

#endif // RELOCO_CALL_LOCATION_HAS_BUILTINS

TEST_F(CallLocationTest, ExplicitlyPassingNoneYieldsNoLocation) {
  record(call_location_ref::none());
  EXPECT_FALSE(shared_had_location);
  EXPECT_EQ(shared_file, nullptr);
  EXPECT_EQ(shared_line, 0);
}

TEST_F(CallLocationTest, FixedSignatureExportForwardsNoLocation) {
  record_no_location();
  EXPECT_FALSE(shared_had_location);
  EXPECT_EQ(shared_file, nullptr);
  EXPECT_EQ(shared_line, 0);
}

TEST_F(CallLocationTest, DebugCallLocationRefIsSameTypeAsCallLocationRef) {
  static_assert(std::is_same<debug_call_location_ref, call_location_ref>::value,
                "debug_call_location_ref must alias call_location_ref");
}

TEST_F(CallLocationTest, ReleaseCallLocationRefIsAlwaysEmpty) {
  EXPECT_FALSE(release_call_location_ref().has_value());
  EXPECT_FALSE(release_call_location_ref::none().has_value());
  EXPECT_FALSE(release_call_location_ref::current().has_value());
}

TEST_F(CallLocationTest, ReleaseCallLocationRefIsAnEmptyType) { EXPECT_TRUE(std::is_empty<release_call_location_ref>::value); }

namespace {

// Paired debug/release overload pattern from call_location.hpp's @file
// mutex::lock example, exercised end to end: a real overload set (not
// just two distinctly-named functions), confirming an omitted-argument
// call resolves unambiguously to whichever side RELOCO_CALL_LOCATION_DEBUG
// currently selects.
bool last_had_location = false;
bool last_was_release_overload = false;

void lock_impl(call_location_ref where) {
  last_had_location = where.has_value();
  last_was_release_overload = false;
}

void lock(debug_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG) { lock_impl(where); }

void lock(release_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE) {
  (void)where;
  lock_impl(call_location_ref::none());
  last_was_release_overload = true;
}

} // namespace

TEST_F(CallLocationTest, OmittedArgumentCallResolvesToDebugOverloadByDefault) {
  static_assert(RELOCO_CALL_LOCATION_DEBUG, "this test assumes the default RELOCO_CALL_LOCATION_DEBUG == 1");
  lock();
  EXPECT_FALSE(last_was_release_overload);
#if RELOCO_CALL_LOCATION_HAS_BUILTINS
  EXPECT_TRUE(last_had_location);
#endif
}

TEST_F(CallLocationTest, ExplicitDebugOverloadCallStillWorks) {
  lock(debug_call_location_ref::none());
  EXPECT_FALSE(last_was_release_overload);
  EXPECT_FALSE(last_had_location);
}

TEST_F(CallLocationTest, ExplicitReleaseOverloadCallStillWorks) {
  lock(release_call_location_ref::none());
  EXPECT_TRUE(last_was_release_overload);
  EXPECT_FALSE(last_had_location);
}
