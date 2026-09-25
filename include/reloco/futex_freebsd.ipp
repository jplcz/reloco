// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// FreeBSD RELOCO_FUTEX_BACKEND_FREEBSD implementation of
// reloco/futex.hpp's futex_wait/futex_wait_timeout/futex_wake_one/
// futex_wake_all, via _umtx_op(2) (UMTX_OP_WAIT_UINT/UMTX_OP_WAKE),
// futex_wait_timeout additionally passing a struct _umtx_time relative
// timeout. Included only from futex.hpp itself when
// RELOCO_FUTEX_BACKEND_FREEBSD is defined -- never include this file
// directly.
//
// NOTE: this backend is not exercised by this repository's test suite
// (see futex.hpp's file-level documentation) -- review carefully before
// relying on it in production.

#include <cerrno>
#include <climits>
#include <cstdint>
#include <sys/types.h>
#include <sys/umtx.h>

namespace reloco {

namespace detail {

// _umtx_op(2) takes a plain `void *`, never a
// `std::atomic<std::uint32_t> *` -- std::atomic<std::uint32_t> is
// required (is_always_lock_free on every target reloco cares about) to
// share std::uint32_t's object representation and alignment, so
// reinterpret_cast-ing its address is well-defined here specifically.
inline void *raw_addr(const futex_word &word) noexcept { return const_cast<void *>(static_cast<const void *>(&word)); }

} // namespace detail

RELOCO_API void futex_wait(const futex_word &word, std::uint32_t expected) noexcept {
  // Return value deliberately ignored: a mismatched expected value, an
  // interrupted wait, and a genuine wake are all handled identically by
  // the caller re-checking its own condition after futex_wait() returns
  // (see barrier.hpp) -- spurious wakeups are always tolerated, exactly
  // like condition_variable::wait.
  static_cast<void>(_umtx_op(detail::raw_addr(word), UMTX_OP_WAIT_UINT, static_cast<u_long>(expected), nullptr,
                             static_cast<void *>(nullptr)));
}

RELOCO_API bool futex_wait_timeout(const futex_word &word, std::uint32_t expected, duration timeout) noexcept {
  // UMTX_OP_WAIT_UINT accepts an optional timeout via a struct
  // _umtx_time*, passed through uaddr2 with uaddr1 holding its size --
  // _flags left at 0 (default) selects a *relative* timeout, matching
  // FUTEX_WAIT's own relative-timeout convention on Linux.
  struct _umtx_time utime {};
  utime._timeout = duration_cast<struct timespec>(timeout);
  utime._flags = 0;
  utime._clockid = CLOCK_MONOTONIC;
  errno = 0;
  int ret = _umtx_op(detail::raw_addr(word), UMTX_OP_WAIT_UINT, static_cast<u_long>(expected),
                      reinterpret_cast<void *>(sizeof(utime)), static_cast<void *>(&utime));
  // A mismatched expected value, an interrupted wait, and a genuine wake
  // all mean "returned for a reason other than the timeout elapsing";
  // only ETIMEDOUT means the timeout was definitely observed to elapse.
  return !(ret == -1 && errno == ETIMEDOUT);
}

RELOCO_API void futex_wake_one(futex_word &word) noexcept {
  static_cast<void>(_umtx_op(detail::raw_addr(word), UMTX_OP_WAKE, 1u, nullptr, static_cast<void *>(nullptr)));
}

RELOCO_API void futex_wake_all(futex_word &word) noexcept {
  static_cast<void>(_umtx_op(detail::raw_addr(word), UMTX_OP_WAKE, static_cast<u_long>(INT_MAX), nullptr,
                             static_cast<void *>(nullptr)));
}

} // namespace reloco
