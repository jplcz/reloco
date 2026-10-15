// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file per_cpu_provider.template.hpp
 * @brief Documentation-only scaffold for `RELOCO_PER_CPU_MODEL_OS`.
 *
 * Never `#include`d by anything -- copy this file to `detail/porting/per_cpu_provider.hpp` (dropping `.template`),
 * fill it in for your target, and define `RELOCO_PER_CPU_MODEL` to `RELOCO_PER_CPU_MODEL_OS`, or point the
 * `JPLCZ_RELOCO_PORTING_HEADERS` CMake variable at a directory containing your finished `per_cpu_provider.hpp`.
 *
 * Sketches, loosely, a **FreeBSD kernel** port. `DPCPU_DEFINE_STATIC(9)` needs a literal identifier, so it cannot be
 * stamped out per `<T, Tag>` from a template. The dynamic counterpart can: `uma_zalloc_pcpu(9)` returns a *base*
 * pointer for a zero-filled per-CPU array, and `zpcpu_get(9)` / `zpcpu_get_cpu(9)` turn it into the current / a given
 * CPU's copy. The base is created lazily once per `<T, Tag>` (a real port would do it from a `SYSINIT(9)` or the
 * module's load handler, not on first use). Not compiled by this repository, which targets hosted userspace.
 *
 * The contract to keep:
 * - `get()` is callable in atomic context only (the caller has disabled preemption, e.g. `critical_enter(9)`), so the
 *   CPU cannot change under the returned reference. It never allocates and never fails.
 * - the block is zero-filled; `T` must be valid when all-zero.
 * - nothing is ever destroyed.
 */

#include <sys/param.h>

#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <vm/uma.h>

namespace reloco {

template <typename T, typename Tag> struct per_cpu_provider {
  /** The calling CPU's block. Atomic context only. */
  [[nodiscard]] RELOCO_ATOMIC_ONLY static T &get() noexcept {
    return *static_cast<T *>(zpcpu_get(base()));
  }

  [[nodiscard]] static std::size_t cpu_count() noexcept { return static_cast<std::size_t>(mp_ncpus); }

  /** Another CPU's block, for bring-up and reporting code that is not on that CPU. */
  [[nodiscard]] static result<std::reference_wrapper<T>> get_cpu(std::size_t cpu) noexcept {
    if (cpu >= cpu_count()) {
      return unexpected(error::out_of_bounds);
    }
    return std::ref(*static_cast<T *>(zpcpu_get_cpu(base(), static_cast<int>(cpu))));
  }

private:
  static void *base() noexcept {
    static uma_zone_t zone = uma_zcreate("reloco_pcpu", sizeof(T), nullptr, nullptr, nullptr, nullptr,
                                         UMA_ALIGN_PTR, UMA_ZONE_PCPU);
    static void *block = uma_zalloc_pcpu(zone, M_WAITOK | M_ZERO); // zero-filled on every CPU
    return block;
  }
};

} // namespace reloco
