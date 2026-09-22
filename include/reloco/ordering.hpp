// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file ordering.hpp
 * @brief Rust `std::cmp::Ordering` equivalent for pre-C++20 three-way
 * comparison and multi-key comparator chaining.
 *
 * `reloco::ordering` targets the same C++17 baseline as the rest of the
 * library (no `operator<=>`), and gives multi-key comparators (as used by
 * `flat_set`/`flat_map`'s `Compare` parameter) a way to chain
 * "compare by key A, and if that ties, break it with key B" without
 * hand-written `if (a.x != b.x) return a.x < b.x; return a.y < b.y;`
 * boilerplate -- exactly what Rust's `Ordering::then`/`then_with` are for.
 */

#include <cstdint>

namespace reloco {

/**
 * @brief The result of a three-way comparison, matching Rust's
 * `std::cmp::Ordering`.
 */
enum class ordering : std::int8_t { less = -1, equal = 0, greater = 1 };

/**
 * @brief Rust `Ord::cmp` equivalent: compares @p a and @p b with
 * `operator<`, producing an `ordering`.
 */
template <typename T> [[nodiscard]] constexpr ordering compare(const T &a, const T &b) noexcept {
  if (a < b)
    return ordering::less;
  if (b < a)
    return ordering::greater;
  return ordering::equal;
}

[[nodiscard]] constexpr bool is_lt(ordering o) noexcept { return o == ordering::less; }
[[nodiscard]] constexpr bool is_eq(ordering o) noexcept { return o == ordering::equal; }
[[nodiscard]] constexpr bool is_gt(ordering o) noexcept { return o == ordering::greater; }
[[nodiscard]] constexpr bool is_le(ordering o) noexcept { return o != ordering::greater; }
[[nodiscard]] constexpr bool is_ge(ordering o) noexcept { return o != ordering::less; }

/**
 * @brief Rust `Ordering::reverse` equivalent: swaps `less`/`greater`,
 * leaving `equal` untouched.
 */
[[nodiscard]] constexpr ordering reverse(ordering o) noexcept {
  switch (o) {
  case ordering::less:
    return ordering::greater;
  case ordering::greater:
    return ordering::less;
  case ordering::equal:
    return ordering::equal;
  }
  return ordering::equal;
}

/**
 * @brief Rust `Ordering::then` equivalent: returns @p first unless it is
 * `equal`, in which case @p second breaks the tie. Chain calls to compare
 * by successive keys: `then(compare(a.x, b.x), compare(a.y, b.y))`.
 */
[[nodiscard]] constexpr ordering then(ordering first, ordering second) noexcept {
  return first == ordering::equal ? second : first;
}

/**
 * @brief Rust `Ordering::then_with` equivalent: returns @p first unless
 * it is `equal`, in which case @p f is invoked (lazily, avoiding the cost
 * of computing a tie-breaking key unless it is actually needed) and its
 * `ordering` result is returned instead.
 */
template <typename F> [[nodiscard]] constexpr ordering then_with(ordering first, F &&f) noexcept {
  return first == ordering::equal ? f() : first;
}

} // namespace reloco
