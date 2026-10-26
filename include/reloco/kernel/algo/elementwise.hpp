// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/algo/elementwise.hpp
 * @brief Element-wise algorithms over spans: `fill`, `try_copy`, `try_transform`, `transform_in_place`,
 * `try_zip_transform`.
 *
 * Each checked entry point binds its spans once (length and aliasing, see `docs/kernel/bind.md`) and then runs a loop
 * whose accesses carry no bounds checks. The output span must be at least as long as the input; extra output elements
 * are left untouched. Output and input must not overlap (`invalid_argument`); use `transform_in_place` to update a
 * buffer in place.
 *
 * See `docs/kernel/algorithms.md`.
 */

#include "../../error.hpp"
#include "../../span.hpp"
#include "../bind/bound.hpp"
#include "../config.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace reloco::kernel::detail {
// Each algorithm has its own brand: the token never leaves the function, so the brand only has to be unique.
struct fill_brand {};
struct copy_brand {};
struct transform_brand {};
struct in_place_brand {};
struct zip_brand {};
} // namespace reloco::kernel::detail

RELOCO_KERNEL_BEGIN

/** @brief Sets every element of `out` to `value`. Cannot fail. */
template <class T> constexpr void fill(span<T> out, const std::remove_const_t<T> &value) noexcept {
  static_assert(!std::is_const_v<T>, "fill needs a mutable span");
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  for (std::size_t i = 0; i < out.size(); ++i) // i < size by the loop bound
    out.unchecked_at(i) = value;
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

/** @brief Copies `in` to the start of `out`; `out_of_bounds` if `out` is shorter, `invalid_argument` if they overlap.
 */
template <class T> [[nodiscard]] result<void> try_copy(span<T> out, std::type_identity_t<span<const T>> in) noexcept {
  auto b = try_bind<detail::copy_brand>(in.size(), out, in);
  if (!b)
    return unexpected(b.error());
  auto dst = b.value().template view<0>();
  auto src = b.value().template view<1>();
  for (auto i : b.value().domain())
    dst(i) = src(i);
  return {};
}

/** @brief `out[i] = f(in[i])` for every element of `in`. Same errors as `try_copy`. */
template <class T, class U, class F>
[[nodiscard]] result<void> try_transform(span<T> out, span<U> in,
                                         F &&f) noexcept(std::is_nothrow_invocable_v<F &, const U &>) {
  auto b = try_bind<detail::transform_brand>(in.size(), out, in);
  if (!b)
    return unexpected(b.error());
  auto dst = b.value().template view<0>();
  auto src = b.value().template view<1>();
  for (auto i : b.value().domain())
    dst(i) = f(src(i));
  return {};
}

/** @brief `x = f(x)` for every element of `data`. Cannot fail. */
template <class T, class F>
void transform_in_place(span<T> data, F &&f) noexcept(std::is_nothrow_invocable_v<F &, T &>) {
  static_assert(!std::is_const_v<T>, "transform_in_place needs a mutable span");
  // A single span is always long enough for its own length and cannot overlap anything.
  auto b = try_bind<detail::in_place_brand>(data.size(), data);
  RELOCO_ASSERT(b.has_value(), "transform_in_place: binding a span to its own length cannot fail");
  auto view = b.value().template view<0>();
  for (auto i : b.value().domain())
    view(i) = f(view(i));
}

/** @brief `out[i] = f(a[i], b[i])`. `invalid_argument` if `a` and `b` differ in length or `out` overlaps an input. */
template <class T, class U, class V, class F>
[[nodiscard]] result<void> try_zip_transform(span<T> out, span<U> a, span<V> b,
                                             F &&f) noexcept(std::is_nothrow_invocable_v<F &, const U &, const V &>) {
  if (a.size() != b.size())
    return unexpected(error::invalid_argument);
  auto bd = try_bind<detail::zip_brand>(a.size(), out, a, b);
  if (!bd)
    return unexpected(bd.error());
  auto dst = bd.value().template view<0>();
  auto lhs = bd.value().template view<1>();
  auto rhs = bd.value().template view<2>();
  for (auto i : bd.value().domain())
    dst(i) = f(lhs(i), rhs(i));
  return {};
}

RELOCO_KERNEL_END
