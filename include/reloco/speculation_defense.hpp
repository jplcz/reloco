// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <reloco/detail/assert.hpp>
#include <reloco/detail/compat.hpp>
#include <type_traits>

namespace reloco {
namespace nospec {

/**
 * @brief Architectural speculation barrier.
 * Halts speculative execution until prior branches are architecturally resolved.
 * Use this in slow-paths or immediately after critical security checks if
 * data masking is not feasible.
 */
inline void barrier() noexcept {
#if defined(__x86_64__) || defined(__i386__)
  __asm__ __volatile__("lfence" ::: "memory");
#elif defined(__aarch64__)
  __asm__ __volatile__("csdb" ::: "memory");
#elif defined(__arm__)
#ifdef __thumb__
  // Thumb-2 encoding for CSDB: 0xF3AF 8014.
  // Emitted as two 16-bit halfwords to ensure ancient assemblers
  // handle the endianness and alignment correctly.
  __asm__ __volatile__("isb\n\t"
                       ".short 0xf3af, 0x8014" ::
                           : "memory");
#else
  // ARM encoding for CSDB: 0xE320F014.
  // Emitted as a single 32-bit word.
  __asm__ __volatile__("isb\n\t"
                       ".word 0xe320f014" ::
                           : "memory");
#endif
#elif defined(__riscv)
  // RISC-V Zisb extension is not yet ubiquitous.
  // A full memory fence acts as a pipeline serializing fallback on most current microarchitectures.
  __asm__ __volatile__("fence rw, rw" ::: "memory");
#else
  // Fallback compiler barrier
  __asm__ __volatile__("" ::: "memory");
#endif
}

/**
 * @brief Masks a value speculatively based on a condition without introducing branches.
 *
 * If the CPU mispredicts the branch and speculates past it, this function
 * forces the value to a safe fallback using bitwise math or compiler intrinsics,
 * preventing speculative out-of-bounds reads/writes.
 *
 * @param value The value to pass through if the condition is met.
 * @param is_safe The condition that must be true (e.g., a bounds check).
 * @param fail_val The safe fallback value if the condition is false.
 */
template <typename T> [[nodiscard]] constexpr T sanitize(T value, bool is_safe, T fail_val = T{0}) noexcept {
  static_assert(std::is_integral_v<T>, "nospec::sanitize requires integral types");

#if defined(__has_builtin)
#if __has_builtin(__builtin_speculation_safe_value)
  // Utilizes compiler-native hardware tracking if available (e.g., Clang/GCC on ARM CSEL)
  T result = is_safe ? value : fail_val;
  return __builtin_speculation_safe_value(result);
#endif
#endif

  // Branchless fallback using two's complement masking.
  // If is_safe is true (1), mask is 0xFF...FF.
  // If is_safe is false (0), mask is 0x00...00.
  using unsigned_T = std::make_unsigned_t<T>;

  const unsigned_T u_val = static_cast<unsigned_T>(value);
  const unsigned_T u_fail = static_cast<unsigned_T>(fail_val);
  const unsigned_T mask = static_cast<unsigned_T>(-static_cast<std::make_signed_t<unsigned_T>>(is_safe));

  // When is_safe = true: (u_val & 0xFFFF) | (u_fail & ~0xFFFF) -> u_val
  // When is_safe = false: (u_val & 0x0) | (u_fail & ~0x0) -> u_fail
  return static_cast<T>((u_val & mask) | (u_fail & ~mask));
}

/**
 * @brief Shorthand to clamp an index/offset against an upper bound speculatively.
 */
template <typename T> [[nodiscard]] constexpr T clamp_bounds(T index, T upper_bound, T fail_val = T{0}) noexcept {
  return sanitize(index, index < upper_bound, fail_val);
}

} // namespace nospec
} // namespace reloco