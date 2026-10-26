// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/bind/bound_span.hpp
 * @brief `bound_span<T, Brand>`: a span whose accessors take only proven indexes of one binding.
 *
 * You never construct one directly: `bound::view<I>()` hands them out (see `bound.hpp`). The `Brand` ties the view to
 * the proven indexes of the same binding, so an index minted for another binding is a compile error.
 *
 * See `docs/kernel/bind.md`.
 */

#include "../../lifetime.hpp"
#include "../../span.hpp"
#include "../config.hpp"
#include "../index/blocks.hpp"
#include "../index/proven_index.hpp"
#include "../index/range.hpp"
#include "../index/windows.hpp"
#include "../view/lane_traits.hpp"

#include <cstddef>
#include <type_traits>

namespace reloco::kernel::detail {
struct bind_access;
}

RELOCO_KERNEL_BEGIN

/**
 * @brief Elements of one span, known to number at least as many as the binding's extent.
 * @tparam T element type (`const U` for read-only).
 * @tparam Brand the binding this view belongs to.
 * @details Pointer-like handle (copying it copies the handle). Accessors do not check anything in a normal build:
 * the binding proved `size() >= extent` once, and a proven index of the same brand is always `< extent`.
 * With `RELOCO_KERNEL_CHECKS >= 1` every access re-verifies that.
 */
template <class T, class Brand> class RELOCO_POINTER bound_span {
public:
  using element_type = T;
  using value_type = std::remove_const_t<T>;

  /** @brief Number of elements in the underlying span (at least the binding's extent). */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return storage_.size(); }
  /** @brief The underlying span, for code that leaves the proven world. */
  [[nodiscard]] constexpr span<T> storage() const noexcept { return storage_; }

  /** @brief The element at a proven index; no check in a normal build. */
  [[nodiscard]] constexpr T &operator()(proven_index<Brand> i) const noexcept {
    RELOCO_KERNEL_CHECK(i.value() < storage_.size(), "bound_span: proven index outside the view");
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return storage_.unchecked_at(i.value());
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  /** @brief Loads the `W` elements of a block as one lane (`lane_traits<T, W>::type`). */
  template <std::size_t W> [[nodiscard]] constexpr auto load(const block<W, Brand> &blk) const noexcept {
    return lane_traits<T, W>::load(proven_span(blk.base(), W));
  }

  /** @brief Stores one lane into the `W` elements of a block. */
  template <std::size_t W>
  constexpr void store(const block<W, Brand> &blk, const typename lane_traits<T, W>::type &lane) const noexcept
    requires(!std::is_const_v<T>)
  {
    lane_traits<T, W>::store(proven_span(blk.base(), W), lane);
  }

  /** @brief The elements of a window, as a plain span. */
  [[nodiscard]] constexpr span<T> subspan(const window<Brand> &w) const noexcept {
    return proven_span(w.start(), w.size());
  }

private:
  friend struct detail::bind_access;

  constexpr explicit bound_span(span<T> storage) noexcept : storage_(storage) {}

  // The range [first, first + count) comes from a proven block or window of this binding, so it is inside the span.
  [[nodiscard]] constexpr span<T> proven_span(std::size_t first, std::size_t count) const noexcept {
    RELOCO_KERNEL_CHECK(first <= storage_.size() && count <= storage_.size() - first,
                        "bound_span: proven range outside the view");
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return storage_.unchecked_subspan(first, count);
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  span<T> storage_;
};

/**
 * @brief One element per block: a table with `extent / B` entries, indexed by the blocks of a binding.
 * @tparam T element type (`const U` for read-only).
 * @tparam B block width of the blocks that index it.
 * @tparam Brand the binding this view belongs to.
 * @details Made by `bound::try_blockwise<B>`. The block starting at `q * B` reads entry `q`, which the one-time check
 * proved to exist.
 */
template <class T, std::size_t B, class Brand> class RELOCO_POINTER block_span {
  static_assert(B > 0, "block_span: block width must be positive");

public:
  using element_type = T;

  /** @brief Number of entries in the underlying span (at least the binding's extent divided by `B`). */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return storage_.size(); }
  /** @brief The underlying span, for code that leaves the proven world. */
  [[nodiscard]] constexpr span<T> storage() const noexcept { return storage_; }

  /** @brief The entry of a block; no check in a normal build. */
  [[nodiscard]] constexpr T &operator()(const block<B, Brand> &blk) const noexcept {
    RELOCO_KERNEL_CHECK(blk.base() / B < storage_.size(), "block_span: block outside the table");
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return storage_.unchecked_at(blk.base() / B);
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

private:
  friend struct detail::bind_access;

  constexpr explicit block_span(span<T> storage) noexcept : storage_(storage) {}

  span<T> storage_;
};

RELOCO_KERNEL_END
