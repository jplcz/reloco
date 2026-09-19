// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file error.hpp
 * @brief `reloco::error`, reloco's single error enum, and `reloco::result<T>`,
 * its matching `expected<T, error>` alias.
 *
 * Ported from `reloco_legacy/include/reloco/core.hpp`, where `error` was
 * the *only* error type in the library, used by every fallible operation.
 * reloco keeps that requirement: every fallible operation in the library --
 * `allocator_ref`, `span`/`array`/`string_view`'s `try_*` accessors, the
 * fallible-construction protocol in `concepts.hpp`/`construction_helpers.hpp`,
 * and any user-defined `try_create`/`try_allocate`/`try_construct`/
 * `try_clone`/`try_clone_at` -- returns `reloco::result<T>` (or
 * `result<void>`). No type defines its own scoped, per-feature error enum;
 * `concepts.hpp`'s detection traits only recognize a `try_*` operation that
 * itself returns `reloco::result<...>`, so this is enforced, not just a
 * convention.
 */

#include "expected.hpp"

namespace reloco {

/**
 * @brief The single error enum used by every fallible operation in reloco.
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
 * one error type every fallible reloco operation returns.
 */
template <typename T> using result = expected<T, error>;

} // namespace reloco
