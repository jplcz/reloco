// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file heap_allocator.hpp
 * @brief Built-in stateless `allocator_traits` backend over the process
 * heap (`std::malloc`/`std::realloc`/`std::free`, or their aligned
 * equivalents for over-aligned requests). */

#include "allocator.hpp"
#include <cstdlib>
#include <cstring>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

// This backend is raw malloc/realloc/free/aligned_alloc plumbing throughout;
// treated as a single checked boundary, matching allocator.hpp itself.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief Tag for the built-in process-heap allocator backend.
 */
struct RELOCO_EXPORT heap_allocator_tag {};

namespace detail {

// `deallocate`/`reallocate` always release blocks with `std::free`, so the
// allocation side must only ever hand out `std::free`-compatible pointers.
// `std::aligned_alloc`-obtained memory satisfies that on every platform that
// provides it; MSVC's STL does not provide `std::aligned_alloc`.
#if !defined(_MSC_VER)
RELOCO_API void *heap_aligned_alloc(std::size_t alignment, std::size_t bytes) noexcept;
#endif

} // namespace detail

/**
 * @brief Traits for the built-in @ref heap_allocator_tag.
 *
 * Stateless: every call answers directly through the C heap, so
 * `context_type` is `void`.
 *
 * On MSVC (which provides no `std::aligned_alloc`), every block -- not
 * just over-aligned ones -- is obtained through `_aligned_malloc` and
 * released/resized through `_aligned_free`/`_aligned_realloc` instead of
 * `std::malloc`/`std::free`/`std::realloc`. This keeps every block in this
 * backend consistently `_aligned_free`-compatible: `deallocate` has no
 * `alignment` parameter to dispatch on (see `allocator.hpp`), so it cannot
 * tell an over-aligned block from a normally-aligned one apart -- it must
 * release every block the same way. `_aligned_malloc`/`_aligned_realloc`
 * accept any power-of-two alignment (including small/default ones), so
 * this is not a functional restriction, just a different underlying call.
 */
template <> struct allocator_traits<heap_allocator_tag> {
  using context_type = void;

  /**
   * @brief Allocates `bytes` with at least `alignment` alignment.
   * @param bytes Requested size in bytes.
   * @param alignment Requested alignment; must be a power of two.
   * @return The allocated block, or `error::allocation_failed`.
   */
  [[nodiscard]] static RELOCO_API result<mem_block> allocate(std::size_t bytes, std::size_t alignment) noexcept;

  /**
   * @brief Frees a block previously returned by `allocate`/`reallocate`.
   */
  static RELOCO_API void deallocate(void *ptr, std::size_t) noexcept;

  /**
   * @brief Resizes a block in place when possible, falling back to a fresh
   * allocation plus copy otherwise.
   */
  [[nodiscard]] static RELOCO_API result<mem_block> reallocate(void *ptr, std::size_t old_size, std::size_t new_size,
                                                                std::size_t alignment) noexcept;
};

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "heap_allocator.ipp"
#endif

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
