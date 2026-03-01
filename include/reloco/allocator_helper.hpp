#pragma once
#include <limits>
#include <reloco/assert.hpp>
#include <reloco/construction_helpers.hpp>
#include <reloco/core.hpp>
#include <reloco/rvalue_safety.hpp>

#if defined(_MSC_VER)
#include <intsafe.h>
#endif

namespace reloco {

namespace detail {

/**
 * @brief Manual overflow check for compilers without intrinsics.
 */
template <typename T>
[[nodiscard]] constexpr bool manual_check_mul(T a, T b, T *result) noexcept {
  if (a > 0 && b > std::numeric_limits<T>::max() / a) {
    return true;
  }
  *result = a * b;
  return false;
}

/**
 * @brief Performs an overflow-safe multiplication: result = a * b
 * @return true if overflow occurred, false otherwise.
 */
template <typename T>
[[nodiscard]] constexpr bool check_mul(T a, T b, T *result) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  // GCC and Clang intrinsic
  return __builtin_mul_overflow(a, b, result);
#elif defined(_MSC_VER)
  if constexpr (sizeof(T) == 8 && std::is_unsigned_v<T>) {
    return SizeTMult(a, b, result) != 0; // Returns non-zero on error
  }
  // Fallback for MSVC or other types:
  return manual_check_mul(a, b, result);
#else
  return manual_check_mul(a, b, result);
#endif
}

} // namespace detail

/**
 * @brief An RAII handle for a fallibly allocated and constructed array.
 * * This type is move-only to ensure strict ownership of the allocated block.
 * It tracks both the pointer and the count to perform proper cleanup on failure
 * or scope exit.
 */
template <typename T> class [[nodiscard]] fallible_array_ptr {
  friend class allocator_helper;

  constexpr fallible_array_ptr(T *ptr, size_t count,
                               fallible_allocator *alloc) noexcept
      : m_ptr(ptr), m_count(count), m_alloc(alloc) {}

public:
  constexpr fallible_array_ptr() noexcept
      : m_ptr(nullptr), m_count(0), m_alloc(nullptr) {}

  // Move-only semantics
  fallible_array_ptr(const fallible_array_ptr &) = delete;
  fallible_array_ptr &operator=(const fallible_array_ptr &) = delete;

  constexpr fallible_array_ptr(fallible_array_ptr &&other) noexcept
      : m_ptr(std::exchange(other.m_ptr, nullptr)),
        m_count(std::exchange(other.m_count, 0)),
        m_alloc(std::exchange(other.m_alloc, nullptr)) {}

  /**
   * @brief Destructor ensures every constructed element is destroyed
   * and memory is returned to the allocator.
   */
  ~fallible_array_ptr() {
    if (m_ptr && m_alloc) {
      if (!std::is_trivially_destructible_v<T>) {
        for (size_t i = m_count; i > 0; --i) {
          m_ptr[i - 1].~T();
        }
      }
      m_alloc->deallocate(m_ptr, m_count * sizeof(T));
    }
  }

  constexpr fallible_array_ptr &operator=(fallible_array_ptr &&other) noexcept {
    if (this != &other) {
      if (m_ptr && m_alloc) {
        if (!std::is_trivially_destructible_v<T>) {
          for (size_t i = m_count; i > 0; --i) {
            m_ptr[i - 1].~T();
          }
        }
        m_alloc->deallocate(m_ptr, m_count * sizeof(T));
      }

      m_ptr = std::exchange(other.m_ptr, nullptr);
      m_count = std::exchange(other.m_count, 0);
      m_alloc = std::exchange(other.m_alloc, nullptr);
    }
    return *this;
  }

  // --- Accessors ---

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  [[nodiscard]] constexpr T *unsafe_get() const & noexcept { return m_ptr; }
  [[nodiscard]] constexpr size_t size() const noexcept { return m_count; }
  [[nodiscard]] constexpr bool empty() const noexcept { return m_count == 0; }

  [[nodiscard]] constexpr T &operator[](size_t index) const & noexcept {
    RELOCO_ASSERT(index < m_count, "fallible_array_ptr index out of bounds");
    return m_ptr[index];
  }

  [[nodiscard]] result<std::reference_wrapper<T>>
  at(size_t index) const & noexcept {
    if (index >= m_count)
      return unexpected(error::out_of_bounds);
    return std::ref(m_ptr[index]);
  }

  [[nodiscard]] T &unsafe_at(size_t index) const & noexcept {
    RELOCO_DEBUG_ASSERT(index < m_count,
                        "fallible_array_ptr index out of bounds");
    return m_ptr[index];
  }

  /**
   * @brief Releases ownership of the pointer.
   * Used by containers (like vector) to take control of the memory
   * after a successful allocation/construction phase.
   */
  [[nodiscard]] constexpr T *unsafe_release() noexcept {
    T *temp = m_ptr;
    m_ptr = nullptr;
    m_count = 0;
    return temp;
  }

private:
  T *m_ptr;
  size_t m_count;
  fallible_allocator *m_alloc;
};

class allocator_helper {
public:
  constexpr explicit allocator_helper(fallible_allocator &alloc) noexcept
      : m_alloc(&alloc) {}

  /**
   * @brief Allocates and constructs a single object of type T.
   * Transactional: If construction fails, memory is automatically freed.
   */
  template <typename T, typename... Args>
  [[nodiscard]] result<T *> try_allocate(Args &&...args) const noexcept {
    auto block_res = m_alloc->allocate(sizeof(T), alignof(T));
    if (!block_res)
      return unexpected(block_res.error());

    T *ptr = static_cast<T *>(block_res->ptr);

    // Use construction_helpers to initialize
    auto res = construction_helpers::try_construct(*m_alloc, ptr,
                                                   std::forward<Args>(args)...);
    if (!res) {
      m_alloc->deallocate(ptr, sizeof(T));
      return unexpected(res.error());
    }

    return ptr;
  }

  /**
   * @brief Destroys and deallocates a typed object.
   */
  template <typename T> void try_deallocate(T *ptr) const noexcept {
    if (ptr) {
      ptr->~T();
      m_alloc->deallocate(ptr, sizeof(T));
    }
  }

  /**
   * @brief Allocates an array of T with overflow-safe size calculation.
   * Returns a fallible_array_ptr for RAII safety.
   */
  template <typename T, typename... Args>
  [[nodiscard]] result<fallible_array_ptr<T>>
  allocate_array(size_t count, Args &&...args) const noexcept {
    if (count == 0)
      return unexpected(error::invalid_argument);

    std::size_t total_bytes;
    if (detail::check_mul(count, sizeof(T), &total_bytes)) {
      return unexpected(error::integer_overflow);
    }

    auto block_res = m_alloc->allocate(total_bytes, alignof(T));
    if (!block_res)
      return unexpected(block_res.error());

    T *elements = static_cast<T *>(block_res->ptr);

    for (size_t initialized = 0; initialized < count; ++initialized) {
      auto res = construction_helpers::try_construct(
          *m_alloc, &elements[initialized], std::forward<Args>(args)...);
      if (!res) {
        if (!std::is_trivially_destructible_v<T>) {
          for (size_t i = 0; i < initialized; ++i)
            elements[i].~T();
        }
        m_alloc->deallocate(elements, total_bytes);
        return unexpected(res.error());
      }
    }

    return fallible_array_ptr<T>(elements, count, m_alloc);
  }

  /**
   * @brief Clones an object into a new heap allocation.
   */
  template <typename T>
  [[nodiscard]] result<T *> try_clone(const T &source) const noexcept {
    auto block_res = m_alloc->allocate(sizeof(T), alignof(T));
    if (!block_res)
      return unexpected(block_res.error());

    T *storage = static_cast<T *>(block_res->ptr);
    auto res = construction_helpers::try_clone_at(*m_alloc, storage, source);

    if (!res) {
      m_alloc->deallocate(storage, sizeof(T));
      return unexpected(res.error());
    }

    return storage;
  }

  fallible_allocator &get_allocator() const noexcept { return *m_alloc; }

private:
  fallible_allocator *m_alloc;
};

} // namespace reloco
