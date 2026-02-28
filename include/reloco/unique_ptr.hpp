#pragma once
#include <reloco/allocator.hpp>
#include <reloco/concepts.hpp>
#include <reloco/rvalue_safety.hpp>

namespace reloco {

template <typename T> class [[nodiscard]] unique_ptr {
  T *m_ptr = nullptr;
  fallible_allocator *m_alloc = nullptr;

public:
  RELOCO_BLOCK_RVALUE_ACCESS(T);

  template <typename... Args>
  static auto try_allocate(fallible_allocator &alloc, Args &&...args) noexcept
      -> result<unique_ptr<T>> {
    if constexpr (has_try_create<T, Args...>) {
      auto created_res = T::try_create(std::forward<Args>(args)...);
      if (!created_res)
        return unexpected(created_res.error());
      auto res = alloc.allocate(sizeof(T), alignof(T));
      if (!res)
        return unexpected(res.error());
      return unique_ptr<T>(new (res->ptr) T(std::move(*created_res)), &alloc);
    } else {
      auto res = alloc.allocate(sizeof(T), alignof(T));
      if (!res)
        return unexpected(res.error());

      return unique_ptr<T>(new (res->ptr) T(std::forward<Args>(args)...),
                           &alloc);
    }
  }

  template <typename... Args>
  static auto try_create(Args &&...args) noexcept -> result<unique_ptr<T>> {
    return try_allocate(get_default_allocator(), std::forward<Args>(args)...);
  }

  constexpr unique_ptr() noexcept = default;

  constexpr unique_ptr(unique_ptr &&other) noexcept
      : m_ptr(other.m_ptr), m_alloc(other.m_alloc) {
    other.m_ptr = nullptr;
  }

  unique_ptr &operator=(unique_ptr &&other) noexcept {
    if (this != &other) {
      reset();
      m_ptr = other.m_ptr;
      m_alloc = other.m_alloc;
      other.m_ptr = nullptr;
    }
    return *this;
  }

  ~unique_ptr() noexcept { reset(); }

  T &operator*() const & noexcept {
    RELOCO_ASSERT(m_ptr, "Dereference of null unique_ptr");
    return *m_ptr;
  }

  T *operator->() const & noexcept {
    RELOCO_ASSERT(m_ptr, "Access of null unique_ptr");
    return m_ptr;
  }

  explicit operator bool() const noexcept { return m_ptr != nullptr; }
  T *unsafe_get() const & noexcept { return m_ptr; }

  void reset() noexcept {
    if (m_ptr) {
      m_ptr->~T();
      m_alloc->deallocate(m_ptr, sizeof(T));
      m_ptr = nullptr;
    }
  }

private:
  constexpr unique_ptr(T *ptr, fallible_allocator *alloc) noexcept
      : m_ptr(ptr), m_alloc(alloc) {}
};

template <typename T> struct is_relocatable<unique_ptr<T>> : std::true_type {};

} // namespace reloco
