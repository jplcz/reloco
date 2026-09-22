// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/cell.hpp>

#include <string>
#include <utility>

TEST(CellTest, GetReturnsTheStoredValue) {
  const reloco::cell<int> c(42);
  EXPECT_EQ(c.get(), 42);
}

TEST(CellTest, DefaultConstructsToADefaultValue) {
  const reloco::cell<int> c;
  EXPECT_EQ(c.get(), 0);
}

TEST(CellTest, SetOverwritesTheValue) {
  reloco::cell<int> c(1);
  c.set(2);
  EXPECT_EQ(c.get(), 2);
}

TEST(CellTest, SetWorksThroughAConstReference) {
  const reloco::cell<int> c(1);
  c.set(2); // interior mutability: mutates despite the `const` binding
  EXPECT_EQ(c.get(), 2);
}

TEST(CellTest, ReplaceReturnsThePreviousValue) {
  reloco::cell<int> c(1);
  const int old = c.replace(2);
  EXPECT_EQ(old, 1);
  EXPECT_EQ(c.get(), 2);
}

TEST(CellTest, TakeResetsToADefaultValueAndReturnsThePrevious) {
  reloco::cell<int> c(7);
  const int taken = c.take();
  EXPECT_EQ(taken, 7);
  EXPECT_EQ(c.get(), 0);
}

TEST(CellTest, SetReplaceAndTakeWorkForANonTriviallyCopyableMovableType) {
  // std::string is not trivially copyable, so cell<std::string>::get() is
  // unavailable, but set()/replace()/take() only ever move, so they work
  // for any movable T -- matching Rust's Cell::set()/Cell::replace(),
  // which place no Copy bound on T.
  reloco::cell<std::string> c(std::string("hello"));

  const std::string old = c.replace(std::string("world"));
  EXPECT_EQ(old, "hello");

  c.set(std::string("moved-in"));

  const std::string taken = c.take();
  EXPECT_EQ(taken, "moved-in");
}

TEST(RefCellTest, TryBorrowAllowsMultipleConcurrentSharedBorrows) {
  reloco::ref_cell<std::string> rc(std::string("hello"));

  auto b1 = rc.try_borrow();
  ASSERT_TRUE(b1.has_value());
  EXPECT_EQ(**b1, "hello");

  auto b2 = rc.try_borrow();
  ASSERT_TRUE(b2.has_value());
  EXPECT_EQ(**b2, "hello");
  EXPECT_EQ((*b2)->size(), 5u); // operator-> forwards to std::string::size()
}

TEST(RefCellTest, TryBorrowMutFailsWhileASharedBorrowIsOutstanding) {
  reloco::ref_cell<std::string> rc(std::string("hello"));

  auto shared = rc.try_borrow();
  ASSERT_TRUE(shared.has_value());

  auto exclusive = rc.try_borrow_mut();
  ASSERT_FALSE(exclusive.has_value());
  EXPECT_EQ(exclusive.error(), reloco::error::busy);
}

TEST(RefCellTest, TryBorrowFailsWhileAnExclusiveBorrowIsOutstanding) {
  reloco::ref_cell<std::string> rc(std::string("hello"));

  auto exclusive = rc.try_borrow_mut();
  ASSERT_TRUE(exclusive.has_value());

  auto shared = rc.try_borrow();
  ASSERT_FALSE(shared.has_value());
  EXPECT_EQ(shared.error(), reloco::error::busy);
}

TEST(RefCellTest, TryBorrowMutFailsWhileAnotherExclusiveBorrowIsOutstanding) {
  reloco::ref_cell<std::string> rc(std::string("hello"));

  auto first = rc.try_borrow_mut();
  ASSERT_TRUE(first.has_value());

  auto second = rc.try_borrow_mut();
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), reloco::error::busy);
}

TEST(RefCellTest, MutGuardMutatesTheWrappedValue) {
  reloco::ref_cell<std::string> rc(std::string("hello"));

  {
    auto guard = rc.try_borrow_mut();
    ASSERT_TRUE(guard.has_value());
    **guard = "world";
  }

  auto reader = rc.try_borrow();
  ASSERT_TRUE(reader.has_value());
  EXPECT_EQ(**reader, "world");
}

TEST(RefCellTest, ReleasingAGuardAllowsANewBorrow) {
  reloco::ref_cell<int> rc(1);

  {
    auto exclusive = rc.try_borrow_mut();
    ASSERT_TRUE(exclusive.has_value());
    // `exclusive` releases its borrow here, at the end of the block.
  }

  auto next = rc.try_borrow_mut();
  EXPECT_TRUE(next.has_value());
}

TEST(RefCellTest, MoveConstructingAGuardTransfersTheBorrowWithoutReleasingIt) {
  reloco::ref_cell<int> rc(1);

  auto original = rc.try_borrow_mut();
  ASSERT_TRUE(original.has_value());

  auto moved = std::move(original).value();
  // The borrow is still outstanding after the move: a new exclusive
  // borrow must still fail.
  auto conflict = rc.try_borrow_mut();
  EXPECT_FALSE(conflict.has_value());

  EXPECT_EQ(*moved, 1);
}

TEST(RefCellTest, BorrowTrapsOnlyOnConflictAndSucceedsOtherwise) {
  reloco::ref_cell<int> rc(5);

  auto guard = rc.borrow();
  EXPECT_EQ(*guard, 5);
}

TEST(RefCellTest, BorrowMutTrapsOnlyOnConflictAndSucceedsOtherwise) {
  reloco::ref_cell<int> rc(5);

  auto guard = rc.borrow_mut();
  *guard = 6;
  EXPECT_EQ(*guard, 6);
}

TEST(RefCellTest, GetMutBypassesBorrowTrackingForAnExclusiveReference) {
  reloco::ref_cell<int> rc(1);
  rc.get_mut() = 42;
  EXPECT_EQ(rc.get_mut(), 42);

  // get_mut() does not affect the runtime borrow counter, so an ordinary
  // borrow is still available afterwards.
  auto borrowed = rc.try_borrow();
  EXPECT_TRUE(borrowed.has_value());
}
