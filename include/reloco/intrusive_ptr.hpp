#pragma once
#include <atomic>
#include <boost/intrusive_ptr.hpp>
#include <reloco/allocator.hpp>
#include <reloco/concepts.hpp>
#include <reloco/core.hpp>

namespace reloco {

template <typename Derived> class intrusive_base {
protected:
  mutable std::atomic<std::size_t> ref_count_{0};
  fallible_allocator *alloc_ = nullptr;

public:
  constexpr intrusive_base() noexcept = default;
  constexpr intrusive_base(const intrusive_base &) noexcept
      : ref_count_{0}, alloc_{nullptr} {}
  constexpr intrusive_base &operator=(const intrusive_base &) noexcept {
    return *this;
  }
  constexpr intrusive_base(intrusive_base &&) noexcept
      : ref_count_{0}, alloc_{nullptr} {}
  constexpr intrusive_base &operator=(intrusive_base &&) noexcept {
    return *this;
  }

  void set_reloco_context(fallible_allocator *a) noexcept { alloc_ = a; }

  friend void intrusive_ptr_add_ref(const intrusive_base<Derived> *p) noexcept {
    p->ref_count_.fetch_add(1, std::memory_order_relaxed);
  }

  friend void intrusive_ptr_release(const intrusive_base<Derived> *p) noexcept {
    if (p->ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      fallible_allocator *a = p->alloc_;

      constexpr std::size_t sz = sizeof(Derived);

      auto *derived_ptr = static_cast<const Derived *>(p);
      derived_ptr->~Derived();

      if (a) {
        a->deallocate(const_cast<Derived *>(derived_ptr), sz);
      }
    }
  }
};

template <typename Derived> class intrusive_base_dynamic {
  mutable std::atomic<std::size_t> ref_count_{0};
  fallible_allocator *alloc_ = nullptr;
  std::size_t total_size_ = 0; // Stored explicitly

public:
  constexpr intrusive_base_dynamic() noexcept = default;
  constexpr intrusive_base_dynamic(const intrusive_base_dynamic &) noexcept
      : ref_count_{0}, alloc_{nullptr}, total_size_{0} {}
  constexpr intrusive_base_dynamic &
  operator=(const intrusive_base_dynamic &) noexcept {
    return *this;
  }
  constexpr intrusive_base_dynamic(intrusive_base_dynamic &&) noexcept
      : ref_count_{0}, alloc_{nullptr}, total_size_(0) {}
  constexpr intrusive_base_dynamic &
  operator=(intrusive_base_dynamic &&) noexcept {
    return *this;
  }

  void set_reloco_context(fallible_allocator *a, std::size_t sz) noexcept {
    alloc_ = a;
    total_size_ = sz;
  }

  friend void
  intrusive_ptr_add_ref(const intrusive_base_dynamic<Derived> *p) noexcept {
    p->ref_count_.fetch_add(1, std::memory_order_relaxed);
  }

  friend void
  intrusive_ptr_release(const intrusive_base_dynamic<Derived> *p) noexcept {
    if (p->ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      std::size_t actual_size = p->total_size_; // Use the stored size
      auto *a = p->alloc_;
      auto *o = const_cast<Derived *>(static_cast<const Derived *>(p));
      o->~Derived();
      a->deallocate(o, actual_size);
    }
  }
};

template <typename T>
concept derives_from_intrusive_base = std::is_base_of_v<intrusive_base<T>, T>;

// Concept for Dynamic base
template <typename T>
concept derives_from_intrusive_dynamic =
    std::is_base_of_v<intrusive_base_dynamic<T>, T>;

template <typename T, typename... Args>
[[nodiscard]] result<boost::intrusive_ptr<T>>
try_allocate_intrusive(fallible_allocator &alloc, Args &&...args) noexcept {
  auto block = alloc.allocate(sizeof(T), alignof(T));
  if (!block)
    return std::unexpected(block.error());

  T *ptr = static_cast<T *>(block->ptr);

  // Construction (handling has_try_create if applicable)
  if constexpr (has_try_create<T, Args...>) {
    auto res = T::try_create(std::forward<Args>(args)...);
    if (!res) {
      alloc.deallocate(ptr, sizeof(T));
      return std::unexpected(res.error());
    }
    new (ptr) T(std::move(*res));
  } else {
    new (ptr) T(std::forward<Args>(args)...);
  }

  ptr->set_reloco_context(&alloc);
  return boost::intrusive_ptr<T>(ptr);
}

// Default allocator version
template <typename T, typename... Args>
[[nodiscard]] result<boost::intrusive_ptr<T>>
try_make_intrusive(Args &&...args) noexcept {
  return try_allocate_intrusive<T>(get_default_allocator(),
                                   std::forward<Args>(args)...);
}

template <typename T, typename... Args>
[[nodiscard]] result<boost::intrusive_ptr<T>>
try_allocate_intrusive_dynamic(fallible_allocator &alloc,
                               std::size_t total_bytes,
                               Args &&...args) noexcept {
  // Ensure we at least allocate enough for the header T
  const std::size_t actual_size = std::max(sizeof(T), total_bytes);

  auto block = alloc.allocate(actual_size, alignof(T));
  if (!block)
    return std::unexpected(block.error());

  T *ptr = static_cast<T *>(block->ptr);

  // Construct header
  if constexpr (has_try_create<T, Args...>) {
    auto res = T::try_create(std::forward<Args>(args)...);
    if (!res) {
      alloc.deallocate(ptr, actual_size);
      return std::unexpected(res.error());
    }
    new (ptr) T(std::move(*res));
  } else {
    new (ptr) T(std::forward<Args>(args)...);
  }

  ptr->set_reloco_context(&alloc, actual_size);
  return boost::intrusive_ptr<T>(ptr);
}

// Default allocator version
template <typename T, typename... Args>
[[nodiscard]] result<boost::intrusive_ptr<T>>
try_make_intrusive_dynamic(std::size_t total_bytes, Args &&...args) noexcept {
  return try_allocate_intrusive_dynamic<T>(get_default_allocator(), total_bytes,
                                           std::forward<Args>(args)...);
}

} // namespace reloco
