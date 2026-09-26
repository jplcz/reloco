// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file spin_lock.hpp
 * @brief A busy-wait lock that never parks/blocks and never makes a
 * syscall, matching the ecosystem `spin` crate's `spin::Mutex<T>` (Rust's
 * own `std::sync` has no spinlock -- this is deliberately *not* modeled
 * on anything in `std`).
 *
 * `mutex.hpp`'s `mutex` (and everything built on it, transitively
 * including `guarded_mutex<T>`'s default `MutexT`) ultimately blocks a
 * contended thread by parking it with the OS scheduler -- `futex_wait`,
 * `pthread_mutex_lock`, .... That is the right default virtually
 * everywhere, but it is unusable in a handful of specific contexts this
 * header exists for instead:
 *
 * - **Interrupt/exception handlers and other contexts with no "current
 *   thread" to park** -- there is nothing for the scheduler to suspend.
 * - **Before a kernel's scheduler/threading subsystem is initialized at
 *   all** (early boot), or in code that must not depend on one existing
 *   (a `panic`/fault handler that must still be able to take a lock to
 *   print a diagnostic).
 * - **SMP kernels protecting a data structure shared with an interrupt
 *   handler on another core**, where the holder is never itself
 *   descheduled while holding the lock (interrupts are typically masked),
 *   so the wait is always provably short and spinning is cheaper than a
 *   syscall.
 *
 * `spin_lock` itself needs none of that: it is pure `std::atomic<bool>` +
 * `hint::spin_loop()` (see `hint.hpp`), with zero OS dependency, so it
 * works unchanged in a freestanding/bare-kernel build using
 * `RELOCO_THREAD_BACKEND_CUSTOM`/`RELOCO_MUTEX_BACKEND_CUSTOM` (or no
 * threading backend at all). This is the one piece of "kernel provides
 * its own locking primitive" territory reloco *does* supply directly,
 * specifically because -- unlike `mutex`/`thread` -- there is nothing
 * kernel-specific to defer to: a spinlock's entire contract is "atomically
 * swap a flag, retry while set", which every kernel/RTOS/bare-metal target
 * already has the exact same `<atomic>`-shaped hardware to implement it
 * with. A kernel that already ships its own spinlock (most do, often
 * tied into its own interrupt-masking/preemption-disabling conventions)
 * should of course keep using that one instead -- this is only for a
 * consumer (kernel or otherwise) that does not have one yet.
 *
 * Satisfies the same minimal `lock()`/`unlock()`/`try_lock()` surface as
 * `reloco::mutex`, so it slots directly into `guarded_mutex<T, MutexT>`
 * (see `guarded_mutex.hpp`) as a drop-in `MutexT`:
 *
 * @code
 * reloco::guarded_mutex<int, reloco::spin_lock> counter;
 * auto guard = counter.lock(); // never parks; spins instead.
 * @endcode
 *
 * Never fair (no queueing/ticketing -- a thread that keeps re-winning the
 * race against a newer waiter can, in principle, starve it) and never
 * adaptive (always spins, never falls back to parking after some
 * threshold, unlike e.g. glibc's own adaptive mutexes) -- both deliberate
 * simplifications matching the `spin` crate's own `Mutex`. Use
 * `reloco::mutex`/`guarded_mutex<T>` (blocking, fair-ish, scheduler-aware)
 * instead whenever one of the specific contexts above does not apply.
 *
 * **`RELOCO_SPIN_LOCK_BACKEND_CUSTOM`**: even though the plain
 * `std::atomic<bool>` implementation above needs nothing OS-specific to
 * work correctly, a *kernel* target usually still wants its own native
 * spinlock instead of reloco's: it is typically wired into that kernel's
 * own interrupt-masking/preemption-disabling/lock-order-verification
 * conventions (e.g. FreeBSD's `mtx_lock_spin` disables interrupts on the
 * current CPU for as long as the lock is held and integrates with
 * `WITNESS`; Linux's `raw_spinlock_t` disables preemption and, on `-rt`
 * kernels, is a different type entirely from a regular `spinlock_t`) that
 * a freestanding, OS-agnostic `std::atomic` cannot replicate and must not
 * silently omit. Define `RELOCO_SPIN_LOCK_BACKEND_CUSTOM` to suppress this
 * header's own definition of `reloco::spin_lock` entirely; this header
 * then `#include`s a fixed path, `detail/porting/spin_lock.hpp`, right at
 * the point the built-in definition above would otherwise appear -- the
 * same fixed-include mechanism `mutex.hpp`/`thread.hpp` use for their own
 * `_CUSTOM` backends (see `mutex.hpp` for the full rationale on why a
 * fixed include path, not "included by the application through the
 * normal path", is used). That file does not ship in this repository
 * (only `detail/porting/spin_lock.template.hpp`, an unused
 * documentation-only scaffold sketching a FreeBSD `MTX_SPIN`-backed
 * implementation, does); supply your own, most conveniently through the
 * `JPLCZ_RELOCO_PORTING_HEADERS` CMake variable (see `CMakeLists.txt`),
 * which copies it into that exact path and defines
 * `RELOCO_SPIN_LOCK_BACKEND_CUSTOM` automatically. The replacement must
 * keep the same `lock()`/`unlock()`/`try_lock()` surface used by
 * `guarded_mutex<T, MutexT>`, but is free to add its own construction
 * requirements.
 */

#include "detail/compat.hpp"
#include "hint.hpp"
#include "lifetime.hpp"

#include <atomic>

#if !defined(RELOCO_SPIN_LOCK_BACKEND_CUSTOM)

namespace reloco {

/**
 * @brief Busy-wait lock; never parks, blocks, or calls into the OS.
 * See this file's top-level docs for when to reach for this instead of
 * `reloco::mutex`.
 */
class RELOCO_CAPABILITY("mutex") spin_lock {
public:
  constexpr spin_lock() noexcept = default;

  spin_lock(const spin_lock &) = delete;
  spin_lock &operator=(const spin_lock &) = delete;

  /** @brief Spins until the lock is acquired. */
  void lock() & noexcept RELOCO_ACQUIRE() {
    // Test-and-test-and-set: retry the cheap relaxed load while contended
    // instead of hammering the exchange itself, which would otherwise
    // force the cache line to bounce between cores on every iteration
    // even though only one of them can ever win it.
    while (locked_.exchange(true, std::memory_order_acquire)) {
      while (locked_.load(std::memory_order_relaxed))
        hint::spin_loop();
    }
  }

  /** @brief Attempts to acquire the lock without spinning; returns
   * whether it succeeded. */
  [[nodiscard]] bool try_lock() & noexcept RELOCO_TRY_ACQUIRE(true) {
    return !locked_.exchange(true, std::memory_order_acquire);
  }

  /** @brief Releases a lock held by the calling thread. */
  void unlock() & noexcept RELOCO_RELEASE() { locked_.store(false, std::memory_order_release); }

  /**
   * @brief Best-effort snapshot of whether the lock is currently held,
   * matching the `spin` crate's own `Mutex::is_locked()`. Racy by nature
   * (another thread may lock/unlock immediately after this returns) --
   * useful only for diagnostics/assertions, never for making a
   * synchronization decision.
   */
  [[nodiscard]] bool is_locked() const noexcept { return locked_.load(std::memory_order_relaxed); }

private:
  std::atomic<bool> locked_{false};
};

} // namespace reloco

#else // RELOCO_SPIN_LOCK_BACKEND_CUSTOM

// See this file's top-level docs and `detail/porting/spin_lock.template.hpp`
// for the exact API this must provide -- including opening its own
// `namespace reloco { ... }`, exactly like the built-in definition above.
#include "detail/porting/spin_lock.hpp"

#endif // !RELOCO_SPIN_LOCK_BACKEND_CUSTOM
