// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/seqlock.hpp>

#include <cstdint>

TEST(GuardedSeqlockTest, WriterPublishesPayloadToReaders) {
  reloco::guarded_seqlock<uint64_t> cell;

  {
    auto writer = cell.write_lock();
    *writer = 42;
  }

  EXPECT_EQ(cell.read(), 42u);
}

TEST(GuardedSeqlockTest, ReadTransactionRejectsSnapshotOverlappingWrite) {
  reloco::guarded_seqlock<uint64_t> cell;
  {
    auto writer = cell.write_lock();
    *writer = 10;
  }

  reloco::guarded_seqlock<uint64_t>::read_tx read_before_write(cell);
  ASSERT_TRUE(read_before_write.verify());
  EXPECT_EQ(read_before_write.extract(), 10u);

  {
    auto writer = cell.write_lock();
    *writer = 20;
    auto read_during_write = reloco::guarded_seqlock<uint64_t>::read_tx(cell);
    EXPECT_FALSE(read_during_write.verify());

    auto second_writer = cell.try_write_lock();
    ASSERT_FALSE(second_writer);
    EXPECT_EQ(second_writer.error(), reloco::error::busy);
  }

  EXPECT_FALSE(read_before_write.verify());
  EXPECT_EQ(cell.read(), 20u);
}

TEST(GuardedSeqlockTest, MovingWriterGuardRetainsLockUntilDestinationDies) {
  reloco::guarded_seqlock<uint64_t> cell;
  {
    auto source = cell.write_lock();
    auto destination = std::move(source);
    *destination = 7;

    auto competing_writer = cell.try_write_lock();
    EXPECT_FALSE(competing_writer);
    EXPECT_EQ(competing_writer.error(), reloco::error::busy);
  }

  EXPECT_EQ(cell.read(), 7u);
}
