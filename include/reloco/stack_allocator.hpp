// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "allocator.hpp"
#include "error.hpp"
#include "expected.hpp"
#include <cstddef>
#include <memory>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief Tag identifying the stack allocator backend.
 */
struct stack_allocator_tag {};

/**
 * @brief Context state for the stack allocator.
 */
struct RELOCO_POINTER stack_allocator_context {
  std::byte *buffer;
  std::size_t capacity;
  std::size_t offset = 0;

  constexpr stack_allocator_context(void *ptr RELOCO_LIFETIME_CAPTURE_BY_THIS, std::size_t size) noexcept
      : buffer(static_cast<std::byte *>(ptr)), capacity(size) {}

  /**
   * @brief Resets the allocator back to the beginning of the buffer.
   * All previously allocated blocks are invalidated.
   */
  constexpr void reset() noexcept { offset = 0; }
};

/**
 * @brief Traits specialization binding the stack allocator logic to its tag.
 */
template <> struct allocator_traits<stack_allocator_tag> {
  using context_type = stack_allocator_context;

  [[nodiscard]] static result<mem_block> allocate(value_ref<context_type> ctx, std::size_t bytes,
                                                  std::size_t alignment) noexcept {
    void *current_ptr = ctx->buffer + ctx->offset;
    std::size_t space = ctx->capacity - ctx->offset;

    // std::align automatically updates current_ptr and space if successful
    void *aligned_ptr = std::align(alignment, bytes, current_ptr, space);
    if (!aligned_ptr) {
      return unexpected(error::allocation_failed);
    }

    ctx->offset = static_cast<std::size_t>(static_cast<std::byte *>(aligned_ptr) + bytes - ctx->buffer);
    return mem_block{aligned_ptr, bytes};
  }

  static void deallocate(value_ref<context_type>, void *, std::size_t) noexcept {
    // Stack allocator doesn't free individual blocks. Reclaimed via reset() on the context.
  }

  [[nodiscard]] static result<std::size_t> expand_in_place(value_ref<context_type> ctx, void *ptr, std::size_t old_size,
                                                           std::size_t new_size) noexcept {
    // If the pointer is the very last thing we allocated, we can just bump the offset
    if (static_cast<std::byte *>(ptr) + old_size == ctx->buffer + ctx->offset) {
      const std::size_t added = new_size - old_size;
      if (ctx->offset + added <= ctx->capacity) {
        ctx->offset += added;
        return new_size;
      }
    }
    return unexpected(error::allocation_failed);
  }

  // NOTE: `reallocate` and `advise` are intentionally omitted.
  // allocator_ref::can_reallocate() and can_advise() will detect their absence
  // at compile time and return false / unsupported_operation seamlessly.
};

/**
 * @brief Owning wrapper for the stack allocator.
 *
 * Usage:
 * @code
 *   alignas(std::max_align_t) std::byte buffer[1024];
 *   reloco::stack_allocator arena(reloco::stack_allocator_context(buffer, sizeof(buffer)));
 *
 *   auto vec = reloco::vector<int>::try_allocate(arena.ref());
 *
 *   // Reset the entire arena to reclaim memory
 *   arena.context()->reset();
 * @endcode
 */
using stack_allocator = allocator<stack_allocator_tag>;

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE