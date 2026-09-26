// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <atomic>
#include <gtest/gtest.h>
#include <reloco/channel.hpp>
#include <reloco/duration.hpp>
#include <reloco/park.hpp>
#include <reloco/send_sync.hpp>
#include <reloco/thread.hpp>
#include <vector>

TEST(ChannelTest, SendThenRecvRoundTrips) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto &[tx, rx] = *ends;

  ASSERT_TRUE(tx.try_send(42));
  auto value = rx.recv();
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, 42);
}

TEST(ChannelTest, PreservesFifoOrder) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto &[tx, rx] = *ends;

  ASSERT_TRUE(tx.try_send(1));
  ASSERT_TRUE(tx.try_send(2));
  ASSERT_TRUE(tx.try_send(3));

  {
    auto v = rx.recv();
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, 1);
  }
  {
    auto v = rx.recv();
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, 2);
  }
  {
    auto v = rx.recv();
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, 3);
  }
}

TEST(ChannelTest, TryRecvFailsWithTryAgainWhenEmptyButSendersRemain) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto &[tx, rx] = *ends;
  (void)tx;

  auto value = rx.try_recv();
  ASSERT_FALSE(value);
  EXPECT_EQ(value.error(), reloco::error::try_again);
}

TEST(ChannelTest, RecvFailsWithContainerEmptyAfterAllSendersDropped) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto [tx, rx] = std::move(*ends);
  {
    auto dropped = std::move(tx);
  }

  auto value = rx.recv();
  ASSERT_FALSE(value);
  EXPECT_EQ(value.error(), reloco::error::container_empty);
}

TEST(ChannelTest, RecvTimeoutReturnsValueImmediatelyWhenAlreadyQueued) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto &[tx, rx] = *ends;

  ASSERT_TRUE(tx.try_send(7));
  auto value = rx.recv_timeout(reloco::duration::from_secs(5));
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, 7);
}

TEST(ChannelTest, RecvTimeoutFailsWithTimedOutWhenNothingIsSent) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto &[tx, rx] = *ends;
  (void)tx;

  auto value = rx.recv_timeout(reloco::duration::from_millis(20));
  ASSERT_FALSE(value);
  EXPECT_EQ(value.error(), reloco::error::timed_out);
}

TEST(ChannelTest, RecvTimeoutFailsWithContainerEmptyAfterAllSendersDropped) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto [tx, rx] = std::move(*ends);
  {
    auto dropped = std::move(tx);
  }

  auto value = rx.recv_timeout(reloco::duration::from_secs(5));
  ASSERT_FALSE(value);
  EXPECT_EQ(value.error(), reloco::error::container_empty);
}

TEST(ChannelTest, RecvTimeoutReturnsValueWhenSentBeforeTimeoutElapses) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto [tx, rx] = std::move(*ends);

  auto handle = reloco::spawn([tx = std::move(tx)]() mutable noexcept {
    reloco::this_thread::sleep_for(reloco::duration::from_millis(10));
    static_cast<void>(tx.try_send(99));
  });
  ASSERT_TRUE(handle);

  auto value = rx.recv_timeout(reloco::duration::from_secs(5));
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, 99);
  std::move(*handle).join();
}

TEST(ChannelTest, TrySendFailsWithInvalidStateAfterReceiverDropped) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto [tx, rx] = std::move(*ends);
  {
    auto dropped = std::move(rx);
  }

  auto sent = tx.try_send(1);
  ASSERT_FALSE(sent);
  EXPECT_EQ(sent.error(), reloco::error::invalid_state);
}

TEST(ChannelTest, SenderClonesShareOneQueue) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto &[tx, rx] = *ends;

  reloco::sender<int> tx2(tx); // NOLINT(performance-unnecessary-copy-initialization)
  ASSERT_TRUE(tx.try_send(1));
  ASSERT_TRUE(tx2.try_send(2));

  {
    auto v = rx.recv();
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, 1);
  }
  {
    auto v = rx.recv();
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, 2);
  }
}

TEST(ChannelTest, ReceiverObservesDisconnectOnlyAfterEveryCloneDrops) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto [tx, rx] = std::move(*ends);

  reloco::sender<int> tx2(tx); // NOLINT(performance-unnecessary-copy-initialization)
  {
    auto dropped = std::move(tx);
  }

  // tx2 still alive: the channel is not yet disconnected.
  auto still_pending = rx.try_recv();
  ASSERT_FALSE(still_pending);
  EXPECT_EQ(still_pending.error(), reloco::error::try_again);

  {
    auto dropped = std::move(tx2);
  }

  auto after_disconnect = rx.try_recv();
  ASSERT_FALSE(after_disconnect);
  EXPECT_EQ(after_disconnect.error(), reloco::error::container_empty);
}

TEST(ChannelTest, WorksAcrossSpawnedThreads) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto [tx, rx] = std::move(*ends);

  auto handle = reloco::spawn([tx = std::move(tx)]() mutable noexcept {
    for (int i = 0; i < 5; ++i)
      static_cast<void>(tx.try_send(i));
  });
  ASSERT_TRUE(handle);

  for (int i = 0; i < 5; ++i) {
    auto value = rx.recv();
    ASSERT_TRUE(value);
    EXPECT_EQ(*value, i);
  }

  std::move(*handle).join();
  auto disconnected = rx.recv();
  ASSERT_FALSE(disconnected);
  EXPECT_EQ(disconnected.error(), reloco::error::container_empty);
}

TEST(ChannelTest, SenderIsSendAndSyncWhenTIsSend) {
  EXPECT_TRUE(reloco::is_send_v<reloco::sender<int>>);
  EXPECT_TRUE(reloco::is_sync_v<reloco::sender<int>>);
}

TEST(ChannelTest, ReceiverIsSendButNeverSync) {
  EXPECT_TRUE(reloco::is_send_v<reloco::receiver<int>>);
  EXPECT_FALSE(reloco::is_sync_v<reloco::receiver<int>>);
}

TEST(ChannelTest, RangeForConsumesEveryValueUntilSendersDrop) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto [tx, rx] = std::move(*ends);

  auto handle = reloco::spawn([tx = std::move(tx)]() mutable noexcept {
    for (int i = 0; i < 5; ++i)
      static_cast<void>(tx.try_send(i));
    // tx (and every clone) drops here, ending the range-for below.
  });
  ASSERT_TRUE(handle);

  std::vector<int> received;
  for (int value : rx)
    received.push_back(value);

  std::move(*handle).join();
  EXPECT_EQ(received, (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(ChannelTest, TryIterDrainsOnlyWhatIsAlreadyQueuedWithoutBlocking) {
  auto ends = reloco::channel<int>();
  ASSERT_TRUE(ends);
  auto &[tx, rx] = *ends;

  ASSERT_TRUE(tx.try_send(1));
  ASSERT_TRUE(tx.try_send(2));
  ASSERT_TRUE(tx.try_send(3));

  std::vector<int> received;
  for (int value : rx.try_iter())
    received.push_back(value);
  EXPECT_EQ(received, (std::vector<int>{1, 2, 3}));

  // The queue is now empty but the sender is still alive: try_iter() must
  // stop without blocking rather than waiting for a value that may never
  // come.
  received.clear();
  for (int value : rx.try_iter())
    received.push_back(value);
  EXPECT_TRUE(received.empty());
}

TEST(SyncChannelTest, SendThenRecvRoundTrips) {
  auto ends = reloco::sync_channel<int>(2);
  ASSERT_TRUE(ends);
  auto &[tx, rx] = *ends;

  ASSERT_TRUE(tx.send(42));
  auto value = rx.recv();
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, 42);
}

TEST(SyncChannelTest, TrySendFailsWithCapacityExceededWhenFull) {
  auto ends = reloco::sync_channel<int>(2);
  ASSERT_TRUE(ends);
  auto &[tx, rx] = *ends;

  ASSERT_TRUE(tx.try_send(1));
  ASSERT_TRUE(tx.try_send(2));
  auto overflow = tx.try_send(3);
  ASSERT_FALSE(overflow);
  EXPECT_EQ(overflow.error(), reloco::error::capacity_exceeded);

  // Draining one slot makes room for another try_send().
  auto v = rx.recv();
  ASSERT_TRUE(v);
  EXPECT_EQ(*v, 1);
  ASSERT_TRUE(tx.try_send(3));
}

TEST(SyncChannelTest, SendBlocksUntilRoomFreesUp) {
  auto ends = reloco::sync_channel<int>(1);
  ASSERT_TRUE(ends);
  auto [tx, rx] = std::move(*ends);

  ASSERT_TRUE(tx.send(1)); // Fills the only slot without blocking.

  auto handle = reloco::spawn([tx = std::move(tx)]() mutable noexcept {
    static_cast<void>(tx.send(2)); // Must block until the slot below is drained.
  });
  ASSERT_TRUE(handle);

  reloco::this_thread::sleep_for(reloco::duration::from_millis(20));

  auto first = rx.recv();
  ASSERT_TRUE(first);
  EXPECT_EQ(*first, 1);

  auto second = rx.recv();
  ASSERT_TRUE(second);
  EXPECT_EQ(*second, 2);

  std::move(*handle).join();
}

TEST(SyncChannelTest, SendFailsWithInvalidStateAfterReceiverDropped) {
  auto ends = reloco::sync_channel<int>(1);
  ASSERT_TRUE(ends);
  auto [tx, rx] = std::move(*ends);
  {
    auto dropped = std::move(rx);
  }

  auto sent = tx.send(1);
  ASSERT_FALSE(sent);
  EXPECT_EQ(sent.error(), reloco::error::invalid_state);
}

TEST(SyncChannelTest, SendUnblocksWithInvalidStateWhenReceiverDroppedWhileWaiting) {
  auto ends = reloco::sync_channel<int>(1);
  ASSERT_TRUE(ends);
  auto [tx, rx] = std::move(*ends);

  ASSERT_TRUE(tx.send(1)); // Fills the only slot.

  reloco::result<void> send_result = reloco::unexpected(reloco::error::not_initialized);
  auto handle = reloco::spawn([tx = std::move(tx), &send_result]() mutable noexcept {
    send_result = tx.send(2); // Blocks for room that will never come.
  });
  ASSERT_TRUE(handle);

  reloco::this_thread::sleep_for(reloco::duration::from_millis(20));
  {
    auto dropped = std::move(rx);
  } // Wakes the blocked send() above with invalid_state.

  std::move(*handle).join();
  ASSERT_FALSE(send_result);
  EXPECT_EQ(send_result.error(), reloco::error::invalid_state);
}

TEST(SyncChannelTest, RendezvousSendBlocksUntilConsumed) {
  auto ends = reloco::sync_channel<int>(0);
  ASSERT_TRUE(ends);
  auto [tx, rx] = std::move(*ends);

  std::atomic<bool> send_returned{false};
  auto handle = reloco::spawn([tx = std::move(tx), &send_returned]() mutable noexcept {
    static_cast<void>(tx.send(7));
    send_returned.store(true, std::memory_order_release);
  });
  ASSERT_TRUE(handle);

  // Give send() a chance to run: it must still be blocked (nothing has
  // called recv() yet), since a rendezvous channel only unblocks send()
  // once the value has actually been received.
  reloco::this_thread::sleep_for(reloco::duration::from_millis(20));
  EXPECT_FALSE(send_returned.load(std::memory_order_acquire));

  auto value = rx.recv();
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, 7);

  std::move(*handle).join();
  EXPECT_TRUE(send_returned.load(std::memory_order_acquire));
}

TEST(SyncChannelTest, RendezvousTrySendSucceedsOnlyWhenReceiverIsWaiting) {
  auto ends = reloco::sync_channel<int>(0);
  ASSERT_TRUE(ends);
  auto [tx, rx] = std::move(*ends);

  auto too_early = tx.try_send(1);
  ASSERT_FALSE(too_early);
  EXPECT_EQ(too_early.error(), reloco::error::capacity_exceeded);

  int received_value = 0;
  bool received_ok = false;
  auto handle = reloco::spawn([rx = std::move(rx), &received_value, &received_ok]() mutable noexcept {
    auto value = rx.recv();
    received_ok = static_cast<bool>(value);
    if (value)
      received_value = *value;
  });
  ASSERT_TRUE(handle);
  reloco::this_thread::sleep_for(reloco::duration::from_millis(20)); // Let recv() park.

  auto sent = tx.try_send(2);
  ASSERT_TRUE(sent);

  std::move(*handle).join();
  ASSERT_TRUE(received_ok);
  EXPECT_EQ(received_value, 2);
}

TEST(SyncChannelTest, SyncSenderClonesShareOneQueue) {
  auto ends = reloco::sync_channel<int>(4);
  ASSERT_TRUE(ends);
  auto &[tx, rx] = *ends;

  reloco::sync_sender<int> tx2(tx); // NOLINT(performance-unnecessary-copy-initialization)
  ASSERT_TRUE(tx.try_send(1));
  ASSERT_TRUE(tx2.try_send(2));

  {
    auto v = rx.recv();
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, 1);
  }
  {
    auto v = rx.recv();
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, 2);
  }
}

TEST(SyncChannelTest, SyncSenderIsSendAndSyncWhenTIsSend) {
  EXPECT_TRUE(reloco::is_send_v<reloco::sync_sender<int>>);
  EXPECT_TRUE(reloco::is_sync_v<reloco::sync_sender<int>>);
}
