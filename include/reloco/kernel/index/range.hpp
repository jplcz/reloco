// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/index/range.hpp
 * @brief `index_range<Tag>`: a half-open range of proven indexes, and the plain `domain(n)` / `tail<B>(n)` factories.
 *
 * `index_range` is the building block the other domains hand out (the tail of a blocked loop, the lanes of a
 * block, the indexes inside a window). It iterates `proven_index<Tag>` values in `[first, last)` where
 * `last <= extent`, so every value it yields is `< extent`.
 *
 * See `docs/kernel/index.md`.
 */

#include "proven_index.hpp"

#include <cstddef>

RELOCO_KERNEL_BEGIN

/**
 * @brief Iterates the proven indexes `[first, last)` of an extent.
 * @tparam Tag brand of the produced indexes.
 * @details Works with range-based `for`. The iterator is an input-style iterator: it only supports `*`, `++` and
 * comparison, which is all range-based `for` needs.
 */
template <class Tag = unbranded> class index_range {
public:
  class iterator {
  public:
    using value_type = proven_index<Tag>;
    using difference_type = std::ptrdiff_t;

    /** @brief The current proven index; must not be called on `end()`. */
    // NOLINTNEXTLINE(reloco-checked-operator): bounded by the domain; checks opt-in
    [[nodiscard]] constexpr proven_index<Tag> operator*() const noexcept {
      RELOCO_KERNEL_CHECK(cur_ < last_, "index_range: dereferenced an end iterator");
      return detail::index_access::make<Tag>(cur_);
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

    [[nodiscard]] friend constexpr bool operator==(const iterator &a, const iterator &b) noexcept = default;

  private:
    friend class index_range;
    constexpr iterator(std::size_t cur, std::size_t last) noexcept : cur_(cur), last_(last) {}

    std::size_t cur_;
    std::size_t last_;
  };

  /**
   * @brief Creates the range `[first, last)` of an extent.
   * @param key passkey; only the framework can create one (see `proven_index.hpp`).
   * @param first first index of the range.
   * @param last one past the last index of the range.
   * @param extent the size the indexes must stay below; requires `first <= last <= extent`.
   */
  constexpr index_range(detail::mint_key, std::size_t first, std::size_t last,
                        [[maybe_unused]] std::size_t extent) noexcept
      : first_(first), last_(last) {
    RELOCO_KERNEL_CHECK(first <= last && last <= extent, "index_range: requires first <= last <= extent");
  }

  [[nodiscard]] constexpr iterator begin() const noexcept { return iterator(first_, last_); }
  [[nodiscard]] constexpr iterator end() const noexcept { return iterator(last_, last_); }

  /** @brief Number of indexes in the range. */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return last_ - first_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return first_ == last_; }

  /** @brief First index of the range as a plain integer. */
  [[nodiscard]] constexpr std::size_t first() const noexcept { return first_; }
  /** @brief One past the last index of the range as a plain integer. */
  [[nodiscard]] constexpr std::size_t last() const noexcept { return last_; }

private:
  std::size_t first_;
  std::size_t last_;
};

/**
 * @brief The indexes `0 .. n-1`.
 * @param n the extent; every produced index is `< n`.
 * @code
 * std::size_t total = 0;
 * for (auto i : reloco::kernel::domain(5))   // i is a proven_index: 0, 1, 2, 3, 4
 *   total += i.value();
 * @endcode
 */
[[nodiscard]] constexpr index_range<> domain(std::size_t n) noexcept {
  return index_range<>(detail::index_access::key(), 0, n, n);
}

/**
 * @brief The leftover indexes of a loop that processed `B` elements at a time: `[n - n % B, n)`.
 * @tparam B the block width; must be positive.
 * @details Together with `blocks<B>(n)` it covers `[0, n)` exactly once. It holds fewer than `B` indexes.
 */
template <std::size_t B> [[nodiscard]] constexpr index_range<> tail(std::size_t n) noexcept {
  static_assert(B > 0, "tail: block width must be positive");
  return index_range<>(detail::index_access::key(), n - n % B, n, n);
}

RELOCO_KERNEL_END
