// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/cow.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>

#include <utility>

using reloco::cow;
using reloco::string;

namespace {

string make_string(reloco::string_view text) {
  auto res = string::try_create();
  string s = std::move(res.value());
  (void)s.try_append(text);
  return s;
}

// A plain, non-allocator-aware, nothrow-copy-constructible type: exercises
// the nothrow-copy fallback tier of construction_helpers::try_clone.
struct point {
  int x;
  int y;
  bool operator==(const point &o) const noexcept { return x == o.x && y == o.y; }
};

// Custom cow_traits specialization: tracks how many times a clone was
// requested, to verify the customization point is actually used.
struct tracked {
  int value;
  static int clone_count;
};
int tracked::clone_count = 0;

} // namespace

template <> struct reloco::cow_traits<tracked> {
  static reloco::result<tracked> try_clone(reloco::allocator_ref, const tracked &source) noexcept {
    ++tracked::clone_count;
    return tracked{source.value};
  }
};

TEST(CowTest, BorrowedConstructionDoesNotClone) {
  string original = make_string("hi");
  cow<string> c(original);
  EXPECT_TRUE(c.is_borrowed());
  EXPECT_FALSE(c.is_owned());
  EXPECT_EQ(c->size(), 2u);
}

TEST(CowTest, OwnedConstructionMovesValueIn) {
  string s = make_string("x");
  cow<string> c(std::move(s));
  EXPECT_TRUE(c.is_owned());
  EXPECT_EQ(c->size(), 1u);
}

TEST(CowTest, ToMutClonesOnFirstMutationAndLeavesSourceUntouched) {
  string original = make_string("hi");
  cow<string> c(original);
  ASSERT_TRUE(c.is_borrowed());

  auto mut_ref = c.to_mut();
  ASSERT_TRUE(mut_ref.has_value());
  EXPECT_TRUE(c.is_owned());
  ASSERT_TRUE(mut_ref.value().get().try_push_back('!').has_value());

  EXPECT_EQ(c->size(), 3u);
  EXPECT_EQ(original.size(), 2u); // the original borrowed string is untouched
}

TEST(CowTest, ToMutOnAlreadyOwnedIsANoOpBeyondTheReference) {
  string s = make_string("y");
  cow<string> c(std::move(s));
  auto mut_ref1 = c.to_mut();
  ASSERT_TRUE(mut_ref1.has_value());
  auto mut_ref2 = c.to_mut();
  ASSERT_TRUE(mut_ref2.has_value());
  EXPECT_EQ(&mut_ref1.value().get(), &mut_ref2.value().get());
}

TEST(CowTest, IntoOwnedFromBorrowedClones) {
  string original = make_string("hi");
  cow<string> c(original);
  auto owned = std::move(c).into_owned();
  ASSERT_TRUE(owned.has_value());
  EXPECT_EQ(owned->size(), 2u);
  EXPECT_EQ(original.size(), 2u);
}

TEST(CowTest, IntoOwnedFromOwnedMovesOut) {
  string s = make_string("z");
  cow<string> c(std::move(s));
  auto owned = std::move(c).into_owned();
  ASSERT_TRUE(owned.has_value());
  EXPECT_EQ(owned->size(), 1u);
}

TEST(CowTest, TryCloneProducesAnIndependentOwnedCow) {
  string original = make_string("ab");
  cow<string> c(original);
  auto cloned = c.try_clone();
  ASSERT_TRUE(cloned.has_value());
  EXPECT_TRUE(cloned->is_owned());
  EXPECT_EQ((*cloned)->size(), 2u);

  // Mutating the clone must not affect the original cow.
  auto mut_ref = cloned->to_mut();
  ASSERT_TRUE(mut_ref.value().get().try_push_back('c').has_value());
  EXPECT_EQ((*cloned)->size(), 3u);
  EXPECT_EQ(c->size(), 2u);
}

TEST(CowTest, MoveConstructionFromBorrowedTransfersBorrow) {
  string original = make_string("hi");
  cow<string> c1(original);
  cow<string> c2(std::move(c1));
  EXPECT_TRUE(c2.is_borrowed());
  EXPECT_EQ(c2->size(), 2u);
}

TEST(CowTest, MoveConstructionFromOwnedTransfersOwnership) {
  string s = make_string("hi");
  cow<string> c1(std::move(s));
  cow<string> c2(std::move(c1));
  EXPECT_TRUE(c2.is_owned());
  EXPECT_EQ(c2->size(), 2u);
}

TEST(CowTest, WorksWithNothrowCopyConstructibleValueTypes) {
  point p{1, 2};
  cow<point> c(p);
  EXPECT_TRUE(c.is_borrowed());
  auto mut_ref = c.to_mut();
  ASSERT_TRUE(mut_ref.has_value());
  mut_ref.value().get().x = 42;
  EXPECT_EQ(c->x, 42);
  EXPECT_EQ(p.x, 1); // untouched
}

TEST(CowTest, CustomCowTraitsSpecializationIsUsedForCloning) {
  tracked::clone_count = 0;
  tracked t{5};
  cow<tracked> c(t);
  EXPECT_EQ(tracked::clone_count, 0);

  auto mut_ref = c.to_mut();
  ASSERT_TRUE(mut_ref.has_value());
  EXPECT_EQ(tracked::clone_count, 1);
  EXPECT_EQ(mut_ref.value().get().value, 5);
}
