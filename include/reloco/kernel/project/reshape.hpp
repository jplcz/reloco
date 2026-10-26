// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/project/reshape.hpp
 * @brief `try_reshape` (same elements, new shape) and `try_broadcast` (repeat along a dimension, read-only).
 *
 * See `docs/kernel/projections.md`.
 */

#include "../../array.hpp"
#include "../../error.hpp"
#include "../../int_ops.hpp"
#include "../config.hpp"
#include "../view/shape.hpp"
#include "../view/stride.hpp"
#include "../view/tensor_view.hpp"
#include "slice.hpp"

#include <cstddef>

RELOCO_KERNEL_BEGIN

/**
 * @brief The same elements, in the same order, with a different shape of rank `N`.
 * @return `invalid_argument` if the element counts differ or the view is not a dense row-major block (padded rows,
 * transposed or broadcast views cannot be reshaped without copying).
 * @details Dimensions of extent 1 may have any stride. An empty view only reshapes to an empty shape.
 * @code
 * auto v = try_make_contiguous_tensor<int, 2>(storage, {2, 6}).value();
 * auto cube = try_reshape<3>(v, {2, 3, 2}).value();   // cube.at(i, j, k) is v.at(i, 2 * j + k)
 * @endcode
 */
template <std::size_t N, class T, std::size_t R, class S>
[[nodiscard]] constexpr result<tensor_view<T, N>> try_reshape(const tensor_view<T, R, S> &v,
                                                              const shape<N> &extents) noexcept {
  auto old_count = v.shape().try_elements();
  auto new_count = extents.try_elements();
  if (!old_count || !new_count || *old_count != *new_count)
    return unexpected(error::invalid_argument);
  reloco::array<std::size_t, N> str{};
  reloco::array<std::size_t, N> ext{};
  for (std::size_t d = 0; d < N; ++d)
    ext[d] = extents[d];
  if (*old_count == 0)
    return detail::make_projection<T, N>(v.storage(), 0, ext, str);

  // Dense row-major: each non-trivial dimension's stride is the product of the extents after it.
  std::size_t running = 1;
  for (std::size_t d = R; d-- > 0;) {
    if (v.extent(d) > 1 && v.stride(d) != running)
      return unexpected(error::invalid_argument);
    running *= v.extent(d); // cannot overflow: the product of all extents was just computed
  }
  running = 1;
  for (std::size_t d = N; d-- > 0;) {
    str[d] = running;
    running *= extents[d];
  }
  return detail::make_projection<T, N>(v.storage(), 0, ext, str);
}

/**
 * @brief A read-only view that repeats the source along new or size-1 dimensions, NumPy style.
 * @return `invalid_argument` if `extents` is not compatible: aligned from the last dimension, every source extent must
 * equal the target extent or be 1; leading target dimensions are new. The result has stride 0 where it repeats.
 * @details The result is always read-only (`const T`): repeated elements are one element, so writing through the view
 * would alias. Rank `N` must be at least the source rank.
 */
template <std::size_t N, class T, std::size_t R, class S>
  requires(N >= R)
[[nodiscard]] constexpr result<tensor_view<const T, N>> try_broadcast(const tensor_view<T, R, S> &v,
                                                                      const shape<N> &extents) noexcept {
  reloco::array<std::size_t, N> ext{};
  reloco::array<std::size_t, N> str{};
  for (std::size_t d = 0; d < N; ++d) {
    ext[d] = extents[d];
    if (d < N - R) // a new leading dimension repeats
      continue;
    const std::size_t src = d - (N - R);
    if (v.extent(src) == extents[d])
      str[d] = v.stride(src);
    else if (v.extent(src) != 1)
      return unexpected(error::invalid_argument);
  }
  return detail::make_projection<const T, N>(span<const T>(v.storage()), 0, ext, str);
}

RELOCO_KERNEL_END
