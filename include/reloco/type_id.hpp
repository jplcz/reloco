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
 * per-instantiation static data member (`type_id_tag<T>::tag`). Every
 * translation unit that instantiates `type_id_tag<T>` for the same `T`
 * refers to the same (implicitly `inline`, since C++17) symbol, so its
 * address is a stable, process-wide, per-type identity that two `type_id`
 * values can compare by pointer equality -- no type name string, no hash
 * of a mangled name, and no runtime registration involved.
 *
 * `type_id` is a small, trivially-copyable value type (two pointers): copy
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
 * `type_id_tag<T>` is deliberately public (unlike most of reloco's
 * implementation details, which live under `reloco::detail`), because it
 * doubles as an optional customization point for a human-readable debug
 * name, in the same non-RTTI, opt-in-registration style game engines (e.g.
 * Unreal, EnTT) use to give a type-erased identity a friendly name for
 * logging/debugging: `type_id_tag<T>::name` is `nullptr` unless a `T` has
 * one explicitly registered, via a full specialization of `type_id_tag<T>`
 * (see `RELOCO_TYPE_ID_NAME` below), and `type_id::name()` surfaces it.
 * Registering, or not registering, a name never changes `type_id`
 * equality/ordering/hashing -- those are based solely on `tag`'s address;
 * `name` is a pure debugging aid layered on top. See `RELOCO_TYPE_ID_NAME`
 * for the set of standard and reloco types named out of the box.
 *
 * `type_id_tag<T>` is marked `RELOCO_EXPORT` (see `detail/compat.hpp`) so
 * its address-as-identity trick can survive being compiled into a
 * consumer built with `-fvisibility=hidden`/`-fvisibility-inlines-hidden`
 * (a common default for shared libraries): without it, two shared objects
 * that each implicitly instantiate `type_id_tag<T>` for the same,
 * otherwise externally-visible `T` would each get their own separate,
 * hidden copy of that symbol instead of the dynamic linker merging them
 * into one, silently breaking `type_id` equality (and `any::is<T>()`/
 * `is()` built on it) for any `T` shared across that boundary. A `T` that
 * is itself TU-local (e.g. declared in an anonymous namespace, or
 * explicitly hidden) is unaffected either way: its `type_id_tag<T>`
 * instantiation correctly stays hidden too, since GCC/Clang compute
 * template instantiation visibility as the minimum of the template's own
 * visibility and each template argument's -- which also means
 * `RELOCO_EXPORT` on `type_id_tag<T>` only takes effect for a `T` that
 * *itself* has default visibility. A fundamental type like `int` always
 * qualifies; an ordinary consumer-defined class does not automatically,
 * under `-fvisibility=hidden`, unless the consumer also gives that
 * specific `T` its own `__attribute__((visibility("default")))` (or an
 * equivalent, e.g. via a `-fvisibility=default` override on its
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

/**
 * @brief Per-`T` identity, and optional debug name, backing
 * `type_id::of<T>()`. See the file-level docs for the full rationale.
 *
 * A full specialization of this template (typically written via
 * `RELOCO_TYPE_ID_NAME` rather than by hand) must keep `tag` exactly as
 * `static constexpr char tag = 0;` -- its *address*, not its value, is
 * what `type_id::of<T>()` uses as `T`'s identity -- and may set `name` to
 * any string literal (or other `constexpr const char *`) naming `T`.
 */
template <typename T> struct RELOCO_EXPORT type_id_tag {
  static constexpr char tag = 0;
  static constexpr const char *name = nullptr;
};

/**
 * @def RELOCO_TYPE_ID_NAME
 * @brief Registers a debug name for `T`, retrievable via
 * `type_id::of<T>().name()`, by declaring a `reloco::type_id_tag<T>` full
 * specialization.
 *
 * Usable at namespace scope, inside or outside `namespace reloco` (it
 * fully qualifies the specialization itself); does not end in `;` -- add
 * one at the call site, matching every other multi-statement reloco macro
 * (e.g. `RELOCO_BLOCK_RVALUE_ACCESS`).
 *
 * @param T The type to name. Only needs to be a valid template argument
 * (an incomplete type is fine) at the point of use.
 * @param name_str A string literal (or other `constexpr const char *`)
 * naming `T`, e.g. `"int"`.
 *
 * @code
 * RELOCO_TYPE_ID_NAME(my_widget, "my_widget");
 * @endcode
 */
#define RELOCO_TYPE_ID_NAME(T, name_str)                                                                             \
  template <> struct reloco::type_id_tag<T> {                                                                        \
    static constexpr char tag = 0;                                                                                   \
    static constexpr const char *name = name_str;                                                                   \
  }

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
    return type_id(&type_id_tag<T>::tag, type_id_tag<T>::name);
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

  /**
   * @brief Optional, game-engine-style debug name registered for this
   * type via `type_id_tag<T>::name` (see `RELOCO_TYPE_ID_NAME`); `nullptr`
   * if `T` has no name registered, or if this is the "no type" sentinel.
   * Purely a debugging aid: never participates in equality, ordering, or
   * hashing.
   */
  [[nodiscard]] constexpr const char *name() const noexcept { return name_; }

private:
  constexpr explicit type_id(const void *tag, const char *name) noexcept : tag_(tag), name_(name) {}

  const void *tag_{nullptr};
  const char *name_{nullptr};
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

// Debug names for the fundamental types and reloco::type_id itself.
// "Classic" reloco types (reloco::error, reloco::string, ...) register
// their own name from their own header instead, matching how this
// codebase's std::hash<reloco::X> specializations are likewise defined
// alongside each X rather than centralized here -- keeping this
// foundational, dependency-light header from pulling in the rest of the
// library.
RELOCO_TYPE_ID_NAME(bool, "bool");
RELOCO_TYPE_ID_NAME(char, "char");
RELOCO_TYPE_ID_NAME(signed char, "signed char");
RELOCO_TYPE_ID_NAME(unsigned char, "unsigned char");
RELOCO_TYPE_ID_NAME(wchar_t, "wchar_t");
RELOCO_TYPE_ID_NAME(char16_t, "char16_t");
RELOCO_TYPE_ID_NAME(char32_t, "char32_t");
#if RELOCO_CXX20
RELOCO_TYPE_ID_NAME(char8_t, "char8_t");
#endif
RELOCO_TYPE_ID_NAME(short, "short");
RELOCO_TYPE_ID_NAME(unsigned short, "unsigned short");
RELOCO_TYPE_ID_NAME(int, "int");
RELOCO_TYPE_ID_NAME(unsigned int, "unsigned int");
RELOCO_TYPE_ID_NAME(long, "long");
RELOCO_TYPE_ID_NAME(unsigned long, "unsigned long");
RELOCO_TYPE_ID_NAME(long long, "long long");
RELOCO_TYPE_ID_NAME(unsigned long long, "unsigned long long");
RELOCO_TYPE_ID_NAME(float, "float");
RELOCO_TYPE_ID_NAME(double, "double");
RELOCO_TYPE_ID_NAME(long double, "long double");
RELOCO_TYPE_ID_NAME(std::nullptr_t, "std::nullptr_t");
RELOCO_TYPE_ID_NAME(reloco::type_id, "reloco::type_id");

