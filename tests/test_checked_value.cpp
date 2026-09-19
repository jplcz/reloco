// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <reloco/checked_value.hpp>

#include <string>
#include <type_traits>
#include <utility>

namespace {

using reloco::checked_value;

struct widget {
  int x{7};
};

static_assert(!std::is_copy_constructible_v<checked_value<int>>);
static_assert(!std::is_copy_assignable_v<checked_value<int>>);
static_assert(std::is_move_constructible_v<checked_value<int>>);
static_assert(std::is_move_assignable_v<checked_value<int>>);

TEST(CheckedValueTest, ConstructsFreshAndUnmovedFrom) {
  checked_value<int> value(42);
  EXPECT_FALSE(value.is_moved_from());
  EXPECT_EQ(value.get(), 42);
  EXPECT_EQ(*value, 42);
}

TEST(CheckedValueTest, MutableGetAllowsInPlaceModification) {
  checked_value<int> value(1);
  value.get() = 7;
  EXPECT_EQ(value.get(), 7);
}

TEST(CheckedValueTest, OperatorArrowBorrowsMemberOfHeldValue) {
  checked_value<std::string> value(std::string("hello"));
  EXPECT_EQ(value->size(), 5u);
}

TEST(CheckedValueTest, MoveConstructionTransfersValueAndPoisonsSource) {
  checked_value<std::string> a(std::string("payload"));
  checked_value<std::string> b(std::move(a));

  EXPECT_EQ(b.get(), "payload");
  EXPECT_TRUE(a.is_moved_from());
}

TEST(CheckedValueTest, MoveAssignmentTransfersValueAndPoisonsSource) {
  checked_value<int> a(1);
  checked_value<int> b(2);

  b = std::move(a);

  EXPECT_EQ(b.get(), 1);
  EXPECT_TRUE(a.is_moved_from());
}

TEST(CheckedValueTest, ReassigningAMovedFromWrapperRevivesIt) {
  checked_value<int> a(1);
  checked_value<int> b(std::move(a));
  ASSERT_TRUE(a.is_moved_from());

  a = checked_value<int>(99);

  EXPECT_FALSE(a.is_moved_from());
  EXPECT_EQ(a.get(), 99);
}

TEST(CheckedValueTest, TakeMovesValueOutAndPoisonsWrapper) {
  checked_value<std::string> value(std::string("owned"));

  std::string extracted = std::move(value).take();

  EXPECT_EQ(extracted, "owned");
  EXPECT_TRUE(value.is_moved_from());
}

TEST(CheckedValueTest, CloneProducesAnIndependentCopy) {
  checked_value<int> a(5);

  checked_value<int> b = a.clone();
  b.get() = 6;

  EXPECT_EQ(a.get(), 5);
  EXPECT_EQ(b.get(), 6);
  EXPECT_FALSE(a.is_moved_from());
}

TEST(CheckedValueTest, UnsafeGetSkipsTheCheckedMovedFromGuard) {
  checked_value<int> value(41);

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(value.unsafe_get(), 41);
  value.unsafe_get() = 42;
  RELOCO_END_UNSAFE_BUFFER_USAGE

  EXPECT_EQ(value.get(), 42);
}

TEST(CheckedValueTest, AsKnownReturnsSelfWithoutChangingObservableState) {
  checked_value<int> value(3);

  checked_value<int> &self = value.as_known();

  EXPECT_EQ(&self, &value);
  EXPECT_EQ(self.get(), 3);
  EXPECT_FALSE(value.is_moved_from());
}

TEST(CheckedValueTest, DeductionGuideInfersValueType) {
  checked_value value(10);
  static_assert(std::is_same_v<decltype(value), checked_value<int>>);
  EXPECT_EQ(value.get(), 10);
}

static_assert(!std::is_copy_constructible_v<checked_value<widget *>>);
static_assert(std::is_move_constructible_v<checked_value<widget *>>);

TEST(CheckedValuePointerTest, ConstructsFromNonNullPointer) {
  widget w;
  checked_value<widget *> p(&w);

  EXPECT_TRUE(static_cast<bool>(p));
  EXPECT_FALSE(p.is_null());
  EXPECT_EQ(p.get(), &w);
  EXPECT_EQ(p->x, 7);
  EXPECT_EQ((*p).x, 7);
}

TEST(CheckedValuePointerTest, ConstructsFromNullPointer) {
  checked_value<widget *> p(nullptr);

  EXPECT_FALSE(static_cast<bool>(p));
  EXPECT_TRUE(p.is_null());
  EXPECT_EQ(p.get(), nullptr);
}

TEST(CheckedValuePointerTest, MoveTransfersPointerAndNullsSource) {
  widget w;
  checked_value<widget *> a(&w);
  checked_value<widget *> b(std::move(a));

  EXPECT_TRUE(a.is_moved_from());
  EXPECT_EQ(b.get(), &w);
}

TEST(CheckedValuePointerTest, TakeNullsSourceAndPoisonsWrapper) {
  widget w;
  checked_value<widget *> p(&w);

  widget *raw = std::move(p).take();

  EXPECT_EQ(raw, &w);
  EXPECT_TRUE(p.is_moved_from());
}

TEST(CheckedValuePointerTest, CloneProducesAnIndependentCopyOfThePointer) {
  widget w;
  checked_value<widget *> p(&w);

  checked_value<widget *> cloned = p.clone();

  EXPECT_EQ(cloned.get(), &w);
  EXPECT_FALSE(p.is_moved_from());
}

TEST(CheckedValuePointerTest, UnsafeGetSkipsTheCheckedMovedFromGuard) {
  widget w;
  checked_value<widget *> p(&w);

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(p.unsafe_get(), &w);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

TEST(CheckedValuePointerTest, UnsafeDerefSkipsTheCheckedMovedFromAndNullGuards) {
  widget w;
  checked_value<widget *> p(&w);

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(p.unsafe_deref().x, 7);
  p.unsafe_deref().x = 9;
  RELOCO_END_UNSAFE_BUFFER_USAGE

  EXPECT_EQ(w.x, 9);
}

TEST(CheckedValuePointerTest, AsKnownReturnsSelfWithoutChangingObservableState) {
  widget w;
  checked_value<widget *> p(&w);

  checked_value<widget *> &self = p.as_known();

  EXPECT_EQ(&self, &p);
  EXPECT_EQ(self.get(), &w);
}

} // namespace
