// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file relocatable.hpp
 * @brief `is_trivially_relocatable`, a customization-point trait marking
 * types whose object representation may be *relocated* -- moved to a new
 * address by copying its bytes (e.g. via `memcpy`/`realloc`) and then
 * treating the old address as holding no object at all -- without invoking
 * a move constructor or destructor at either address.
 *
 * This is the same property `reloco_legacy` tracked as `is_relocatable<T>`
 * (see `reloco_legacy/include/reloco/core.hpp`), renamed to
 * `is_trivially_relocatable` to match the terminology of the standard's own
 * relocation proposal (P1144, `std::is_trivially_relocatable`). Every
 * `std::is_trivially_copyable` type trivially qualifies (bytewise copy is
 * already how the standard permits moving/copying it), which is why that is
 * the default; the interesting cases are move-only or non-trivial types
 * that still happen to hold no self-referential pointers or external
 * registrations tied to their own address (an internal pointer to
 * heap-allocated storage is fine -- only a pointer *back into the object
 * itself*, or a pointer stored in some external registry, is not).
 *
 * A generic container that knows every one of its elements is trivially
 * relocatable can grow (`realloc`) or shift its storage with a raw
 * `memcpy`/`memmove` instead of a loop of move-construct + destroy, exactly
 * like `basic_string::try_reserve` already does for its own backing buffer.
 *
 * Specialize `is_trivially_relocatable<T>` for a move-only or otherwise
 * non-trivially-copyable type once its author has verified there is no
 * such internal or external self-reference. reloco itself specializes it
 * for `unique_ptr<T>` (see `unique_ptr.hpp`), `basic_string<CharT, TraitsT>`
 * (see `string.hpp`), and `checked_value<T>`/`checked_value<T *>` (see
 * `checked_value.hpp`).
 */

#include "detail/compat.hpp"

#include <type_traits>

namespace reloco {

/**
 * @brief Customization point: does `T`'s object representation survive
 * being relocated by copying its bytes to a new address and abandoning the
 * old one, without running any constructor or destructor?
 *
 * Defaults to `std::is_trivially_copyable_v<T>`. Specialize for a
 * non-trivially-copyable type that is nonetheless safe to relocate this
 * way (see the file-level documentation above for what "safe" requires).
 */
template <typename T> struct is_trivially_relocatable : std::is_trivially_copyable<T> {};

/**
 * @brief Convenience variable template for `is_trivially_relocatable<T>::value`.
 */
template <typename T> inline constexpr bool is_trivially_relocatable_v = is_trivially_relocatable<T>::value;

#if RELOCO_CXX20

template <typename T>
concept trivially_relocatable = is_trivially_relocatable_v<T>;

#endif // RELOCO_CXX20

} // namespace reloco
