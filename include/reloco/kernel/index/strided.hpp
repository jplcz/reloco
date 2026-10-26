// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/index/strided.hpp
 * @brief `strided(n, step)`: the indexes `0, step, 2*step, ...` below `n`.
 *
 * The number of iterations is computed up front (`(n - 1) / step + 1`) and each index is `k * step` for a counter
 * `k`, so the arithmetic can never overflow, even for `n` near the largest `std::size_t`.
 *
 * See `docs/kernel/index.md`.
 */

#include "range.hpp"

#include <cstddef>

RELOCO_KERNEL_BEGIN

/** @brief The indexes `0, step, 2*step, ...` that are `< extent`. A step of 0 yields no indexes. */
template <class Tag = unbranded> class strided_domain {
public:
  class iterator {
  public:
    using value_type = proven_index<Tag>;
    using difference_type = std::ptrdiff_t;

    // NOLINTNEXTLINE(reloco-checked-operator): bounded by the domain; checks opt-in
    [[nodiscard]] constexpr proven_index<Tag> operator*() const noexcept {
      RELOCO_KERNEL_CHECK(k_ < count_, "strided_domain: dereferenced an end iterator");
      return detail::index_access::make<Tag>(k_ * step_);
    }

    constexpr iterator &operator++() & noexcept {
      ++k_;
      return *this;
    }

    constexpr iterator operator++(int) & noexcept {
      iterator old(*this);
      ++k_;
      return old;
    }

    [[nodiscard]] friend constexpr bool operator==(const iterator &a, const iterator &b) noexcept {
      return a.k_ == b.k_;
    }

  private:
    friend class strided_domain;
    constexpr iterator(std::size_t k, std::size_t count, std::size_t step) noexcept
        : k_(k), count_(count), step_(step) {}

    std::size_t k_;
    std::size_t count_;
    std::size_t step_;
  };

  constexpr strided_domain(detail::mint_key, std::size_t extent, std::size_t step) noexcept
      : step_(step), count_(extent == 0 || step == 0 ? 0 : (extent - 1) / step + 1) {}

  [[nodiscard]] constexpr iterator begin() const noexcept { return iterator(0, count_, step_); }
  [[nodiscard]] constexpr iterator end() const noexcept { return iterator(count_, count_, step_); }

  /** @brief Number of indexes produced. */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

private:
  std::size_t step_;
  std::size_t count_;
};

/**
 * @brief Every `step`-th index below `n`, starting at 0.
 * @code
 * for (auto i : reloco::kernel::strided(10, 4))   // 0, 4, 8
 *   use(i.value());
 * @endcode
 */
[[nodiscard]] constexpr strided_domain<> strided(std::size_t n, std::size_t step) noexcept {
  return strided_domain<>(detail::index_access::key(), n, step);
}

RELOCO_KERNEL_END
