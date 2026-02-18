#pragma once
#include <new>
#include <reloco/allocator.hpp>

namespace reloco {

template <typename T> class stl_bridge_allocator {
  fallible_allocator *resource_;

public:
  using value_type = T;

  stl_bridge_allocator() noexcept : resource_(&get_default_allocator()) {}

  explicit stl_bridge_allocator(fallible_allocator &res) noexcept
      : resource_(&res) {}

  template <typename U>
  stl_bridge_allocator(const stl_bridge_allocator<U> &other) noexcept
      : resource_(other.resource()) {}

  fallible_allocator *resource() const noexcept { return resource_; }

  [[nodiscard]] T *allocate(std::size_t n) {
    auto res = resource_->allocate(n * sizeof(T), alignof(T));
    if (!res) {
      throw std::bad_alloc();
    }
    return static_cast<T *>(res->ptr);
  }

  void deallocate(T *p, std::size_t n) noexcept {
    resource_->deallocate(p, n * sizeof(T));
  }

  constexpr bool operator==(const stl_bridge_allocator &other) const noexcept {
    return resource_ == other.resource_;
  }

  constexpr bool operator!=(const stl_bridge_allocator &other) const noexcept {
    return !(*this == other);
  }
};

} // namespace reloco