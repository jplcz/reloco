// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/config.hpp
 * @brief Language gate, `RELOCO_KERNEL_CHECKS` level and the per-level inline namespace of the kernel framework.
 *
 * "Kernel" here means a compute kernel (a hot numeric loop), not an operating system kernel; it is unrelated to the
 * `RELOCO_KERNEL` freestanding-build macro.
 *
 * The kernel headers require C++20 or newer (the rest of reloco stays C++17-compatible).
 *
 * `RELOCO_KERNEL_CHECKS` selects how much the *inner loop* re-verifies; the outer boundary (building views and
 * bindings) is always checked regardless of the level:
 *  - `0` (default): no inner checks; accessors lower to plain loads and stores.
 *  - `1`: every minted proof (index, block, window) asserts its range invariant.
 *  - `2`: level 1 plus the heavier token/alias checks added by the binding layer.
 * The default does not depend on `NDEBUG`: a Debug build must not silently slow kernels down.
 *
 * Every kernel symbol lives in `reloco::kernel::checks_<level>` (an inline namespace), so translation units built
 * with different levels get different symbols instead of silently sharing one ODR-violating definition. Object
 * layout never depends on the level.
 *
 * See `docs/kernel/checks.md`.
 */

#if defined(_MSVC_LANG)
#if _MSVC_LANG < 202002L
#error "reloco/kernel requires C++20 or newer"
#endif
#elif __cplusplus < 202002L
#error "reloco/kernel requires C++20 or newer"
#endif

#include "../detail/assert.hpp"

#ifndef RELOCO_KERNEL_CHECKS
#define RELOCO_KERNEL_CHECKS 0
#endif

#if RELOCO_KERNEL_CHECKS < 0 || RELOCO_KERNEL_CHECKS > 2
#error "RELOCO_KERNEL_CHECKS must be 0, 1 or 2"
#endif

#define RELOCO_KERNEL_DETAIL_CAT(a, b) a##b
#define RELOCO_KERNEL_DETAIL_LEVEL_NS(level) RELOCO_KERNEL_DETAIL_CAT(checks_, level)

/** @brief Opens `reloco::kernel::checks_<RELOCO_KERNEL_CHECKS>`; close with `RELOCO_KERNEL_END`. */
#define RELOCO_KERNEL_BEGIN                                                                                            \
  namespace reloco::kernel {                                                                                           \
  inline namespace RELOCO_KERNEL_DETAIL_LEVEL_NS(RELOCO_KERNEL_CHECKS) {

/** @brief Closes the namespaces opened by `RELOCO_KERNEL_BEGIN`. */
#define RELOCO_KERNEL_END                                                                                              \
  }                                                                                                                    \
  }

RELOCO_KERNEL_BEGIN

/** @brief The `RELOCO_KERNEL_CHECKS` level this translation unit was built with. */
inline constexpr int checks_level = RELOCO_KERNEL_CHECKS;

RELOCO_KERNEL_END

/** @brief Re-verifies a proof invariant when `RELOCO_KERNEL_CHECKS >= 1`; compiles to nothing at level 0. */
#define RELOCO_KERNEL_CHECK(cond, msg)                                                                                 \
  do {                                                                                                                 \
    if constexpr (::reloco::kernel::checks_level >= 1) {                                                               \
      RELOCO_ASSERT(cond, msg);                                                                                        \
    }                                                                                                                  \
  } while (0)
