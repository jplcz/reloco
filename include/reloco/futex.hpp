// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file futex.hpp
 * @brief Low-level "futex"-like word-wait/wake primitive:
 * `reloco::futex_word` (a plain `std::atomic<std::uint32_t>`) plus
 * `futex_wait(word, expected)`/`futex_wait_timeout(word, expected,
 * timeout)`/`futex_wake_one(word)`/`futex_wake_all(word)` -- the building
 * block a handful of higher-level reloco primitives can use instead of a
 * full `mutex` + `condition_variable` pair (see `mutex.hpp`) when all
 * they actually need is "block until this word changes"/"wake whoever is
 * blocked on this word": `barrier.hpp`, `scope.hpp`, `once_lock.hpp`,
 * `fallible_singleton.hpp`'s `atomic_fallible_singleton<T>`, and
 * `tls_provider.hpp`'s `RELOCO_TLS_MODEL_PTHREAD` backend all use it
 * today.
 *
 * Backend selection, mirroring `mutex.hpp`'s `RELOCO_MUTEX_BACKEND_*`
 * customization point -- select **at most one**: `RELOCO_FUTEX_BACKEND_
 * LINUX` (raw `futex(2)` syscall, `futex_linux.ipp`),
 * `RELOCO_FUTEX_BACKEND_FREEBSD` (`_umtx_op(2)`, `futex_freebsd.ipp`),
 * `RELOCO_FUTEX_BACKEND_CUSTOM` (application/kernel supplies its own
 * `futex_word`/`futex_wait`/`futex_wait_timeout`/`futex_wake_one`/
 * `futex_wake_all`, e.g. for a FreeBSD **kernel** `msleep(9)`/`wakeup(9)`
 * backend), or none defined (default: a portable "parking lot" fallback
 * in `futex_std.ipp`, built entirely on `mutex.hpp`'s `mutex`/
 * `condition_variable`, always available on any hosted target). See
 * [`docs/futex.md`](../../docs/futex.md) for the full backend writeup,
 * the `RELOCO_FUTEX_BACKEND_CUSTOM` FreeBSD-kernel example, and the
 * parking-lot collision-safety argument.
 *
 * Every `futex_wait`/`futex_wait_timeout`/`futex_wake_one`/
 * `futex_wake_all` declaration below is `RELOCO_API`-decorated (see
 * `detail/compat.hpp`) so the built-in fallback backend participates in
 * reloco's header-only/`RELOCO_SHARED` split exactly like `mutex.hpp`'s
 * own classes: out-of-line definitions live in `futex_std.ipp`/
 * `futex_linux.ipp`/`futex_freebsd.ipp`, included from here only when
 * `RELOCO_SHARED_PROVIDE_DEFINITIONS` is `1`.
 *
 * `futex_wait` tolerates spurious wakeups (it may return even though
 * `word` still equals `expected`), exactly like
 * `condition_variable::wait` -- callers must always re-check their own
 * condition in a loop, never assume a single `futex_wait` call implies
 * the word actually changed (see `barrier.hpp` for the expected calling
 * pattern).
 *
 * `futex_wait_timeout(word, expected, timeout)` is `futex_wait`'s bounded
 * counterpart, matching `condition_variable::wait_for`'s/`park_timeout`'s
 * own "may return early" contract: it returns `true` if it returned for
 * any reason *other* than the timeout definitely elapsing (a genuine
 * wake, or a spurious wakeup -- `word` may still equal `expected`), and
 * `false` only once `timeout` has definitely elapsed with no wake
 * observed. Exactly like `futex_wait`, callers must always re-check
 * their own condition afterward regardless of the return value --
 * `park.hpp`'s `park_timeout()` is the expected calling pattern.
 */

#include "reloco_config.hpp"

#include "detail/compat.hpp"
#include "duration.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

#if defined(RELOCO_FUTEX_BACKEND_LINUX) && defined(RELOCO_FUTEX_BACKEND_FREEBSD)
#error "reloco/futex.hpp: define at most one of RELOCO_FUTEX_BACKEND_LINUX/RELOCO_FUTEX_BACKEND_FREEBSD"
#endif
#if defined(RELOCO_FUTEX_BACKEND_CUSTOM) &&                                                                            \
    (defined(RELOCO_FUTEX_BACKEND_LINUX) || defined(RELOCO_FUTEX_BACKEND_FREEBSD))
#error "reloco/futex.hpp: RELOCO_FUTEX_BACKEND_CUSTOM is exclusive of RELOCO_FUTEX_BACKEND_LINUX/_FREEBSD"
#endif

#if !defined(RELOCO_FUTEX_BACKEND_CUSTOM)

namespace reloco {

/** @brief The word a `futex_wait()`/`futex_wake_one()`/`futex_wake_all()`
 * call operates on. Exactly one machine word wide
 * (`std::atomic<std::uint32_t>`): every concrete backend above operates
 * on a word this size (Linux's and FreeBSD's own kernel futex primitives
 * are fixed at 32 bits; a `RELOCO_FUTEX_BACKEND_CUSTOM` backend must
 * honor the same width to stay a drop-in replacement). */
using futex_word = std::atomic<std::uint32_t>;

/** @brief Blocks the calling thread while `word == expected`, matching a
 * futex's `WAIT` operation: returns as soon as either `word` no longer
 * equals `expected`, or a concurrent `futex_wake_one()`/
 * `futex_wake_all()` call targeting the same `word` observes this thread
 * waiting on it. Spurious wakeups are possible (`word` may still equal
 * `expected` when this returns) and must be tolerated by the caller,
 * exactly like `condition_variable::wait`. */
RELOCO_API void futex_wait(const futex_word &word, std::uint32_t expected) noexcept;

/** @brief Bounded `futex_wait()`: blocks the calling thread while `word ==
 * expected`, for at most `timeout`. Returns `true` if it returned for any
 * reason other than the timeout definitely elapsing (a genuine wake, or
 * a spurious wakeup -- `word` may still equal `expected` when this
 * returns), `false` only once `timeout` has definitely elapsed with no
 * wake observed. Exactly like `futex_wait`, the caller must always
 * re-check its own condition afterward regardless of the return value. */
RELOCO_API bool futex_wait_timeout(const futex_word &word, std::uint32_t expected, duration timeout) noexcept;

/** @brief Wakes at most one thread currently blocked in `futex_wait()` on
 * `word`. */
RELOCO_API void futex_wake_one(futex_word &word) noexcept;

/** @brief Wakes every thread currently blocked in `futex_wait()` on
 * `word`. */
RELOCO_API void futex_wake_all(futex_word &word) noexcept;

} // namespace reloco

#if defined(RELOCO_FUTEX_BACKEND_LINUX)
#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "futex_linux.ipp"
#endif
#elif defined(RELOCO_FUTEX_BACKEND_FREEBSD)
#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "futex_freebsd.ipp"
#endif
#else
#include "mutex.hpp"

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "futex_std.ipp"
#endif
#endif

#endif // !RELOCO_FUTEX_BACKEND_CUSTOM
