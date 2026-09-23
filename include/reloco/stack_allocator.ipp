// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file stack_allocator.ipp @brief Out-of-line bodies for
 * allocator_traits<stack_allocator_tag>::allocate/expand_in_place (see
 * stack_allocator.hpp). Included from stack_allocator.hpp itself, guarded
 * on RELOCO_SHARED_PROVIDE_DEFINITIONS (see reloco/detail/compat.hpp).
 * Never included directly. */

RELOCO_API result<mem_block> allocator_traits<stack_allocator_tag>::allocate(value_ref<context_type> ctx,
                                                                              std::size_t bytes,
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

RELOCO_API result<std::size_t> allocator_traits<stack_allocator_tag>::expand_in_place(value_ref<context_type> ctx,
                                                                                       void *ptr,
                                                                                       std::size_t old_size,
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
