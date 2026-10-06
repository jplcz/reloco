// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file error_errno.hpp
 * @brief Opt-in, allocation-free `reloco::error` -> POSIX `errno` bridge.
 *
 * Kept in a separate header from `error.hpp` on purpose, matching
 * `error_std.hpp`'s precedent (see its own file docs): `error.hpp` itself
 * depends on nothing beyond `expected.hpp`, and most reloco code never
 * needs an `errno` mapping at all. Only a user who explicitly `#include`s
 * this header opts into the (single, `constexpr`, header-only) conversion
 * below.
 *
 * Unlike `error_std.hpp`, this header pulls in only `<cerrno>` -- no
 * `<system_error>`, no `<string>`, no heap allocation, and no out-of-line
 * `.ipp` body -- so it is just as usable from a `RELOCO_KERNEL`/
 * freestanding build (e.g. a FreeBSD-style kernel module translating a
 * `reloco::result<T>` failure into the `int` a syscall handler returns)
 * as from hosted userspace code that would rather not drag in
 * `<system_error>` just to get a plain `errno` value.
 *
 * `reloco::to_errno(error)` maps every `reloco::error` member onto the
 * closest matching POSIX `errno` macro (the same mapping `error_std.hpp`'s
 * `error_category_impl::default_error_condition()` uses, by way of
 * `std::errc`, for the subset of members it recognizes -- see that file's
 * docs). Every member has *some* answer here, including the ones
 * `error_std.hpp` leaves as an unmapped identity condition, because
 * `errno` (unlike `std::error_condition`) has no "no equivalent, just
 * compare by category+value" fallback to defer to; the mapping for those
 * members is necessarily an approximation, called out per-member below.
 *
 * This header does not change how any reloco function reports failure --
 * every `try_*` operation still returns `reloco::result<T>` (`expected<T,
 * error>`); this is purely an opt-in, one-way (`error` -> `errno`)
 * convenience for code that must eventually hand a plain `int` errno
 * value to something else (a syscall return value, `strerror()`, a C API).
 */

#include "error.hpp"

#include <cerrno>

namespace reloco {

/**
 * @brief Maps @p e onto the closest matching POSIX `errno` macro.
 *
 * `constexpr` and allocation-free: no `<system_error>`, no heap, no
 * out-of-line definition, safe to call from a `RELOCO_KERNEL`/freestanding
 * build. See the file-level docs for the rationale behind each mapping,
 * especially the members with no precise `errno` equivalent.
 */
[[nodiscard]] constexpr int to_errno(error e) noexcept {
  switch (e) {
  case error::allocation_failed:
    return ENOMEM;
  case error::in_place_growth_failed:
    // No dedicated errno for "couldn't grow without moving"; closest is
    // the same resource-exhaustion bucket as allocation_failed.
    return ENOMEM;
  case error::unsupported_operation:
    return ENOTSUP;
  case error::out_of_range:
    return ERANGE;
  case error::invalid_argument:
    return EINVAL;
  case error::already_exists:
    return EEXIST;
  case error::empty_pointer:
    // No pointer-specific errno; EINVAL matches "argument failed a
    // precondition check", which is exactly what an empty required
    // pointer is.
    return EINVAL;
  case error::pointer_expired:
    // ESTALE ("stale file handle") is POSIX's own precedent for "the
    // handle you're holding refers to something that no longer exists",
    // which is precisely a weak_ptr::lock() failure.
    return ESTALE;
  case error::no_owner:
    return EPERM;
  case error::out_of_bounds:
    // Same bucket as out_of_range: error_std.hpp's default_error_condition
    // maps both onto std::errc::result_out_of_range too.
    return ERANGE;
  case error::deadlock:
    return EDEADLK;
  case error::invalid_owner:
    // "Attempted by a thread/handle that doesn't own the resource" is an
    // operation-not-permitted condition.
    return EPERM;
  case error::still_locked:
    return EBUSY;
  case error::not_locked:
    // No dedicated errno for "resource wasn't locked"; EINVAL matches the
    // operation being invalid given the object's current state.
    return EINVAL;
  case error::timed_out:
    return ETIMEDOUT;
  case error::try_again:
    return EAGAIN;
  case error::not_initialized:
    // No dedicated errno for "used before initialization"; EINVAL matches
    // the operation being invalid given the object's current state.
    return EINVAL;
  case error::container_empty:
    // No dedicated errno for "container has no elements"; ENOENT matches
    // "nothing there to return" more closely than any other POSIX code.
    return ENOENT;
  case error::not_found:
    return ENOENT;
  case error::integer_overflow:
    return EOVERFLOW;
  case error::division_by_zero:
    // Matches error_std.hpp's std::errc::argument_out_of_domain mapping.
    return EDOM;
  case error::capacity_exceeded:
    // Matches error_std.hpp's std::errc::no_buffer_space mapping.
    return ENOBUFS;
  case error::invalid_state:
    return EINVAL;
  case error::permission_denied:
    return EACCES;
  case error::interrupted:
    return EINTR;
  case error::resource_exhausted:
    // A system-imposed limit unrelated to heap memory (handle/thread
    // counts, ...); EAGAIN is POSIX's own precedent for hitting a
    // process/thread resource limit (e.g. pthread_create, fork).
    return EAGAIN;
  case error::busy:
    return EBUSY;
  case error::io_error:
    return EIO;
  case error::operation_canceled:
    return ECANCELED;
  case error::security_violation:
    // Same bucket as permission_denied: error_std.hpp's
    // default_error_condition maps both onto std::errc::permission_denied.
    return EACCES;
  case error::page_fault:
    return EFAULT;
  }
  return EINVAL;
}

} // namespace reloco
