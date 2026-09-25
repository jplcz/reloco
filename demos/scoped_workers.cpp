// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file scoped_workers.cpp
 * @brief Demo: `scope()` + `guarded_mutex<T>` + `barrier` used together to
 * run a fixed pool of worker threads that borrow stack-local state
 * directly (no `shared_ptr`/heap capture needed) and synchronize in
 * lock-step "waves" using a reusable `barrier`.
 *
 * Every worker increments a shared counter (protected by
 * `guarded_mutex<std::uint64_t>`, Rust's `Mutex<T>` equivalent) once per
 * wave, then calls `barrier::wait()`; the barrier's designated "leader"
 * thread for that wave prints a progress line. `scope()` guarantees every
 * worker has fully finished before it returns, so the stack-local
 * `barrier`/`guarded_mutex` referenced by every closure are provably still
 * alive for the whole run.
 */

#include <reloco/barrier.hpp>
#include <reloco/guarded_mutex.hpp>
#include <reloco/scope.hpp>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr std::size_t kNumWorkers = 4;
constexpr std::size_t kNumWaves = 3;

} // namespace

int main() {
  reloco::guarded_mutex<std::uint64_t> counter(0);
  reloco::barrier wave_barrier(kNumWorkers);

  auto scope_result = reloco::scope([&](reloco::thread_scope &s) {
    // Handles must be kept alive for the whole loop, not just their own
    // iteration: scoped_join_handle::~scoped_join_handle() joins if not
    // already joined, so a handle destroyed at the end of each loop
    // iteration would join that worker before the next one is even
    // spawned, serializing every "concurrent" worker.
    std::vector<reloco::scoped_join_handle<void>> handles;

    for (std::size_t worker_id = 0; worker_id < kNumWorkers; ++worker_id) {
      auto spawn_result = s.spawn([&counter, &wave_barrier]() noexcept {
        for (std::size_t wave = 0; wave < kNumWaves; ++wave) {
          {
            auto guard = counter.lock();
            *guard += 1;
          }

          // Exactly one thread per wave observes true here -- useful for
          // once-per-wave leader work without any extra synchronization.
          if (wave_barrier.wait()) {
            auto guard = counter.lock();
            std::printf("wave %zu complete, counter = %llu\n", wave, static_cast<unsigned long long>(*guard));
          }
        }
      });

      if (!spawn_result) {
        std::fprintf(stderr, "failed to spawn worker %zu\n", worker_id);
        return;
      }

      handles.push_back(std::move(*spawn_result));
    }

    // scope() itself would still wait for every worker even without this
    // loop (see ~thread_scope()), but joining explicitly here also
    // surfaces each worker's completion in program order.
    for (auto &handle : handles)
      std::move(handle).join();
  });

  if (!scope_result) {
    std::fprintf(stderr, "scope() failed\n");
    return EXIT_FAILURE;
  }

  auto final_guard = counter.lock();
  std::printf("final counter = %llu (expected %llu)\n", static_cast<unsigned long long>(*final_guard),
              static_cast<unsigned long long>(kNumWorkers * kNumWaves));
  return EXIT_SUCCESS;
}
