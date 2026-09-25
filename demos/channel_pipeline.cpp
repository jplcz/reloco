// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file channel_pipeline.cpp
 * @brief Demo: `channel<T>()` (Rust `std::sync::mpsc` equivalent) used to
 * hand work items from a producer thread to a consumer running on the
 * main thread, plus `receiver<T>::recv_timeout()` to show how a consumer
 * can give up waiting instead of blocking forever.
 *
 * The producer sends a handful of items with a small delay between each
 * (via `this_thread::sleep_for`, itself futex-backed -- see
 * `docs/futex.md`) and then drops its `sender<T>`, which unblocks the
 * consumer's final `recv()` with `error::container_empty` (every sender
 * gone) instead of hanging.
 */

#include <reloco/channel.hpp>
#include <reloco/duration.hpp>
#include <reloco/park.hpp>
#include <reloco/thread.hpp>

#include <cstdio>
#include <cstdlib>
#include <utility>

int main() {
  auto channel_result = reloco::channel<int>();
  if (!channel_result) {
    std::fprintf(stderr, "channel() failed\n");
    return EXIT_FAILURE;
  }

  auto [tx, rx] = std::move(*channel_result);

  auto producer = reloco::spawn([sender = std::move(tx)]() mutable noexcept {
    for (int item = 1; item <= 5; ++item) {
      auto send_result = sender.try_send(item);
      if (!send_result) {
        std::fprintf(stderr, "producer: try_send failed for item %d\n", item);
        return;
      }
      reloco::this_thread::sleep_for(reloco::duration::from_millis(20));
    }
    // sender's destructor runs here, dropping the last (only) sender clone
    // and unblocking the consumer's final recv()/recv_timeout() call.
  });

  if (!producer) {
    std::fprintf(stderr, "spawn(producer) failed\n");
    return EXIT_FAILURE;
  }

  // Demonstrates recv_timeout(): the first item may not have arrived yet,
  // but 200ms is generously longer than the producer's 20ms send interval.
  auto first = rx.recv_timeout(reloco::duration::from_millis(200));
  if (!first) {
    std::fprintf(stderr, "recv_timeout() unexpectedly failed\n");
    return EXIT_FAILURE;
  }
  std::printf("received (via recv_timeout): %d\n", *first);

  // Remaining items via plain blocking recv(), until every sender has
  // been dropped (error::container_empty).
  for (;;) {
    auto item = rx.recv();
    if (!item)
      break;
    std::printf("received: %d\n", *item);
  }

  std::move(*producer).join();
  std::printf("channel drained, producer joined\n");
  return EXIT_SUCCESS;
}
