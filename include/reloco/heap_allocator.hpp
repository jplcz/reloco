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

namespace reloco {

/**
 * @brief Tag for the built-in process-heap allocator backend.
 */
struct heap_allocator_tag {};

namespace detail {

// `deallocate`/`reallocate` always release blocks with `std::free`, so the
// allocation side must only ever hand out `std::free`-compatible pointers.
// `std::aligned_alloc`-obtained memory satisfies that on every platform that
// provides it; MSVC's STL does not provide `std::aligned_alloc`, and its
// `_aligned_malloc` counterpart requires the mismatched `_aligned_free`, so
// over-aligned requests are reported as unsupported there instead.
#if !defined(_MSC_VER)
inline void *heap_aligned_alloc(std::size_t alignment, std::size_t bytes) noexcept {
  // std::aligned_alloc requires bytes to be a non-zero multiple of alignment.
  std::size_t rounded = ((bytes + alignment - 1) / alignment) * alignment;
  if (rounded == 0)
    rounded = alignment;
  return std::aligned_alloc(alignment, rounded);
}
#endif

} // namespace detail

/**
 * @brief Traits for the built-in @ref heap_allocator_tag.
 *
 * Stateless: every call answers directly through the C heap, so
 * `context_type` is `void`.
 */
template <> struct allocator_traits<heap_allocator_tag> {
  using context_type = void;

  /**
   * @brief Allocates `bytes` with at least `alignment` alignment.
   * @param bytes Requested size in bytes.
   * @param alignment Requested alignment; must be a power of two.
   * @return The allocated block, or `allocator_error::allocation_failed`.
   */
  static alloc_result<mem_block> allocate(std::size_t bytes, std::size_t alignment) noexcept {
    if (alignment <= alignof(std::max_align_t)) {
      void *ptr = std::malloc(bytes == 0 ? 1 : bytes);
      if (!ptr)
        return unexpected(allocator_error::allocation_failed);
      return mem_block{ptr, bytes};
    }
#if defined(_MSC_VER)
    return unexpected(allocator_error::unsupported_operation);
#else
    void *ptr = detail::heap_aligned_alloc(alignment, bytes == 0 ? alignment : bytes);
    if (!ptr)
      return unexpected(allocator_error::allocation_failed);
    return mem_block{ptr, bytes};
#endif
  }

  /**
   * @brief Frees a block previously returned by `allocate`/`reallocate`.
   */
  static void deallocate(void *ptr, std::size_t) noexcept { std::free(ptr); }

  /**
   * @brief Resizes a block in place when possible, falling back to a fresh
   * allocation plus copy otherwise.
   */
  static alloc_result<mem_block> reallocate(void *ptr, std::size_t old_size,
                                            std::size_t new_size,
                                            std::size_t alignment) noexcept {
    if (alignment <= alignof(std::max_align_t)) {
      void *new_ptr = std::realloc(ptr, new_size == 0 ? 1 : new_size);
      if (!new_ptr)
        return unexpected(allocator_error::allocation_failed);
      return mem_block{new_ptr, new_size};
    }

    auto block = allocate(new_size, alignment);
    if (!block)
      return block;
    if (ptr != nullptr) {
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
      std::memcpy(block->ptr, ptr, old_size < new_size ? old_size : new_size);
      RELOCO_END_UNSAFE_BUFFER_USAGE
      std::free(ptr);
    }
    return block;
  }
};

} // namespace reloco
