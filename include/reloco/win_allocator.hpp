#pragma once
#include <cstring>
#include <malloc.h>
#include <reloco/core.hpp>

namespace reloco {

class win_allocator final : public fallible_allocator {
public:
  [[nodiscard]] result<mem_block>
  allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    void *ptr = _aligned_malloc(bytes, alignment);

    if (!ptr)
      return unexpected(error::allocation_failed);
    return mem_block{ptr, bytes};
  }

  [[nodiscard]] result<std::size_t>
  expand_in_place(void *, std::size_t, std::size_t) noexcept override {
    return unexpected(error::in_place_growth_failed);
  }

  [[nodiscard]] result<mem_block>
  reallocate(void *ptr, std::size_t old_size, std::size_t new_size,
             std::size_t alignment) noexcept override {
    void *new_ptr = _aligned_realloc(ptr, new_size, alignment);

    if (!new_ptr)
      return unexpected(error::allocation_failed);
    return mem_block{new_ptr, new_size};
  }

  void deallocate(void *ptr, std::size_t /*bytes*/) noexcept override {
    // Windows requires _aligned_free for pointers from _aligned_malloc
    _aligned_free(ptr);
  }
};

} // namespace reloco
