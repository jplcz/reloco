// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <atomic>
#include <gtest/gtest.h>
#include <reloco/channel.hpp>
#include <reloco/send_sync.hpp>
#include <reloco/thread.hpp>

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
