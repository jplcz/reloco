// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file park_ping_pong.cpp
 * @brief Demo: `this_thread::current()`/`park()`/`park_timeout()`/
 * `unpark()` (Rust `std::thread::park`/`Thread::unpark` equivalents) used
 * for a direct, allocation-free handshake between two threads -- no
 * channel, mutex, or condition variable involved.
 *
 * The main thread captures the worker's `thread_handle` (via a
 * `once_lock`, since the handle only exists once the worker starts
 * running) and "pings" it a few times; the worker replies by unparking
 * the main thread back. The final round demonstrates `park_timeout()`:
 * the worker deliberately never replies, so the main thread's wait times
 * out instead of hanging forever.
 */

#include <reloco/duration.hpp>
#include <reloco/once_lock.hpp>
#include <reloco/park.hpp>
#include <reloco/thread.hpp>

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace {

constexpr int kNumPings = 3;

} // namespace

int main() {
  reloco::once_lock<reloco::thread_handle> worker_handle;
  reloco::thread_handle main_handle = reloco::this_thread::current();

  auto worker = reloco::spawn([&worker_handle, main_handle]() noexcept {
    // Publishes this thread's own handle for the main thread to unpark,
    // matching Rust's usual "hand the JoinHandle's thread() out, or park
    // until told to start" pattern.
    static_cast<void>(worker_handle.get_or_init([]() noexcept { return reloco::this_thread::current(); }));

    for (int ping = 0; ping < kNumPings; ++ping) {
      reloco::this_thread::park();
      std::printf("worker: received ping %d, replying\n", ping);
      main_handle.unpark();
    }

    // Final round: deliberately do not reply, so the main thread's
    // park_timeout() below observes a timeout instead of a reply.
    reloco::this_thread::park();
    std::printf("worker: received final ping, not replying (demonstrates park_timeout on the main thread)\n");
  });

  if (!worker) {
    std::fprintf(stderr, "spawn(worker) failed\n");
    return EXIT_FAILURE;
  }

  // Wait for the worker to publish its handle. A tight busy-loop is fine
  // here purely because it only ever runs once, for a very short time, in
  // a demo -- real code would use a channel or another once_lock-backed
  // signal instead.
  const reloco::thread_handle *handle_ptr = nullptr;
  while (handle_ptr == nullptr)
    handle_ptr = worker_handle.get_mut();

  for (int ping = 0; ping < kNumPings; ++ping) {
    std::printf("main: sending ping %d\n", ping);
    handle_ptr->unpark();
    reloco::this_thread::park();
    std::printf("main: received reply for ping %d\n", ping);
  }

  std::printf("main: sending final ping (worker will not reply)\n");
  handle_ptr->unpark();
  bool replied = reloco::this_thread::park_timeout(reloco::duration::from_millis(200));
  std::printf("main: park_timeout() returned %s (expected false)\n", replied ? "true" : "false");

  std::move(*worker).join();
  return EXIT_SUCCESS;
}
