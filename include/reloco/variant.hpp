// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file variant.hpp
 * @brief `reloco::variant<Ts...>`: a thin, public-inheritance wrapper
 * around `std::variant<Ts...>` adding a handful of Rust-inspired
 * ergonomics on top -- pattern-matching via `match()`, and the
 * checked/fallible/unsafe tri-tier access convention every other reloco
 * container follows -- without reimplementing any part of `std::variant`
 * itself.
 *
 * `std::variant<Ts...>` already provides everything a tagged union needs
 * (`index()`, `holds_alternative`, `get`/`get_if`, `visit`, comparison
 * operators, exception-safe assignment); none of that is re-derived here.
 * `reloco::variant<Ts...>` is deliberately a genuine `std::variant<Ts...>`
 * (public inheritance, inherited constructors, no additional data
 * members) so it converts to/from/interoperates with `std::variant<Ts...>`
 * and every standard algorithm that accepts one, exactly like Rust
 * `Option`/`Result` do not exist as distinct types from what the rest of
 * the standard library expects -- this wrapper only adds what
 * `std::variant` itself is missing:
 *
 * - `match(fs...)`: dispatches to whichever callable in `fs...` accepts
 *   the currently active alternative, built from an ad-hoc overload set
 *   (`reloco::overloaded`) and `std::visit` under the hood -- this is
 *   exactly the "overloaded-lambda-set visitor" idiom every C++17
 *   `std::variant` user ends up hand-rolling, closest in spirit to Rust's
 *   exhaustive `match` expression over an enum.
 * - `is<T>()`/`is<T>(pred)`: alias of `std::holds_alternative<T>(*this)`,
 *   optionally combined with a predicate on the active value (matching
 *   Rust's `Option::is_some_and`), matching the naming Rust-inspired
 *   reloco APIs already use elsewhere (`is_ok`/`is_some`, etc.).
 * - The reloco checked/fallible/unsafe tri-tier access convention (see
 *   `optional.hpp`), which `std::variant` itself only offers a
 *   throwing/fallible pair for:
 *   1. **Checked (Default):** `get<T>()` behaves like `std::get<T>`, but
 *      uses `RELOCO_ASSERT` to trap on a mismatched alternative instead of
 *      throwing `std::bad_variant_access` -- reloco is exception-averse
 *      elsewhere (see `detail/assert.hpp`'s `RELOCO_KERNEL` support), so a
 *      throwing-only checked accessor doesn't fit the rest of the library.
 *   2. **Fallible:** `try_get<T>()` returns
 *      `result<reference_wrapper<T>>` (present and bound to the active
 *      alternative if it holds a `T`, `unexpected(error::not_found)`
 *      otherwise), matching `optional<T>::try_value()`'s established
 *      convention for bridging into a `result<T>` pipeline. `as<T>()` is
 *      the `optional`-returning sibling of the same tier -- like
 *      `std::get_if<T>(this)`, but bridges into
 *      `optional<std::reference_wrapper<T>>` instead of a raw pointer.
 *   3. **Unsafe:** `unsafe_get<T>()` is explicitly gated behind
 *      `RELOCO_UNSAFE_BUFFER_USAGE` and only checked via
 *      `RELOCO_DEBUG_ASSERT`, exactly like `optional<T>::unsafe_value()`.
 */

#include "detail/assert.hpp"
#include "error.hpp"
#include "optional.hpp"
#include "relocatable.hpp"

#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

namespace reloco {

/**
 * @brief Builds an ad-hoc overload set out of any number of callables,
 * for use with `std::visit`/`variant<Ts...>::match`. The classic C++17
 * "overloaded lambda" idiom, exposed as a standalone, independently
 * useful utility.
 */
template <typename... Fs> struct overloaded : Fs... {
  using Fs::operator()...;
};

template <typename... Fs> overloaded(Fs...) -> overloaded<Fs...>;

/**
 * @brief A `std::variant<Ts...>` with a small set of Rust-inspired
 * additions layered on top; see the file-level documentation for the full
 * rationale. Adds no data members, so every `std::variant<Ts...>`
 * operation, and `is_trivially_relocatable<variant<Ts...>>`, behave
 * exactly like the underlying `std::variant<Ts...>`.
 */
template <typename... Ts> class variant : public std::variant<Ts...> {
public:
  using std::variant<Ts...>::variant;

  /** @brief Rust-naming alias of `std::holds_alternative<T>(*this)`. */
  template <typename T> [[nodiscard]] bool is() const noexcept { return std::holds_alternative<T>(*this); }

  /**
   * @brief Rust `Option::is_some_and`-style equivalent: `true` if the
   * active alternative is a `T` *and* @p pred applied to it returns
   * `true`; `pred` is not invoked when a different alternative is active.
   */
  template <typename T, typename Pred> [[nodiscard]] bool is(Pred &&pred) const noexcept {
    const auto *value = std::get_if<T>(static_cast<const std::variant<Ts...> *>(this));
    return value != nullptr && pred(*value);
  }

  /**
   * @brief Checked-tier accessor: like `std::get<T>(*this)`, but uses
   * `RELOCO_ASSERT` to trap on a mismatched alternative instead of
   * throwing `std::bad_variant_access`.
   */
  template <typename T> [[nodiscard]] T &get() & noexcept RELOCO_LIFETIMEBOUND {
    auto *value = std::get_if<T>(static_cast<std::variant<Ts...> *>(this));
    RELOCO_ASSERT(value != nullptr, "variant does not hold the requested alternative");
    return *value;
  }

  /** @copydoc get() & */
  template <typename T> [[nodiscard]] const T &get() const & noexcept RELOCO_LIFETIMEBOUND {
    const auto *value = std::get_if<T>(static_cast<const std::variant<Ts...> *>(this));
    RELOCO_ASSERT(value != nullptr, "variant does not hold the requested alternative");
    return *value;
  }

  /** @copydoc get() & */
  template <typename T> [[nodiscard]] T &&get() && noexcept RELOCO_LIFETIMEBOUND {
    auto *value = std::get_if<T>(static_cast<std::variant<Ts...> *>(this));
    RELOCO_ASSERT(value != nullptr, "variant does not hold the requested alternative");
    return std::move(*value);
  }

  /**
   * @brief Fallible-tier accessor: like `std::get_if<T>(this)`, but
   * bridges into `result<std::reference_wrapper<T>>` instead of a raw
   * pointer -- present and bound to the active alternative if it holds a
   * `T`, `unexpected(error::not_found)` otherwise. Matches
   * `optional<T>::try_value()`'s established convention for bridging a
   * "maybe absent" reference-returning accessor into a `result<T>`
   * pipeline.
   */
  template <typename T> [[nodiscard]] result<std::reference_wrapper<T>> try_get() & noexcept RELOCO_LIFETIMEBOUND {
    if (auto *value = std::get_if<T>(static_cast<std::variant<Ts...> *>(this)))
      return std::ref(*value);
    return unexpected(error::not_found);
  }

  /** @copydoc try_get() & */
  template <typename T>
  [[nodiscard]] result<std::reference_wrapper<const T>> try_get() const & noexcept RELOCO_LIFETIMEBOUND {
    if (const auto *value = std::get_if<T>(static_cast<const std::variant<Ts...> *>(this)))
      return std::cref(*value);
    return unexpected(error::not_found);
  }

  /**
   * @brief Like `std::get_if<T>(this)`, but returns
   * `optional<std::reference_wrapper<T>>` instead of a raw pointer:
   * present and bound to the active alternative if it holds a `T`, empty
   * otherwise.
   */
  template <typename T> [[nodiscard]] auto as() & noexcept RELOCO_LIFETIMEBOUND {
    using Result = optional<std::reference_wrapper<T>>;
    if (auto *value = std::get_if<T>(static_cast<std::variant<Ts...> *>(this)))
      return Result(std::ref(*value));
    return Result(nullopt);
  }

  /** @copydoc as() & */
  template <typename T> [[nodiscard]] auto as() const & noexcept RELOCO_LIFETIMEBOUND {
    using Result = optional<std::reference_wrapper<const T>>;
    if (const auto *value = std::get_if<T>(static_cast<const std::variant<Ts...> *>(this)))
      return Result(std::cref(*value));
    return Result(nullopt);
  }

  /**
   * @brief Unsafe-tier accessor: returns a reference to the active
   * alternative assuming it is a `T`, without checking in release builds
   * (only `RELOCO_DEBUG_ASSERT`-checked). Caller must have verified via
   * `is<T>()`/`index()` beforehand -- reading through this when a
   * different alternative is active is undefined behavior, exactly like
   * `optional<T>::unsafe_value()`.
   */
  template <typename T> [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_get() & noexcept RELOCO_LIFETIMEBOUND {
    auto *value = std::get_if<T>(static_cast<std::variant<Ts...> *>(this));
    RELOCO_DEBUG_ASSERT(value != nullptr, "variant does not hold the requested alternative");
    return *value;
  }

  /** @copydoc unsafe_get() & */
  template <typename T>
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_get() const & noexcept RELOCO_LIFETIMEBOUND {
    const auto *value = std::get_if<T>(static_cast<const std::variant<Ts...> *>(this));
    RELOCO_DEBUG_ASSERT(value != nullptr, "variant does not hold the requested alternative");
    return *value;
  }

  /**
   * @brief Rust `match`-expression equivalent: dispatches to whichever
   * callable in @p fs accepts the currently active alternative, via
   * `std::visit` over an ad-hoc `overloaded{fs...}` overload set. As with
   * `std::visit`, the call is ill-formed unless the overload set is
   * callable with every alternative in `Ts...` -- there is no silent
   * "no match" case, matching Rust's requirement that a `match` be
   * exhaustive.
   */
  template <typename... Fs> decltype(auto) match(Fs &&...fs) & {
    return std::visit(overloaded{std::forward<Fs>(fs)...}, static_cast<std::variant<Ts...> &>(*this));
  }

  /** @copydoc match(Fs &&...) & */
  template <typename... Fs> decltype(auto) match(Fs &&...fs) const & {
    return std::visit(overloaded{std::forward<Fs>(fs)...}, static_cast<const std::variant<Ts...> &>(*this));
  }

  /** @copydoc match(Fs &&...) & */
  template <typename... Fs> decltype(auto) match(Fs &&...fs) && {
    return std::visit(overloaded{std::forward<Fs>(fs)...}, static_cast<std::variant<Ts...> &&>(*this));
  }
};

/**
 * @brief `variant<Ts...>` is trivially relocatable iff every `Ts` is --
 * it adds no data members over `std::variant<Ts...>`, which itself stores
 * whichever alternative is active inline plus an index, never a pointer
 * back into itself.
 */
template <typename... Ts>
struct is_trivially_relocatable<variant<Ts...>> : std::bool_constant<(is_trivially_relocatable_v<Ts> && ...)> {};

} // namespace reloco
