// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file alignment.hpp
 * @brief `alignment_of<T>`, a customization-point trait controlling the
 * allocation/storage alignment reloco containers request for `T`,
 * independent of `T`'s own `alignof(T)`.
 *
 * By default `alignment_of<T>::value` is exactly `alignof(T)`, so nothing
 * changes for ordinary types. Specialize it when `T` needs to be reachable
 * through a pointer stronger-aligned than the compiler's ordinary ABI
 * alignment for it -- typically to feed contiguous storage directly into
 * SIMD load/store intrinsics (SSE/AVX/NEON/...) that require 16/32/64-byte
 * aligned pointers, without redeclaring `T` itself with `alignas(...)`
 * (which would also change `sizeof(T)`/padding for every other use of `T`,
 * not just the allocation).
 *
 * Every reloco container that allocates or embeds storage for `T`
 * (`vector<T>`, `array<T, N>`, `inline_vector<T, Capacity>`) uses
 * `effective_alignment_v<T>` -- `std::max(alignof(T), alignment_of_v<T>)`
 * -- as its actual allocation/storage alignment, so specializing this trait
 * affects every container instantiated with `T` across the whole program,
 * exactly like `is_trivially_relocatable` (see `relocatable.hpp`).
 *
 * ```cpp
 * struct alignas(4) vec4f { float x, y, z, w; };
 *
 * template <> struct reloco::alignment_of<vec4f> : std::integral_constant<std::size_t, 32> {};
 *
 * // Every vector<vec4f>/array<vec4f, N>/inline_vector<vec4f, N> below now
 * // allocates/embeds its storage 32-byte aligned, suitable for AVX loads.
 * reloco::vector<vec4f> positions;
 * ```
 *
 * `data()`/`unsafe_data()`/`begin()` on these containers are additionally
 * annotated with `RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>)` (see
 * `lifetime.hpp`), so GCC/Clang can auto-vectorize caller loops over the
 * returned pointer without an alignment-check prologue.
 */

#include <cstddef>
#include <type_traits>

namespace reloco {

/**
 * @brief Customization point: the alignment reloco containers should
 * request when allocating or embedding storage for `T`. Defaults to
 * `alignof(T)`. Must be a power of two if specialized -- see
 * `effective_alignment_v`, which validates this on first use.
 */
template <typename T> struct alignment_of : std::integral_constant<std::size_t, alignof(T)> {};

/**
 * @brief Convenience variable template for `alignment_of<T>::value`.
 */
template <typename T> inline constexpr std::size_t alignment_of_v = alignment_of<T>::value;

/**
 * @brief The alignment reloco containers actually use for `T`'s storage:
 * `alignment_of_v<T>`, but never weaker than `alignof(T)` itself, so an
 * unspecialized or under-specialized `alignment_of<T>` can never reduce
 * alignment below what `T` already requires.
 */
template <typename T>
inline constexpr std::size_t effective_alignment_v = []() constexpr {
  static_assert((alignment_of_v<T> & (alignment_of_v<T> - 1)) == 0,
                "reloco::alignment_of<T>::value must be a power of two");
  return alignment_of_v<T> > alignof(T) ? alignment_of_v<T> : alignof(T);
}();

} // namespace reloco
