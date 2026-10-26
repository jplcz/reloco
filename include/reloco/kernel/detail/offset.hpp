// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/detail/offset.hpp
 * @brief Internal: the overflow-checked offset math behind `try_make_tensor`. Not part of the public API.
 */

#include "../../array.hpp"
#include "../../error.hpp"
#include "../../int_ops.hpp"

#include <cstddef>

namespace reloco::kernel::detail {

/**
 * @brief Largest element offset reachable inside a non-empty tensor: `sum((extent[d] - 1) * stride[d])`.
 * @return the offset, or `integer_overflow` if the sum does not fit `size_t`.
 * @pre no extent is zero.
 */
template <std::size_t Rank>
[[nodiscard]] constexpr result<std::size_t> max_offset(const reloco::array<std::size_t, Rank> &extents,
                                                       const reloco::array<std::size_t, Rank> &strides) noexcept {
  std::size_t total = 0;
  for (std::size_t d = 0; d < Rank; ++d) {
    auto term = checked_mul(extents[d] - 1, strides[d]);
    if (!term)
      return unexpected(term.error());
    auto sum = checked_add(total, *term);
    if (!sum)
      return unexpected(sum.error());
    total = *sum;
  }
  return total;
}

/**
 * @brief `true` if no two distinct index tuples map to the same offset.
 * @details Sorts the dimensions by stride and requires each stride to exceed the span of all smaller strides. This is
 * exact for nested layouts (row-major, column-major, tiled, padded) and conservative for exotic interleavings, which it
 * rejects. Dimensions with extent 1 never matter. Assumes `max_offset` did not overflow.
 */
template <std::size_t Rank>
[[nodiscard]] constexpr bool strides_are_disjoint(const reloco::array<std::size_t, Rank> &extents,
                                                  const reloco::array<std::size_t, Rank> &strides) noexcept {
  reloco::array<std::size_t, Rank> order{};
  std::size_t count = 0;
  for (std::size_t d = 0; d < Rank; ++d)
    if (extents[d] > 1)
      order[count++] = d;

  for (std::size_t i = 1; i < count; ++i) {
    const std::size_t key = order[i];
    std::size_t j = i;
    while (j > 0 && strides[order[j - 1]] > strides[key]) {
      order[j] = order[j - 1];
      --j;
    }
    order[j] = key;
  }

  std::size_t reach = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t d = order[i];
    if (strides[d] <= reach)
      return false;
    reach += (extents[d] - 1) * strides[d];
  }
  return true;
}

} // namespace reloco::kernel::detail
