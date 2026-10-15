// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/**
 * @file per_cpu_provider.hpp
 * @brief `per_cpu_provider<T, Tag>`: one zero-filled `T` per CPU, reached only from atomic context.
 *
 * The per-CPU counterpart of `tls_provider.hpp`. Where `tls_provider<T, Tag>::get()` is per *thread* and may fail
 * (allocation), `per_cpu_provider<T, Tag>::get()` is per *CPU*, cannot fail, and may only be called where the
 * caller cannot migrate (preemption or interrupts off: `RELOCO_ATOMIC_ONLY`, checked by `reloco-atomic-context`).
 * The `T` of every CPU is **zero-filled** when the provider creates it; nothing runs afterwards except what the caller
 * does (see `per_cpu_slot.hpp`), and nothing ever destroys it: a CPU does not exit.
 *
 * One provider per *module* (one `Tag`), whose `T` is the module's block of per-CPU cells; that keeps the number of
 * per-CPU areas the OS must reserve proportional to the number of modules, not of variables.
 *
 * Models (`RELOCO_PER_CPU_MODEL`):
 * - **`RELOCO_PER_CPU_MODEL_SINGLE`** (default): one global instance, "one CPU". For hosted single-CPU builds.
 * - **`RELOCO_PER_CPU_MODEL_THREAD_LOCAL`**: one instance per *thread*, so a thread plays the part of a CPU. For hosted
 *   tests of per-CPU code under real concurrency; `cpu_count()` is 1 and `get_cpu(0)` is the calling thread's block.
 * - **`RELOCO_PER_CPU_MODEL_OS`**: includes `detail/porting/per_cpu_provider.hpp`, which defines
 *   `per_cpu_provider<T, Tag>` for the target kernel (see `detail/porting/per_cpu_provider.template.hpp`, a FreeBSD
 *   kernel sketch). Supply it through `JPLCZ_RELOCO_PORTING_HEADERS`, which also defines the model.
 *
 * docs/per-cpu-slot.md has the guide.
 *
 * @code
 * struct net_stats { std::uint64_t rx; std::uint64_t tx; };      // zero is a valid state
 * struct net_tag {};
 *
 * void count_rx() noexcept RELOCO_ATOMIC_ONLY {                  // caller disabled preemption (critical_enter(9) on FreeBSD)
 *   per_cpu_provider<net_stats, net_tag>::get().rx++;           // this CPU's block; no lock, no atomic
 * }
 * @endcode
 */

#include "atomic_context.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "lifetime.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>

/** @name Per-CPU Storage Models */
/// @{
#define RELOCO_PER_CPU_MODEL_SINGLE 0       ///< One global instance (a single "CPU").
#define RELOCO_PER_CPU_MODEL_THREAD_LOCAL 1 ///< One instance per thread: a thread stands in for a CPU (hosted tests).
#define RELOCO_PER_CPU_MODEL_OS 2           ///< Target-specific per-CPU areas (`detail/porting/per_cpu_provider.hpp`).
/// @}

#if !defined(RELOCO_PER_CPU_MODEL)
#define RELOCO_PER_CPU_MODEL RELOCO_PER_CPU_MODEL_SINGLE
#endif

namespace reloco {

#if (RELOCO_PER_CPU_MODEL == RELOCO_PER_CPU_MODEL_SINGLE)

/** @brief Single-CPU model: every call returns the one global block. @tparam T zero-valid. @tparam Tag distinguishes providers. */
template <typename T, typename Tag> struct per_cpu_provider {
  static_assert(std::is_standard_layout_v<T>, "per_cpu_provider<T>: T must be standard layout (a block of cells)");

  /** @brief The current CPU's block. Atomic context only: the caller must not migrate while holding the reference. */
  [[nodiscard]] RELOCO_ATOMIC_ONLY static T &get() noexcept { return instance_; }

  /** @brief Number of CPUs the provider has blocks for. */
  [[nodiscard]] static std::size_t cpu_count() noexcept { return 1; }

  /** @brief The block of CPU `cpu` (for initialisation and aggregation); `error::out_of_bounds` past `cpu_count()`. */
  [[nodiscard]] static result<std::reference_wrapper<T>> get_cpu(std::size_t cpu) noexcept {
    if (cpu >= 1) {
      return unexpected(error::out_of_bounds);
    }
    return std::ref(instance_);
  }

private:
  // NOLINTNEXTLINE(reloco-mutable-global): the single-CPU model's one block; access is atomic-context only.
  static inline T instance_ = {};
};

#elif (RELOCO_PER_CPU_MODEL == RELOCO_PER_CPU_MODEL_THREAD_LOCAL)

/** @brief Thread-as-CPU model: each thread has its own zero-filled block. @tparam T zero-valid. @tparam Tag distinguishes providers. */
template <typename T, typename Tag> struct per_cpu_provider {
  static_assert(std::is_standard_layout_v<T>, "per_cpu_provider<T>: T must be standard layout (a block of cells)");

  /** @brief The calling thread's block. */
  [[nodiscard]] RELOCO_ATOMIC_ONLY static T &get() noexcept { return instance_; }

  /** @brief One: only the calling thread's block is reachable. */
  [[nodiscard]] static std::size_t cpu_count() noexcept { return 1; }

  /** @brief The calling thread's block for `cpu == 0`; `error::out_of_bounds` otherwise. */
  [[nodiscard]] static result<std::reference_wrapper<T>> get_cpu(std::size_t cpu) noexcept {
    if (cpu >= 1) {
      return unexpected(error::out_of_bounds);
    }
    return std::ref(instance_);
  }

private:
  // NOLINTNEXTLINE(reloco-mutable-global): the hosted stand-in for the CPU-local block; it is thread-local.
  static inline thread_local T instance_ = {};
};

#elif (RELOCO_PER_CPU_MODEL == RELOCO_PER_CPU_MODEL_OS)

} // namespace reloco

// See this file's top-level docs and `detail/porting/per_cpu_provider.template.hpp` for the exact
// `per_cpu_provider<T, Tag>` shape this must define, including opening its own `namespace reloco { ... }`.
#include "detail/porting/per_cpu_provider.hpp"

namespace reloco {

#else
#error "RELOCO_PER_CPU_MODEL must be RELOCO_PER_CPU_MODEL_SINGLE, _THREAD_LOCAL or _OS"
#endif

} // namespace reloco
