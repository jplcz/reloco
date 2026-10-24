// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file span.hpp
 * @brief Hardened C++17-compatible non-owning contiguous view. */

#include "byte_like.hpp"
#include "contiguous_iterator.hpp"
#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "iterator.hpp"
#include "lifetime.hpp"
#include "optional.hpp"
#include "relocatable_std.hpp"
#include "rvalue_safety.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

// This class is the checked boundary around the raw pointer arithmetic needed
// to implement a C++17-compatible contiguous view.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T> class vector; // complete type needed only where try_to_vector() is called (include vector.hpp)

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

namespace detail {

/** @brief `true` iff @p U is some `span<X>` instantiation (any @p X,
 * including cv-qualified). Used to keep the generic container
 * constructors below from ever intercepting a `span`-to-`span` argument
 * (e.g. `span<std::byte>` converting to `span<const std::byte>`) --
 * `span<X>` itself satisfies the container constructors' own
 * `.data()`/`.size()` shape, so without this exclusion an rvalue
 * `span<X>` prefers binding to the container constructors' `Container &&`
 * overload (a non-converting exact match) over the dedicated,
 * cross-qualification `span(const span<U> &)` converting constructor --
 * and the former is deliberately `= delete`d to prevent binding a
 * *non*-span rvalue container (whose `.data()` would dangle once the
 * temporary is destroyed). */
template <typename U> struct is_span : std::false_type {};
template <typename U> struct is_span<span<U>> : std::true_type {};
template <typename U> constexpr inline bool is_span_v = is_span<std::decay_t<U>>::value;

} // namespace detail

/**
 * @brief Rust `slice::chunks` equivalent: a lazy, non-overlapping forward
 * range of `span<T>` sub-views, each of at most `chunk_size` elements (the
 * final chunk may be shorter). Never allocates; each dereference produces
 * a fresh `span<T>` computed from the current position.
 */
template <typename T> class RELOCO_POINTER span_chunks {
public:
  class RELOCO_POINTER iterator {
  public:
    using value_type = span<T>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    constexpr iterator(T *ptr, std::size_t remaining, std::size_t chunk_size) noexcept
        : m_ptr(ptr), m_remaining(remaining), m_chunk_size(chunk_size) {}

    RELOCO_CHECKED [[nodiscard]] constexpr span<T> operator*() const noexcept RELOCO_LIFETIMEBOUND {
      return span<T>(m_ptr, m_remaining < m_chunk_size ? m_remaining : m_chunk_size);
    }

    constexpr iterator &operator++() & noexcept {
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
  class RELOCO_POINTER iterator {
  public:
    using value_type = span<T>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    constexpr iterator(T *ptr, std::size_t remaining_windows, std::size_t window_size) noexcept
        : m_ptr(ptr), m_remaining_windows(remaining_windows), m_window_size(window_size) {}

    RELOCO_CHECKED [[nodiscard]] constexpr span<T> operator*() const noexcept RELOCO_LIFETIMEBOUND {
      return span<T>(m_ptr, m_window_size);
    }

    constexpr iterator &operator++() & noexcept {
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

  RELOCO_BLOCK_RVALUE_ACCESS(T);
  RELOCO_GENERATE_VIEW_ITERATORS(T, m_ptr, m_size)
  RELOCO_GENERATE_ITER()

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
  // mirrors std::span: implicit from arrays, containers and std::span, and to std::span.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr span(T (&arr RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS)[N]) noexcept : m_ptr(arr), m_size(N) {}

  /**
   * @brief Constructs a span from a compatible reloco span.
   * @tparam U Source element type.
   * @param other Source span. Not lifetimebound: the result borrows what `other` views, not `other` itself,
   *              so converting a temporary span (e.g. `span<const T>(s.subspan(a, n))`) is safe.
   */
  template <typename U, std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>, int> = 0>
  // mirrors std::span: implicit from arrays, containers and std::span, and to std::span.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr span(const span<U> &other) noexcept : m_ptr(other.data()), m_size(other.size()) {}

  template <typename Container, typename = std::enable_if_t<
                                    // Prevent hijacking the copy/move constructors, and defer to the
                                    // dedicated span(const span<U> &) converting constructor above for
                                    // any span<X> (see detail::is_span_v's doc comment).
                                    !std::is_same_v<std::decay_t<Container>, span> && !detail::is_span_v<Container> &&
                                    // Ensure the container has a .data() that converts to T*
                                    std::is_convertible_v<decltype(std::declval<Container &>().data()), T *> &&
                                    // Ensure the container has a .size() that returns an integer
                                    std::is_integral_v<decltype(std::declval<Container &>().size())>>>
  // mirrors std::span: implicit from arrays, containers and std::span, and to std::span.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr span(Container &cont RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_ptr(cont.data()), m_size(cont.size()) {}

  template <typename Container, typename = std::enable_if_t<
                                    !std::is_same_v<std::decay_t<Container>, span> && !detail::is_span_v<Container> &&
                                    std::is_convertible_v<decltype(std::declval<const Container &>().data()), T *> &&
                                    std::is_integral_v<decltype(std::declval<const Container &>().size())>>>
  // mirrors std::span: implicit from arrays, containers and std::span, and to std::span.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr span(const Container &cont RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_ptr(cont.data()), m_size(cont.size()) {}

  template <typename Container, typename = std::enable_if_t<
                                    !std::is_same_v<std::decay_t<Container>, span> && !detail::is_span_v<Container> &&
                                    std::is_convertible_v<decltype(std::declval<Container &>().data()), T *> &&
                                    std::is_integral_v<decltype(std::declval<Container &>().size())>>>
  constexpr span(Container &&) = delete;

#if RELOCO_HAS_STD_SPAN
  // std-interop-begin: opt-in conversions to/from std::span for interop
  // with APIs that already speak <span>; reloco::span needs neither.
  /**
   * @brief Constructs a `reloco::span` from any `std::span` (dynamic or
   * static extent).
   * @tparam U Element type (supports cv-qualifier conversion like const
   * propagation).
   * @tparam Extent The static extent of the standard span.
   */
  template <typename U, std::size_t Extent>
    requires(std::is_convertible_v<U (*)[], T (*)[]>)
  // mirrors std::span: implicit from arrays, containers and std::span, and to std::span.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr span(std::span<U, Extent> s) noexcept : m_ptr(s.data()), m_size(s.size()) {}

  /**
   * @brief Implicit conversion operator to `std::span<T>`.
   * @return An equivalent dynamic-extent `std::span<T>` covering the same
   * buffer.
   */
  // mirrors std::span: implicit from arrays, containers and std::span, and to std::span.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  [[nodiscard]] constexpr operator std::span<T>() const & noexcept { return std::span<T>(m_ptr, m_size); }

  operator std::span<T>() const && = delete;
  // std-interop-end
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
  RELOCO_CHECKED [[nodiscard]] constexpr T &operator[](std::size_t idx) const & noexcept RELOCO_LIFETIMEBOUND {
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
    // NOLINTNEXTLINE(reloco-banned-construct): byte view of the elements, within the span bounds.
    return span<const std::byte>(reinterpret_cast<const std::byte *>(m_ptr), size_bytes());
  }

  /**
   * @brief Reinterprets the elements as a smaller (or equal) unit @p U: `span<uint32_t>` to `span<uint8_t>`,
   * `span<char>` to `span<uint8_t>`, `span<const Foo>` to `span<const char>`.
   *
   * Compile-time requirements: `sizeof(U)` divides `sizeof(T)`, `alignof(U) <= alignof(T)`, `T` is trivially
   * copyable, const is never dropped, and @p U is byte-like (see `is_byte_like_v`) or the same type as `T` modulo
   * cv (anything else would be a strict-aliasing violation; use `unsafe_cast_to` if you really mean it). The
   * result has `size() * sizeof(T) / sizeof(U)` elements.
   */
  template <typename U> [[nodiscard]] span<U> cast_to() const & noexcept RELOCO_LIFETIMEBOUND {
    static_assert(is_byte_like_v<U> || std::is_same_v<std::remove_cv_t<U>, std::remove_cv_t<T>>,
                  "cast_to<U> only views memory as a byte-like type; use unsafe_cast_to for other types");
    return unsafe_cast_to<U>();
  }

  /**
   * @brief Like `cast_to` but without the byte-like restriction on @p U: any trivially copyable `U` for which
   * `sizeof(U)` divides `sizeof(T)` and `alignof(U) <= alignof(T)`. Reading through the result can violate strict
   * aliasing (e.g. `uint64_t` as `uint32_t`), so it is a deliberate opt-in.
   */
  template <typename U> [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE span<U> unsafe_cast_to() const & noexcept
      RELOCO_LIFETIMEBOUND {
    using From = std::remove_cv_t<T>;
    using To = std::remove_cv_t<U>;
    static_assert(!std::is_const_v<T> || std::is_const_v<U>, "cast_to cannot drop const");
    static_assert(std::is_trivially_copyable_v<From> && std::is_trivially_copyable_v<To>,
                  "cast_to requires trivially copyable element types");
    static_assert(sizeof(From) % sizeof(To) == 0, "sizeof(U) must divide sizeof(T)");
    static_assert(alignof(To) <= alignof(From), "U must not be more strictly aligned than T");
    // NOLINTNEXTLINE(reloco-banned-construct): same bytes, smaller unit, count scaled accordingly.
    return span<U>(reinterpret_cast<U *>(m_ptr), m_size * (sizeof(From) / sizeof(To)));
  }

  /// `char` view of the same memory (`span<const char>` for const `T`).
  [[nodiscard]] auto as_chars() const & noexcept RELOCO_LIFETIMEBOUND {
    return cast_to<std::conditional_t<std::is_const_v<T>, const char, char>>();
  }

  /// `uint8_t` view of the same memory (`span<const uint8_t>` for const `T`).
  [[nodiscard]] auto as_uint8() const & noexcept RELOCO_LIFETIMEBOUND {
    return cast_to<std::conditional_t<std::is_const_v<T>, const unsigned char, unsigned char>>();
  }

  /// Writable `std::byte` view (requires non-const `T`).
  [[nodiscard]] span<std::byte> as_writable_bytes() const & noexcept RELOCO_LIFETIMEBOUND {
    static_assert(!std::is_const_v<T>, "as_writable_bytes requires a non-const span");
    return cast_to<std::byte>();
  }

  /**
   * @brief Checked "upcast" for parsing: views a byte-like span as `U` elements. Fails with
   * `error::invalid_argument` if the data is not aligned for `U` or the byte count is not a multiple of
   * `sizeof(U)`. `T` must be byte-like and `U` trivially copyable (const is preserved).
   */
  template <typename U> [[nodiscard]] result<span<U>> try_cast_to() const noexcept {
    static_assert(is_byte_like_v<T>, "try_cast_to is for byte-like spans; use cast_to to go the other way");
    static_assert(std::is_trivially_copyable_v<std::remove_cv_t<U>>, "try_cast_to requires a trivially copyable U");
    static_assert(!std::is_const_v<T> || std::is_const_v<U>, "try_cast_to cannot drop const");
    if (m_size % sizeof(U) != 0) {
      return unexpected(error::invalid_argument);
    }
    // NOLINTNEXTLINE(reloco-banned-construct): alignment check on the address value.
    if (m_size != 0 && reinterpret_cast<std::uintptr_t>(m_ptr) % alignof(U) != 0) {
      return unexpected(error::invalid_argument);
    }
    // NOLINTNEXTLINE(reloco-banned-construct): validated above; U is trivially copyable.
    return span<U>(reinterpret_cast<U *>(m_ptr), m_size / sizeof(U));
  }

  /**
   * @brief Copies the elements into a new owning `vector` (default allocator). Include `vector.hpp` where this is
   * called; `Vec` defaults to `vector<remove_const_t<T>>`, so the element type must be copy-constructible.
   */
  template <typename Vec = vector<std::remove_const_t<T>>>
  [[nodiscard]] result<Vec> try_to_vector() const noexcept {
    return Vec::try_create_from(span<const std::remove_const_t<T>>(m_ptr, m_size));
  }

  /// `try_to_vector()` with an explicit `allocator_ref`.
  template <typename Alloc, typename Vec = vector<std::remove_const_t<T>>>
  [[nodiscard]] result<Vec> try_to_vector(Alloc alloc) const noexcept {
    return Vec::try_allocate_from(alloc, span<const std::remove_const_t<T>>(m_ptr, m_size));
  }

  /**
   * @brief Index of the first element equal to @p value, or `nullopt`.
   */
  [[nodiscard]] optional<std::size_t> position(const T &value) const noexcept {
    return position_if([&value](const T &item) noexcept { return item == value; });
  }

  /**
   * @brief Index of the first element for which `pred(element)` is true, or `nullopt`.
   */
  template <typename Pred> [[nodiscard]] optional<std::size_t> position_if(Pred &&pred) const noexcept {
    for (std::size_t i = 0; i < m_size; ++i) {
      if (pred(m_ptr[i]))
        return i;
    }
    return nullopt;
  }

  /**
   * @brief Maps every element through `fn` into a new owning `Vec` (default: `vector` of the mapped type) with a
   * single up-front reservation. Include `vector.hpp` where this is called. Fails with the allocation error; the
   * partial result is dropped.
   */
  template <typename F, typename Vec = vector<std::decay_t<std::invoke_result_t<F &, const std::remove_const_t<T> &>>>>
  [[nodiscard]] result<Vec> try_map_to(F &&fn) const noexcept {
    auto out = Vec::try_create();
    if (!out)
      return out;
    if (auto r = out->try_reserve(m_size); !r)
      return unexpected(r.error());
    for (std::size_t i = 0; i < m_size; ++i) {
      if (auto r = out->try_push_back(fn(m_ptr[i])); !r)
        return unexpected(r.error());
    }
    return out;
  }

private:
  [[nodiscard]] constexpr T *pointer_at(std::size_t offset) const noexcept RELOCO_LIFETIMEBOUND {
    return offset == 0 ? m_ptr : m_ptr + offset;
  }

  T *m_ptr;
  std::size_t m_size;
};

template <typename T, std::size_t N> constexpr span<T> array<T, N>::as_span() & noexcept { return span<T>(data_, N); }

template <typename T, std::size_t N> constexpr span<const T> array<T, N>::as_span() const & noexcept {
  return span<const T>(data_, N);
}

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE