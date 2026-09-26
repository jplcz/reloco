// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file hint.hpp
 * @brief `hint::spin_loop()`, matching Rust's `std::hint::spin_loop()`: a
 * hardware hint that the calling thread is in a busy-wait spin loop.
 *
 * Emits the target architecture's dedicated spin-wait instruction where
 * one exists -- x86/x86-64 `pause`, AArch64/AArch32 `yield`, POWER
 * `or 27,27,27` -- which lets a hyperthreaded/SMT sibling core run and
 * reduces the memory-order mis-speculation penalty this core otherwise
 * pays on every loop iteration spinning on a value another core is about
 * to write, without actually yielding the CPU back to the scheduler
 * (unlike `this_thread::yield()`/a full `park()`). On an architecture
 * with no such instruction, falls back to a plain compiler-only fence
 * (`std::atomic_signal_fence`) that at least stops the loop from being
 * folded away entirely -- functionally a no-op otherwise.
 *
 * Pure architecture dispatch on top of `<atomic>` (for the portable
 * fallback) plus a couple of intrinsic/inline-asm forms -- no OS
 * dependency at all, so this header (and anything built on it) remains
 * usable in a freestanding/bare-kernel build. A hand-rolled spinlock (or
 * any other busy-wait loop retrying a `compare_exchange`/polling a flag
 * before falling back to `futex_wait`/`park()`) should call this once per
 * spin iteration, exactly like Rust's own spinlock-style crates
 * (`spin`, `crossbeam-utils`) call `std::hint::spin_loop()`.
 */

#include <atomic>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#endif

namespace reloco {
namespace hint {

/**
 * @brief Hints to the CPU that the calling thread is spinning in a
 * busy-wait loop, matching Rust's `std::hint::spin_loop()`. See the
 * file-level documentation above.
 */
inline void spin_loop() noexcept {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
  _mm_pause();
#elif defined(__i386__) || defined(__x86_64__)
  __asm__ __volatile__("pause" ::: "memory");
#elif defined(_MSC_VER) && (defined(_M_ARM) || defined(_M_ARM64))
  __yield();
#elif defined(__arm__) || defined(__aarch64__)
  __asm__ __volatile__("yield" ::: "memory");
#elif defined(__powerpc__) || defined(__ppc__) || defined(__PPC__)
  __asm__ __volatile__("or 27,27,27" ::: "memory");
#else
  // No dedicated spin-wait instruction on this architecture: a
  // compiler-only fence at least prevents the loop from being optimized
  // away, without pretending to hint anything to the hardware.
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

} // namespace hint
} // namespace reloco
