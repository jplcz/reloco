// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file error.hpp
 * @brief `reloco::error`, a general-purpose default error enum, and
 * `reloco::result<T>`, its matching `expected<T, error>` alias.
 *
 * Ported from `reloco_legacy/include/reloco/core.hpp`, where `error` was
 * the *only* error type in the library, used by every fallible operation.
 * That is no longer reloco's convention: most library-owned fallible types
 * define their own scoped, per-feature error enum instead (`span_error`,
 * `string_view_error`, ...), so a caller only ever has to handle the
 * specific failure modes that operation can actually produce (see
 * `docs/hardened-containers.md`). `allocator_ref` (`allocator.hpp`) is a
 * deliberate exception: its two failure modes (`allocation_failed`,
 * `unsupported_operation`) are exactly a subset of `reloco::error`'s, so it
 * returns `reloco::result<T>` directly instead of defining its own
 * single-purpose enum.
 *
 * `reloco::error` still earns a place alongside those: it is a ready-made,
 * reasonably complete default for application code (and reloco utilities
 * like `construction_helpers.hpp`) that needs *some* concrete, meaningful
 * error type — a user-defined `try_create`, a generic helper's otherwise
 * unreachable fallback tier, a quick prototype — without first designing a
 * dedicated enum. Prefer a scoped, per-feature enum for a new library-owned
 * type; reach for `reloco::error` for everything else.
 */

#include "expected.hpp"

namespace reloco {

/**
 * @brief General-purpose default error enum for fallible reloco operations
 * that do not define their own scoped error enum.
 */
enum class error : int {
  allocation_failed = 1,
  in_place_growth_failed,
  unsupported_operation,
  out_of_range,
  invalid_argument,
  already_exists,
  empty_pointer,
  pointer_expired,
  no_owner,
  out_of_bounds,
  deadlock,
  invalid_owner,
  still_locked,
  not_locked,
  timed_out,
  try_again,
  not_initialized,
  container_empty,
  not_found,
  integer_overflow
};

/**
 * @brief Convenience alias for `reloco::expected<T, reloco::error>`, the
 * general-purpose default error enum above.
 */
template <typename T> using result = expected<T, error>;

} // namespace reloco
