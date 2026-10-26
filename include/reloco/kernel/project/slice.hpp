// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/project/slice.hpp
 * @brief Zero-allocation projections of a `tensor_view`: `try_slice`, `try_index`, `try_row`, `try_column`,
 * `try_tile`.
 *
 * A projection builds a new view over the same storage by changing only the shape, the strides and the starting
 * offset. No element is copied. Each one validates its arguments against the parent view with a few integer
 * operations; because the parent was already proven to lie inside its storage, a validated projection lies inside it
 * too. A projection of a mutable view stays mutable: it reaches a subset of the parent's elements, so it cannot
 * overlap itself if the parent could not.
 *
 * The result always has runtime strides (`dynamic_strides`).
 *
 * See `docs/kernel/projections.md`.
 */

#include "../../array.hpp"
#include "../../error.hpp"
#include "../../span.hpp"
#include "../config.hpp"
#include "../view/shape.hpp"
#include "../view/stride.hpp"
#include "../view/tensor_view.hpp"

#include <cstddef>

namespace reloco::kernel::detail {

/** @brief Builds a projected view whose first element sits `offset` elements into the parent's storage. */
template <class T, std::size_t R>
[[nodiscard]] constexpr tensor_view<T, R> make_projection(span<T> parent_storage, std::size_t offset,
                                                          const reloco::array<std::size_t, R> &ext,
                                                          const reloco::array<std::size_t, R> &str) noexcept {
  const shape<R> extents(ext);
  if (extents.empty()) // an empty view reaches nothing, so the storage and offset are irrelevant
    return tensor_access::make(parent_storage, extents, dynamic_strides<R>(str));
  RELOCO_KERNEL_CHECK(offset < parent_storage.size(), "projection: offset outside the parent storage");
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  // The caller checked every index against the parent's shape, so offset < parent_storage.size().
  const span<T> rebased = parent_storage.unchecked_subspan(offset, parent_storage.size() - offset);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  return tensor_access::make(rebased, extents, dynamic_strides<R>(str));
}

} // namespace reloco::kernel::detail

RELOCO_KERNEL_BEGIN

/**
 * @brief The sub-range `[first, first + count)` of dimension `D`; the other dimensions are unchanged.
 * @return a view of the same rank; `out_of_bounds` if the range does not fit in the extent of dimension `D`.
 * @details `count == 0` is legal (it gives an empty view), including `first == extent`.
 */
template <std::size_t D, class T, std::size_t R, class S>
[[nodiscard]] constexpr result<tensor_view<T, R>> try_slice(const tensor_view<T, R, S> &v, std::size_t first,
                                                            std::size_t count) noexcept {
  static_assert(D < R, "try_slice: dimension out of range");
  const std::size_t extent = v.extent(D);
  if (first > extent || count > extent - first)
    return unexpected(error::out_of_bounds);
  reloco::array<std::size_t, R> ext{};
  reloco::array<std::size_t, R> str{};
  for (std::size_t d = 0; d < R; ++d) {
    ext[d] = d == D ? count : v.extent(d);
    str[d] = v.stride(d);
  }
  // When the result is non-empty, first < extent, so first * stride is within the parent's reach.
  return detail::make_projection<T, R>(v.storage(), first * v.stride(D), ext, str);
}

/**
 * @brief Fixes dimension `D` at `index`, dropping that dimension: a matrix becomes a row or a column.
 * @return a view of rank `R - 1`; `out_of_bounds` if `index >= extent(D)`.
 */
template <std::size_t D, class T, std::size_t R, class S>
  requires(R >= 2)
[[nodiscard]] constexpr result<tensor_view<T, R - 1>> try_index(const tensor_view<T, R, S> &v,
                                                                std::size_t index) noexcept {
  static_assert(D < R, "try_index: dimension out of range");
  if (index >= v.extent(D))
    return unexpected(error::out_of_bounds);
  reloco::array<std::size_t, R - 1> ext{};
  reloco::array<std::size_t, R - 1> str{};
  std::size_t out = 0;
  for (std::size_t d = 0; d < R; ++d) {
    if (d == D)
      continue;
    ext[out] = v.extent(d);
    str[out] = v.stride(d);
    ++out;
  }
  return detail::make_projection<T, R - 1>(v.storage(), index * v.stride(D), ext, str);
}

/** @brief Row `r` of a view of rank >= 2: `try_index<0>`. */
template <class T, std::size_t R, class S>
  requires(R >= 2)
[[nodiscard]] constexpr result<tensor_view<T, R - 1>> try_row(const tensor_view<T, R, S> &v, std::size_t r) noexcept {
  return try_index<0>(v, r);
}

/** @brief Column `c` of a view of rank >= 2: `try_index<R - 1>`. For a matrix the result has stride `stride(0)`. */
template <class T, std::size_t R, class S>
  requires(R >= 2)
[[nodiscard]] constexpr result<tensor_view<T, R - 1>> try_column(const tensor_view<T, R, S> &v,
                                                                 std::size_t c) noexcept {
  return try_index<R - 1>(v, c);
}

/**
 * @brief A rectangular block: starts at `origin` and spans `extents`, in every dimension.
 * @return a view of the same rank; `out_of_bounds` if the block does not fit in the view.
 * @details A block with a zero extent is empty.
 */
template <class T, std::size_t R, class S>
[[nodiscard]] constexpr result<tensor_view<T, R>> try_tile(const tensor_view<T, R, S> &v, const shape<R> &origin,
                                                           const shape<R> &extents) noexcept {
  reloco::array<std::size_t, R> ext{};
  reloco::array<std::size_t, R> str{};
  std::size_t offset = 0;
  for (std::size_t d = 0; d < R; ++d) {
    const std::size_t total = v.extent(d);
    if (origin[d] > total || extents[d] > total - origin[d])
      return unexpected(error::out_of_bounds);
    ext[d] = extents[d];
    str[d] = v.stride(d);
    offset += origin[d] * v.stride(d); // for a non-empty block each term is below the parent's reach
  }
  return detail::make_projection<T, R>(v.storage(), offset, ext, str);
}

RELOCO_KERNEL_END
