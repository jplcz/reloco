#pragma once
#include <memory>
#include <reloco/core.hpp>

namespace reloco {

class stack_allocator : public fallible_allocator {
public:
  stack_allocator(void *ptr, const std::size_t size) noexcept
      : buffer_(static_cast<std::byte *>(ptr)), capacity_(size) {}

  [[nodiscard]] result<mem_block>
  allocate(const std::size_t bytes,
           const std::size_t alignment) noexcept override {
    void *current_ptr = buffer_ + offset_;
    std::size_t space = capacity_ - offset_;
    void *aligned_ptr = std::align(alignment, bytes, current_ptr, space);
    if (!aligned_ptr) {
      return std::unexpected(error::allocation_failed);
    }
    offset_ = static_cast<std::byte *>(aligned_ptr) + bytes - buffer_;
    return mem_block{aligned_ptr, bytes};
  }

  [[nodiscard]] result<std::size_t>
  expand_in_place(void *ptr, std::size_t old_size,
                  std::size_t new_size) noexcept override {
    // If the pointer is the very last thing we allocated, we can just grow the
    // offset
    if (static_cast<std::byte *>(ptr) + old_size == buffer_ + offset_) {
      const std::size_t added = new_size - old_size;
      if (offset_ + added <= capacity_) {
        offset_ += added;
        return new_size;
      }
    }
    return std::unexpected(error::in_place_growth_failed);
  }

  [[nodiscard]] result<mem_block> reallocate(void *, std::size_t, std::size_t,
                                             std::size_t) noexcept override {
    return std::unexpected(error::allocation_failed);
  }

  void deallocate(void *, std::size_t) noexcept override {}

  // Reset the whole arena
  void reset() noexcept { offset_ = 0; }

private:
  std::byte *buffer_;
  std::size_t capacity_;
  std::size_t offset_ = 0;
};

} // namespace reloco