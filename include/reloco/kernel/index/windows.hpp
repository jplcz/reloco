// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/index/windows.hpp
 * @brief `windows(n, w)`: every start position of a width-`w` sliding window that fits inside `n` elements.
 *
 * Moving averages, convolutions and pattern matching all slide a window across a sequence. A window that
 * starts at `s` is valid exactly when `s + w <= n`; `windows` generates only such starts, so the indexes inside the
 * window need no further check.
 *
 * See `docs/kernel/index.md`.
 */

#include "range.hpp"

#include <cstddef>

RELOCO_KERNEL_BEGIN

/**
 * @brief One window `[start, start + size)`, known to lie inside the extent; `size >= 1`.
 * @tparam Tag brand of the produced indexes.
 */
template <class Tag = unbranded> class window {
public:
  /** @brief Index of the first element as a plain integer. */
  [[nodiscard]] constexpr std::size_t start() const noexcept { return start_; }
  /** @brief Number of elements in the window. */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

  /** @brief The first element of the window. */
  [[nodiscard]] constexpr proven_index<Tag> first() const noexcept { return detail::index_access::make<Tag>(start_); }
  /** @brief The last element of the window. */
  [[nodiscard]] constexpr proven_index<Tag> last() const noexcept {
    return detail::index_access::make<Tag>(start_ + size_ - 1);
  }

  /** @brief All indexes inside the window, as proven indexes. */
  [[nodiscard]] constexpr index_range<Tag> indices() const noexcept {
    return index_range<Tag>(detail::index_access::key(), start_, start_ + size_, extent_);
  }

  constexpr window(detail::mint_key, std::size_t start, std::size_t size, std::size_t extent) noexcept
      : start_(start), size_(size), extent_(extent) {
    RELOCO_KERNEL_CHECK(size >= 1 && size <= extent && start <= extent - size,
                        "window: requires 1 <= size and start + size <= extent");
  }

private:
  std::size_t start_;
  std::size_t size_;
  std::size_t extent_;
};

/** @brief All windows of a given width inside an extent. A width of 0 yields no windows. */
template <class Tag = unbranded> class window_domain {
public:
  class iterator {
  public:
    using value_type = window<Tag>;
    using difference_type = std::ptrdiff_t;

    // NOLINTNEXTLINE(reloco-checked-operator): bounded by the domain; checks opt-in
    [[nodiscard]] constexpr window<Tag> operator*() const noexcept {
      return window<Tag>(detail::index_access::key(), cur_, width_, extent_);
    }

    constexpr iterator &operator++() & noexcept {
      ++cur_;
      return *this;
    }

    constexpr iterator operator++(int) & noexcept {
      iterator old(*this);
      ++cur_;
      return old;
    }

    [[nodiscard]] friend constexpr bool operator==(const iterator &a, const iterator &b) noexcept {
      return a.cur_ == b.cur_;
    }

  private:
    friend class window_domain;
    constexpr iterator(std::size_t cur, std::size_t width, std::size_t extent) noexcept
        : cur_(cur), width_(width), extent_(extent) {}

    std::size_t cur_;
    std::size_t width_;
    std::size_t extent_;
  };

  constexpr window_domain(detail::mint_key, std::size_t extent, std::size_t width) noexcept
      : extent_(extent), width_(width), count_(width == 0 || width > extent ? 0 : extent - width + 1) {}

  [[nodiscard]] constexpr iterator begin() const noexcept { return iterator(0, width_, extent_); }
  [[nodiscard]] constexpr iterator end() const noexcept { return iterator(count_, width_, extent_); }

  /** @brief Number of windows: `n - w + 1`, or 0 when `w == 0` or `w > n`. */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

private:
  std::size_t extent_;
  std::size_t width_;
  std::size_t count_;
};

/**
 * @brief Every window of `w` consecutive elements inside `n` elements.
 * @code
 * for (auto win : reloco::kernel::windows(8, 3)) {   // starts 0..5
 *   for (auto i : win.indices())                     // each i < 8, no check needed
 *     use(i.value());
 * }
 * @endcode
 */
[[nodiscard]] constexpr window_domain<> windows(std::size_t n, std::size_t w) noexcept {
  return window_domain<>(detail::index_access::key(), n, w);
}

RELOCO_KERNEL_END
