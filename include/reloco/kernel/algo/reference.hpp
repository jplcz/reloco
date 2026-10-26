// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/algo/reference.hpp
 * @brief `reloco::kernel::reference`: plain, fully checked scalar versions of every core algorithm.
 *
 * Each function has the same signature, results and errors as its twin in `reloco::kernel`, but it is a simple loop
 * that indexes spans with the always-checked `operator[]` and never uses a binding, a proven index or an unchecked
 * accessor. It is the oracle for audits: run the optimized algorithm and the reference over the same inputs and
 * compare (see `docs/kernel/algorithms.md`). Not meant for production hot paths.
 *
 * The only shared piece is the overlap test (`detail::ranges_are_compatible`), because it needs address arithmetic.
 */

#include "../../array.hpp"
#include "../../error.hpp"
#include "../../int_ops.hpp"
#include "../../span.hpp"
#include "../bind/bound.hpp"
#include "../config.hpp"

#include <concepts>
#include <cstddef>
#include <type_traits>

RELOCO_KERNEL_BEGIN
namespace reference {

namespace ref_detail {
// True if a mutable `out` does not overlap the read-only `in`.
template <class T, class U> [[nodiscard]] bool disjoint(span<T> out, span<U> in) noexcept {
  RELOCO_BEGIN_UNSAFE_CODE
  const reloco::array<::reloco::kernel::detail::range_info, 2> ranges{::reloco::kernel::detail::unsafe_describe(out),
                                                                      ::reloco::kernel::detail::unsafe_describe(in)};
  RELOCO_END_UNSAFE_CODE
  return ::reloco::kernel::detail::ranges_are_compatible<false>(ranges);
}
} // namespace ref_detail

template <class T> void fill(span<T> out, const std::remove_const_t<T> &value) noexcept {
  for (std::size_t i = 0; i < out.size(); ++i)
    out[i] = value;
}

template <class T> [[nodiscard]] result<void> try_copy(span<T> out, std::type_identity_t<span<const T>> in) noexcept {
  if (out.size() < in.size())
    return unexpected(error::out_of_bounds);
  if (!ref_detail::disjoint(out, in))
    return unexpected(error::invalid_argument);
  for (std::size_t i = 0; i < in.size(); ++i)
    out[i] = in[i];
  return {};
}

template <class T, class U, class F>
[[nodiscard]] result<void> try_transform(span<T> out, span<U> in,
                                         F &&f) noexcept(std::is_nothrow_invocable_v<F &, const U &>) {
  if (out.size() < in.size())
    return unexpected(error::out_of_bounds);
  if (!ref_detail::disjoint(out, in))
    return unexpected(error::invalid_argument);
  for (std::size_t i = 0; i < in.size(); ++i)
    out[i] = f(in[i]);
  return {};
}

template <class T, class F>
void transform_in_place(span<T> data, F &&f) noexcept(std::is_nothrow_invocable_v<F &, T &>) {
  for (std::size_t i = 0; i < data.size(); ++i)
    data[i] = f(data[i]);
}

template <class T, class U, class V, class F>
[[nodiscard]] result<void> try_zip_transform(span<T> out, span<U> a, span<V> b,
                                             F &&f) noexcept(std::is_nothrow_invocable_v<F &, const U &, const V &>) {
  if (a.size() != b.size())
    return unexpected(error::invalid_argument);
  if (out.size() < a.size())
    return unexpected(error::out_of_bounds);
  if (!ref_detail::disjoint(out, a) || !ref_detail::disjoint(out, b))
    return unexpected(error::invalid_argument);
  for (std::size_t i = 0; i < a.size(); ++i)
    out[i] = f(a[i], b[i]);
  return {};
}

template <class Acc, class T, class Op>
[[nodiscard]] Acc reduce(span<T> in, Acc init, Op &&op) noexcept(std::is_nothrow_invocable_v<Op &, Acc, const T &>) {
  Acc acc = init;
  for (std::size_t i = 0; i < in.size(); ++i)
    acc = op(acc, in[i]);
  return acc;
}

template <std::integral Acc, std::integral T> [[nodiscard]] result<Acc> try_sum(span<T> in) noexcept {
  Acc acc{};
  for (std::size_t i = 0; i < in.size(); ++i) {
    auto next = checked_add<Acc>(acc, static_cast<Acc>(in[i]));
    if (!next)
      return unexpected(next.error());
    acc = *next;
  }
  return acc;
}

template <std::integral Acc, std::integral T, std::integral U>
[[nodiscard]] result<Acc> try_dot(span<T> a, span<U> b) noexcept {
  if (a.size() != b.size())
    return unexpected(error::invalid_argument);
  Acc acc{};
  for (std::size_t i = 0; i < a.size(); ++i) {
    auto term = checked_mul<Acc>(static_cast<Acc>(a[i]), static_cast<Acc>(b[i]));
    if (!term)
      return unexpected(term.error());
    auto next = checked_add<Acc>(acc, *term);
    if (!next)
      return unexpected(next.error());
    acc = *next;
  }
  return acc;
}

template <class T> [[nodiscard]] result<std::size_t> try_argmin(span<T> in) noexcept {
  if (in.empty())
    return unexpected(error::container_empty);
  std::size_t best = 0;
  for (std::size_t i = 1; i < in.size(); ++i)
    if (in[i] < in[best])
      best = i;
  return best;
}

template <class T> [[nodiscard]] result<std::size_t> try_argmax(span<T> in) noexcept {
  if (in.empty())
    return unexpected(error::container_empty);
  std::size_t best = 0;
  for (std::size_t i = 1; i < in.size(); ++i)
    if (in[best] < in[i])
      best = i;
  return best;
}

template <class T> [[nodiscard]] result<std::remove_const_t<T>> try_min(span<T> in) noexcept {
  auto idx = try_argmin(in);
  if (!idx)
    return unexpected(idx.error());
  return in[*idx];
}

template <class T> [[nodiscard]] result<std::remove_const_t<T>> try_max(span<T> in) noexcept {
  auto idx = try_argmax(in);
  if (!idx)
    return unexpected(idx.error());
  return in[*idx];
}

template <std::integral Acc, std::integral T>
[[nodiscard]] result<void> try_inclusive_scan(span<Acc> out, span<T> in) noexcept {
  if (out.size() < in.size())
    return unexpected(error::out_of_bounds);
  if (!ref_detail::disjoint(out, in))
    return unexpected(error::invalid_argument);
  Acc running{};
  for (std::size_t i = 0; i < in.size(); ++i) {
    auto next = checked_add<Acc>(running, static_cast<Acc>(in[i]));
    if (!next)
      return unexpected(next.error());
    running = *next;
    out[i] = running;
  }
  return {};
}

template <std::integral Acc, std::integral T>
[[nodiscard]] result<void> try_exclusive_scan(span<Acc> out, span<T> in, Acc init = Acc{}) noexcept {
  if (out.size() < in.size())
    return unexpected(error::out_of_bounds);
  if (!ref_detail::disjoint(out, in))
    return unexpected(error::invalid_argument);
  Acc running = init;
  for (std::size_t i = 0; i < in.size(); ++i) {
    out[i] = running;
    if (i + 1 < in.size()) {
      auto next = checked_add<Acc>(running, static_cast<Acc>(in[i]));
      if (!next)
        return unexpected(next.error());
      running = *next;
    }
  }
  return {};
}

} // namespace reference
RELOCO_KERNEL_END
