// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file keyed_intrusive_registry_demo.cpp
 * @brief Demo: `keyed_intrusive_registry<T, OwnerKey, Tag, Lock>`
 * (`keyed_intrusive_registry.hpp`) used to back *two* independent
 * "one value per owner key" tables at once -- one keyed by a per-thread
 * identity, one keyed by a per-CPU identity -- to show that `OwnerKey`
 * is deliberately not fixed to "the current thread": reloco has no
 * opinion on what identifies a calling context, and a real kernel/RTOS
 * port typically wants both concepts side by side (e.g. `curthread` for
 * per-thread state, `PCPU_GET(cpuid)` for per-CPU state), each with its
 * own registry, `Tag`, and lifetime.
 *
 * Both registries are shared, program-wide instances reached
 * concurrently by every worker thread (unlike `tls_local_slots_demo.cpp`,
 * where each thread only ever touches its *own* private storage) --
 * `get_or_create`/`set` mutate the same underlying `intrusive_hash_table`
 * bucket chains no matter which key is involved, so both need a real
 * `Lock`, not the default `null_mutex`. The two registries deliberately
 * use *different* lock types, matching a real kernel port:
 *
 * - `thread_registry` uses `reloco::mutex` (blocking): a per-thread
 *   `OwnerKey` (`curthread` in a real port) is reached from an ordinary,
 *   preemptible context, so sleeping while contended is fine.
 * - `cpu_registry` uses `reloco::spin_lock` (never blocks/sleeps): a
 *   real per-CPU `OwnerKey` (`PCPU_GET(cpuid)`) is read/written with
 *   preemption disabled specifically so the calling context cannot
 *   migrate CPUs mid-access, and blocking while preemption is disabled
 *   is illegal on most kernels -- see `keyed_intrusive_registry.hpp`'s
 *   own "Locking" section. `get_or_create`'s built-in "unlock, allocate,
 *   relock" discipline already keeps the allocator call itself outside
 *   this spinlock; a genuine preemption-disabled port additionally needs
 *   to re-enable preemption around any `get_or_create` call that might
 *   allocate (i.e. any call outside a `try_find`-only fast path), retrying
 *   the fast path once preemption is disabled again -- this demo, being
 *   hosted and never actually disabling preemption, does not need that
 *   outer retry loop, but the inner one (already inside
 *   `get_or_create`) is exactly the same shape it would take.
 *
 * Each worker is pinned to exactly one simulated CPU (`kNumWorkers ==
 * kNumCpus`), matching how real per-CPU counters are actually kept
 * race-free in a kernel: at most one execution context ever observes a
 * given CPU id at a time (via affinity/preemption-disable), so bumping a
 * per-CPU counter needs no atomics beyond what `keyed_intrusive_registry`
 * itself already provides for the lookup/insert.
 */

#include <reloco/keyed_intrusive_registry.hpp>
#include <reloco/mutex.hpp>
#include <reloco/scope.hpp>
#include <reloco/spin_lock.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr std::size_t kNumWorkers = 4; // == kNumCpus: one worker pinned per simulated CPU, see file docs.
constexpr std::size_t kRequestsPerWorker = 5;

struct thread_owner_tag {};
struct cpu_owner_tag {};

// OwnerKey = a small integer standing in for "the calling thread's
// identity" (a real port would use `curthread`/a task pointer instead).
// `reloco::mutex`: a per-thread context is reached from an ordinary,
// preemptible caller -- see the file-level doc comment.
using thread_registry = reloco::keyed_intrusive_registry<std::size_t, std::size_t, thread_owner_tag, reloco::mutex>;

// OwnerKey = a small integer standing in for "the current CPU id" (a
// real port would use `PCPU_GET(cpuid)`/similar instead) -- deliberately
// a *different* identity concept from thread_registry above, even though
// both happen to be `std::size_t` here. `reloco::spin_lock`: a real
// per-CPU context is reached with preemption disabled, where blocking is
// illegal -- see the file-level doc comment.
using cpu_registry = reloco::keyed_intrusive_registry<std::size_t, std::size_t, cpu_owner_tag, reloco::spin_lock>;

void handle_requests(std::size_t worker_id, thread_registry &threads, cpu_registry &cpus) {
  std::size_t cpu_id = worker_id; // this worker's pinned simulated CPU, see file docs.

  for (std::size_t request = 0; request < kRequestsPerWorker; ++request) {
    auto thread_count = threads.get_or_create(worker_id, reloco::default_allocator(), std::size_t{0});
    if (!thread_count) {
      std::fprintf(stderr, "worker %zu: thread_registry::get_or_create failed\n", worker_id);
      return;
    }
    thread_count->get() += 1;

    // Simulates one interrupt/request serviced "on" this CPU -- safe
    // without an atomic increment only because this worker is the sole
    // context ever using cpu_id, exactly like the file docs describe.
    auto cpu_count = cpus.get_or_create(cpu_id, reloco::default_allocator(), std::size_t{0});
    if (!cpu_count) {
      std::fprintf(stderr, "worker %zu: cpu_registry::get_or_create failed\n", worker_id);
      return;
    }
    cpu_count->get() += 1;
  }
}

} // namespace

int main() {
  std::array<thread_registry::node *, 16> thread_buckets{};
  thread_registry threads(reloco::span<thread_registry::node *>(thread_buckets.data(), thread_buckets.size()));

  std::array<cpu_registry::node *, 16> cpu_buckets{};
  cpu_registry cpus(reloco::span<cpu_registry::node *>(cpu_buckets.data(), cpu_buckets.size()));

  auto scope_result = reloco::scope([&](reloco::thread_scope &s) {
    std::vector<reloco::scoped_join_handle<void>> handles;

    for (std::size_t worker_id = 0; worker_id < kNumWorkers; ++worker_id) {
      auto spawn_result = s.spawn([worker_id, &threads, &cpus]() noexcept {
        handle_requests(worker_id, threads, cpus);
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

  // Every worker has joined by now, so reading both registries from the
  // main thread with no further locking concern is safe: there are no
  // concurrent writers left.
  for (std::size_t worker_id = 0; worker_id < kNumWorkers; ++worker_id) {
    auto thread_count = threads.try_find(worker_id);
    auto cpu_count = cpus.try_find(worker_id);
    if (!thread_count || !cpu_count) {
      std::fprintf(stderr, "missing stats for worker/cpu %zu\n", worker_id);
      return EXIT_FAILURE;
    }
    std::printf("thread %zu handled %zu requests, cpu %zu serviced %zu requests (expected %zu each)\n", worker_id,
                thread_count->get(), worker_id, cpu_count->get(), kRequestsPerWorker);
  }

  std::printf("thread_registry size = %zu, cpu_registry size = %zu (expected %zu each)\n", threads.size(),
              cpus.size(), kNumWorkers);
  return EXIT_SUCCESS;
}
