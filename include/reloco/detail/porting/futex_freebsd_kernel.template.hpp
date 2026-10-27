// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file futex_freebsd_kernel.template.hpp
 * @brief Documentation-only `RELOCO_FUTEX_BACKEND_CUSTOM` example for the
 * **FreeBSD kernel**, built on the `sleepq(9)` primitives (the sleep queues
 * `msleep(9)`/`wakeup(9)` themselves are implemented with).
 *
 * Never `#include`d by anything -- copy it to `detail/porting/futex.hpp`
 * (see `futex.template.hpp` and `docs/futex.md`) and adapt it. Not compiled
 * or exercised by this repository (which targets hosted userspace); the
 * `sleepq_*` signatures follow `sys/sleepqueue.h` of FreeBSD 13/14 -- check
 * them against your kernel version.
 *
 * Why `sleepq` instead of `msleep`: the kernel hashes the wait channel
 * (`&word`) into a sleep-queue chain and protects each chain with its own
 * spin lock. `sleepq_lock(&word)` takes that per-chain lock, so no global
 * `mtx` of our own is needed. Holding the chain lock while re-checking
 * `word` and then calling `sleepq_add` closes the lost-wakeup window: a
 * waker must take the same chain lock before `sleepq_signal`, so it either
 * runs before our check (and we see the new value) or after we are already
 * queued (and we get woken).
 */

#include <atomic>
#include <cstdint>

#include <sys/param.h>

#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sleepqueue.h>

namespace reloco {

using futex_word = std::atomic<std::uint32_t>;

namespace detail {

// Wait-channel identity: the word's own address, exactly what a
// userspace futex keys on.
inline const void *futex_wchan(const futex_word &word) noexcept { return &word; }

// Converts a reloco::duration to a relative sbintime_t (32.32 fixed
// point seconds), integer arithmetic only.
inline sbintime_t futex_to_sbt(duration timeout) noexcept {
  return (static_cast<sbintime_t>(timeout.as_secs()) << 32) |
         static_cast<sbintime_t>((static_cast<std::uint64_t>(timeout.subsec_nanos()) << 32) / 1'000'000'000ULL);
}

// Shared body of futex_wait/futex_wait_timeout. Returns the sleepq wait
// status: 0 on wake (or value mismatch), EWOULDBLOCK on timeout.
inline int futex_sleep(const futex_word &word, std::uint32_t expected, bool timed, sbintime_t sbt) noexcept {
  const void *wchan = futex_wchan(word);

  // Take the chain lock FIRST, then re-check the word under it.
  sleepq_lock(wchan);
  if (word.load(std::memory_order_acquire) != expected) {
    sleepq_release(wchan);
    return 0;
  }

  // SLEEPQ_SLEEP: an ordinary (non-condvar) sleep. lock == nullptr: we do
  // not hold an interlock mutex -- the chain lock is our interlock.
  // Queue 0: single queue per channel.
  sleepq_add(wchan, nullptr, "rlfutex", SLEEPQ_SLEEP, 0);

  if (!timed) {
    // sleepq_wait() drops the chain lock and switches away atomically.
    // PPAUSE without PCATCH: not interruptible by signals, so waits
    // return only on wake (spurious wakeups remain allowed by contract).
    sleepq_wait(wchan, PPAUSE);
    return 0;
  }

  // Must be armed after sleepq_add and before sleepq_timedwait. pr == 0:
  // exact timer precision; flags == 0: relative timeout.
  sleepq_set_timeout_sbt(wchan, sbt, 0, 0);
  return sleepq_timedwait(wchan, PPAUSE);
}

} // namespace detail

inline void futex_wait(const futex_word &word, std::uint32_t expected) noexcept {
  static_cast<void>(detail::futex_sleep(word, expected, false, 0));
}

inline bool futex_wait_timeout(const futex_word &word, std::uint32_t expected, duration timeout) noexcept {
  // Only EWOULDBLOCK means the timeout definitely elapsed.
  return detail::futex_sleep(word, expected, true, detail::futex_to_sbt(timeout)) != EWOULDBLOCK;
}

inline void futex_wake_one(futex_word &word) noexcept {
  const void *wchan = detail::futex_wchan(word);
  sleepq_lock(wchan);
  // pri == 0: keep the woken thread's own priority. sleepq_signal wakes
  // the highest-priority waiter; its return value (a "swapper needs a
  // kick" flag on older kernels, void on newer ones) is handled below.
  sleepq_signal(wchan, SLEEPQ_SLEEP, 0, 0);
  sleepq_release(wchan);
}

inline void futex_wake_all(futex_word &word) noexcept {
  const void *wchan = detail::futex_wchan(word);
  sleepq_lock(wchan);
  sleepq_broadcast(wchan, SLEEPQ_SLEEP, 0, 0);
  sleepq_release(wchan);
}

} // namespace reloco
