// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/algo/scan.hpp
 * @brief Prefix sums: `try_inclusive_scan`, `try_exclusive_scan`.
 *
 * `out` must be at least as long as `in` (`out_of_bounds`) and must not overlap it (`invalid_argument`). The sums are
 * overflow-checked in the element type of `out` (`integer_overflow`); on an error the contents of `out` are
 * unspecified. See `docs/kernel/algorithms.md`.
 */

#include "../../error.hpp"
#include "../../int_ops.hpp"
#include "../../span.hpp"
#include "../bind/bound.hpp"
#include "../config.hpp"

#include <concepts>
#include <cstddef>

namespace reloco::kernel::detail {
struct scan_brand {};
} // namespace reloco::kernel::detail

RELOCO_KERNEL_BEGIN

/** @brief `out[i] = in[0] + ... + in[i]`. */
template <std::integral Acc, std::integral T>
[[nodiscard]] result<void> try_inclusive_scan(span<Acc> out, span<T> in) noexcept {
  auto b = try_bind<detail::scan_brand>(in.size(), out, in);
  if (!b)
    return unexpected(b.error());
  auto dst = b.value().template view<0>();
  auto src = b.value().template view<1>();
  Acc running{};
  for (auto i : b.value().domain()) {
    auto next = checked_add<Acc>(running, static_cast<Acc>(src(i)));
    if (!next)
      return unexpected(next.error());
    running = *next;
    dst(i) = running;
  }
  return {};
}

/** @brief `out[0] = init`, `out[i] = init + in[0] + ... + in[i-1]`. */
template <std::integral Acc, std::integral T>
[[nodiscard]] result<void> try_exclusive_scan(span<Acc> out, span<T> in, Acc init = Acc{}) noexcept {
  auto b = try_bind<detail::scan_brand>(in.size(), out, in);
  if (!b)
    return unexpected(b.error());
  auto dst = b.value().template view<0>();
  auto src = b.value().template view<1>();
  Acc running = init;
  const std::size_t n = in.size();
  for (auto i : b.value().domain()) {
    dst(i) = running;
    if (i.value() + 1 < n) { // the sum after the last element is never stored, so it must not report overflow
      auto next = checked_add<Acc>(running, static_cast<Acc>(src(i)));
      if (!next)
        return unexpected(next.error());
      running = *next;
    }
  }
  return {};
}

RELOCO_KERNEL_END
