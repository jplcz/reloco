// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file relocatable_std.hpp
 * @brief `is_trivially_relocatable` specializations for `std::pair`,
 * `std::tuple`, `std::optional`, and `std::variant`.
 *
 * Kept in a separate header from `relocatable.hpp` on purpose, exactly like
 * `container_ref_std.hpp` is kept separate from `container_ref.hpp`: these
 * specializations require `<utility>`/`<tuple>`/`<optional>`/`<variant>`,
 * which the rest of reloco does not otherwise need. Only a user who
 * explicitly `#include`s this header opts into them.
 *
 * `std::pair<T1, T2>`, `std::tuple<Ts...>`, `std::optional<T>`, and
 * `std::variant<Ts...>` are already trivially relocatable whenever every
 * one of their contained types is trivially *copyable* -- the primary
 * `is_trivially_relocatable<T> : std::is_trivially_copyable<T>` template
 * (see `relocatable.hpp`) already gets that case right on its own, because
 * each of these standard wrapper types is conditionally trivially copyable
 * based on its own contained type(s).
 *
 * What the primary template gets *wrong* is the case reloco cares about
 * most: a contained type that is move-only or otherwise non-trivially
 * copyable, but is still trivially *relocatable* -- `reloco::unique_ptr<T>`,
 * `reloco::basic_string`, `reloco::vector<T>`, `std::unique_ptr<T>`, and so
 * on. In that case the wrapper itself is not trivially copyable (its
 * special member functions cannot be trivial when a contained type's
 * aren't), so the default answers `false`, even though `std::pair`,
 * `std::tuple`, `std::optional`, and `std::variant` are each, by
 * construction, flat aggregates of their contained type(s) plus (at most) a
 * discriminant/index -- with no pointer back into the wrapper itself and no
 * external registration tied to the wrapper's address. Relocating the
 * wrapper's bytes and abandoning the old address is exactly as safe as
 * relocating each contained type would be on its own.
 *
 * This assumes the standard library implementation does not add hidden
 * self-referential state to these class templates beyond what the standard
 * requires -- true of libstdc++, libc++, and MSVC STL in practice (and the
 * same assumption the P1144 relocation proposal itself makes when listing
 * `std::pair`/`std::tuple`/`std::optional` as conditionally trivially
 * relocatable), but not something the standard formally guarantees today.
 */

#include "relocatable.hpp"

#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace reloco {

/**
 * @brief `std::pair<T1, T2>` is trivially relocatable iff both `T1` and
 * `T2` are.
 */
template <typename T1, typename T2>
struct is_trivially_relocatable<std::pair<T1, T2>>
    : std::bool_constant<is_trivially_relocatable_v<T1> && is_trivially_relocatable_v<T2>> {};

/**
 * @brief `std::tuple<Ts...>` is trivially relocatable iff every `Ts` is.
 */
template <typename... Ts>
struct is_trivially_relocatable<std::tuple<Ts...>> : std::bool_constant<(is_trivially_relocatable_v<Ts> && ...)> {};

/**
 * @brief `std::optional<T>` is trivially relocatable iff `T` is -- it
 * stores `T` inline plus an `is_engaged` flag, never a pointer back into
 * itself. Mirrors `reloco::optional<T>`'s own specialization (see
 * `optional.hpp`).
 */
template <typename T> struct is_trivially_relocatable<std::optional<T>> : is_trivially_relocatable<T> {};

/**
 * @brief `std::variant<Ts...>` is trivially relocatable iff every `Ts` is --
 * it stores whichever alternative is active inline plus an index, never a
 * pointer back into itself.
 */
template <typename... Ts>
struct is_trivially_relocatable<std::variant<Ts...>> : std::bool_constant<(is_trivially_relocatable_v<Ts> && ...)> {};

} // namespace reloco
