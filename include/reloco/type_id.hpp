// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file type_id.hpp
 * @brief `reloco::type_id`, a process-wide type identity established
 * without RTTI, matching Rust's `std::any::TypeId`.
 *
 * Rust's `TypeId::of::<T>()` hands back an opaque, `'static`, `Copy` +
 * `Eq` + `Hash` value that uniquely identifies `T` for the lifetime of the
 * process, without needing anything like C++'s `typeid`/`<typeinfo>`.
 * `reloco::type_id` reproduces that without RTTI, using the same trick
 * `any.hpp` used internally before this was pulled out into its own
 * public, reusable header: `type_id::of<T>()` returns the address of a
 * per-instantiation static data member (`detail::type_id_tag<T>::tag`).
 * Every translation unit that instantiates `type_id_tag<T>` for the same
 * `T` refers to the same (implicitly `inline`, since C++17) symbol, so its
 * address is a stable, process-wide, per-type identity that two `type_id`
 * values can compare by pointer equality -- no type name string, no hash
 * of a mangled name, and no runtime registration involved.
 *
 * `type_id` is a small, trivially-copyable value type (one pointer): copy
 * it, store it, compare it, hash it. A default-constructed `type_id` is a
 * distinct "no type" sentinel (`operator bool()` is `false`), never equal
 * to `type_id::of<T>()` for any `T`; this is what a type-erased container
 * like `any` returns for its own `type_id()` accessor when empty, since --
 * unlike Rust's `dyn Any`, which is never "empty" -- a `reloco::any` can be.
 *
 * Rust's own `TypeId` implements only `Eq`/`Hash` (deliberately not `Ord`,
 * since a stable cross-compilation ordering isn't guaranteed). reloco adds
 * `operator<`/`operator<=`/`operator>`/`operator>=` (via `std::less<const
 * void *>`, so the order is at least well-defined and total within a
 * single process even though pointer relational operators alone would not
 * guarantee that across unrelated objects) so a `type_id` can be used as
 * a `flat_set<type_id>`/`flat_map<type_id, V>` key directly; `std::hash`
 * is specialized too, for interop with `std::unordered_map`/`unordered_set`.
 *
 * `detail::type_id_tag<T>` is marked `RELOCO_EXPORT` (see
 * `detail/compat.hpp`) so its address-as-identity trick can survive being
 * compiled into a consumer built with `-fvisibility=hidden`/
 * `-fvisibility-inlines-hidden` (a common default for shared libraries):
 * without it, two shared objects that each implicitly instantiate
 * `type_id_tag<T>` for the same, otherwise externally-visible `T` would
 * each get their own separate, hidden copy of that symbol instead of the
 * dynamic linker merging them into one, silently breaking `type_id`
 * equality (and `any::is<T>()`/`is()` built on it) for any `T` shared
 * across that boundary. A `T` that is itself TU-local (e.g. declared in an
 * anonymous namespace, or explicitly hidden) is unaffected either way:
 * its `type_id_tag<T>` instantiation correctly stays hidden too, since
 * GCC/Clang compute template instantiation visibility as the minimum of
 * the template's own visibility and each template argument's -- which also
 * means `RELOCO_EXPORT` on `type_id_tag<T>` only takes effect for a `T`
 * that *itself* has default visibility. A fundamental type like `int`
 * always qualifies; an ordinary consumer-defined class does not
 * automatically, under `-fvisibility=hidden`, unless the consumer also
 * gives that specific `T` its own `__attribute__((visibility("default")))`
 * (or an equivalent, e.g. via a `-fvisibility=default` override on its
 * translation unit). This is not a reloco limitation to work around: it is
 * the same, correct minimum-visibility rule that keeps a genuinely
 * TU-local `T` correctly TU-local; a consumer that wants a specific `T`'s
 * `type_id` to compare equal across a shared-object boundary must
 * explicitly mark that `T` exported too, exactly as they already would for
 * any other symbol of `T`'s they intend to share across that boundary.
 *
 * `RELOCO_EXPORT` is opt-in (see `reloco_config.hpp`): it does nothing
 * unless the consumer defines `RELOCO_ENABLE_EXPORT` before including any
 * reloco header, since forcing default visibility on these symbols is
 * only useful -- and only something a consumer should have to reason
 * about -- if `type_id`/`any` values for a shared `T` actually do cross a
 * shared-object boundary. On a backend without an equivalent attribute
 * (e.g. MSVC), it is a no-op regardless; Windows' PE/COFF model has no
 * equivalent to ELF's merged default-visibility symbols, so `type_id`
 * equality is not guaranteed across separate DLLs there either way.
 */

#include "detail/compat.hpp"

#include <cstddef>
#include <functional>

namespace reloco {

namespace detail {

/**
 * @brief Per-instantiation static storage whose address serves as `T`'s
 * type identity (see the file-level docs above for why this needs no
 * RTTI, and why it is `RELOCO_EXPORT`-annotated).
 */
template <typename T> struct RELOCO_EXPORT type_id_tag {
  static constexpr char tag = 0;
};

} // namespace detail

/**
 * @brief Opaque, process-wide type identity established without RTTI,
 * matching Rust's `std::any::TypeId`. See the file-level docs for details.
 */
class type_id {
public:
  /** @brief The "no type" sentinel: `operator bool()` is `false`, and it
   * never compares equal to `type_id::of<T>()` for any `T`. */
  constexpr type_id() noexcept = default;

  /**
   * @brief Returns the identity of `T`, matching Rust's
   * `TypeId::of::<T>()`. Two calls with the same `T` (including
   * cv-qualification and reference-ness -- `of<T>()` never decays `T` for
   * you, unlike `any::is<T>()`) always compare equal, from any
   * translation unit.
   */
  template <typename T> [[nodiscard]] static constexpr type_id of() noexcept {
    return type_id(&detail::type_id_tag<T>::tag);
  }

  /** @brief `false` for a default-constructed ("no type") instance, `true`
   * for any `type_id::of<T>()`. */
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return tag_ != nullptr; }

  [[nodiscard]] constexpr bool operator==(type_id other) const noexcept { return tag_ == other.tag_; }
  [[nodiscard]] constexpr bool operator!=(type_id other) const noexcept { return tag_ != other.tag_; }

  [[nodiscard]] bool operator<(type_id other) const noexcept { return std::less<const void *>{}(tag_, other.tag_); }
  [[nodiscard]] bool operator>(type_id other) const noexcept { return other < *this; }
  [[nodiscard]] bool operator<=(type_id other) const noexcept { return !(other < *this); }
  [[nodiscard]] bool operator>=(type_id other) const noexcept { return !(*this < other); }

  /** @brief Matching Rust's `Hash` implementation for `TypeId`; also
   * backs the `std::hash<reloco::type_id>` specialization below. */
  [[nodiscard]] std::size_t hash() const noexcept { return std::hash<const void *>{}(tag_); }

private:
  constexpr explicit type_id(const void *tag) noexcept : tag_(tag) {}

  const void *tag_{nullptr};
};

/**
 * @brief Free-function convenience alias for `type_id::of<T>()`.
 */
template <typename T> [[nodiscard]] constexpr type_id type_id_of() noexcept { return type_id::of<T>(); }

} // namespace reloco

namespace std {

template <> struct hash<reloco::type_id> {
  std::size_t operator()(const reloco::type_id &id) const noexcept { return id.hash(); }
};

} // namespace std
