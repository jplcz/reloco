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
 *
 * `reloco_user_config.hpp` must not `#include` any reloco header
 * (directly or transitively). It is reached from `detail/compat.hpp`'s very
 * first line, before that header has defined even its own feature-detection
 * macros (`RELOCO_HAS_ATTRIBUTE` and friends); any reloco header pulled in
 * from here would re-enter `compat.hpp` while it is still on the include
 * stack, so `#pragma once` would skip it and leave those macros undefined,
 * breaking the build. Keep `reloco_user_config.hpp` to plain `#define`s;
 * customization points that need a real type or definition (like
 * `RELOCO_DEFAULT_ALLOCATOR_CUSTOM` below) work by opting out of the
 * library's own definition, so the actual override can be written as an
 * ordinary out-of-line definition in its own header, included by the
 * application through the normal path rather than from here.
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
//
// RELOCO_DEFAULT_ALLOCATOR_CUSTOM
//     Define (to any value) to take over reloco::reloco_global_alloc::
//     default_allocator(), the hook reloco::default_allocator() (see
//     reloco/default_allocator.hpp) forwards to. Defining it suppresses the
//     library's own definition (allocator<heap_allocator_tag>::ref(), the
//     process heap) so exactly one definition -- yours -- exists.
//
//     Since reloco_user_config.hpp cannot #include reloco/allocator.hpp
//     (see above), define the Tag/allocator_traits<Tag> specialization and
//     the out-of-line hook definition in their own header, included by the
//     application through the normal path, not from reloco_user_config.hpp:
//
//         // reloco_user_config.hpp
//         #define RELOCO_DEFAULT_ALLOCATOR_CUSTOM
//
//         // my_arena_allocator.hpp, included normally elsewhere by the app
//         #include <reloco/allocator.hpp>
//         #include <reloco/default_allocator.hpp>
//         struct my_arena_tag {};
//         template <> struct reloco::allocator_traits<my_arena_tag> { ... };
//         inline reloco::allocator_ref
//         reloco::reloco_global_alloc::default_allocator() noexcept {
//           static my_arena arena;
//           return reloco::allocator<my_arena_tag>(arena).ref();
//         }
