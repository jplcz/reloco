// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/cell.hpp>
#include <reloco/rc.hpp>
#include <reloco/send_sync.hpp>
#include <reloco/shared_ptr.hpp>

namespace {

struct widget {
  int value;
};

// Explicitly not thread-transferable/-shareable at all, to exercise
// is_send<shared_ptr<T>>/is_sync<shared_ptr<T>> requiring both of T.
struct not_send_or_sync {};

} // namespace

template <> struct reloco::is_send<not_send_or_sync> : std::false_type {};
template <> struct reloco::is_sync<not_send_or_sync> : std::false_type {};

TEST(SendSyncTest, OrdinaryTypesAreSendAndSyncByDefault) {
  EXPECT_TRUE(reloco::is_send_v<int>);
  EXPECT_TRUE(reloco::is_sync_v<int>);
  EXPECT_TRUE(reloco::is_send_v<widget>);
  EXPECT_TRUE(reloco::is_sync_v<widget>);
}

TEST(SendSyncTest, RcIsNeitherSendNorSync) {
  EXPECT_FALSE(reloco::is_send_v<reloco::rc<widget>>);
  EXPECT_FALSE(reloco::is_sync_v<reloco::rc<widget>>);
}

TEST(SendSyncTest, WeakRcIsNeitherSendNorSync) {
  EXPECT_FALSE(reloco::is_send_v<reloco::weak_rc<widget>>);
  EXPECT_FALSE(reloco::is_sync_v<reloco::weak_rc<widget>>);
}

TEST(SendSyncTest, CellIsSendButNeverSync) {
  EXPECT_TRUE(reloco::is_send_v<reloco::cell<int>>);
  EXPECT_FALSE(reloco::is_sync_v<reloco::cell<int>>);
}

TEST(SendSyncTest, CellForwardsSendToItsHeldType) { EXPECT_FALSE(reloco::is_send_v<reloco::cell<reloco::rc<widget>>>); }

TEST(SendSyncTest, RefCellIsSendButNeverSync) {
  EXPECT_TRUE(reloco::is_send_v<reloco::ref_cell<int>>);
  EXPECT_FALSE(reloco::is_sync_v<reloco::ref_cell<int>>);
}

TEST(SendSyncTest, RefCellForwardsSendToItsHeldType) {
  EXPECT_FALSE(reloco::is_send_v<reloco::ref_cell<reloco::rc<widget>>>);
}

TEST(SendSyncTest, SharedPtrIsSendAndSyncWhenHeldTypeIsBoth) {
  EXPECT_TRUE(reloco::is_send_v<reloco::shared_ptr<widget>>);
  EXPECT_TRUE(reloco::is_sync_v<reloco::shared_ptr<widget>>);
  EXPECT_TRUE(reloco::is_send_v<reloco::weak_ptr<widget>>);
  EXPECT_TRUE(reloco::is_sync_v<reloco::weak_ptr<widget>>);
}

TEST(SendSyncTest, SharedPtrRequiresBothSendAndSyncFromHeldType) {
  EXPECT_FALSE(reloco::is_send_v<reloco::shared_ptr<not_send_or_sync>>);
  EXPECT_FALSE(reloco::is_sync_v<reloco::shared_ptr<not_send_or_sync>>);
  // rc<widget> is itself neither Send nor Sync, so sharing it inside a
  // shared_ptr does not launder that away.
  EXPECT_FALSE(reloco::is_send_v<reloco::shared_ptr<reloco::rc<widget>>>);
}

#if RELOCO_CXX20
TEST(SendSyncTest, ConceptsMatchTraits) {
  static_assert(reloco::sendable<int>);
  static_assert(reloco::syncable<int>);
  static_assert(!reloco::sendable<reloco::rc<widget>>);
  static_assert(!reloco::syncable<reloco::rc<widget>>);
  static_assert(reloco::sendable<reloco::cell<int>>);
  static_assert(!reloco::syncable<reloco::cell<int>>);
}
#endif
