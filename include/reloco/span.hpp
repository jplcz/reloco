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
#include <tuple>
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

/**
 * @brief Rust `slice::rchunks` equivalent: like `span_chunks`, but starting from the back. The final chunk (the one at
 * the front of the span) may be shorter.
 */
template <typename T> class RELOCO_POINTER span_rchunks {
public:
  class RELOCO_POINTER iterator {
  public:
    using value_type = span<T>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    constexpr iterator(T *base, std::size_t remaining, std::size_t chunk_size) noexcept
        : m_base(base), m_remaining(remaining), m_chunk_size(chunk_size) {}

    RELOCO_CHECKED [[nodiscard]] constexpr span<T> operator*() const noexcept RELOCO_LIFETIMEBOUND {
      const std::size_t take = m_remaining < m_chunk_size ? m_remaining : m_chunk_size;
      return span<T>(m_base + (m_remaining - take), take);
    }

    constexpr iterator &operator++() & noexcept {
      m_remaining -= m_remaining < m_chunk_size ? m_remaining : m_chunk_size;
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
    T *m_base;
    std::size_t m_remaining;
    std::size_t m_chunk_size;
  };

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  constexpr span_rchunks(T *ptr, std::size_t size, std::size_t chunk_size) noexcept
      : m_ptr(ptr), m_size(size), m_chunk_size(chunk_size) {}

  [[nodiscard]] constexpr iterator begin() & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(m_ptr, m_size, m_chunk_size);
  }
  [[nodiscard]] constexpr iterator begin() const & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(m_ptr, m_size, m_chunk_size);
  }
  [[nodiscard]] constexpr iterator end() & noexcept RELOCO_LIFETIMEBOUND { return iterator(m_ptr, 0, m_chunk_size); }
  [[nodiscard]] constexpr iterator end() const & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(m_ptr, 0, m_chunk_size);
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
 * @brief Rust `slice::split` / `splitn` / `rsplit` equivalent: a lazy range of sub-views separated by the elements
 * for which `pred(element)` is true (the separators are not part of any piece). `n` separators give `n + 1` pieces
 * (some possibly empty); an empty span gives one empty piece. @p Reverse walks from the back. @p Pred is stored by
 * value and must be callable on a const object.
 */
template <typename T, typename Pred, bool Reverse> class RELOCO_POINTER span_split {
public:
  class RELOCO_POINTER iterator {
  public:
    using value_type = span<T>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    constexpr iterator(const span_split *owner, std::size_t lo, std::size_t hi, std::size_t pieces_left,
                       bool done) noexcept
        : m_owner(owner), m_lo(lo), m_hi(hi), m_pieces_left(pieces_left), m_done(done) {}

    RELOCO_CHECKED [[nodiscard]] constexpr span<T> operator*() const noexcept RELOCO_LIFETIMEBOUND {
      const piece p = locate();
      return span<T>(m_owner->m_ptr + p.start, p.end - p.start);
    }

    constexpr iterator &operator++() & noexcept {
      const piece p = locate();
      if (p.last) {
        m_done = true;
      } else {
        m_lo = p.next_lo;
        m_hi = p.next_hi;
        if (m_pieces_left != static_cast<std::size_t>(-1)) {
          --m_pieces_left;
        }
      }
      return *this;
    }

    constexpr iterator operator++(int) noexcept {
      iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    [[nodiscard]] constexpr bool operator==(const iterator &other) const noexcept {
      return m_done == other.m_done && (m_done || (m_lo == other.m_lo && m_hi == other.m_hi));
    }

    [[nodiscard]] constexpr bool operator!=(const iterator &other) const noexcept { return !(*this == other); }

  private:
    struct piece {
      std::size_t start;
      std::size_t end;
      std::size_t next_lo;
      std::size_t next_hi;
      bool last;
    };

    [[nodiscard]] constexpr piece locate() const noexcept {
      RELOCO_ASSERT(!m_done, "dereferencing the end of a split range");
      if (m_pieces_left == 1) {
        return piece{m_lo, m_hi, m_hi, m_hi, true};
      }
      if constexpr (Reverse) {
        for (std::size_t i = m_hi; i > m_lo; --i) {
          if (m_owner->m_pred(m_owner->m_ptr[i - 1])) {
            return piece{i, m_hi, m_lo, i - 1, false};
          }
        }
      } else {
        for (std::size_t i = m_lo; i < m_hi; ++i) {
          if (m_owner->m_pred(m_owner->m_ptr[i])) {
            return piece{m_lo, i, i + 1, m_hi, false};
          }
        }
      }
      return piece{m_lo, m_hi, m_hi, m_hi, true};
    }

    const span_split *m_owner;
    std::size_t m_lo;
    std::size_t m_hi;
    std::size_t m_pieces_left;
    bool m_done;
  };

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  /** @param max_pieces Upper bound on the number of pieces (`splitn`), `size_t(-1)` for unlimited. */
  constexpr span_split(T *ptr, std::size_t size, Pred pred, std::size_t max_pieces) noexcept
      : m_ptr(ptr), m_size(size), m_pred(std::move(pred)), m_max_pieces(max_pieces) {}

  [[nodiscard]] constexpr iterator begin() const & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(this, 0, m_size, m_max_pieces, m_max_pieces == 0);
  }
  [[nodiscard]] constexpr iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return iterator(this, 0, 0, 0, true); }

private:
  T *m_ptr;
  std::size_t m_size;
  Pred m_pred;
  std::size_t m_max_pieces;
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
  /// Read-only view of the same element type; the parameter type of the comparison/search helpers.
  using const_view = span<const std::remove_const_t<T>>;

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
  // mirrors std::span: implicit from arrays, containers and std::span, and to std::span. An empty container yields a
  // null pointer without calling data(), which reloco containers assert on when empty.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr span(Container &cont RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_ptr(cont.size() == 0 ? nullptr : cont.data()), m_size(cont.size()) {}

  template <typename Container, typename = std::enable_if_t<
                                    !std::is_same_v<std::decay_t<Container>, span> && !detail::is_span_v<Container> &&
                                    std::is_convertible_v<decltype(std::declval<const Container &>().data()), T *> &&
                                    std::is_integral_v<decltype(std::declval<const Container &>().size())>>>
  // mirrors std::span: implicit from arrays, containers and std::span, and to std::span.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr span(const Container &cont RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_ptr(cont.size() == 0 ? nullptr : cont.data()), m_size(cont.size()) {}

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
  [[nodiscard]] constexpr span<T> subspan(std::size_t offset,
                                          std::size_t count = static_cast<std::size_t>(-1)) const & noexcept {
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
  template <std::size_t Count> [[nodiscard]] constexpr span<T> subspan(std::size_t offset) const & noexcept {
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
  [[nodiscard]] result<span<T>> try_subspan(std::size_t offset,
                                            std::size_t count = static_cast<std::size_t>(-1)) const & noexcept {
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
  unsafe_subspan(std::size_t offset, std::size_t count = static_cast<std::size_t>(-1)) const & noexcept {
    RELOCO_DEBUG_ASSERT(offset <= m_size, "subspan offset exceeds span size");
    const std::size_t rem = m_size - offset;
    const std::size_t actual_count = count == static_cast<std::size_t>(-1) ? rem : count;
    RELOCO_DEBUG_ASSERT(actual_count <= rem, "subspan count exceeds remaining span size");
    return span<T>(pointer_at(offset), actual_count);
  }

  /**
   * @brief Subspan `[offset, offset + count)` with no check at all, in any build mode; see `unchecked_at`.
   * @details For loops whose range was proven once, outside the loop. An out-of-range request is undefined behavior.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr span<T> unchecked_subspan(std::size_t offset,
                                                                               std::size_t count) const & noexcept {
    return span<T>(pointer_at(offset), count);
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

  /**
   * @brief Accesses an element with no check at all, in any build mode.
   * @details For hot loops whose index range was proven once, outside the loop (for example by the kernel framework's
   * proven indexes). Unlike `unsafe_at`, which asserts in debug builds and lets the optimizer assume the index is in
   * range in release builds, this call never emits a check or an assumption. An out-of-range @p idx is undefined
   * behavior.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &
  unchecked_at(std::size_t idx) const & noexcept RELOCO_LIFETIMEBOUND {
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

  [[nodiscard]] constexpr span<T> first(std::size_t count) const & noexcept {
    RELOCO_ASSERT(count <= m_size, "first count exceeds span size");
    return span<T>(m_ptr, count);
  }

  [[nodiscard]] constexpr span<T> last(std::size_t count) const & noexcept {
    RELOCO_ASSERT(count <= m_size, "last count exceeds span size");
    return span<T>(pointer_at(m_size - count), count);
  }

  [[nodiscard]] result<span<T>> try_first(std::size_t count) const & noexcept {
    if (count > m_size)
      return unexpected(error::out_of_bounds);
    return span<T>(m_ptr, count);
  }

  [[nodiscard]] result<span<T>> try_last(std::size_t count) const & noexcept {
    if (count > m_size)
      return unexpected(error::out_of_bounds);
    return span<T>(pointer_at(m_size - count), count);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr span<T> unsafe_first(std::size_t count) const & noexcept {
    RELOCO_DEBUG_ASSERT(count <= m_size, "first count exceeds span size");
    return span<T>(m_ptr, count);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr span<T> unsafe_last(std::size_t count) const & noexcept {
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
  template <typename U>
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE span<U> unsafe_cast_to() const & noexcept RELOCO_LIFETIMEBOUND {
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
  template <typename Vec = vector<std::remove_const_t<T>>> [[nodiscard]] result<Vec> try_to_vector() const noexcept {
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

  // ===== Rust slice API: splitting =====
  //
  // `span` is a view, so "mutating" operations modify the elements it refers to through a `const` span, like `sort()`.
  // Every operation that can fail has a checked form (asserts) and a `try_` form that returns `result`.

  /** @brief Rust `split_first`: the first element and the rest. The span must not be empty. */
  [[nodiscard]] constexpr std::pair<T &, span<T>> split_first() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "split_first() called on empty span");
    return {*m_ptr, span<T>(pointer_at(1), m_size - 1)};
  }

  [[nodiscard]] result<std::pair<std::reference_wrapper<T>, span<T>>>
  try_split_first() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::pair<std::reference_wrapper<T>, span<T>>(std::ref(*m_ptr), span<T>(pointer_at(1), m_size - 1));
  }

  /** @brief Rust `split_last`: the last element and everything before it. The span must not be empty. */
  [[nodiscard]] constexpr std::pair<T &, span<T>> split_last() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "split_last() called on empty span");
    return {m_ptr[m_size - 1], span<T>(m_ptr, m_size - 1)};
  }

  [[nodiscard]] result<std::pair<std::reference_wrapper<T>, span<T>>>
  try_split_last() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::pair<std::reference_wrapper<T>, span<T>>(std::ref(m_ptr[m_size - 1]), span<T>(m_ptr, m_size - 1));
  }

  /** @brief Rust `take_first(n)`: returns the first @p count elements and removes them from this span. */
  constexpr span<T> split_off_first(std::size_t count) & noexcept {
    RELOCO_ASSERT(count <= m_size, "split_off_first count exceeds span size");
    const span<T> head(m_ptr, count);
    m_ptr = pointer_at(count);
    m_size -= count;
    return head;
  }

  [[nodiscard]] result<span<T>> try_split_off_first(std::size_t count) & noexcept {
    if (count > m_size)
      return unexpected(error::out_of_bounds);
    return split_off_first(count);
  }

  /** @brief Rust `take_last(n)`: returns the last @p count elements and removes them from this span. */
  constexpr span<T> split_off_last(std::size_t count) & noexcept {
    RELOCO_ASSERT(count <= m_size, "split_off_last count exceeds span size");
    m_size -= count;
    return span<T>(pointer_at(m_size), count);
  }

  [[nodiscard]] result<span<T>> try_split_off_last(std::size_t count) & noexcept {
    if (count > m_size)
      return unexpected(error::out_of_bounds);
    return split_off_last(count);
  }

  /** @brief Rust `chunks_exact`: like `chunks`, but drops the trailing short chunk (see `chunks_exact_remainder`). */
  [[nodiscard]] constexpr span_chunks<T> chunks_exact(std::size_t chunk_size) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(chunk_size > 0, "chunks_exact size must be non-zero");
    return span_chunks<T>(m_ptr, m_size - m_size % chunk_size, chunk_size);
  }

  /** @brief The trailing elements (fewer than @p chunk_size) that `chunks_exact(chunk_size)` leaves out. */
  [[nodiscard]] constexpr span<T> chunks_exact_remainder(std::size_t chunk_size) const & noexcept {
    RELOCO_ASSERT(chunk_size > 0, "chunks_exact size must be non-zero");
    const std::size_t rem = m_size % chunk_size;
    return span<T>(pointer_at(m_size - rem), rem);
  }

  /** @brief Rust `rchunks`: non-overlapping chunks of at most @p chunk_size elements, starting from the back. */
  [[nodiscard]] constexpr span_rchunks<T> rchunks(std::size_t chunk_size) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(chunk_size > 0, "rchunks size must be non-zero");
    return span_rchunks<T>(m_ptr, m_size, chunk_size);
  }

  /** @brief Rust `split`: sub-views separated by elements matching @p pred (see `span_split`). */
  template <typename Pred>
  [[nodiscard]] constexpr span_split<T, std::decay_t<Pred>, false>
  split(Pred &&pred) const & noexcept RELOCO_LIFETIMEBOUND {
    return span_split<T, std::decay_t<Pred>, false>(m_ptr, m_size, std::forward<Pred>(pred),
                                                    static_cast<std::size_t>(-1));
  }

  /** @brief Rust `splitn`: like `split`, but yields at most @p max_pieces pieces; the last holds the remainder. */
  template <typename Pred>
  [[nodiscard]] constexpr span_split<T, std::decay_t<Pred>, false>
  splitn(std::size_t max_pieces, Pred &&pred) const & noexcept RELOCO_LIFETIMEBOUND {
    return span_split<T, std::decay_t<Pred>, false>(m_ptr, m_size, std::forward<Pred>(pred), max_pieces);
  }

  /** @brief Rust `rsplit`: like `split`, but yields the pieces from the back. */
  template <typename Pred>
  [[nodiscard]] constexpr span_split<T, std::decay_t<Pred>, true>
  rsplit(Pred &&pred) const & noexcept RELOCO_LIFETIMEBOUND {
    return span_split<T, std::decay_t<Pred>, true>(m_ptr, m_size, std::forward<Pred>(pred),
                                                   static_cast<std::size_t>(-1));
  }

  /** @brief `true` if the span begins with @p prefix. */
  [[nodiscard]] bool starts_with(const_view prefix) const noexcept {
    return prefix.size() <= m_size && std::equal(prefix.data(), prefix.data() + prefix.size(), m_ptr);
  }

  /** @brief `true` if the span ends with @p suffix. */
  [[nodiscard]] bool ends_with(const_view suffix) const noexcept {
    return suffix.size() <= m_size &&
           std::equal(suffix.data(), suffix.data() + suffix.size(), pointer_at(m_size - suffix.size()));
  }

  /** @brief Rust `strip_prefix`: the span without @p prefix, or `nullopt` if it does not start with it. */
  [[nodiscard]] optional<span<T>> strip_prefix(const_view prefix) const noexcept {
    if (!starts_with(prefix))
      return optional<span<T>>(nullopt);
    return optional<span<T>>(span<T>(pointer_at(prefix.size()), m_size - prefix.size()));
  }

  /** @brief Rust `strip_suffix`: the span without @p suffix, or `nullopt` if it does not end with it. */
  [[nodiscard]] optional<span<T>> strip_suffix(const_view suffix) const noexcept {
    if (!ends_with(suffix))
      return optional<span<T>>(nullopt);
    return optional<span<T>>(span<T>(m_ptr, m_size - suffix.size()));
  }

  // ===== Rust slice API: searching =====

  /** @brief Rust `partition_point`: index of the first element for which @p pred is false, assuming the span is
   * partitioned (all `true` elements before all `false` ones). Binary search, O(log n). */
  template <typename Pred> [[nodiscard]] std::size_t partition_point(Pred pred) const noexcept {
    std::size_t lo = 0;
    std::size_t hi = m_size;
    while (lo < hi) {
      const std::size_t mid = lo + (hi - lo) / 2;
      if (pred(m_ptr[mid]))
        lo = mid + 1;
      else
        hi = mid;
    }
    return lo;
  }

  /** @brief Index of the first element not less than @p value in a span sorted by `operator<` (C++ `lower_bound`). */
  [[nodiscard]] std::size_t lower_bound(const T &value) const noexcept {
    return partition_point([&value](const T &item) { return item < value; });
  }

  /** @brief Index of the first element greater than @p value in a span sorted by `operator<` (C++ `upper_bound`). */
  [[nodiscard]] std::size_t upper_bound(const T &value) const noexcept {
    return partition_point([&value](const T &item) { return !(value < item); });
  }

  /** @brief Rust `rposition`: index of the last element equal to @p value, or `nullopt`. */
  [[nodiscard]] optional<std::size_t> rposition(const T &value) const noexcept {
    return rposition_if([&value](const T &item) { return item == value; });
  }

  /** @brief Index of the last element for which `pred(element)` is true, or `nullopt`. */
  template <typename Pred> [[nodiscard]] optional<std::size_t> rposition_if(Pred &&pred) const noexcept {
    for (std::size_t i = m_size; i > 0; --i) {
      if (pred(m_ptr[i - 1]))
        return i - 1;
    }
    return nullopt;
  }

  /** @brief First element equal to @p value, or `error::not_found`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_find(const T &value) const & noexcept RELOCO_LIFETIMEBOUND {
    return try_find_if([&value](const T &item) { return item == value; });
  }

  /** @brief First element for which `pred(element)` is true, or `error::not_found`. */
  template <typename Pred>
  [[nodiscard]] result<std::reference_wrapper<T>> try_find_if(Pred &&pred) const & noexcept RELOCO_LIFETIMEBOUND {
    for (std::size_t i = 0; i < m_size; ++i) {
      if (pred(m_ptr[i]))
        return std::ref(m_ptr[i]);
    }
    return unexpected(error::not_found);
  }

  /** @brief `true` if `pred(element)` holds for at least one element. */
  template <typename Pred> [[nodiscard]] bool any(Pred &&pred) const noexcept { return position_if(pred).has_value(); }

  /** @brief `true` if `pred(element)` holds for every element (vacuously `true` when empty). */
  template <typename Pred> [[nodiscard]] bool all(Pred &&pred) const noexcept {
    for (std::size_t i = 0; i < m_size; ++i) {
      if (!pred(m_ptr[i]))
        return false;
    }
    return true;
  }

  /** @brief Number of elements for which `pred(element)` is true. */
  template <typename Pred> [[nodiscard]] std::size_t count_if(Pred &&pred) const noexcept {
    std::size_t n = 0;
    for (std::size_t i = 0; i < m_size; ++i) {
      if (pred(m_ptr[i]))
        ++n;
    }
    return n;
  }

  /** @brief Number of elements equal to @p value. */
  [[nodiscard]] std::size_t count(const T &value) const noexcept {
    return count_if([&value](const T &item) { return item == value; });
  }

  /** @brief Rust `max_by`: the last maximal element under the strict weak order @p less, or `container_empty`. */
  template <typename Less>
  [[nodiscard]] result<std::reference_wrapper<T>> try_max_by(Less less) const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    std::size_t best = 0;
    for (std::size_t i = 1; i < m_size; ++i) {
      if (!less(m_ptr[i], m_ptr[best]))
        best = i;
    }
    return std::ref(m_ptr[best]);
  }

  /** @brief Rust `min_by`: the first minimal element under the strict weak order @p less, or `container_empty`. */
  template <typename Less>
  [[nodiscard]] result<std::reference_wrapper<T>> try_min_by(Less less) const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    std::size_t best = 0;
    for (std::size_t i = 1; i < m_size; ++i) {
      if (less(m_ptr[i], m_ptr[best]))
        best = i;
    }
    return std::ref(m_ptr[best]);
  }

  /** @brief Rust `max_by_key`: the last element with the largest `key(element)` (compared with `<`). */
  template <typename Key>
  [[nodiscard]] result<std::reference_wrapper<T>> try_max_by_key(Key key) const & noexcept RELOCO_LIFETIMEBOUND {
    return try_max_by([&key](const T &a, const T &b) { return key(a) < key(b); });
  }

  /** @brief Rust `min_by_key`: the first element with the smallest `key(element)` (compared with `<`). */
  template <typename Key>
  [[nodiscard]] result<std::reference_wrapper<T>> try_min_by_key(Key key) const & noexcept RELOCO_LIFETIMEBOUND {
    return try_min_by([&key](const T &a, const T &b) { return key(a) < key(b); });
  }

  /** @brief Largest element by `operator<` (the last one among equals), or `container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_max() const & noexcept RELOCO_LIFETIMEBOUND {
    return try_max_by([](const T &a, const T &b) { return a < b; });
  }

  /** @brief Smallest element by `operator<` (the first one among equals), or `container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_min() const & noexcept RELOCO_LIFETIMEBOUND {
    return try_min_by([](const T &a, const T &b) { return a < b; });
  }

  /** @brief `{min, max}` in one pass, or `container_empty`. */
  [[nodiscard]] result<std::pair<std::reference_wrapper<T>, std::reference_wrapper<T>>>
  try_minmax() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    std::size_t lo = 0;
    std::size_t hi = 0;
    for (std::size_t i = 1; i < m_size; ++i) {
      if (m_ptr[i] < m_ptr[lo])
        lo = i;
      if (!(m_ptr[i] < m_ptr[hi]))
        hi = i;
    }
    return std::pair<std::reference_wrapper<T>, std::reference_wrapper<T>>(std::ref(m_ptr[lo]), std::ref(m_ptr[hi]));
  }

  /** @brief `true` if the elements are in non-descending order by `operator<`. */
  [[nodiscard]] bool is_sorted() const noexcept {
    return is_sorted_by([](const T &a, const T &b) { return a < b; });
  }

  /** @brief `true` if no element is ordered before its predecessor under @p less. */
  template <typename Less> [[nodiscard]] bool is_sorted_by(Less less) const noexcept {
    for (std::size_t i = 1; i < m_size; ++i) {
      if (less(m_ptr[i], m_ptr[i - 1]))
        return false;
    }
    return true;
  }

  /** @brief `true` if `key(element)` is non-descending. */
  template <typename Key> [[nodiscard]] bool is_sorted_by_key(Key key) const noexcept {
    return is_sorted_by([&key](const T &a, const T &b) { return key(a) < key(b); });
  }

  /**
   * @brief Rust `select_nth_unstable`: rearranges the elements so that the one at @p nth is where it would be in sorted
   * order, everything before it is not greater and everything after it not less. Returns `{before, nth, after}`.
   */
  [[nodiscard]] std::tuple<span<T>, T &, span<T>> select_nth_unstable(std::size_t nth) const & noexcept {
    return select_nth_unstable_by(nth, [](const T &a, const T &b) { return a < b; });
  }

  template <typename Less>
  [[nodiscard]] std::tuple<span<T>, T &, span<T>> select_nth_unstable_by(std::size_t nth, Less less) const & noexcept {
    static_assert(!std::is_const_v<T>, "select_nth_unstable requires a non-const span");
    RELOCO_ASSERT(nth < m_size, "select_nth_unstable index exceeds span size");
    std::nth_element(m_ptr, m_ptr + nth, m_ptr + m_size, less);
    return {span<T>(m_ptr, nth), m_ptr[nth], span<T>(pointer_at(nth + 1), m_size - nth - 1)};
  }

  [[nodiscard]] result<std::tuple<span<T>, std::reference_wrapper<T>, span<T>>>
  try_select_nth_unstable(std::size_t nth) const & noexcept {
    if (nth >= m_size)
      return unexpected(error::out_of_bounds);
    auto parts = select_nth_unstable(nth);
    return std::tuple<span<T>, std::reference_wrapper<T>, span<T>>(std::get<0>(parts), std::ref(std::get<1>(parts)),
                                                                   std::get<2>(parts));
  }

  // ===== Rust slice API: in-place mutation (non-const T only) =====

  /** @brief Reverses the order of the elements. */
  void reverse() const & noexcept {
    static_assert(!std::is_const_v<T>, "reverse requires a non-const span");
    std::reverse(m_ptr, m_ptr + m_size);
  }

  /** @brief Rotates left by @p mid: the element at index @p mid becomes the first. */
  void rotate_left(std::size_t mid) const & noexcept {
    static_assert(!std::is_const_v<T>, "rotate_left requires a non-const span");
    RELOCO_ASSERT(mid <= m_size, "rotate_left amount exceeds span size");
    std::rotate(m_ptr, m_ptr + mid, m_ptr + m_size);
  }

  [[nodiscard]] result<void> try_rotate_left(std::size_t mid) const & noexcept {
    if (mid > m_size)
      return unexpected(error::out_of_bounds);
    rotate_left(mid);
    return {};
  }

  /** @brief Rotates right by @p count: the last @p count elements move to the front. */
  void rotate_right(std::size_t count) const & noexcept {
    static_assert(!std::is_const_v<T>, "rotate_right requires a non-const span");
    RELOCO_ASSERT(count <= m_size, "rotate_right amount exceeds span size");
    std::rotate(m_ptr, m_ptr + (m_size - count), m_ptr + m_size);
  }

  [[nodiscard]] result<void> try_rotate_right(std::size_t count) const & noexcept {
    if (count > m_size)
      return unexpected(error::out_of_bounds);
    rotate_right(count);
    return {};
  }

  /** @brief Assigns @p value to every element. */
  void fill(const T &value) const & noexcept {
    static_assert(!std::is_const_v<T>, "fill requires a non-const span");
    std::fill(m_ptr, m_ptr + m_size, value);
  }

  /** @brief Assigns `fn()` to every element, in order. */
  template <typename F> void fill_with(F &&fn) const & noexcept {
    static_assert(!std::is_const_v<T>, "fill_with requires a non-const span");
    for (std::size_t i = 0; i < m_size; ++i) {
      m_ptr[i] = fn();
    }
  }

  /** @brief Rust `swap(a, b)`: exchanges the elements at indices @p a and @p b. */
  void swap_at(std::size_t a, std::size_t b) const & noexcept {
    static_assert(!std::is_const_v<T>, "swap_at requires a non-const span");
    RELOCO_ASSERT(a < m_size && b < m_size, "swap_at index out of bounds");
    using std::swap;
    swap(m_ptr[a], m_ptr[b]);
  }

  [[nodiscard]] result<void> try_swap_at(std::size_t a, std::size_t b) const & noexcept {
    if (a >= m_size || b >= m_size)
      return unexpected(error::out_of_bounds);
    swap_at(a, b);
    return {};
  }

  /** @brief `true` if this span and @p other share at least one element's storage. */
  [[nodiscard]] bool overlaps(const_view other) const noexcept {
    if (m_size == 0 || other.size() == 0)
      return false;
    const std::less<const void *> lt{};
    const void *const a_begin = m_ptr;
    const void *const a_end = m_ptr + m_size;
    const void *const b_begin = other.data();
    const void *const b_end = other.data() + other.size();
    return lt(a_begin, b_end) && lt(b_begin, a_end);
  }

  /** @brief Rust `swap_with_slice`: exchanges the contents with @p other (same length, no overlap). */
  void swap_with_span(span<T> other) const & noexcept {
    static_assert(!std::is_const_v<T>, "swap_with_span requires a non-const span");
    RELOCO_ASSERT(other.size() == m_size, "swap_with_span length mismatch");
    RELOCO_ASSERT(!overlaps(other), "swap_with_span spans must not overlap");
    std::swap_ranges(m_ptr, m_ptr + m_size, other.data());
  }

  [[nodiscard]] result<void> try_swap_with_span(span<T> other) const & noexcept {
    if (other.size() != m_size || overlaps(other))
      return unexpected(error::invalid_argument);
    swap_with_span(other);
    return {};
  }

  /** @brief Rust `copy_from_slice` / `clone_from_slice`: copy-assigns @p src (same length, no overlap). */
  void copy_from(const_view src) const & noexcept {
    static_assert(!std::is_const_v<T>, "copy_from requires a non-const span");
    RELOCO_ASSERT(src.size() == m_size, "copy_from length mismatch");
    RELOCO_ASSERT(!overlaps(src), "copy_from spans must not overlap");
    std::copy(src.data(), src.data() + src.size(), m_ptr);
  }

  [[nodiscard]] result<void> try_copy_from(const_view src) const & noexcept {
    if (src.size() != m_size || overlaps(src))
      return unexpected(error::invalid_argument);
    copy_from(src);
    return {};
  }

  /** @brief Rust `copy_within`: copies `[offset, offset + count)` to start at @p dest; the ranges may overlap. */
  void copy_within(std::size_t offset, std::size_t count, std::size_t dest) const & noexcept {
    static_assert(!std::is_const_v<T>, "copy_within requires a non-const span");
    RELOCO_ASSERT(offset <= m_size && count <= m_size - offset, "copy_within source out of bounds");
    RELOCO_ASSERT(dest <= m_size - count, "copy_within destination out of bounds");
    if (dest == offset || count == 0)
      return;
    if (dest < offset)
      std::copy(m_ptr + offset, m_ptr + offset + count, m_ptr + dest);
    else
      std::copy_backward(m_ptr + offset, m_ptr + offset + count, m_ptr + dest + count);
  }

  [[nodiscard]] result<void> try_copy_within(std::size_t offset, std::size_t count, std::size_t dest) const & noexcept {
    if (offset > m_size || count > m_size - offset || dest > m_size - count)
      return unexpected(error::out_of_bounds);
    copy_within(offset, count, dest);
    return {};
  }

  /** @brief Reorders so that elements satisfying @p pred come first; returns how many do. Not stable. */
  template <typename Pred> std::size_t partition(Pred pred) const & noexcept {
    static_assert(!std::is_const_v<T>, "partition requires a non-const span");
    return static_cast<std::size_t>(std::partition(m_ptr, m_ptr + m_size, pred) - m_ptr);
  }

  /** @brief Like `partition`, preserving the relative order within both groups. */
  template <typename Pred> std::size_t stable_partition(Pred pred) const & noexcept {
    static_assert(!std::is_const_v<T>, "stable_partition requires a non-const span");
    return static_cast<std::size_t>(std::stable_partition(m_ptr, m_ptr + m_size, pred) - m_ptr);
  }

  // ===== Rust iterator-style helpers that need no allocation =====

  /** @brief Calls `fn(element)` for every element, in order. */
  template <typename F> void for_each(F &&fn) const & noexcept {
    for (std::size_t i = 0; i < m_size; ++i) {
      fn(m_ptr[i]);
    }
  }

  /** @brief Rust `fold`: `acc = fn(std::move(acc), element)` over all elements; returns the final accumulator. */
  template <typename Acc, typename F> [[nodiscard]] Acc fold(Acc init, F &&fn) const noexcept {
    for (std::size_t i = 0; i < m_size; ++i) {
      init = fn(std::move(init), m_ptr[i]);
    }
    return init;
  }

  // ===== comparison =====

  /** @brief `true` if both spans have the same length and equal elements (`operator==`). */
  [[nodiscard]] bool equals(const_view other) const noexcept {
    return other.size() == m_size && std::equal(m_ptr, m_ptr + m_size, other.data());
  }

  /** @brief Lexicographic three-way comparison with `operator<`: negative, zero or positive. */
  [[nodiscard]] int compare(const_view other) const noexcept {
    const std::size_t common = m_size < other.size() ? m_size : other.size();
    for (std::size_t i = 0; i < common; ++i) {
      if (m_ptr[i] < other.data()[i])
        return -1;
      if (other.data()[i] < m_ptr[i])
        return 1;
    }
    if (m_size == other.size())
      return 0;
    return m_size < other.size() ? -1 : 1;
  }

  // ===== ASCII helpers (byte-like element types) =====

  /** @brief `true` if every byte is below 0x80. */
  [[nodiscard]] bool is_ascii() const noexcept {
    static_assert(is_byte_like_v<T>, "is_ascii requires a byte-like span");
    for (std::size_t i = 0; i < m_size; ++i) {
      if (static_cast<unsigned char>(m_ptr[i]) >= 0x80)
        return false;
    }
    return true;
  }

  /** @brief ASCII case-insensitive equality (letters only; other bytes compare exactly). */
  [[nodiscard]] bool eq_ignore_ascii_case(const_view other) const noexcept {
    static_assert(is_byte_like_v<T>, "eq_ignore_ascii_case requires a byte-like span");
    if (other.size() != m_size)
      return false;
    for (std::size_t i = 0; i < m_size; ++i) {
      if (ascii_lower(static_cast<unsigned char>(m_ptr[i])) != ascii_lower(static_cast<unsigned char>(other.data()[i])))
        return false;
    }
    return true;
  }

  /** @brief Converts ASCII letters to upper case in place. */
  void make_ascii_uppercase() const & noexcept {
    static_assert(is_byte_like_v<T> && !std::is_const_v<T>, "make_ascii_uppercase requires a non-const byte-like span");
    for (std::size_t i = 0; i < m_size; ++i) {
      const unsigned char c = static_cast<unsigned char>(m_ptr[i]);
      if (c >= 'a' && c <= 'z')
        m_ptr[i] = static_cast<std::remove_cv_t<T>>(static_cast<unsigned char>(c - ('a' - 'A')));
    }
  }

  /** @brief Converts ASCII letters to lower case in place. */
  void make_ascii_lowercase() const & noexcept {
    static_assert(is_byte_like_v<T> && !std::is_const_v<T>, "make_ascii_lowercase requires a non-const byte-like span");
    for (std::size_t i = 0; i < m_size; ++i) {
      m_ptr[i] = static_cast<std::remove_cv_t<T>>(ascii_lower(static_cast<unsigned char>(m_ptr[i])));
    }
  }

private:
  [[nodiscard]] static constexpr unsigned char ascii_lower(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>((c | 0x20u) & 0xFFu) : c;
  }

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