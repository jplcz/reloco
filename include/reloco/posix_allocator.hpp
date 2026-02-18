#pragma once
#include <cstdlib>
#include <cstring>
#include <reloco/core.hpp>
#include <sys/mman.h>
#include <unistd.h>

namespace reloco {

class posix_allocator final : public fallible_allocator {
public:
  result<mem_block> allocate(std::size_t bytes,
                             std::size_t alignment) noexcept override {
    // POSIX requires alignment to be a power of two and a multiple of
    // sizeof(void*)
    if (alignment < sizeof(void *))
      alignment = sizeof(void *);

    void *ptr = nullptr;
    if (::posix_memalign(&ptr, alignment, bytes) != 0) {
      return std::unexpected(error::allocation_failed);
    }
    return mem_block{ptr, bytes};
  }

  result<std::size_t> expand_in_place(void *ptr, std::size_t old_size,
                                      std::size_t new_size) noexcept override {
#if defined(__linux__)
    // Try growing without moving the pointer
    void *res = ::mremap(ptr, old_size, new_size, 0);
    if (res == MAP_FAILED) {
      return std::unexpected(error::in_place_growth_failed);
    }
    return new_size;
#else
    // Most other POSIX systems don't have a direct mremap(0) equivalent for
    // heap
    return std::unexpected(error::unsupported_operation);
#endif
  }

  result<mem_block> reallocate(void *ptr, std::size_t old_size,
                               std::size_t new_size,
                               std::size_t alignment) noexcept override {
    if (!ptr)
      return allocate(new_size, alignment);
    if (new_size <= old_size)
      return mem_block{ptr, old_size};

    // Attempt O(1) in-place growth (guarantees alignment preservation)
    if (auto res = expand_in_place(ptr, old_size, new_size); res) {
      return mem_block{ptr, *res};
    }

    // If alignment is standard, try standard realloc
    if (alignment <= alignof(std::max_align_t)) {
      void *new_ptr = ::realloc(ptr, new_size);
      if (!new_ptr)
        return std::unexpected(error::allocation_failed);
      return mem_block{new_ptr, new_size};
    }

    // We can't trust realloc for 64-byte or 4096-byte alignment.
    auto new_block_res = allocate(new_size, alignment);
    if (!new_block_res)
      return std::unexpected(error::allocation_failed);

    void *new_ptr = new_block_res->ptr;
    std::memcpy(new_ptr, ptr, old_size);
    ::free(ptr);

    return mem_block{new_ptr, new_size};
  }

  void deallocate(void *ptr, std::size_t) noexcept override { ::free(ptr); }

  void advise(void *ptr, std::size_t bytes, usage_hint hint) noexcept override {
    int posix_hint = 0;

    switch (hint) {
    case usage_hint::sequential:
      posix_hint = MADV_SEQUENTIAL;
      break;
    case usage_hint::random:
      posix_hint = MADV_RANDOM;
      break;
    case usage_hint::will_need:
      posix_hint = MADV_WILLNEED;
      break;
    case usage_hint::dont_need:
      posix_hint = MADV_DONTNEED;
      break;
#ifdef MADV_COLD
    case usage_hint::cold:
      posix_hint = MADV_COLD;
      break;
#endif
#ifdef MADV_HUGEPAGE
    case usage_hint::huge_pages:
      posix_hint = MADV_HUGEPAGE;
      break;
#endif
    default:
      return; // Do nothing for 'normal' or unknown
    }

    // madvise is a hint; we don't return a result because failure is non-fatal
    ::madvise(ptr, bytes, posix_hint);
  }
};

} // namespace reloco
