// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/project/transpose.hpp
 * @brief `transpose`: swaps two dimensions of a `tensor_view` without moving data.
 *
 * Swapping extents and strides together reaches exactly the same elements, so the result is always valid and the
 * function cannot fail. See `docs/kernel/projections.md`.
 */

#include "../../array.hpp"
#include "../config.hpp"
#include "../view/shape.hpp"
#include "../view/stride.hpp"
#include "../view/tensor_view.hpp"

#include <cstddef>

RELOCO_KERNEL_BEGIN

/**
 * @brief The view with dimensions `A` and `B` swapped; by default the last two (the matrix transpose).
 * @details `transpose(m).at(j, i) == m.at(i, j)`. Same storage, same elements, never fails.
 */
template <std::size_t A, std::size_t B, class T, std::size_t R, class S>
[[nodiscard]] constexpr tensor_view<T, R> transpose(const tensor_view<T, R, S> &v) noexcept {
  static_assert(A < R && B < R, "transpose: dimension out of range");
  reloco::array<std::size_t, R> ext{};
  reloco::array<std::size_t, R> str{};
  for (std::size_t d = 0; d < R; ++d) {
    const std::size_t src = d == A ? B : (d == B ? A : d);
    ext[d] = v.extent(src);
    str[d] = v.stride(src);
  }
  return detail::tensor_access::make(v.storage(), shape<R>(ext), dynamic_strides<R>(str));
}

/** @brief Transposes the last two dimensions. */
template <class T, std::size_t R, class S>
  requires(R >= 2)
[[nodiscard]] constexpr tensor_view<T, R> transpose(const tensor_view<T, R, S> &v) noexcept {
  return transpose<R - 2, R - 1>(v);
}

RELOCO_KERNEL_END
