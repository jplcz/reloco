// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file once_lock_config.cpp
 * @brief Demo: `once_lock<T>::get_or_init` (Rust `std::sync::OnceLock<T>`
 * equivalent) used to lazily initialize a shared, expensive-to-compute
 * value exactly once, no matter how many threads race to be the first
 * reader.
 *
 * Every worker calls `get_or_init` with the *same* closure; only one
 * thread's closure ever actually runs (the "expensive" work is simulated
 * with `this_thread::sleep_for`), and every thread -- including every
 * loser of that race -- observes the identical, fully-initialized value.
 */

#include <reloco/duration.hpp>
#include <reloco/once_lock.hpp>
#include <reloco/park.hpp>
#include <reloco/scope.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kNumWorkers = 8;

} // namespace

int main() {
  reloco::once_lock<std::string> config;
  std::atomic<int> initializer_calls{0};

  auto scope_result = reloco::scope([&](reloco::thread_scope &s) {
    // Kept alive for the whole loop (see scoped_workers.cpp for why):
    // every worker below must actually be racing to reach get_or_init()
    // concurrently for this demo to mean anything.
    std::vector<reloco::scoped_join_handle<void>> handles;

    for (std::size_t worker_id = 0; worker_id < kNumWorkers; ++worker_id) {
      auto spawn_result = s.spawn([&config, &initializer_calls, worker_id]() noexcept {
        const std::string &value = config.get_or_init([&initializer_calls]() noexcept -> std::string {
          initializer_calls.fetch_add(1, std::memory_order_relaxed);
          // Simulates expensive one-time setup (e.g. parsing a config
          // file); every other worker's get_or_init() call blocks here
          // instead of re-running this closure.
          reloco::this_thread::sleep_for(reloco::duration::from_millis(30));
          return "loaded-config-v1";
        });

        std::printf("worker %zu sees config = \"%s\"\n", worker_id, value.c_str());
      });

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

  std::printf("initializer ran %d time(s) (expected 1)\n", initializer_calls.load(std::memory_order_relaxed));
  return EXIT_SUCCESS;
}
