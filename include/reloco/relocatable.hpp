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
 *
 * Composition is **not** automatic here either (the same caveat
 * `send_sync.hpp` documents for `is_send`/`is_sync`): a plain struct
 * embedding a `unique_ptr<T>` field is not `std::is_trivially_copyable`
 * (its implicitly-defined destructor/move calls `unique_ptr<T>`'s
 * non-trivial ones), so it falls back to the safe-but-pessimistic default
 * of `false`, even though it plainly *is* safe to relocate -- nothing
 * walks its fields to notice every one of them already is.
 *
 * **Experimental**: when built with `-freflection` (see
 * `RELOCO_HAS_REFLECTION` in `detail/compat.hpp`), that fallback stops
 * being a blind `is_trivially_copyable_v<T>` and instead applies the same
 * conditional rule P1144 itself proposes for the standard trait: `T`
 * qualifies if (a) it has no *user-provided* special member function
 * (default/copy/move constructor, copy/move assignment, or destructor --
 * a `= default`-explicit or implicitly-generated one is fine, since it
 * cannot hide any address-dependent side effect a raw relocation would
 * skip), and (b) every one of `T`'s base classes and non-static data
 * members (including private ones) is itself `is_trivially_relocatable`,
 * recursing through the same trait. This only ever changes behavior for a
 * `T` with no explicit `is_trivially_relocatable<T>` specialization of its
 * own -- a real specialization is always preferred by ordinary C++
 * overload resolution for class template specializations, so `unique_ptr
 * <T>` (which *does* have a user-provided destructor, and therefore is
 * correctly never auto-derivable) keeps using its own manual, verified
 * specialization either way. See `send_sync.hpp` for the twin
 * `is_send`/`is_sync` feature this mirrors, and `docs/reflection.md` for
 * the full writeup of both.
 */

#include "detail/compat.hpp"

#include <type_traits>

#if RELOCO_HAS_REFLECTION
#include <meta>
#endif

namespace reloco {

#if RELOCO_HAS_REFLECTION

// Forward declaration: `compose_relocatable` (below) needs to recurse into
// `is_trivially_relocatable` before the primary template is declared later
// in this file.
template <typename T> struct is_trivially_relocatable;

namespace detail {

/**
 * @brief `true` iff none of `T`'s special member functions (default/copy/
 * move constructor, copy/move assignment, destructor) is user-provided --
 * i.e. every one is implicitly-defined or explicitly `= default`ed/`=
 * delete`d. A user-provided special member could run arbitrary logic
 * (self-registration, storing `this` somewhere external, ...) that a raw
 * byte-copy relocation would silently skip, so this is the load-bearing
 * precondition `compose_relocatable` below requires before it ever trusts
 * a purely structural, field-by-field composition.
 */
template <typename T> consteval bool has_no_user_provided_special_members() {
  bool ok = true;
  template for (constexpr auto m :
                define_static_array(std::meta::members_of(^^T, std::meta::access_context::unchecked()))) {
    if constexpr (std::meta::is_special_member_function(m)) {
      if (std::meta::is_user_provided(m))
        ok = false;
    }
  }
  return ok;
}

/**
 * @brief Experimental, `-freflection`-only structural fallback for
 * `is_trivially_relocatable<T>` (see the file-level "Experimental"
 * section above). Only ever instantiated for a `T` with no explicit
 * specialization of its own. Mirrors P1144's own conditional rule:
 * `is_trivially_copyable_v<T>` is checked first (the existing, always-
 * correct default); failing that, `T` is derived `true` only if it has no
 * user-provided special member function *and* every base/member is itself
 * `is_trivially_relocatable`, recursing through the same trait so nested
 * reloco-specialized types (`unique_ptr<T>`, `basic_string<...>`, ...) are
 * picked up transitively. A non-class type that already failed the
 * `is_trivially_copyable_v` check (a reference, a function type, ...)
 * conservatively stays `false`.
 */
template <typename T> consteval bool compose_relocatable() {
  if constexpr (std::is_trivially_copyable_v<T>) {
    return true;
  } else if constexpr (std::is_class_v<T>) {
    if (!has_no_user_provided_special_members<T>())
      return false;
    bool ok = true;
    template for (constexpr auto b :
                  define_static_array(std::meta::bases_of(^^T, std::meta::access_context::unchecked()))) ok =
        ok &&is_trivially_relocatable<typename[:std::meta::type_of(b):]>::value;
    template for (constexpr auto m : define_static_array(
                      std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()))) ok =
        ok &&is_trivially_relocatable<typename[:std::meta::type_of(m):]>::value;
    return ok;
  } else {
    return false;
  }
}

} // namespace detail

#endif // RELOCO_HAS_REFLECTION

/**
 * @brief Customization point: does `T`'s object representation survive
 * being relocated by copying its bytes to a new address and abandoning the
 * old one, without running any constructor or destructor?
 *
 * Defaults to `std::is_trivially_copyable_v<T>` (or, on a
 * `RELOCO_HAS_REFLECTION` build, additionally a sound structural
 * composition over `T`'s fields -- see the file-level documentation
 * above). Specialize for a non-trivially-copyable type that is
 * nonetheless safe to relocate this way (see the file-level documentation
 * above for what "safe" requires).
 */
#if RELOCO_HAS_REFLECTION
template <typename T> struct is_trivially_relocatable : std::bool_constant<detail::compose_relocatable<T>()> {};
#else
template <typename T> struct is_trivially_relocatable : std::is_trivially_copyable<T> {};
#endif

/**
 * @brief Convenience variable template for `is_trivially_relocatable<T>::value`.
 */
template <typename T> inline constexpr bool is_trivially_relocatable_v = is_trivially_relocatable<T>::value;

#if RELOCO_CXX20

template <typename T>
concept trivially_relocatable = is_trivially_relocatable_v<T>;

#endif // RELOCO_CXX20

} // namespace reloco
