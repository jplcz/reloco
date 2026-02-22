#pragma once
#include <reloco/allocator.hpp>
#include <reloco/concepts.hpp>

namespace reloco {

template <typename T> class allocator_deleter {
  fallible_allocator *alloc_;

public:
  constexpr explicit allocator_deleter(fallible_allocator *a) noexcept
      : alloc_(a) {}

  void operator()(T *ptr) const noexcept {
    if (ptr && alloc_) {
      std::destroy_at(ptr);
      alloc_->deallocate(ptr, sizeof(T));
    }
  }
};

template <typename T>
using unique_ptr = std::unique_ptr<T, allocator_deleter<T>>;

template <typename T, typename... Args>
[[nodiscard]] result<unique_ptr<T>>
make_unique_fallible(fallible_allocator &alloc, Args &&...args) noexcept {
  // The object provides its own fallible factory
  if constexpr (has_try_create<T, Args...>) {
    auto res = T::try_create(std::forward<Args>(args)...);
    if (!res)
      return unexpected(res.error());

    // We still need to move the result into a managed unique_ptr.
    // Since T was created by its own factory, it might already manage memory.

    // We allocate space for the object wrapper itself
    auto block = alloc.allocate(sizeof(T), alignof(T));
    if (!block)
      return unexpected(error::allocation_failed);

    T *ptr = new (block->ptr) T(std::move(*res));
    return unique_ptr<T>(ptr, allocator_deleter<T>(&alloc));
  }
  // Standard fallible allocation + Placement New
  else {
    auto block = alloc.allocate(sizeof(T), alignof(T));
    if (!block)
      return unexpected(error::allocation_failed);

    // Standard C++ constructor call
    T *ptr = new (block->ptr) T(std::forward<Args>(args)...);
    return unique_ptr<T>(ptr, allocator_deleter<T>(&alloc));
  }
}

template <typename T, typename... Args>
[[nodiscard]] result<unique_ptr<T>> make_unique(Args &&...args) noexcept {
  return make_unique_fallible<T>(get_default_allocator(),
                                 std::forward<Args>(args)...);
}

template <typename T, typename D>
struct is_relocatable<std::unique_ptr<T, D>> : std::true_type {};

template <typename T>
struct is_relocatable<reloco::unique_ptr<T>> : std::true_type {};

} // namespace reloco
