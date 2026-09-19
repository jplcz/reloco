// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file reloco_config.hpp
 * @brief Single build-time customization entry point for every optional
 * reloco feature macro.
 *
 * This header is included first, before anything else, by
 * `reloco/detail/compat.hpp` (the library's common ancestor header), which
 * makes it the earliest point at which configuration can be injected
 * regardless of which reloco header an application includes first.
 *
 * User overrides must not be made by editing this file. Instead, define
 * `RELOCO_CONFIG` (via a compiler `-D` flag, e.g. `-DRELOCO_CONFIG=1`) to
 * opt in to including a header named `reloco_user_config.hpp`, which must be
 * reachable on the compiler's include search path (e.g. in an
 * application-owned include directory listed before reloco's own `include/`
 * in the include path). When `RELOCO_CONFIG` is defined,
 * `reloco_user_config.hpp` is included here, before any library header
 * defines its own default, so every `#define` it contains takes precedence
 * over the library defaults below and over the individual
 * `#ifndef`-guarded defaults each feature header applies on its own.
 *
 * Every macro below may alternatively be set directly with a compiler `-D`
 * flag instead of (or in addition to) `reloco_user_config.hpp`; both
 * approaches are equivalent since library headers only ever apply a default
 * when the macro is not already defined.
 */

#if defined(RELOCO_CONFIG)
#include "reloco_user_config.hpp"
#endif

// ============================================================================
// Available customization points
// ============================================================================
//
// RELOCO_KERNEL
//     Define to build for kernel/freestanding targets. Requires
//     RELOCO_KERNEL_PANIC (see below).
//
// RELOCO_KERNEL_PANIC(expression, file, line, message)
//     Required when RELOCO_KERNEL is defined. Must be defined as a
//     function-like macro that reports assertion failures through the
//     target's own panic/log facility (printf-like, or a sequence of raw
//     string writes as a last resort).
//
// RELOCO_TRAP()
//     Must not return. Defaults to a compiler debug trap, falling back to
//     std::abort().
//
// RELOCO_UNREACHABLE() / RELOCO_HAS_UNREACHABLE
//     Optimizer hint for unreachable code. Set RELOCO_HAS_UNREACHABLE to 0
//     alongside a no-op RELOCO_UNREACHABLE() if the platform has none.
//
// RELOCO_DISABLE_ASSERT / RELOCO_DISABLE_ASSERT_STDIO
//     Disable RELOCO_ASSERT/RELOCO_DEBUG_ASSERT entirely, or just their
//     default std::fprintf(stderr, ...) diagnostic, respectively.
//
// RELOCO_DEBUG
//     Force RELOCO_DEBUG_ASSERT to stay active even when NDEBUG is defined,
//     instead of compiling down to RELOCO_UNREACHABLE()/no-op.
