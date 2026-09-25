// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// FreeBSD RELOCO_FUTEX_BACKEND_FREEBSD implementation of
// reloco/futex.hpp's futex_wait/futex_wake_one/futex_wake_all, via
// _umtx_op(2) (UMTX_OP_WAIT_UINT/UMTX_OP_WAKE). Included only from
// futex.hpp itself when RELOCO_FUTEX_BACKEND_FREEBSD is defined -- never
// include this file directly.
//
// NOTE: this backend is not exercised by this repository's test suite
// (see futex.hpp's file-level documentation) -- review carefully before
// relying on it in production.

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

RELOCO_API void futex_wake_one(futex_word &word) noexcept {
  static_cast<void>(_umtx_op(detail::raw_addr(word), UMTX_OP_WAKE, 1u, nullptr, static_cast<void *>(nullptr)));
}

RELOCO_API void futex_wake_all(futex_word &word) noexcept {
  static_cast<void>(_umtx_op(detail::raw_addr(word), UMTX_OP_WAKE, static_cast<u_long>(INT_MAX), nullptr,
                             static_cast<void *>(nullptr)));
}

} // namespace reloco
