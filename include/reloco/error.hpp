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
 *
 * There is deliberately only one error type in the whole library (see the
 * file-level docs above) -- every member below is generic enough to apply
 * across containers, allocators, smart pointers, and synchronization
 * primitives, rather than being scoped to one feature. See
 * `docs/reference.md`'s "`error`" section for a fuller description of each
 * member and which reloco operations return it today.
 */
enum class error : int {
  allocation_failed = 1,  // The allocator failed to provide/grow/shrink a memory block.
  in_place_growth_failed, // `allocator_ref::expand_in_place` could not grow a block without moving it.
  unsupported_operation,  // The operation is not supported by this concrete type/backend (e.g. a
                          // `function_ref`/`function` invoked with the wrong signature, an allocator tag
                          // that doesn't implement an optional operation).
  out_of_range,           // A value fell outside the range required by the operation (e.g. a parsed
                          // number, a duration, a size argument) -- distinct from `out_of_bounds`, which
                          // is specifically about container indices/iterators.
  invalid_argument,       // An argument failed a precondition check unrelated to range/bounds (e.g. a
                          // malformed string, a null callback, mismatched key/value in `flat_set`).
  already_exists,         // Insertion failed because an equivalent key/element is already present
                          // (e.g. `flat_set::try_insert`, `flat_map::try_insert`).
  empty_pointer,          // A smart pointer (`unique_ptr`, `shared_ptr`, `value_ptr`, ...) was empty
                          // when a non-empty one was required.
  pointer_expired,        // A `weak_ptr::lock()` failed because the last owning `shared_ptr` has
                          // already released the object.
  no_owner,               // An operation requiring an owning handle was attempted on a non-owning one.
  out_of_bounds,          // A container index/iterator fell outside `[0, size())` (see `out_of_range`
                          // for non-index range checks).
  deadlock,               // A locking operation detected it would deadlock (e.g. recursive
                          // non-recursive lock acquisition) and failed instead of blocking forever.
  invalid_owner,          // An operation was attempted by a thread/handle that does not own the
                          // resource it is trying to operate on (e.g. unlocking a mutex you don't hold).
  still_locked,           // An operation requiring an unlocked resource found it still locked (e.g.
                          // destroying/reclaiming a lock that is still held).
  not_locked,             // An operation requiring a locked resource (e.g. unlocking, or asserting
                          // exclusive access) found it was not locked.
  timed_out,              // A bounded-wait operation (e.g. a timed lock acquisition) did not complete
                          // within its deadline.
  try_again,              // The operation could not complete right now for a transient reason and may
                          // succeed if retried (e.g. contended non-blocking lock acquisition).
  not_initialized,        // The object/subsystem was used before the initialization step its protocol
                          // requires (e.g. a two-phase `try_construct` shell that was never followed
                          // through, see `concepts.hpp`).
  container_empty,        // An operation requiring at least one element (`front`/`back`/`pop_back`) was
                          // called on an empty container.
  not_found,              // A lookup (`flat_set::try_find`, `flat_map::try_find`, ...) found no matching
                          // key/element.
  integer_overflow,       // An arithmetic computation (e.g. a size/capacity calculation) would overflow
                          // its integer type.
  division_by_zero,       // A division or remainder operation was attempted with a zero divisor (e.g.
                          // `checked_div`/`checked_rem`, see `int_ops.hpp`).
  capacity_exceeded,      // A fixed-capacity container (`inline_vector`, `inline_flat_set`, `inline_flat_map`,
                          // `inplace_function`, ...) has no room left for another element and, unlike a
                          // heap-backed container, cannot grow.
  invalid_state,          // The operation is not valid given the object's current state (e.g. a moved-from
                          // object, or an operation attempted in the wrong phase of a multi-step protocol).
  permission_denied,      // An OS- or allocator-level access-control check failed (e.g. `mmap` with
                          // insufficient permissions).
  interrupted,            // The underlying operation was interrupted (e.g. by a signal) and may be safely
                          // retried.
  resource_exhausted,     // A system-imposed resource limit unrelated to heap memory (e.g. a
                          // handle/descriptor count, a thread count) was reached.
  busy,                   // The resource is currently in use by someone else and the operation could not
                          // proceed non-blockingly; distinct from `still_locked`/`not_locked` (specifically
                          // about lock state) and `try_again` (any transient retryable failure).
  io_error,               // A lower-level I/O operation (e.g. one performed by an allocator backend) failed
                          // for a reason not otherwise covered by a more specific member.
  operation_canceled      // The operation was explicitly canceled before it could complete.
};

/**
 * @brief Convenience alias for `reloco::expected<T, reloco::error>`, the
 * one error type every fallible reloco operation returns.
 */
template <typename T> using result = expected<T, error>;

} // namespace reloco
