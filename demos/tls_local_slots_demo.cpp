// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file tls_local_slots_demo.cpp
 * @brief Demo: `tls_local_slots<>` (`tls_slot_vector.hpp`) used as a
 * minimal, hosted TLS "provider" -- one independent, tag-selected value
 * per worker thread, exactly like `tls_provider<T, Tag>` but built out of
 * the lower-level slot-index/generation-counter machinery this file
 * exposes for kernel/RTOS ports.
 *
 * `Lock` stays the default `null_mutex`: each worker only ever touches
 * its *own* `tls_local_slots<>` storage, never another thread's, so there
 * is nothing here for a lock to protect (see `tls_slot_vector.hpp`'s own
 * doc comment -- `reloco::mutex`/`spin_lock` is only needed when some
 * other context, e.g. a reaper, reaches the *same* instance
 * concurrently).
 *
 * `tls_local_slots<>::clear_current()` must be called before a thread
 * exits (dead-thread cleanup is the caller's responsibility -- nothing in
 * `tls_slot_vector.hpp` hooks thread exit on its own, since a true
 * kernel/RTOS target may have no portable, allocation-free way to do
 * so). This demo discharges that responsibility with a plain hosted
 * trick: a function-local `thread_local` guard object whose destructor
 * runs exactly once per thread, right when that thread exits, and calls
 * `clear_current()` for us.
 */

#include <reloco/scope.hpp>
#include <reloco/tls_slot_vector.hpp>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr std::size_t kNumWorkers = 4;
constexpr std::size_t kRequestsPerWorker = 5;

struct request_counter_tag {};

using worker_tls = reloco::tls_local_slots<>; // Lock = null_mutex, Tag = void (hosted thread_local default).

// Runs `worker_tls::clear_current()` when the owning thread exits --
// `thread_local` objects are destroyed at thread exit, so declaring one
// (see `handle_requests` below) is enough to discharge the "caller must
// clean up before the thread goes away" contract without any kernel-
// specific thread-exit hook.
struct tls_cleanup_guard {
  ~tls_cleanup_guard() { worker_tls::clear_current(); }
};

void handle_requests(std::size_t worker_id) {
  thread_local tls_cleanup_guard cleanup_guard;

  for (std::size_t request = 0; request < kRequestsPerWorker; ++request) {
    auto counter = worker_tls::get_or_create<std::size_t, request_counter_tag>();
    if (!counter) {
      std::fprintf(stderr, "worker %zu: get_or_create failed\n", worker_id);
      return;
    }
    counter->get() += 1;
  }

  auto counter = worker_tls::try_find<std::size_t, request_counter_tag>();
  if (!counter) {
    std::fprintf(stderr, "worker %zu: try_find failed\n", worker_id);
    return;
  }
  std::printf("worker %zu handled %zu requests (own TLS counter, expected %zu)\n", worker_id, counter->get(),
              kRequestsPerWorker);

  // Normally left implicit (the thread_local guard above runs this at
  // thread exit regardless); called out explicitly here just so the demo
  // output shows it happening.
  worker_tls::clear_current();
  std::printf("worker %zu: TLS state cleared\n", worker_id);
}

} // namespace

int main() {
  auto scope_result = reloco::scope([&](reloco::thread_scope &s) {
    std::vector<reloco::scoped_join_handle<void>> handles;

    for (std::size_t worker_id = 0; worker_id < kNumWorkers; ++worker_id) {
      auto spawn_result = s.spawn([worker_id]() noexcept { handle_requests(worker_id); });

      if (!spawn_result) {
        std::fprintf(stderr, "failed to spawn worker %zu\n", worker_id);
        return;
      }
      handles.push_back(std::move(*spawn_result));
    }

    for (auto &handle : handles)
      std::move(handle).join();
  });

  if (!scope_result) {
    std::fprintf(stderr, "scope() failed\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
