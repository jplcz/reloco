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
 */

#include <cstddef>
#include <functional>

namespace reloco {

namespace detail {

/**
 * @brief Per-instantiation static storage whose address serves as `T`'s
 * type identity (see the file-level docs above for why this needs no
 * RTTI).
 */
template <typename T> struct type_id_tag {
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
