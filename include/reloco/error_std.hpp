// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file error_std.hpp
 * @brief Binds `reloco::error` to the standard `<system_error>` framework.
 *
 * Kept in a separate header from `error.hpp` on purpose, matching
 * `container_ref_std.hpp`'s precedent (see its own file docs): `error.hpp`
 * itself has zero standard-library dependencies beyond `expected.hpp`, and
 * most reloco code never needs `<system_error>`. Only a user who explicitly
 * `#include`s this header opts into the (small, allocation-free until
 * `message()`/`what()` is actually called) interop surface below.
 *
 * Including this header makes `reloco::error` a first-class
 * `std::error_code`-compatible enum:
 *
 * - `reloco::make_error_code(error)` (found via ADL) and the
 *   `std::is_error_code_enum<reloco::error>` specialization below let
 *   `reloco::error` implicitly convert to `std::error_code` -- e.g.
 *   `std::error_code ec = reloco::error::not_found;` or
 *   `throw std::system_error(reloco::error::allocation_failed);` -- and be
 *   compared directly against other `std::error_code`/`std::error_condition`
 *   values with `==`/`!=`.
 * - `reloco::error_category()` returns the singleton `std::error_category`
 *   whose `name()` is `"reloco"` and whose `message(int)` returns the same
 *   one-line description documented for each member in
 *   `docs/reference.md`'s "`error`" section.
 * - `reloco::make_error_condition(error)` and the
 *   `std::is_error_condition_enum<reloco::error>` specialization below let
 *   `reloco::error` also convert to `std::error_condition` directly (e.g.
 *   `if (ec == reloco::error::not_found)` where `ec` is any
 *   `std::error_code`).
 * - `error_category_impl::default_error_condition(int)` maps several
 *   members onto the closest matching portable `std::errc` condition
 *   (e.g. `error::timed_out` -> `std::errc::timed_out`,
 *   `error::busy` -> `std::errc::device_or_resource_busy`), which is
 *   what plugs `reloco::error` into the standard POSIX/`errno` bridge:
 *   `std::error_code(reloco::error::timed_out) == std::errc::timed_out`
 *   is `true`, and the same holds against any other category whose
 *   codes report that same generic condition (e.g. codes built from
 *   `errno` via `std::generic_category()`). Members with no
 *   sufficiently precise POSIX equivalent keep the default identity
 *   condition (equal only to themselves).
 *
 * This header does not change how any reloco function reports failure --
 * every `try_*` operation still returns `reloco::result<T>` (`expected<T,
 * error>`); this is purely an opt-in bridge for code that also needs to
 * hand a `reloco::error` to (or compare it against) APIs built around
 * `std::error_code`.
 *
 * ### A note on cross-translation-unit/DSO category identity
 *
 * `error_code`/`error_condition` comparisons that go through a category's
 * virtual `equivalent()` (i.e. any comparison between an `error_code` and
 * an `error_condition`) are, in `error_category_impl` below, first
 * resolved the same way the standard's own default implementation would
 * (category-object identity, or -- for the `equivalent(int, const
 * error_condition&)` overload -- `default_error_condition()`, which is
 * also what plugs in the POSIX bridge documented above). Only if that
 * fails does `equivalent()` additionally accept a category object that
 * merely reports the same `name()` string (`"reloco"`), which needs no
 * RTTI/`typeid` at all and keeps such comparisons correct even if
 * `reloco::error_category()`'s function-local `static` singleton were
 * ever duplicated across a shared-library boundary (a known
 * header-only-library pitfall on some platforms/visibility settings).
 * Plain `error_code == error_code` equality is
 * unaffected by any of this: the standard defines it purely via
 * category-object identity (never RTTI/`typeid`), which no category
 * override can change.
 */

#include "error.hpp"

#include <string>
#include <string_view>
#include <system_error>

namespace reloco {

namespace detail {

/**
 * @brief The `std::error_category` implementation backing
 * `reloco::error_category()`.
 *
 * Not meant to be named directly -- go through `reloco::error_category()`,
 * which returns a reference to the single process-wide instance.
 */
class RELOCO_EXPORT error_category_impl final : public std::error_category {
public:
  [[nodiscard]] const char *name() const noexcept override { return "reloco"; }

  [[nodiscard]] RELOCO_API std::string message(int ev) const override;

  /**
   * @brief Maps @p ev onto the closest matching portable POSIX condition.
   *
   * Lets `reloco::error` participate in `<system_error>`'s POSIX bridge:
   * e.g. `std::error_code(reloco::error::timed_out) ==
   * std::errc::timed_out` is `true`, even though the two sides use
   * different categories, because `std::errc::timed_out` (via
   * `std::generic_category()`) is what this function returns for
   * `error::timed_out`. Members with no sufficiently precise POSIX
   * equivalent (e.g. `not_found`, `pointer_expired`) fall back to the
   * base class behavior, which is an identity condition using this
   * category (`error_condition(ev, *this)`).
   */
  [[nodiscard]] RELOCO_API std::error_condition default_error_condition(int ev) const noexcept override;

private:
  /**
   * @brief Whether `other`'s `name()` string content equals `"reloco"`.
   *
   * Used by `equivalent()` below in place of category-object identity, so
   * that comparisons still succeed even if the category singleton were
   * ever duplicated across a translation-unit/shared-library boundary
   * (see the file-level docs). Comparing as `std::string_view` (rather
   * than e.g. `std::strcmp`) needs no length assumption about the
   * incoming C string and matches the rest of reloco's own
   * string-handling style.
   */
  [[nodiscard]] RELOCO_API bool same_name(const std::error_category &other) const noexcept;

public:
  /**
   * @brief Matches `code` against `condition` for an `error_code ==
   * error_condition` comparison originating on this category's side.
   *
   * First mirrors the standard's own default (`*this == code.category()
   * && code.value() == condition`), additionally tolerating
   * `code.category()` being a *different* object that merely reports the
   * same `name()` (`"reloco"`, see the file-level docs). Otherwise,
   * bridges `code` (from *any* category, e.g. an `errno`-based
   * `std::generic_category()` code) against our own POSIX mapping by
   * re-wrapping `code` as a condition and comparing it to
   * `default_error_condition(condition)` -- this is what makes an
   * `errno`-based `error_code` compare equal to a `reloco::error`-based
   * `error_condition` (e.g. `std::error_condition(reloco::error::busy)`),
   * not just the reverse direction handled below.
   */
  [[nodiscard]] RELOCO_API bool equivalent(const std::error_code &code, int condition) const noexcept override;

  /**
   * @brief Matches `code` against `condition` for an `error_code ==
   * error_condition` comparison originating on the condition's side.
   *
   * Mirrors the standard's own default (`default_error_condition(code) ==
   * condition`), so this is what makes `error::busy ==
   * std::errc::device_or_resource_busy`-style POSIX bridging work.
   * Additionally tolerates `condition.category()` being a *different*
   * object that merely reports the same `name()` (`"reloco"`) -- see the
   * file-level docs.
   */
  [[nodiscard]] RELOCO_API bool equivalent(int code, const std::error_condition &condition) const noexcept override;
};

} // namespace detail

/**
 * @brief Returns the singleton `std::error_category` for `reloco::error`.
 *
 * `name()` is `"reloco"`; `message(int)` returns the one-line description
 * documented for each `reloco::error` member in `docs/reference.md`.
 */
[[nodiscard]] RELOCO_EXPORT RELOCO_API const std::error_category &error_category() noexcept;

/**
 * @brief Wraps @p e in a `std::error_code` using `reloco::error_category()`.
 *
 * Found via ADL, this is what makes `reloco::error` satisfy
 * `std::is_error_code_enum` (see the specialization below) and therefore
 * implicitly convertible to `std::error_code` wherever the standard library
 * expects one.
 */
[[nodiscard]] RELOCO_EXPORT RELOCO_API std::error_code make_error_code(error e) noexcept;

/**
 * @brief Wraps @p e in a `std::error_condition` using
 * `reloco::error_category()`.
 *
 * Found via ADL, this is what makes `reloco::error` satisfy
 * `std::is_error_condition_enum` (see the specialization below) and
 * therefore implicitly convertible to `std::error_condition`, and directly
 * comparable against any `std::error_code` via `equivalent()` (see the
 * file-level docs).
 */
[[nodiscard]] RELOCO_EXPORT RELOCO_API std::error_condition make_error_condition(error e) noexcept;

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "error_std.ipp"
#endif

} // namespace reloco

namespace std {

template <> struct is_error_code_enum<reloco::error> : true_type {};

template <> struct is_error_condition_enum<reloco::error> : true_type {};

} // namespace std
