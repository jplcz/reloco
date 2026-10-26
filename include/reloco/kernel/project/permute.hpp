// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/project/permute.hpp
 * @brief `permute` (reorder dimensions) and `diagonal` (walk two dimensions together) projections.
 *
 * Both reach a subset of the parent's elements through a rearranged shape and strides, so they cannot fail and a
 * mutable parent gives a mutable result. See `docs/kernel/projections.md`.
 */

#include "../../array.hpp"
#include "../config.hpp"
#include "../view/shape.hpp"
#include "../view/stride.hpp"
#include "../view/tensor_view.hpp"
#include "slice.hpp"

#include <cstddef>

namespace reloco::kernel::detail {

/** @brief `true` if `P...` lists every value in `[0, sizeof...(P))` exactly once. */
template <std::size_t... P> [[nodiscard]] constexpr bool is_permutation_of_dimensions() noexcept {
  constexpr std::size_t n = sizeof...(P);
  const reloco::array<std::size_t, n> p{P...};
  reloco::array<bool, n> seen{};
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  for (std::size_t i = 0; i < n; ++i) {
    if (p.unchecked_at(i) >= n || seen.unchecked_at(p.unchecked_at(i))) // p[i] < n is checked first
      return false;
    seen.unchecked_at(p.unchecked_at(i)) = true;
  }
  RELOCO_END_UNSAFE_BUFFER_USAGE
  return true;
}

} // namespace reloco::kernel::detail

RELOCO_KERNEL_BEGIN

/**
 * @brief The view whose dimension `d` is dimension `P[d]` of the parent: `permute<2, 0, 1>(v).at(i, j, k) ==
 * v.at(j, k, i)`.
 * @details `P...` must list every dimension once (a compile-time error otherwise). `permute<1, 0>` is `transpose`.
 */
template <std::size_t... P, class T, std::size_t R, class S>
[[nodiscard]] constexpr tensor_view<T, R> permute(const tensor_view<T, R, S> &v) noexcept {
  static_assert(sizeof...(P) == R, "permute: give one dimension per parent dimension");
  static_assert(detail::is_permutation_of_dimensions<P...>(), "permute: not a permutation of the dimensions");
  const reloco::array<std::size_t, R> order{P...};
  reloco::array<std::size_t, R> ext{};
  reloco::array<std::size_t, R> str{};
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  for (std::size_t d = 0; d < R; ++d) { // order[d] < R was proven at compile time
    ext.unchecked_at(d) = v.extent(order.unchecked_at(d));
    str.unchecked_at(d) = v.stride(order.unchecked_at(d));
  }
  RELOCO_END_UNSAFE_BUFFER_USAGE
  return detail::tensor_access::make(v.storage(), shape<R>(ext), dynamic_strides<R>(str));
}

/**
 * @brief Merges dimensions `A` and `B` into one that walks both together: `diagonal<0, 1>(m).at(i) == m.at(i, i)`.
 * @return a view of rank `R - 1`. The other dimensions keep their order; the diagonal dimension is last. Its extent
 * is `min(extent(A), extent(B))`. Never fails.
 */
template <std::size_t A, std::size_t B, class T, std::size_t R, class S>
  requires(R >= 2)
[[nodiscard]] constexpr tensor_view<T, R - 1> diagonal(const tensor_view<T, R, S> &v) noexcept {
  static_assert(A < R && B < R && A != B, "diagonal: give two different dimensions of the view");
  const std::size_t n = v.extent(A) < v.extent(B) ? v.extent(A) : v.extent(B);
  reloco::array<std::size_t, R - 1> ext{};
  reloco::array<std::size_t, R - 1> str{};
  std::size_t out = 0;
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  for (std::size_t d = 0; d < R; ++d) {
    if (d == A || d == B)
      continue;
    ext.unchecked_at(out) = v.extent(d); // out < R - 2 here, since two dimensions are skipped
    str.unchecked_at(out) = v.stride(d);
    ++out;
  }
  // Step i reaches i * stride(A) + i * stride(B), which a non-empty parent already reaches. With fewer than two
  // steps the stride is never multiplied by anything but zero, so 0 avoids an overflowing sum in an empty parent.
  ext.unchecked_at(R - 2) = n;
  str.unchecked_at(R - 2) = n < 2 ? 0 : v.stride(A) + v.stride(B);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  return detail::tensor_access::make(v.storage(), shape<R - 1>(ext), dynamic_strides<R - 1>(str));
}

RELOCO_KERNEL_END
