// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Linux RELOCO_FUTEX_BACKEND_LINUX implementation of reloco/futex.hpp's
// futex_wait/futex_wake_one/futex_wake_all, via a direct futex(2) syscall
// (no glibc futex wrapper exists or is required). Included only from
// futex.hpp itself when RELOCO_FUTEX_BACKEND_LINUX is defined -- never
// include this file directly.
//
// NOTE: this backend is not exercised by this repository's test suite
// (see futex.hpp's file-level documentation) -- review carefully before
// relying on it in production.

#include <climits>
#include <cstdint>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace reloco {

namespace detail {

// futex(2)'s prototype takes a plain `int *`/`std::uint32_t *`, never a
// `std::atomic<std::uint32_t> *` -- std::atomic<std::uint32_t> is
// required (is_always_lock_free on every target reloco cares about) to
// share std::uint32_t's object representation and alignment, so
// reinterpret_cast-ing its address is well-defined here specifically.
inline std::uint32_t *raw_addr(const futex_word &word) noexcept {
  return const_cast<std::uint32_t *>(reinterpret_cast<const std::uint32_t *>(&word));
}

inline long futex_syscall(std::uint32_t *addr, int futex_op, std::uint32_t val) noexcept {
  return syscall(SYS_futex, addr, futex_op, val, nullptr, nullptr, 0);
}

} // namespace detail

RELOCO_API void futex_wait(const futex_word &word, std::uint32_t expected) noexcept {
  // Return value/errno deliberately ignored: EAGAIN (word had already
  // changed by the time the kernel checked it), EINTR, and a genuine wake
  // are all handled identically by the caller re-checking its own
  // condition after futex_wait() returns (see barrier.hpp) -- spurious
  // wakeups are always tolerated, exactly like condition_variable::wait.
  static_cast<void>(detail::futex_syscall(detail::raw_addr(word), FUTEX_WAIT_PRIVATE, expected));
}

RELOCO_API void futex_wake_one(futex_word &word) noexcept {
  static_cast<void>(detail::futex_syscall(detail::raw_addr(word), FUTEX_WAKE_PRIVATE, 1));
}

RELOCO_API void futex_wake_all(futex_word &word) noexcept {
  static_cast<void>(
      detail::futex_syscall(detail::raw_addr(word), FUTEX_WAKE_PRIVATE, static_cast<std::uint32_t>(INT_MAX)));
}

} // namespace reloco
