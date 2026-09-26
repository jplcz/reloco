// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file once.hpp
 * @brief `once`, matching Rust's `std::sync::Once`: runs a closure exactly
 * once across any number of racing callers, blocking every other caller
 * until it completes.
 *
 * Complements `once_lock.hpp`'s `once_lock<T>` rather than replacing it:
 * `once_lock<T>` is a write-once, read-many *value* cell (`get()` hands
 * back a `const T *`), while `once` carries no value at all -- it is
 * purely a "has this run yet" gate around a side-effecting closure,
 * matching Rust's own split between `Once` (side effects only) and
 * `OnceLock<T>`/`LazyLock<T>` (a value). A `once` can also be reused as
 * the state word backing an `once_lock<T>`-shaped type that stores its
 * value elsewhere (e.g. a `static` in a freestanding/kernel context
 * where placing the value inside the synchronization primitive itself is
 * undesirable).
 *
 * Like every other reloco synchronization primitive, `once` is built
 * directly on `futex.hpp`'s `futex_word`/`futex_wait`/`futex_wake_all`
 * rather than a `mutex` + `condition_variable` pair: a single
 * `futex_word` state (`not_started`/`running`/`completed`) is enough,
 * with no heap allocation and no dependency on `mutex.hpp` at all --
 * deliberately, so `once` (and anything built on it, such as a
 * kernel-side lazy-initialization gate) stays usable in a freestanding
 * or bare-kernel environment that provides its own `RELOCO_FUTEX_BACKEND_
 * CUSTOM` (see `futex.hpp`) but has no OS-backed mutex/thread available
 * at all.
 *
 * - `call_once(F)`, where `F` is invocable as `void()`: runs `F` exactly
 *   once (across every `once` instance's lifetime, no matter how many
 *   threads call `call_once`/`try_call_once` concurrently or how many
 *   times), matching Rust's `Once::call_once`. Every caller -- including
 *   the one actually running `F` -- returns only once `F` has completed.
 *   Assumed to never fail; use `try_call_once` if `F` can fail and the
 *   attempt should be retried later.
 * - `try_call_once(F)`, where `F` is invocable as `result<void>()`:
 *   same as `call_once`, but if `F` fails, `once` reverts to
 *   not-yet-run so a later call (from any thread) may retry -- matching
 *   this file's own `once_lock<T>::get_or_try_init` convention (Rust's
 *   `Once` has no failure-recovery equivalent of its own: `call_once`'s
 *   `F` cannot fail, only panic, which poisons the `Once` permanently --
 *   `try_call_once` is a deliberate reloco-specific extension instead of
 *   a straight port).
 * - `is_completed()` -> `bool`: `true` once `F` has run to completion,
 *   matching Rust's `Once::is_completed()`. Never blocks.
 * - `unsafe_reset()`: unconditionally resets this `once` to not-yet-run.
 *   **Unsafe**: only sound if the caller can guarantee no other thread is
 *   concurrently calling `call_once`/`try_call_once`/`is_completed` on
 *   the same instance -- see the method's own doc comment. Not part of
 *   Rust's `Once` (which has no reset at all), but matches
 *   `parking_lot::Once::reset(&mut self)`'s exclusive-access contract;
 *   included for niche cases neither can express otherwise, such as
 *   re-running one-time initialization after `fork()` in a freestanding/
 *   kernel context, or resetting a `once` in a test fixture.
 *
 * Unlike Rust's `Once`, there is no `call_once_force`/`OnceState`
 * poisoning-recovery API: reloco has no panic/unwind mechanism for a
 * closure to fail *without* reporting it, so the only way `F` "fails" here
 * is `try_call_once`'s ordinary `result<void>` error return, which already
 * always permits a retry -- there is no permanently-poisoned state to
 * force past.
 *
 * `once` is neither copyable nor movable, matching `barrier`/`mutex`/
 * `once_lock<T>`'s own restriction.
 */

#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "futex.hpp"

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief Runs a closure exactly once across any number of racing callers,
 * matching Rust's `std::sync::Once`. See the file-level documentation
 * above.
 */
class once {
public:
  constexpr once() noexcept = default;

  once(const once &) = delete;
  once &operator=(const once &) = delete;
  once(once &&) = delete;
  once &operator=(once &&) = delete;

  /**
   * @brief Runs `f` exactly once, blocking every other concurrent caller
   * until it completes. `f` must be invocable as `void()` and is assumed
   * to never fail, matching Rust's `Once::call_once(f: impl FnOnce())`.
   * Use `try_call_once` if `f` can fail and a later retry should be
   * possible.
   */
  template <typename F> void call_once(F &&f) noexcept(std::is_nothrow_invocable_v<F &>) {
    auto init_result = try_call_once(
        [&f]() noexcept(std::is_nothrow_invocable_v<F &>) -> result<void> {
          std::forward<F>(f)();
          return {};
        });
    RELOCO_ASSERT(init_result.has_value(), "once::call_once: unreachable -- wrapped closure never fails");
  }

  /**
   * @brief Runs `f` exactly once, blocking every other concurrent caller
   * until it settles. `f` must be invocable as `result<void>()`. If `f`
   * fails, `once` reverts to not-yet-run (this call returns `f`'s error,
   * and a later call from any thread may retry) -- see the file-level
   * documentation above for how this compares to Rust's `Once`.
   */
  template <typename F> [[nodiscard]] result<void> try_call_once(F &&f) noexcept(std::is_nothrow_invocable_v<F &>) {
    if (state_.load(std::memory_order_acquire) == completed)
      return {};

    for (;;) {
      std::uint32_t expected = not_started;
      if (state_.compare_exchange_strong(expected, running, std::memory_order_acq_rel, std::memory_order_acquire))
        break;
      if (expected == completed)
        return {};
      // expected == running: wait for the racing call to settle, then retry the claim.
      futex_wait(state_, running);
    }

    result<void> f_result = f();

    if (!f_result) {
      state_.store(not_started, std::memory_order_release);
      futex_wake_all(state_);
      return unexpected(f_result.error());
    }

    state_.store(completed, std::memory_order_release);
    futex_wake_all(state_);
    return {};
  }

  /**
   * @brief `true` once `f` has run to completion via `call_once`/
   * `try_call_once`, matching Rust's `Once::is_completed()`. Never
   * blocks.
   */
  [[nodiscard]] bool is_completed() const noexcept { return state_.load(std::memory_order_acquire) == completed; }

  /**
   * @brief Unconditionally resets this `once` to not-yet-run, so the next
   * `call_once`/`try_call_once` call runs its closure again.
   *
   * **Unsafe**: unlike every other method here, this is *not* safe to
   * call concurrently with `call_once`/`try_call_once`/`is_completed` on
   * the same instance from another thread -- it plainly overwrites the
   * state word with no regard for a call currently in its "running"
   * state, which would otherwise let two racing closure invocations run
   * at once. The caller must externally guarantee exclusive access (e.g.
   * every other thread that could observe this `once` has already joined,
   * or is blocked elsewhere) before calling this.
   *
   * Not part of Rust's `std::sync::Once` (which has no reset at all --
   * once poisoned/completed, permanently so), but matches
   * `parking_lot::Once::reset(&mut self)`: its `&mut self` receiver is
   * exactly the same "caller must hold exclusive access" contract as
   * this method's "unsafe" naming spells out explicitly instead. Useful
   * for the niche cases Rust's own `Once` cannot express at all --
   * re-running one-time initialization after `fork()` in the child
   * process (the parent's completed-but-now-meaningless state must not
   * leak across the fork), in a test fixture that wants a fresh `once`
   * without re-declaring it, or in a freestanding/kernel context
   * re-initializing a subsystem from scratch.
   */
  RELOCO_UNSAFE_BUFFER_USAGE void unsafe_reset() noexcept { state_.store(not_started, std::memory_order_release); }

private:
  static constexpr std::uint32_t not_started = 0;
  static constexpr std::uint32_t running = 1;
  static constexpr std::uint32_t completed = 2;

  futex_word state_{not_started};
};

} // namespace reloco
