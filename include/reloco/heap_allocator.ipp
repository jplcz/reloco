// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file heap_allocator.ipp @brief Out-of-line bodies for
 * detail::heap_aligned_alloc and allocator_traits<heap_allocator_tag> (see
 * heap_allocator.hpp). Included from heap_allocator.hpp itself, guarded on
 * RELOCO_SHARED_PROVIDE_DEFINITIONS (see reloco/detail/compat.hpp). Never
 * included directly.
 *
 * Every operation here goes straight through the C heap
 * (malloc/realloc/free/aligned_alloc, or their _aligned_* MSVC
 * equivalents) -- no std::string/std::vector/exceptions involved, matching
 * reloco_compile.hpp's default umbrella's std-allocation/exception-free
 * requirement (see docs/shared-library.md).
 */

namespace detail {

#if !defined(_MSC_VER)
RELOCO_API void *heap_aligned_alloc(std::size_t alignment, std::size_t bytes) noexcept {
  // std::aligned_alloc requires bytes to be a non-zero multiple of alignment.
  std::size_t rounded = ((bytes + alignment - 1) / alignment) * alignment;
  if (rounded == 0)
    rounded = alignment;
  return std::aligned_alloc(alignment, rounded);
}
#endif

} // namespace detail

RELOCO_API result<mem_block> allocator_traits<heap_allocator_tag>::allocate(std::size_t bytes,
                                                                            std::size_t alignment) noexcept {
#if defined(_MSC_VER)
  void *ptr = _aligned_malloc(bytes == 0 ? 1 : bytes, alignment);
  if (!ptr)
    return unexpected(error::allocation_failed);
  return mem_block{ptr, bytes};
#else
  if (alignment <= alignof(std::max_align_t)) {
    void *ptr = std::malloc(bytes == 0 ? 1 : bytes);
    if (!ptr)
      return unexpected(error::allocation_failed);
    return mem_block{ptr, bytes};
  }
  void *ptr = detail::heap_aligned_alloc(alignment, bytes == 0 ? alignment : bytes);
  if (!ptr)
    return unexpected(error::allocation_failed);
  return mem_block{ptr, bytes};
#endif
}

RELOCO_API void allocator_traits<heap_allocator_tag>::deallocate(void *ptr, std::size_t) noexcept {
#if defined(_MSC_VER)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

RELOCO_API result<mem_block> allocator_traits<heap_allocator_tag>::reallocate(void *ptr, std::size_t old_size,
                                                                              std::size_t new_size,
                                                                              std::size_t alignment) noexcept {
#if defined(_MSC_VER)
  (void)old_size; // _aligned_realloc doesn't need the previous size.
  void *new_ptr = _aligned_realloc(ptr, new_size == 0 ? 1 : new_size, alignment);
  if (!new_ptr)
    return unexpected(error::allocation_failed);
  return mem_block{new_ptr, new_size};
#else
  if (alignment <= alignof(std::max_align_t)) {
    void *new_ptr = std::realloc(ptr, new_size == 0 ? 1 : new_size);
    if (!new_ptr)
      return unexpected(error::allocation_failed);
    return mem_block{new_ptr, new_size};
  }

  auto block = allocate(new_size, alignment);
  if (!block)
    return block;
  if (ptr != nullptr) {
    std::memcpy(block->ptr, ptr, old_size < new_size ? old_size : new_size);
    std::free(ptr);
  }
  return block;
#endif
}
