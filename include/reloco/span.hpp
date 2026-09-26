// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file span.hpp
 * @brief Hardened C++17-compatible non-owning contiguous view. */

#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "optional.hpp"
#include "relocatable_std.hpp"
#include "rvalue_safety.hpp"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

// This class is the checked boundary around the raw pointer arithmetic needed
// to implement a C++17-compatible contiguous view.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

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
template <typename T> class span;

/**
 * @brief Rust `slice::chunks` equivalent: a lazy, non-overlapping forward
 * range of `span<T>` sub-views, each of at most `chunk_size` elements (the
 * final chunk may be shorter). Never allocates; each dereference produces
 * a fresh `span<T>` computed from the current position.
 */
template <typename T> class RELOCO_POINTER span_chunks {
public:
  class iterator {
  public:
    using value_type = span<T>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    constexpr iterator(T *ptr, std::size_t remaining, std::size_t chunk_size) noexcept
        : m_ptr(ptr), m_remaining(remaining), m_chunk_size(chunk_size) {}

    [[nodiscard]] constexpr span<T> operator*() const noexcept RELOCO_LIFETIMEBOUND {
      return span<T>(m_ptr, m_remaining < m_chunk_size ? m_remaining : m_chunk_size);
    }

    constexpr iterator &operator++() noexcept {
      const std::size_t step = m_remaining < m_chunk_size ? m_remaining : m_chunk_size;
      m_ptr += step;
      m_remaining -= step;
      return *this;
    }

    constexpr iterator operator++(int) noexcept {
      iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    [[nodiscard]] constexpr bool operator==(const iterator &other) const noexcept {
      return m_remaining == other.m_remaining;
    }

    [[nodiscard]] constexpr bool operator!=(const iterator &other) const noexcept { return !(*this == other); }

  private:
    T *m_ptr;
    std::size_t m_remaining;
    std::size_t m_chunk_size;
  };

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  constexpr span_chunks(T *ptr, std::size_t size, std::size_t chunk_size) noexcept
      : m_ptr(ptr), m_size(size), m_chunk_size(chunk_size) {}

  [[nodiscard]] constexpr iterator begin() & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(m_ptr, m_size, m_chunk_size);
  }
  [[nodiscard]] constexpr iterator begin() const & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(m_ptr, m_size, m_chunk_size);
  }
  [[nodiscard]] constexpr iterator end() & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(m_ptr + m_size, 0, m_chunk_size);
  }
  [[nodiscard]] constexpr iterator end() const & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(m_ptr + m_size, 0, m_chunk_size);
  }

  [[nodiscard]] constexpr std::size_t size() const noexcept {
    return m_chunk_size == 0 ? 0 : (m_size + m_chunk_size - 1) / m_chunk_size;
  }

  [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

private:
  T *m_ptr;
  std::size_t m_size;
  std::size_t m_chunk_size;
};

/**
 * @brief Rust `slice::windows` equivalent: a lazy forward range of
 * overlapping `span<T>` sub-views, each exactly `window_size` elements,
 * sliding forward by one element at a time. Empty if `window_size` is `0`
 * or exceeds the span's size.
 */
template <typename T> class RELOCO_POINTER span_windows {
public:
  class iterator {
  public:
    using value_type = span<T>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    constexpr iterator(T *ptr, std::size_t remaining_windows, std::size_t window_size) noexcept
        : m_ptr(ptr), m_remaining_windows(remaining_windows), m_window_size(window_size) {}

    [[nodiscard]] constexpr span<T> operator*() const noexcept RELOCO_LIFETIMEBOUND {
      return span<T>(m_ptr, m_window_size);
    }

    constexpr iterator &operator++() noexcept {
      ++m_ptr;
      --m_remaining_windows;
      return *this;
    }

    constexpr iterator operator++(int) noexcept {
      iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    [[nodiscard]] constexpr bool operator==(const iterator &other) const noexcept {
      return m_remaining_windows == other.m_remaining_windows;
    }

    [[nodiscard]] constexpr bool operator!=(const iterator &other) const noexcept { return !(*this == other); }

  private:
    T *m_ptr;
    std::size_t m_remaining_windows;
    std::size_t m_window_size;
  };

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  constexpr span_windows(T *ptr, std::size_t size, std::size_t window_size) noexcept
      : m_ptr(ptr), m_count(window_size == 0 || window_size > size ? 0 : size - window_size + 1),
        m_window_size(window_size) {}

  [[nodiscard]] constexpr iterator begin() & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(m_ptr, m_count, m_window_size);
  }
  [[nodiscard]] constexpr iterator begin() const & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(m_ptr, m_count, m_window_size);
  }
  [[nodiscard]] constexpr iterator end() & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(m_ptr + m_count, 0, m_window_size);
  }
  [[nodiscard]] constexpr iterator end() const & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(m_ptr + m_count, 0, m_window_size);
  }

  [[nodiscard]] constexpr std::size_t size() const noexcept { return m_count; }

  [[nodiscard]] constexpr bool empty() const noexcept { return m_count == 0; }

private:
  T *m_ptr;
  std::size_t m_count;
  std::size_t m_window_size;
};

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
  constexpr span(T (&arr RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS)[N]) noexcept : m_ptr(arr), m_size(N) {}

  /**
   * @brief Constructs a span from a compatible reloco span.
   * @tparam U Source element type.
   * @param other Source span.
   */
  template <typename U, std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>, int> = 0>
  constexpr span(const span<U> &other RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_ptr(other.data()), m_size(other.size()) {}

  template <typename Container, typename = std::enable_if_t<
                                    // Prevent hijacking the copy/move constructors
                                    !std::is_same_v<std::decay_t<Container>, span> &&
                                    // Ensure the container has a .data() that converts to T*
                                    std::is_convertible_v<decltype(std::declval<Container &>().data()), T *> &&
                                    // Ensure the container has a .size() that returns an integer
                                    std::is_integral_v<decltype(std::declval<Container &>().size())>>>
  constexpr span(Container &cont RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_ptr(cont.data()), m_size(cont.size()) {}

  template <typename Container, typename = std::enable_if_t<
                                    !std::is_same_v<std::decay_t<Container>, span> &&
                                    std::is_convertible_v<decltype(std::declval<const Container &>().data()), T *> &&
                                    std::is_integral_v<decltype(std::declval<const Container &>().size())>>>
  constexpr span(const Container &cont RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_ptr(cont.data()), m_size(cont.size()) {}

  template <typename Container,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<Container>, span> &&
                                        std::is_convertible_v<decltype(std::declval<Container &>().data()), T *> &&
                                        std::is_integral_v<decltype(std::declval<Container &>().size())>>>
  constexpr span(Container &&) = delete;

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
  subspan(std::size_t offset, std::size_t count = static_cast<std::size_t>(-1)) const & noexcept RELOCO_LIFETIMEBOUND {
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
   * @return The requested span or @ref error::out_of_bounds.
   */
  [[nodiscard]] result<span<T>>
  try_subspan(std::size_t offset,
              std::size_t count = static_cast<std::size_t>(-1)) const & noexcept RELOCO_LIFETIMEBOUND {
    if (offset > m_size)
      return unexpected(error::out_of_bounds);
    const std::size_t rem = m_size - offset;
    const std::size_t actual_count = count == static_cast<std::size_t>(-1) ? rem : count;
    if (actual_count > rem)
      return unexpected(error::out_of_bounds);
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
   * @brief Rust `slice::split_at` equivalent: splits the span into two
   * adjacent sub-views at @p mid, `[0, mid)` and `[mid, size())`.
   */
  [[nodiscard]] constexpr std::pair<span<T>, span<T>> split_at(std::size_t mid) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(mid <= m_size, "split_at index exceeds span size");
    return {span<T>(m_ptr, mid), span<T>(pointer_at(mid), m_size - mid)};
  }

  /**
   * @brief Attempts `split_at` without trapping.
   */
  [[nodiscard]] result<std::pair<span<T>, span<T>>>
  try_split_at(std::size_t mid) const & noexcept RELOCO_LIFETIMEBOUND {
    if (mid > m_size)
      return unexpected(error::out_of_bounds);
    return std::pair<span<T>, span<T>>(span<T>(m_ptr, mid), span<T>(pointer_at(mid), m_size - mid));
  }

  /**
   * @brief Rust `slice::chunks` equivalent: a lazy range of non-overlapping
   * `span<T>` sub-views of at most @p chunk_size elements each (the final
   * chunk may be shorter).
   */
  [[nodiscard]] constexpr span_chunks<T> chunks(std::size_t chunk_size) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(chunk_size > 0, "chunks size must be non-zero");
    return span_chunks<T>(m_ptr, m_size, chunk_size);
  }

  /**
   * @brief Rust `slice::windows` equivalent: a lazy range of overlapping
   * `span<T>` sub-views of exactly @p window_size elements each, sliding
   * forward by one element. Empty if @p window_size is `0` or exceeds
   * `size()`.
   */
  [[nodiscard]] constexpr span_windows<T> windows(std::size_t window_size) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(window_size > 0, "windows size must be non-zero");
    return span_windows<T>(m_ptr, m_size, window_size);
  }

  /**
   * @brief Rust `slice::contains` equivalent: `true` if any element
   * compares equal to @p value via `operator==`.
   */
  [[nodiscard]] constexpr bool contains(const T &value) const noexcept {
    for (std::size_t i = 0; i < m_size; ++i) {
      if (m_ptr[i] == value)
        return true;
    }
    return false;
  }

  /**
   * @brief Rust `slice::sort` equivalent: sorts the elements in place
   * using `operator<`, preserving the relative order of equal elements
   * (a stable sort, matching Rust's default `sort`).
   */
  void sort() const & noexcept { std::stable_sort(begin(), end()); }

  /**
   * @brief Rust `slice::sort_by` equivalent: sorts the elements in place
   * using the strict-weak-order predicate @p comp, preserving the
   * relative order of equivalent elements.
   */
  template <typename Compare> void sort_by(Compare comp) const & noexcept { std::stable_sort(begin(), end(), comp); }

  /**
   * @brief Rust `slice::sort_unstable` equivalent: sorts the elements in
   * place using `operator<` without any ordering guarantee among equal
   * elements, which may be faster than `sort()`.
   */
  void sort_unstable() const & noexcept { std::sort(begin(), end()); }

  /**
   * @brief Rust `slice::binary_search` equivalent: assumes the span is
   * already sorted (ascending, by `operator<`) and returns the index of
   * an element equal to @p value, or an empty `optional` if none is
   * found. Behavior is unspecified (though never unsafe) if the span is
   * not actually sorted.
   */
  [[nodiscard]] optional<std::size_t> binary_search(const T &value) const noexcept {
    return binary_search_by(value, [](const T &element, const T &target) { return element < target; });
  }

  /**
   * @brief Rust `slice::binary_search_by` equivalent: assumes the span is
   * already sorted with respect to @p less (a strict-weak-order predicate
   * comparing an element to @p value) and returns the index of an element
   * equal to @p value under that ordering, or an empty `optional` if none
   * is found.
   */
  template <typename Compare>
  [[nodiscard]] optional<std::size_t> binary_search_by(const T &value, Compare less) const noexcept {
    std::size_t lo = 0;
    std::size_t hi = m_size;
    while (lo < hi) {
      const std::size_t mid = lo + (hi - lo) / 2;
      if (less(m_ptr[mid], value))
        lo = mid + 1;
      else
        hi = mid;
    }
    if (lo < m_size && !less(value, m_ptr[lo]) && !less(m_ptr[lo], value))
      return optional<std::size_t>(lo);
    return optional<std::size_t>(nullopt);
  }

  /**
   * @brief Returns a direct pointer to the beginning of the contiguous buffer.
   * @return Raw pointer to the elements, or `nullptr` if empty.
   */
  [[nodiscard]] constexpr T *data() const & noexcept RELOCO_LIFETIMEBOUND { return m_ptr; }

  /**
   * @brief Returns the data pointer when the span is non-empty.
   */
  [[nodiscard]] result<T *> try_data() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
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
  [[nodiscard]] result<std::reference_wrapper<T>> try_at(std::size_t idx) const & noexcept RELOCO_LIFETIMEBOUND {
    if (idx >= m_size)
      return unexpected(error::out_of_bounds);
    return std::ref(m_ptr[idx]);
  }

  /**
   * @brief Accesses an element with a debug-only bounds check.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &
  unsafe_at(std::size_t idx) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(idx < m_size, "span index out of bounds");
    return m_ptr[idx];
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_front() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::ref(*m_ptr);
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_back() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
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

  [[nodiscard]] result<span<T>> try_first(std::size_t count) const & noexcept RELOCO_LIFETIMEBOUND {
    if (count > m_size)
      return unexpected(error::out_of_bounds);
    return span<T>(m_ptr, count);
  }

  [[nodiscard]] result<span<T>> try_last(std::size_t count) const & noexcept RELOCO_LIFETIMEBOUND {
    if (count > m_size)
      return unexpected(error::out_of_bounds);
    return span<T>(pointer_at(m_size - count), count);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr span<T>
  unsafe_first(std::size_t count) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(count <= m_size, "first count exceeds span size");
    return span<T>(m_ptr, count);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr span<T>
  unsafe_last(std::size_t count) const & noexcept RELOCO_LIFETIMEBOUND {
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

  [[nodiscard]] constexpr reverse_iterator rbegin() & noexcept RELOCO_LIFETIMEBOUND { return reverse_iterator(end()); }
  [[nodiscard]] constexpr reverse_iterator rbegin() const & noexcept RELOCO_LIFETIMEBOUND {
    return reverse_iterator(end());
  }

  [[nodiscard]] constexpr reverse_iterator rend() & noexcept RELOCO_LIFETIMEBOUND { return reverse_iterator(begin()); }
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