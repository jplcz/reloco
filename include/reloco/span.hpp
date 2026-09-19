// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file span.hpp
 * @brief Hardened C++17-compatible non-owning contiguous view. */

#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "rvalue_safety.hpp"
#include <cstddef>
#include <functional>
#include <iterator>
#include <type_traits>

// This class is the checked boundary around the raw pointer arithmetic needed
// to implement a C++17-compatible contiguous view.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

enum class span_error {
  out_of_bounds,
  container_empty,
};

/**
 * @brief A lightweight, dynamic-extent view over a contiguous element range.
 *
 * Accessors are ref-qualified to prevent pointers, references, and iterators
 * from being borrowed from temporary span objects. Checked access remains
 * enabled in release builds unless assertions are explicitly disabled.
 *
 * This C++17 implementation interoperates with @c std::span when it is
 * available.
 *
 * @tparam T Element type, optionally const-qualified.
 */
template <typename T> class RELOCO_POINTER span {
public:
  using element_type = T;
  using value_type = typename std::remove_cv<T>::type;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer = T *;
  using const_pointer = const T *;
  using reference = T &;
  using const_reference = const T &;
  using iterator = T *;
  using const_iterator = const T *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  /**
   * @brief Constructs an empty span with `nullptr` data and `0` size.
   */
  constexpr span() noexcept : m_ptr(nullptr), m_size(0) {}

  /**
   * @brief Constructs a span from a pointer and an explicit size.
   * @param ptr Pointer to the first element of the contiguous memory block.
   * @param size Number of elements in the buffer.
   */
  constexpr span(T *ptr, std::size_t size) noexcept : m_ptr(ptr), m_size(ptr == nullptr ? 0 : size) {}

  /**
   * @brief Constructs a span from a pointer pair (range).
   * @param first Pointer to the first element.
   * @param last Pointer to one past the last element.
   */
  constexpr span(T *first, T *last) noexcept : m_ptr(first), m_size(0) {
    RELOCO_ASSERT(first != nullptr || last == nullptr, "non-empty span requires non-null data");
    RELOCO_ASSERT(last != nullptr || first == nullptr, "span end must not be null");
    if (first != nullptr && last != nullptr) {
      RELOCO_ASSERT(last >= first, "span end must not precede span begin");
      m_size = static_cast<std::size_t>(last - first);
    }
  }

  /**
   * @brief Constructs a span from a raw C-style array.
   * @tparam N Size of the fixed array deduced at compile time.
   * @param arr Reference to the array.
   */
  template <std::size_t N>
  constexpr span(
      T (&arr RELOCO_LIFETIMEBOUND
             RELOCO_LIFETIME_CAPTURE_BY_THIS)[N]) noexcept
      : m_ptr(arr), m_size(N) {}

  /**
   * @brief Constructs a span from a compatible reloco span.
   * @tparam U Source element type.
   * @param other Source span.
   */
  template <typename U, std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>, int> = 0>
  constexpr span(
      const span<U> &other RELOCO_LIFETIMEBOUND
          RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_ptr(other.data()), m_size(other.size()) {}

#if RELOCO_HAS_STD_SPAN
  /**
   * @brief Constructs a `reloco::span` from any `std::span` (dynamic or
   * static extent).
   * @tparam U Element type (supports cv-qualifier conversion like const
   * propagation).
   * @tparam Extent The static extent of the standard span.
   */
  template <typename U, std::size_t Extent>
    requires(std::is_convertible_v<U (*)[], T (*)[]>)
  constexpr span(std::span<U, Extent> s) noexcept : m_ptr(s.data()), m_size(s.size()) {}

  /**
   * @brief Implicit conversion operator to `std::span<T>`.
   * @return An equivalent dynamic-extent `std::span<T>` covering the same
   * buffer.
   */
  [[nodiscard]] constexpr operator std::span<T>() const & noexcept { return std::span<T>(m_ptr, m_size); }

  operator std::span<T>() const && = delete;
#endif

  /**
   * @brief Returns a subspan view starting at a given offset.
   * @tparam Count Number of elements in the subspan (default = dynamic/npos).
   * @param offset Zero-based index at which the subspan begins.
   * @param count Number of elements in the subspan (defaults to remainder of
   * span).
   * @return A new `reloco::span` view.
   */
  [[nodiscard]] constexpr span<T>
  subspan(std::size_t offset,
          std::size_t count = static_cast<std::size_t>(-1)) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(offset <= m_size, "subspan offset exceeds span size");
    const std::size_t rem = m_size - offset;
    const std::size_t actual_count = count == static_cast<std::size_t>(-1) ? rem : count;
    RELOCO_ASSERT(actual_count <= rem, "subspan count exceeds remaining span size");
    return span<T>(pointer_at(offset), actual_count);
  }

  /**
   * @brief Returns a subspan with a statically specified compile-time count.
   * @tparam Count Exact number of elements requested.
   * @param offset Zero-based index at which the subspan begins.
   */
  template <std::size_t Count>
  [[nodiscard]] constexpr span<T> subspan(std::size_t offset) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(offset <= m_size, "subspan offset exceeds span size");
    const std::size_t rem = m_size - offset;
    RELOCO_ASSERT(Count <= rem, "subspan count exceeds remaining span size");
    return span<T>(pointer_at(offset), Count);
  }

  /**
   * @brief Attempts to create a subspan without trapping.
   * @param offset Zero-based starting index.
   * @param count Requested number of elements, or all remaining elements.
   * @return The requested span or @ref span_error::out_of_bounds.
   */
  [[nodiscard]] expected<span<T>, span_error>
  try_subspan(std::size_t offset,
              std::size_t count = static_cast<std::size_t>(-1)) const & noexcept RELOCO_LIFETIMEBOUND {
    if (offset > m_size)
      return unexpected(span_error::out_of_bounds);
    const std::size_t rem = m_size - offset;
    const std::size_t actual_count = count == static_cast<std::size_t>(-1) ? rem : count;
    if (actual_count > rem)
      return unexpected(span_error::out_of_bounds);
    return span<T>(pointer_at(offset), actual_count);
  }

  /**
   * @brief Creates a subspan with debug-only precondition checks.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr span<T>
  unsafe_subspan(std::size_t offset,
                 std::size_t count = static_cast<std::size_t>(-1)) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(offset <= m_size, "subspan offset exceeds span size");
    const std::size_t rem = m_size - offset;
    const std::size_t actual_count = count == static_cast<std::size_t>(-1) ? rem : count;
    RELOCO_DEBUG_ASSERT(actual_count <= rem, "subspan count exceeds remaining span size");
    return span<T>(pointer_at(offset), actual_count);
  }

  /**
   * @brief Returns a direct pointer to the beginning of the contiguous buffer.
   * @return Raw pointer to the elements, or `nullptr` if empty.
   */
  [[nodiscard]] constexpr T *data() const & noexcept RELOCO_LIFETIMEBOUND { return m_ptr; }

  /**
   * @brief Returns the data pointer when the span is non-empty.
   */
  [[nodiscard]] expected<T *, span_error> try_data() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(span_error::container_empty);
    return m_ptr;
  }

  /**
   * @brief Returns the data pointer with a debug-only non-empty check.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T *unsafe_data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "span has no data");
    return m_ptr;
  }

  /**
   * @brief Returns the number of elements in the span.
   * @return Element count.
   */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return m_size; }

  /**
   * @brief Returns the size of the viewed range in bytes.
   */
  [[nodiscard]] constexpr std::size_t size_bytes() const noexcept { return m_size * sizeof(T); }

  /**
   * @brief Checks if the span contains zero elements.
   * @return `true` if `size() == 0`, `false` otherwise.
   */
  [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

  /**
   * @brief Accesses an element with an always-on bounds check.
   * @param idx Zero-based index of the element to access.
   * @return Reference to the element at position @p idx.
   */
  [[nodiscard]] constexpr T &operator[](std::size_t idx) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(idx < m_size, "span index out of bounds");
    return m_ptr[idx];
  }

  /**
   * @brief Attempts to access an element without trapping.
   */
  [[nodiscard]] expected<std::reference_wrapper<T>, span_error>
  try_at(std::size_t idx) const & noexcept RELOCO_LIFETIMEBOUND {
    if (idx >= m_size)
      return unexpected(span_error::out_of_bounds);
    return std::ref(m_ptr[idx]);
  }

  /**
   * @brief Accesses an element with a debug-only bounds check.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &unsafe_at(std::size_t idx) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(idx < m_size, "span index out of bounds");
    return m_ptr[idx];
  }

  [[nodiscard]] expected<std::reference_wrapper<T>, span_error> try_front() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(span_error::container_empty);
    return std::ref(*m_ptr);
  }

  [[nodiscard]] expected<std::reference_wrapper<T>, span_error> try_back() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(span_error::container_empty);
    return std::ref(m_ptr[m_size - 1]);
  }

  [[nodiscard]] constexpr T &front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "front() called on empty span");
    return *m_ptr;
  }

  [[nodiscard]] constexpr T &back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "back() called on empty span");
    return m_ptr[m_size - 1];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &unsafe_front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "front() called on empty span");
    return *m_ptr;
  }

  T &unsafe_front() const && = delete;

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &unsafe_back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "back() called on empty span");
    return m_ptr[m_size - 1];
  }

  T &unsafe_back() const && = delete;

  [[nodiscard]] constexpr span<T> first(std::size_t count) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(count <= m_size, "first count exceeds span size");
    return span<T>(m_ptr, count);
  }

  [[nodiscard]] constexpr span<T> last(std::size_t count) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(count <= m_size, "last count exceeds span size");
    return span<T>(pointer_at(m_size - count), count);
  }

  [[nodiscard]] expected<span<T>, span_error> try_first(std::size_t count) const & noexcept RELOCO_LIFETIMEBOUND {
    if (count > m_size)
      return unexpected(span_error::out_of_bounds);
    return span<T>(m_ptr, count);
  }

  [[nodiscard]] expected<span<T>, span_error> try_last(std::size_t count) const & noexcept RELOCO_LIFETIMEBOUND {
    if (count > m_size)
      return unexpected(span_error::out_of_bounds);
    return span<T>(pointer_at(m_size - count), count);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr span<T> unsafe_first(std::size_t count) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(count <= m_size, "first count exceeds span size");
    return span<T>(m_ptr, count);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr span<T> unsafe_last(std::size_t count) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(count <= m_size, "last count exceeds span size");
    return span<T>(pointer_at(m_size - count), count);
  }

  /**
   * @brief Returns a read-only byte view of the represented range.
   */
  [[nodiscard]] span<const std::byte> as_bytes() const & noexcept RELOCO_LIFETIMEBOUND {
    return span<const std::byte>(reinterpret_cast<const std::byte *>(m_ptr), size_bytes());
  }

  /**
   * @brief Returns an iterator to the first element of the span.
   */
  [[nodiscard]] constexpr iterator begin() & noexcept RELOCO_LIFETIMEBOUND { return m_ptr; }
  [[nodiscard]] constexpr iterator begin() const & noexcept RELOCO_LIFETIMEBOUND { return m_ptr; }

  /**
   * @brief Returns an iterator to one past the last element of the span.
   */
  [[nodiscard]] constexpr iterator end() & noexcept RELOCO_LIFETIMEBOUND { return pointer_at(m_size); }
  [[nodiscard]] constexpr iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return pointer_at(m_size); }

  [[nodiscard]] constexpr reverse_iterator rbegin() & noexcept RELOCO_LIFETIMEBOUND {
    return reverse_iterator(end());
  }
  [[nodiscard]] constexpr reverse_iterator rbegin() const & noexcept RELOCO_LIFETIMEBOUND {
    return reverse_iterator(end());
  }

  [[nodiscard]] constexpr reverse_iterator rend() & noexcept RELOCO_LIFETIMEBOUND {
    return reverse_iterator(begin());
  }
  [[nodiscard]] constexpr reverse_iterator rend() const & noexcept RELOCO_LIFETIMEBOUND {
    return reverse_iterator(begin());
  }

  /**
   * @brief Returns a const iterator to the first element of the span.
   */
  [[nodiscard]] constexpr const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return m_ptr; }

  /**
   * @brief Returns a const iterator to one past the last element of the span.
   */
  [[nodiscard]] constexpr const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return pointer_at(m_size); }

  [[nodiscard]] constexpr const_reverse_iterator crbegin() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(cend());
  }

  [[nodiscard]] constexpr const_reverse_iterator crend() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(cbegin());
  }

private:
  [[nodiscard]] constexpr T *pointer_at(std::size_t offset) const noexcept RELOCO_LIFETIMEBOUND {
    return offset == 0 ? m_ptr : m_ptr + offset;
  }

  T *m_ptr;
  std::size_t m_size;
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE