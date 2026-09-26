// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file type_metadata.hpp
 * @brief `type_metadata`: the compile-time facts about a type `T` that
 * every type-erased reloco container engine (`vector_base.hpp` today;
 * a future map/list/node-based engine tomorrow) needs in order to operate
 * on `T` through `void*` without a template parameter of its own.
 *
 * `type_metadata` is deliberately container-shape-agnostic: it only
 * describes `T` itself (size, alignment, and the handful of triviality
 * facts `construction_helpers.hpp` already tiers dispatch on), never
 * anything about how a particular container lays those `T`s out in memory
 * (contiguous array, node, bucket, ...). That split is what lets it be
 * shared as-is by a type-erased `vector_operations`-style table today and
 * an analogous per-node or per-bucket table for a future type-erased
 * map/list/flat-container engine, without those engines needing to agree
 * on anything beyond "here is a `const type_metadata &` describing my
 * element type".
 *
 * `metadata_for<T>` computes the value once per `T` as a `constexpr`
 * variable template, so callers pass `metadata_for<T>` into a type-erased
 * API instead of recomputing these facts themselves.
 *
 * The triviality facts themselves are packed into a single
 * `type_capability` bitmask rather than four separate `bool` fields, with
 * `type_metadata::is_trivially_destructible()`/`is_trivially_relocatable()`/
 * `is_trivially_copyable()`/`is_default_constructible()` as `constexpr`
 * accessors over it -- callers read `type.is_trivially_relocatable()`
 * exactly as before, just through a method instead of a field.
 *
 * `metadata_for<T>` itself is a reference, not a value: it aliases
 * `metadata_instance<Size, Alignment, Capabilities>`, a canonical
 * `type_metadata` keyed purely on those three values rather than on `T`.
 * Distinct types with identical size/alignment/capabilities -- `int` and
 * `unsigned int`, say, both 4-byte/4-aligned and fully trivial -- therefore
 * share one `type_metadata` object instead of each getting an otherwise
 * bit-identical copy of their own; every caller above still writes
 * `metadata_for<T>` and gets a `const type_metadata &`, unaware of whether
 * it is the only user of that particular instance or one of many.
 */

#include "../alignment.hpp"
#include "../relocatable.hpp"
#include "../reloco_extern.hpp"

#include <cstddef>
#include <type_traits>

namespace reloco::detail {

/**
 * @brief Bitmask of the triviality facts `type_metadata` tracks about a
 * type `T`, one bit per fact so all four fit in a single word instead of
 * four separate `bool` fields.
 */
enum class type_capability : unsigned {
  none = 0,
  trivially_destructible = 1u << 0,
  trivially_relocatable = 1u << 1,
  trivially_copyable = 1u << 2,
  default_constructible = 1u << 3,
};

[[nodiscard]] constexpr type_capability operator|(type_capability lhs, type_capability rhs) noexcept {
  return static_cast<type_capability>(static_cast<unsigned>(lhs) | static_cast<unsigned>(rhs));
}

[[nodiscard]] constexpr type_capability operator&(type_capability lhs, type_capability rhs) noexcept {
  return static_cast<type_capability>(static_cast<unsigned>(lhs) & static_cast<unsigned>(rhs));
}

/**
 * @brief `true` when every bit set in @p flag is also set in @p flags.
 */
[[nodiscard]] constexpr bool has_capability(type_capability flags, type_capability flag) noexcept {
  return (flags & flag) == flag;
}

/**
 * @brief Compile-time facts about `T` needed by reloco's type-erased
 * container engines, captured once per `T` in `metadata_for<T>` so the
 * untyped engine classes that consume it never need a template parameter
 * themselves.
 */
struct RELOCO_EXPORT type_metadata {
  std::size_t element_size;
  std::size_t element_alignment;
  type_capability capabilities;

  [[nodiscard]] constexpr bool is_trivially_destructible() const noexcept {
    return has_capability(capabilities, type_capability::trivially_destructible);
  }

  [[nodiscard]] constexpr bool is_trivially_relocatable() const noexcept {
    return has_capability(capabilities, type_capability::trivially_relocatable);
  }

  [[nodiscard]] constexpr bool is_trivially_copyable() const noexcept {
    return has_capability(capabilities, type_capability::trivially_copyable);
  }

  [[nodiscard]] constexpr bool is_default_constructible() const noexcept {
    return has_capability(capabilities, type_capability::default_constructible);
  }
};

/**
 * @brief Computes the `type_capability` bitmask for `T`.
 */
template <typename T>
inline constexpr type_capability capabilities_for =
    (std::is_trivially_destructible_v<T> ? type_capability::trivially_destructible : type_capability::none) |
    (is_trivially_relocatable_v<T> ? type_capability::trivially_relocatable : type_capability::none) |
    (std::is_trivially_copyable_v<T> ? type_capability::trivially_copyable : type_capability::none) |
    (std::is_default_constructible_v<T> ? type_capability::default_constructible : type_capability::none);

/**
 * @brief The canonical `type_metadata` instance for a given (size,
 * alignment, capabilities) triple, keyed purely on those three values
 * rather than on any particular `T` -- so distinct types that happen to
 * share size/alignment/capabilities (e.g. `int` and `unsigned int`, or
 * `float` and any other 4-byte/4-aligned trivial type) resolve to the
 * exact same object instead of each getting their own otherwise-identical
 * copy.
 */
template <std::size_t Size, std::size_t Alignment, type_capability Capabilities>
inline constexpr type_metadata metadata_instance = {Size, Alignment, Capabilities};

/**
 * @brief The `type_metadata` for `T`: a reference to the shared
 * `metadata_instance` for `T`'s (size, alignment, capabilities), so
 * `metadata_for<T>` never allocates a fresh `type_metadata` object of its
 * own -- it is a distinct template instantiation per `T`, but every one
 * with the same size/alignment/capabilities aliases the same underlying
 * `metadata_instance`.
 */
template <typename T>
inline constexpr const type_metadata &metadata_for =
    metadata_instance<sizeof(T), effective_alignment_v<T>, capabilities_for<T>>;

} // namespace reloco::detail
