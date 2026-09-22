// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file mem.hpp
 * @brief Rust `std::mem::take`/`std::mem::replace` equivalents.
 *
 * Both functions move a value out of a live object while leaving that
 * object in a well-defined state, instead of the caller having to
 * hand-roll a `std::move` + reassignment dance. They compose naturally
 * with `reloco::checked_value<T>` (see `checked_value.hpp`): replacing a
 * field with a placeholder before moving it elsewhere is exactly the
 * pattern `checked_value<T>::take()` exists to make safe.
 */

#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief Rust `std::mem::replace` equivalent: overwrites `*slot` with
 * @p new_value, returning the previous value.
 */
template <typename T, typename U = T>
[[nodiscard]] constexpr T replace(T &slot, U &&new_value) noexcept(
    std::is_nothrow_move_constructible_v<T> &&std::is_nothrow_assignable_v<T &, U &&>) {
  T old = std::move(slot);
  slot = std::forward<U>(new_value);
  return old;
}

/**
 * @brief Rust `std::mem::take` equivalent: overwrites `*slot` with a
 * default-constructed `T`, returning the previous value. Requires `T` to
 * be default-constructible (Rust's `T: Default` bound).
 */
template <typename T>
[[nodiscard]] constexpr T take(T &slot) noexcept(std::is_nothrow_move_constructible_v<T>
                                                     &&std::is_nothrow_default_constructible_v<T>) {
  static_assert(std::is_default_constructible_v<T>, "reloco::take requires a default-constructible T");
  return replace(slot, T());
}

} // namespace reloco
