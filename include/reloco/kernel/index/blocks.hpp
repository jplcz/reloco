// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/index/blocks.hpp
 * @brief `blocks<B>(n)`: whole groups of `B` consecutive indexes (SIMD lanes, unrolled iterations) plus the tail.
 *
 * Vectorized loops process `B` elements per iteration and then finish the remaining `n % B` one by one. Writing
 * `i + B <= n` by hand is the classic place to be off by one. `blocks<B>(n)` generates exactly the `n / B` full
 * groups, each carrying the proof `base + B <= n`; `.tail()` gives the leftover indexes. `pairs(n)` is
 * `blocks<2>(n)`.
 *
 * See `docs/kernel/index.md`.
 */

#include "range.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

RELOCO_KERNEL_BEGIN

/**
 * @brief One full group of `B` consecutive indexes `[base, base + B)`, known to lie inside the extent.
 * @tparam B group width, positive.
 * @tparam Tag brand of the produced indexes.
 */
template <std::size_t B, class Tag = unbranded> class block {
public:
  static_assert(B > 0, "block: width must be positive");
  static constexpr std::size_t width = B;

  /** @brief Index of the first lane as a plain integer; `base() + B <= extent` always holds. */
  [[nodiscard]] constexpr std::size_t base() const noexcept { return base_; }

  /** @brief The proven index of lane `K`; `K < B` is checked at compile time. */
  template <std::size_t K> [[nodiscard]] constexpr proven_index<Tag> lane() const noexcept {
    static_assert(K < B, "block::lane: lane out of range");
    return detail::index_access::make<Tag>(base_ + K);
  }

  /** @brief Calls `f(proven_index)` for every lane in order; the loop is unrolled at compile time. */
  template <class F>
  constexpr void for_each_lane(F &&f) const noexcept(std::is_nothrow_invocable_v<F &, proven_index<Tag>>) {
    [this, &f]<std::size_t... K>(std::index_sequence<K...>) {
      (f(this->template lane<K>()), ...);
    }(std::make_index_sequence<B>{});
  }

  /**
   * @brief Calls `f(block<B2, Tag>)` for each of the `B / B2` consecutive narrower blocks inside this one, unrolled.
   * @details Every sub-block lies inside this block, so it is inside the extent too. `B2` must divide `B`.
   */
  template <std::size_t B2, class F>
  RELOCO_ALWAYS_INLINE constexpr void for_each_sub(F &&f) const
      noexcept(std::is_nothrow_invocable_v<F &, block<B2, Tag>>) {
    static_assert(B2 > 0 && B % B2 == 0, "block::for_each_sub: the sub-block width must divide the block width");
    // A plain counted loop (not a fold over indexes) so the optimizer can unroll or vectorize it as it sees fit.
    for (std::size_t j = 0; j < B / B2; ++j)
      f(block<B2, Tag>(detail::index_access::key(), base_ + j * B2, extent_));
  }

  /** @brief The `B` lane indexes as a runtime-iterable range. */
  [[nodiscard]] constexpr index_range<Tag> lanes() const noexcept {
    return index_range<Tag>(detail::index_access::key(), base_, base_ + B, extent_);
  }

  constexpr block(detail::mint_key, std::size_t base, std::size_t extent) noexcept : base_(base), extent_(extent) {
    RELOCO_KERNEL_CHECK(extent >= B && base <= extent - B, "block: requires base + width <= extent");
  }

private:
  std::size_t base_;
  std::size_t extent_;
};

/**
 * @brief The `n / B` full blocks of an extent `n`, plus its tail.
 * @tparam B group width, positive.
 * @tparam Tag brand of the produced indexes.
 */
template <std::size_t B, class Tag = unbranded> class block_domain {
public:
  static_assert(B > 0, "block_domain: width must be positive");

  class iterator {
  public:
    using value_type = block<B, Tag>;
    using difference_type = std::ptrdiff_t;

    // NOLINTNEXTLINE(reloco-checked-operator): bounded by the domain; checks opt-in
    [[nodiscard]] constexpr block<B, Tag> operator*() const noexcept {
      return block<B, Tag>(detail::index_access::key(), cur_ * B, extent_);
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
    friend class block_domain;
    constexpr iterator(std::size_t cur, std::size_t extent) noexcept : cur_(cur), extent_(extent) {}

    std::size_t cur_;
    std::size_t extent_;
  };

  constexpr block_domain(detail::mint_key, std::size_t extent) noexcept : extent_(extent) {}

  [[nodiscard]] constexpr iterator begin() const noexcept { return iterator(0, extent_); }
  [[nodiscard]] constexpr iterator end() const noexcept { return iterator(extent_ / B, extent_); }

  /** @brief Number of full blocks. */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return extent_ / B; }
  [[nodiscard]] constexpr bool empty() const noexcept { return extent_ < B; }

  /** @brief The leftover `n % B` indexes after the last full block. */
  [[nodiscard]] constexpr index_range<Tag> tail() const noexcept {
    return index_range<Tag>(detail::index_access::key(), extent_ - extent_ % B, extent_, extent_);
  }

private:
  std::size_t extent_;
};

/**
 * @brief The full groups of `B` indexes in `[0, n)`; call `.tail()` on the result for the leftovers.
 * @code
 * auto chunks = reloco::kernel::blocks<4>(10);   // blocks at 0 and 4; tail is 8, 9
 * for (auto blk : chunks)                        // blk.base() + 4 <= 10 is guaranteed
 *   blk.for_each_lane([&](auto i) { sum += i.value(); });
 * for (auto i : chunks.tail())
 *   sum += i.value();
 * @endcode
 */
template <std::size_t B> [[nodiscard]] constexpr block_domain<B> blocks(std::size_t n) noexcept {
  return block_domain<B>(detail::index_access::key(), n);
}

/** @brief Adjacent index pairs `(2i, 2i + 1)`, i.e. `blocks<2>(n)`; use `lane<0>()` and `lane<1>()`. */
[[nodiscard]] constexpr block_domain<2> pairs(std::size_t n) noexcept { return blocks<2>(n); }

RELOCO_KERNEL_END
