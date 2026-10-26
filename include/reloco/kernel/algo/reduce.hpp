// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/algo/reduce.hpp
 * @brief Reductions over spans: `reduce`, `try_sum`, `try_dot`, `try_min`, `try_max`, `try_argmin`, `try_argmax`.
 *
 * The accumulator type is declared by the caller (`try_sum<std::int64_t>(bytes)`), so a sum of 8-bit values does not
 * silently wrap. Integer accumulation is overflow-checked and reports `integer_overflow`.
 *
 * See `docs/kernel/algorithms.md`.
 */

#include "../../error.hpp"
#include "../../int_ops.hpp"
#include "../../span.hpp"
#include "../bind/bound.hpp"
#include "../config.hpp"

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace reloco::kernel::detail {
struct reduce_brand {};
struct dot_brand {};
} // namespace reloco::kernel::detail

RELOCO_KERNEL_BEGIN

/** @brief Folds `in` left to right: `acc = op(acc, x)`, starting from `init`. Cannot fail; `op` owns overflow policy.
 */
template <class Acc, class T, class Op>
[[nodiscard]] Acc reduce(span<T> in, Acc init, Op &&op) noexcept(std::is_nothrow_invocable_v<Op &, Acc, const T &>) {
  auto b = try_bind<detail::reduce_brand>(in.size(), in);
  RELOCO_ASSERT(b.has_value(), "reduce: binding a span to its own length cannot fail");
  auto view = b.value().template view<0>();
  Acc acc = init;
  for (auto i : b.value().domain())
    acc = op(acc, view(i));
  return acc;
}

/** @brief Sum of `in` in the integer type `Acc`; `integer_overflow` if it does not fit. Empty input gives 0. */
template <std::integral Acc, std::integral T> [[nodiscard]] result<Acc> try_sum(span<T> in) noexcept {
  auto b = try_bind<detail::reduce_brand>(in.size(), in);
  RELOCO_ASSERT(b.has_value(), "try_sum: binding a span to its own length cannot fail");
  auto view = b.value().template view<0>();
  Acc acc{};
  for (auto i : b.value().domain()) {
    auto next = checked_add<Acc>(acc, static_cast<Acc>(view(i)));
    if (!next)
      return unexpected(next.error());
    acc = *next;
  }
  return acc;
}

/** @brief Dot product in `Acc`; `invalid_argument` if the lengths differ, `integer_overflow` if a term or the sum
 * overflows. */
template <std::integral Acc, std::integral T, std::integral U>
[[nodiscard]] result<Acc> try_dot(span<T> a, span<U> b) noexcept {
  if (a.size() != b.size())
    return unexpected(error::invalid_argument);
  auto bd = try_bind<detail::dot_brand>(a.size(), a, b);
  RELOCO_ASSERT(bd.has_value(), "try_dot: equal-length read-only spans cannot fail to bind");
  auto lhs = bd.value().template view<0>();
  auto rhs = bd.value().template view<1>();
  Acc acc{};
  for (auto i : bd.value().domain()) {
    auto term = checked_mul<Acc>(static_cast<Acc>(lhs(i)), static_cast<Acc>(rhs(i)));
    if (!term)
      return unexpected(term.error());
    auto next = checked_add<Acc>(acc, *term);
    if (!next)
      return unexpected(next.error());
    acc = *next;
  }
  return acc;
}

namespace algo_detail {
// Index of the first element for which `better(candidate, best)` holds against every earlier one.
template <class T, class Better> [[nodiscard]] result<std::size_t> arg_best(span<T> in, Better better) noexcept {
  if (in.empty())
    return unexpected(error::container_empty);
  auto b = try_bind<::reloco::kernel::detail::reduce_brand>(in.size(), in);
  RELOCO_ASSERT(b.has_value(), "arg_best: binding a span to its own length cannot fail");
  auto view = b.value().template view<0>();
  std::remove_const_t<T> best_value{};
  std::size_t best = 0;
  bool first = true;
  for (auto i : b.value().domain()) {
    if (first || better(view(i), best_value)) {
      best_value = view(i);
      best = i.value();
      first = false;
    }
  }
  return best;
}
} // namespace algo_detail

/** @brief Index of the first smallest element (by `operator<`); `container_empty` for empty input. */
template <class T> [[nodiscard]] result<std::size_t> try_argmin(span<T> in) noexcept {
  return algo_detail::arg_best(in, [](const auto &x, const auto &best) noexcept { return x < best; });
}

/** @brief Index of the first largest element (by `operator<`); `container_empty` for empty input. */
template <class T> [[nodiscard]] result<std::size_t> try_argmax(span<T> in) noexcept {
  return algo_detail::arg_best(in, [](const auto &x, const auto &best) noexcept { return best < x; });
}

/** @brief The smallest element; `container_empty` for empty input. */
template <class T> [[nodiscard]] result<std::remove_const_t<T>> try_min(span<T> in) noexcept {
  auto idx = try_argmin(in);
  if (!idx)
    return unexpected(idx.error());
  return in[*idx];
}

/** @brief The largest element; `container_empty` for empty input. */
template <class T> [[nodiscard]] result<std::remove_const_t<T>> try_max(span<T> in) noexcept {
  auto idx = try_argmax(in);
  if (!idx)
    return unexpected(idx.error());
  return in[*idx];
}

RELOCO_KERNEL_END
