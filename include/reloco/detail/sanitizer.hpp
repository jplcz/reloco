// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file sanitizer.hpp
 * @brief Optional AddressSanitizer/Valgrind memory-region annotations that
 * reloco allocators/containers can use to mark memory they own but that is
 * not currently valid to touch (spare/reclaimed storage) as inaccessible,
 * so a sanitizer build catches reads/writes through a stale pointer that
 * would otherwise silently "work" because the bytes are still mapped.
 *
 * Both integrations are strictly additive and each compiles down to a
 * no-op unless its tooling is actually active, so call sites never need
 * their own `#ifdef` guards:
 *
 * - **AddressSanitizer** is auto-detected (`__SANITIZE_ADDRESS__` on GCC,
 *   `__has_feature(address_sanitizer)` on Clang) and, once detected, pulls
 *   in `<sanitizer/asan_interface.h>` (part of the compiler's own runtime,
 *   always present alongside `-fsanitize=address`). Force it off (e.g. to
 *   measure the annotation-free baseline) with
 *   `RELOCO_DISABLE_SANITIZER_ANNOTATIONS`.
 * - **Valgrind (Memcheck)** cannot be auto-detected at compile time --
 *   there is no portable "am I being built to run under Valgrind" check,
 *   and `<valgrind/memcheck.h>` is a separate, not-always-installed
 *   header -- so it is opt-in only via `RELOCO_USE_VALGRIND`, and degrades
 *   to a no-op if the header is not found on the include path.
 *
 * Manual poisoning has one caveat worth knowing before reusing these
 * helpers elsewhere: both ASan's and Valgrind's shadow memory track
 * addressability at an 8-byte granularity. Poisoning/unpoisoning a region
 * whose start or end is not 8-byte aligned rounds *inward* -- it never
 * poisons bytes outside the requested range (so it can never falsely flag
 * an unrelated neighboring field), but it may leave a few boundary bytes
 * of the requested range unprotected. This makes the annotations always
 * safe to add, just not always maximally precise for sub-8-byte regions.
 *
 * See reloco/reloco_config.hpp for the RELOCO_DISABLE_SANITIZER_ANNOTATIONS
 * / RELOCO_USE_VALGRIND toggles themselves.
 */

#include "compat.hpp"

#include <cstddef>

#if !defined(RELOCO_DISABLE_SANITIZER_ANNOTATIONS)

#if defined(__SANITIZE_ADDRESS__)
#define RELOCO_ASAN_ENABLED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RELOCO_ASAN_ENABLED 1
#endif
#endif

#if defined(RELOCO_USE_VALGRIND) && RELOCO_HAS_INCLUDE(<valgrind / memcheck.h>)
#define RELOCO_VALGRIND_ENABLED 1
#endif

#endif // !RELOCO_DISABLE_SANITIZER_ANNOTATIONS

#ifndef RELOCO_ASAN_ENABLED
#define RELOCO_ASAN_ENABLED 0
#endif

#ifndef RELOCO_VALGRIND_ENABLED
#define RELOCO_VALGRIND_ENABLED 0
#endif

#if RELOCO_ASAN_ENABLED
#include <sanitizer/asan_interface.h>
#endif

#if RELOCO_VALGRIND_ENABLED
#include <valgrind/memcheck.h>
#endif

namespace reloco::detail {

/**
 * @brief Marks `[addr, addr + size)` as inaccessible to whichever of
 * ASan/Valgrind is enabled (both, either, or neither -- see the file-level
 * comment). A no-op when neither is enabled. The caller must own this
 * memory for the poisoned duration (e.g. a container's spare capacity, an
 * arena allocator's region reclaimed by a bulk reset) -- poisoning memory
 * a sanitizer doesn't already know belongs to you is undefined by both
 * tools' own contracts.
 */
#if (!RELOCO_ASAN_ENABLED || !RELOCO_VALGRIND_ENABLED)
constexpr
#endif
    inline void poison_memory_region([[maybe_unused]] const void *addr, [[maybe_unused]] std::size_t size) noexcept {
#if RELOCO_ASAN_ENABLED
  __asan_poison_memory_region(addr, size);
#endif
#if RELOCO_VALGRIND_ENABLED
  VALGRIND_MAKE_MEM_NOACCESS(addr, size);
#endif
}

/**
 * @brief Marks `[addr, addr + size)` as accessible again (its contents are
 * "undefined"/uninitialized, not necessarily zero) to whichever of
 * ASan/Valgrind is enabled. A no-op when neither is enabled.
 */
#if (!RELOCO_ASAN_ENABLED || !RELOCO_VALGRIND_ENABLED)
constexpr
#endif
    inline void unpoison_memory_region([[maybe_unused]] const void *addr, [[maybe_unused]] std::size_t size) noexcept {
#if RELOCO_ASAN_ENABLED
  __asan_unpoison_memory_region(addr, size);
#endif
#if RELOCO_VALGRIND_ENABLED
  VALGRIND_MAKE_MEM_UNDEFINED(addr, size);
#endif
}

} // namespace reloco::detail
