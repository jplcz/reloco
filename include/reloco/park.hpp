// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file park.hpp
 * @brief `this_thread::park`/`park_timeout`/`sleep_for` and
 * `thread_handle`/`this_thread::current()`, matching Rust's
 * `std::thread::park`/`park_timeout`/`sleep`/`Thread`/`thread::current()`.
 *
 * A *parker* (`detail::parker`) is a one-slot wake token: a single
 * `futex_word` (see `futex.hpp`), `0` (no token available) or `1` (token
 * available) -- `park()`/`park_timeout(duration)` block (via
 * `futex_wait`/`futex_wait_timeout`) until the token becomes available
 * (consuming it, via an atomic exchange back to `0`) or, for the latter,
 * until the timeout elapses first; `unpark()` makes the token available
 * (an atomic store of `1`) and wakes a blocked (or future) `park()`/
 * `park_timeout()` call via `futex_wake_one` (at most one thread -- the
 * parker's own owning thread -- ever waits on a given parker's word).
 * Tokens do not accumulate -- calling `unpark()` any number of times
 * before the thread next parks is equivalent to calling it once, matching
 * Rust's own semantics exactly.
 *
 * `park_timeout`'s deadline is tracked with `instant.hpp`'s `instant`
 * (captured once as `instant::now() + timeout` before the wait loop
 * begins) rather than re-arming a fresh `timeout`-length
 * `futex_wait_timeout` call after every spurious wakeup -- otherwise a
 * thread repeatedly (if rarely) spuriously woken just before its deadline
 * could be kept parked far longer than the caller asked for.
 *
 * Every OS thread lazily owns exactly one parker, created on first use by
 * `this_thread::current()`/`park()`/`park_timeout()`/`sleep_for()` and
 * cached for the lifetime of the thread in a `tls_provider`
 * (`RELOCO_TLS_MODEL`-selected -- see `tls_provider.hpp`) slot, as
 * a `shared_ptr<detail::parker>`. `thread_handle` (matching Rust's
 * `std::thread::Thread`) is a cheap, cloneable, `Send + Sync` reference to
 * that same parker: `this_thread::current()` (from any thread) captures
 * a clone, and calling `unpark()` through it wakes the thread it was
 * obtained from, even after that thread has since exited -- the
 * `shared_ptr` keeps the parker itself alive regardless.
 *
 * `sleep_for(duration)` is unrelated to parking: it blocks the calling
 * thread for (at least) the given duration unconditionally, waiting on a
 * throwaway, never-woken `futex_word` (see `futex.hpp`) rather than
 * `<chrono>`/`<thread>`'s own sleep, so it stays available under every
 * `futex.hpp` backend and never touches the calling thread's own park
 * token.
 *
 * Unlike Rust's `park_timeout`/`park_deadline` (which return nothing --
 * the caller must re-check its own condition after either call returns),
 * `park_timeout` here returns `bool`: `true` if a token was consumed
 * (`unpark()` won the race), `false` if the timeout elapsed first --
 * matching `futex_wait_timeout`'s own boolean outcome shape. Still safe
 * to ignore, exactly like Rust's spurious-wakeup-tolerant contract: a
 * caller that ignores the return value and simply re-checks its own
 * condition afterward behaves identically to Rust's version.
 */

#include "detail/assert.hpp"
#include "duration.hpp"
#include "futex.hpp"
#include "instant.hpp"
#include "send_sync.hpp"
#include "shared_ptr.hpp"
#include "thread.hpp"
#include "tls_provider.hpp"

#include <atomic>

namespace reloco {

class thread_handle;

namespace this_thread {
[[nodiscard]] thread_handle current() noexcept;
} // namespace this_thread

namespace detail {

/**
 * @brief One-slot wake token: a single `futex_word` (see `futex.hpp`),
 * `0` (no token) or `1` (token available). Not copyable/movable --
 * always accessed through a `shared_ptr` (see `current_thread_parker()`
 * below), so every clone of a `thread_handle` (or the thread's own TLS
 * slot) shares the exact same instance.
 */
class parker {
public:
  parker() noexcept = default;
  parker(const parker &) = delete;
  parker &operator=(const parker &) = delete;

  /** @brief Blocks until a token is available, then consumes it. Returns
   * immediately (still consuming the token) if one was already available. */
  void park() noexcept {
    if (available_.exchange(0, std::memory_order_acquire) == 1)
      return;
    for (;;) {
      futex_wait(available_, 0);
      if (available_.exchange(0, std::memory_order_acquire) == 1)
        return;
    }
  }

  /** @brief Bounded `park()`. Returns `true` if a token was consumed
   * (available immediately, or `unpark()` won the race before `timeout`
   * elapsed), `false` if the timeout elapsed first (no token consumed). */
  [[nodiscard]] bool park_timeout(duration timeout) noexcept {
    if (available_.exchange(0, std::memory_order_acquire) == 1)
      return true;
    // Captured once, up front: futex_wait_timeout's own timeout is
    // relative, so re-arming a fresh timeout-length wait after every
    // spurious wakeup would let a rarely-but-repeatedly-spuriously-woken
    // thread stay parked far longer than timeout -- re-deriving the
    // remaining time from a fixed deadline instead bounds the total wait
    // correctly.
    auto deadline = instant::now() + timeout;
    for (;;) {
      auto now = instant::now();
      if (now >= deadline)
        return available_.exchange(0, std::memory_order_acquire) == 1;
      futex_wait_timeout(available_, 0, deadline - now);
      if (available_.exchange(0, std::memory_order_acquire) == 1)
        return true;
    }
  }

  /** @brief Makes a token available, waking a currently-blocked (or the
   * very next) `park()`/`park_timeout()` call. Idempotent: does not
   * accumulate beyond one outstanding token. */
  void unpark() noexcept {
    available_.store(1, std::memory_order_release);
    // At most one thread -- this parker's own owning thread -- ever
    // waits on available_, so waking one is exactly as effective as
    // waking all here, and cheaper.
    futex_wake_one(available_);
  }

private:
  futex_word available_{0};
};

struct current_thread_parker_tag {};
using current_thread_parker_slot = tls_provider<shared_ptr<parker>, current_thread_parker_tag>;

/** @brief Returns the calling thread's own parker, lazily creating it (and
 * caching it in TLS) on first use. */
[[nodiscard]] inline shared_ptr<parker> current_thread_parker() noexcept {
  auto slot = current_thread_parker_slot::get();
  RELOCO_ASSERT(slot.has_value(), "this_thread: parker TLS slot allocation failed");
  shared_ptr<parker> &existing = slot->get();
  if (existing)
    return existing;
  auto created = try_create_combined_shared<parker>();
  RELOCO_ASSERT(created.has_value(), "this_thread: parker allocation failed");
  auto set_result = current_thread_parker_slot::set(*created);
  RELOCO_ASSERT(set_result.has_value(), "this_thread: parker TLS slot allocation failed");
  return *created;
}

} // namespace detail

/**
 * @brief Cheap, cloneable, `Send + Sync` reference to a specific thread's
 * park/unpark token, matching Rust's `std::thread::Thread`. Obtained via
 * `this_thread::current()`. Every clone shares the same underlying
 * `detail::parker`, so `unpark()` called through any clone wakes the
 * thread that clone was obtained from -- including after that thread has
 * since exited (the `shared_ptr` keeps the parker itself alive).
 */
class thread_handle {
public:
  thread_handle(const thread_handle &) noexcept = default;
  thread_handle &operator=(const thread_handle &) noexcept = default;
  thread_handle(thread_handle &&) noexcept = default;
  thread_handle &operator=(thread_handle &&) noexcept = default;

  /**
   * @brief Makes a single unpark token available for the thread this
   * handle refers to, waking it if currently blocked in `park()`/
   * `park_timeout()`, or making its very next such call return
   * immediately otherwise. Matches Rust's `Thread::unpark()`.
   */
  void unpark() const noexcept { parker_->unpark(); }

private:
  friend thread_handle this_thread::current() noexcept;
  explicit thread_handle(shared_ptr<detail::parker> parker) noexcept : parker_(std::move(parker)) {}

  shared_ptr<detail::parker> parker_;
};

namespace this_thread {

/** @brief Returns a cloneable handle to the calling thread, matching
 * Rust's `std::thread::current()`. */
[[nodiscard]] inline thread_handle current() noexcept { return thread_handle(detail::current_thread_parker()); }

/** @brief Blocks the calling thread until its token becomes available
 * (via some other thread calling `unpark()` on a `thread_handle` referring
 * to it), then consumes it. Returns immediately (still consuming the
 * token) if one is already available -- matching Rust's
 * `std::thread::park()`. */
inline void park() noexcept {
  auto parker = detail::current_thread_parker();
  parker->park();
}

/** @brief Bounded `park()`: returns `true` if a token was consumed before
 * `timeout` elapsed, `false` otherwise. See `detail::parker::park_timeout`
 * and this file's own doc comment for how this return value relates to
 * Rust's `void`-returning `std::thread::park_timeout`. */
[[nodiscard]] inline bool park_timeout(duration timeout) noexcept {
  auto parker = detail::current_thread_parker();
  return parker->park_timeout(timeout);
}

/** @brief Blocks the calling thread for (at least) `timeout`,
 * unconditionally -- matching Rust's `std::thread::sleep`. Unrelated to
 * parking: waits on a throwaway, never-woken `futex_word` private to this
 * call (so it never consumes or is affected by the calling thread's own
 * park token), re-deriving the remaining time from a fixed deadline after
 * every spurious wakeup -- exactly like `detail::parker::park_timeout`'s
 * own deadline loop -- so the "at least `timeout`" guarantee holds even
 * if `futex_wait_timeout` returns early. */
inline void sleep_for(duration timeout) noexcept {
  futex_word never_woken{0};
  auto deadline = instant::now() + timeout;
  for (;;) {
    auto now = instant::now();
    if (now >= deadline)
      return;
    futex_wait_timeout(never_woken, 0, deadline - now);
  }
}

} // namespace this_thread

} // namespace reloco
