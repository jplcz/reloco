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

  [[nodiscard]] std::string message(int ev) const override {
    switch (static_cast<error>(ev)) {
    case error::allocation_failed:
      return "the allocator failed to provide/grow/shrink a memory block";
    case error::in_place_growth_failed:
      return "expand_in_place could not grow a block without moving it";
    case error::unsupported_operation:
      return "the operation is not supported by this concrete type/backend";
    case error::out_of_range:
      return "a value fell outside the range required by the operation";
    case error::invalid_argument:
      return "an argument failed a precondition check";
    case error::already_exists:
      return "an equivalent key/element is already present";
    case error::empty_pointer:
      return "a smart pointer was empty when a non-empty one was required";
    case error::pointer_expired:
      return "the last owning shared_ptr has already released the object";
    case error::no_owner:
      return "an operation requiring an owning handle was attempted on a non-owning one";
    case error::out_of_bounds:
      return "a container index/iterator fell outside its valid range";
    case error::deadlock:
      return "a locking operation detected it would deadlock";
    case error::invalid_owner:
      return "the caller does not own the resource it is trying to operate on";
    case error::still_locked:
      return "an operation requiring an unlocked resource found it still locked";
    case error::not_locked:
      return "an operation requiring a locked resource found it was not locked";
    case error::timed_out:
      return "a bounded-wait operation did not complete within its deadline";
    case error::try_again:
      return "the operation could not complete right now but may succeed if retried";
    case error::not_initialized:
      return "the object/subsystem was used before its required initialization step";
    case error::container_empty:
      return "an operation requiring at least one element was called on an empty container";
    case error::not_found:
      return "a lookup found no matching key/element";
    case error::integer_overflow:
      return "an arithmetic computation would overflow its integer type";
    case error::division_by_zero:
      return "a division or remainder operation was attempted with a zero divisor";
    case error::capacity_exceeded:
      return "a fixed-capacity container has no room left for another element";
    case error::invalid_state:
      return "the operation is not valid given the object's current state";
    case error::permission_denied:
      return "an OS- or allocator-level access-control check failed";
    case error::interrupted:
      return "the underlying operation was interrupted and may be safely retried";
    case error::resource_exhausted:
      return "a system-imposed resource limit unrelated to heap memory was reached";
    case error::busy:
      return "the resource is currently in use by someone else";
    case error::io_error:
      return "a lower-level I/O operation failed";
    case error::operation_canceled:
      return "the operation was explicitly canceled before it could complete";
    }
    return "unknown reloco::error";
  }

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
  [[nodiscard]] std::error_condition default_error_condition(int ev) const noexcept override {
    switch (static_cast<error>(ev)) {
    case error::allocation_failed:
      return std::make_error_condition(std::errc::not_enough_memory);
    case error::invalid_argument:
      return std::make_error_condition(std::errc::invalid_argument);
    case error::out_of_range:
    case error::out_of_bounds:
      return std::make_error_condition(std::errc::result_out_of_range);
    case error::already_exists:
      return std::make_error_condition(std::errc::file_exists);
    case error::deadlock:
      return std::make_error_condition(std::errc::resource_deadlock_would_occur);
    case error::timed_out:
      return std::make_error_condition(std::errc::timed_out);
    case error::try_again:
      return std::make_error_condition(std::errc::resource_unavailable_try_again);
    case error::unsupported_operation:
      return std::make_error_condition(std::errc::operation_not_supported);
    case error::capacity_exceeded:
      return std::make_error_condition(std::errc::no_buffer_space);
    case error::permission_denied:
      return std::make_error_condition(std::errc::permission_denied);
    case error::interrupted:
      return std::make_error_condition(std::errc::interrupted);
    case error::busy:
      return std::make_error_condition(std::errc::device_or_resource_busy);
    case error::io_error:
      return std::make_error_condition(std::errc::io_error);
    case error::operation_canceled:
      return std::make_error_condition(std::errc::operation_canceled);
    case error::integer_overflow:
      return std::make_error_condition(std::errc::value_too_large);
    case error::division_by_zero:
      return std::make_error_condition(std::errc::argument_out_of_domain);
    case error::in_place_growth_failed:
    case error::empty_pointer:
    case error::pointer_expired:
    case error::no_owner:
    case error::invalid_owner:
    case error::still_locked:
    case error::not_locked:
    case error::not_initialized:
    case error::container_empty:
    case error::not_found:
    case error::invalid_state:
    case error::resource_exhausted:
      break;
    }
    return std::error_category::default_error_condition(ev);
  }

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
  [[nodiscard]] bool same_name(const std::error_category &other) const noexcept {
    return std::string_view(other.name()) == std::string_view(name());
  }

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
  [[nodiscard]] bool equivalent(const std::error_code &code, int condition) const noexcept override {
    if (*this == code.category())
      return code.value() == condition;
    if (same_name(code.category()))
      return code.value() == condition;
    return default_error_condition(condition) == std::error_condition(code.value(), code.category());
  }

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
  [[nodiscard]] bool equivalent(int code, const std::error_condition &condition) const noexcept override {
    if (default_error_condition(code) == condition)
      return true;
    if (same_name(condition.category()))
      return code == condition.value();
    return false;
  }
};

} // namespace detail

/**
 * @brief Returns the singleton `std::error_category` for `reloco::error`.
 *
 * `name()` is `"reloco"`; `message(int)` returns the one-line description
 * documented for each `reloco::error` member in `docs/reference.md`.
 */
[[nodiscard]] RELOCO_EXPORT inline const std::error_category &error_category() noexcept {
  static const detail::error_category_impl instance;
  return instance;
}

/**
 * @brief Wraps @p e in a `std::error_code` using `reloco::error_category()`.
 *
 * Found via ADL, this is what makes `reloco::error` satisfy
 * `std::is_error_code_enum` (see the specialization below) and therefore
 * implicitly convertible to `std::error_code` wherever the standard library
 * expects one.
 */
[[nodiscard]] RELOCO_EXPORT inline std::error_code make_error_code(error e) noexcept {
  return {static_cast<int>(e), error_category()};
}

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
[[nodiscard]] RELOCO_EXPORT inline std::error_condition make_error_condition(error e) noexcept {
  return {static_cast<int>(e), error_category()};
}

} // namespace reloco

namespace std {

template <> struct is_error_code_enum<reloco::error> : true_type {};

template <> struct is_error_condition_enum<reloco::error> : true_type {};

} // namespace std
