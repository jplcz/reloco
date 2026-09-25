// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file park.hpp
 * @brief `this_thread::park`/`park_timeout`/`sleep_for` and
 * `thread_handle`/`this_thread::current()`, matching Rust's
 * `std::thread::park`/`park_timeout`/`sleep`/`Thread`/`thread::current()`.
 *
 * A *parker* (`detail::parker`) is a one-slot wake token guarded by one
 * `mutex` + `condition_variable` pair (see `mutex.hpp`), exactly like
 * `channel.hpp`'s own locking approach: `park()`/`park_timeout(duration)`
 * block until the token becomes available (consuming it) or, for the
 * latter, until the timeout elapses first; `unpark()` makes the token
 * available and wakes a blocked (or future) `park()`/`park_timeout()`
 * call. Tokens do not accumulate -- calling `unpark()` any number of times
 * before the thread next parks is equivalent to calling it once, matching
 * Rust's own semantics exactly.
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
 * thread for (at least) the given duration unconditionally, using a
 * throwaway, always-false-predicate `condition_variable::wait_for` (see
 * `mutex.hpp`) rather than `<chrono>`/`<thread>`'s own sleep, so it stays
 * available under every `mutex.hpp` backend, including
 * `RELOCO_MUTEX_BACKEND_PTHREAD`'s monotonic-clock-aware wait.
 *
 * Unlike Rust's `park_timeout`/`park_deadline` (which return nothing --
 * the caller must re-check its own condition after either call returns),
 * `park_timeout` here returns `bool`: `true` if a token was consumed
 * (`unpark()` won the race), `false` if the timeout elapsed first --
 * matching `condition_variable::wait_for`'s own `result<bool>` outcome
 * shape elsewhere in reloco. Still safe to ignore, exactly like Rust's
 * spurious-wakeup-tolerant contract: a caller that ignores the return
 * value and simply re-checks its own condition afterward behaves
 * identically to Rust's version.
 */

#include "duration.hpp"
#include "mutex.hpp"
#include "send_sync.hpp"
#include "shared_ptr.hpp"
#include "thread.hpp"
#include "tls_provider.hpp"

#include <mutex>

namespace reloco {

class thread_handle;

namespace this_thread {
[[nodiscard]] thread_handle current() noexcept;
} // namespace this_thread

namespace detail {

/**
 * @brief One-slot wake token guarded by a `mutex` + `condition_variable`
 * pair. Not copyable/movable -- always accessed through a `shared_ptr`
 * (see `current_thread_parker()` below), so every clone of a
 * `thread_handle` (or the thread's own TLS slot) shares the exact same
 * instance.
 */
class parker {
public:
  parker() noexcept = default;
  parker(const parker &) = delete;
  parker &operator=(const parker &) = delete;

  /** @brief Blocks until a token is available, then consumes it. Returns
   * immediately (still consuming the token) if one was already available. */
  void park() noexcept {
    std::unique_lock<mutex> lock(mutex_);
    if (available_) {
      available_ = false;
      return;
    }
    auto wait_result = cv_.wait(lock, [this] { return available_; });
    RELOCO_ASSERT(wait_result.has_value(), "parker::park: condition_variable::wait failed");
    available_ = false;
  }

  /** @brief Bounded `park()`. Returns `true` if a token was consumed
   * (available immediately, or `unpark()` won the race before `timeout`
   * elapsed), `false` if the timeout elapsed first (no token consumed). */
  [[nodiscard]] bool park_timeout(duration timeout) noexcept {
    std::unique_lock<mutex> lock(mutex_);
    if (available_) {
      available_ = false;
      return true;
    }
    auto wait_result = cv_.wait_for(lock, timeout, [this] { return available_; });
    RELOCO_ASSERT(wait_result.has_value(), "parker::park_timeout: condition_variable::wait_for failed");
    if (!*wait_result)
      return false;
    available_ = false;
    return true;
  }

  /** @brief Makes a token available, waking a currently-blocked (or the
   * very next) `park()`/`park_timeout()` call. Idempotent: does not
   * accumulate beyond one outstanding token. */
  void unpark() noexcept {
    {
      std::lock_guard<mutex> lock(mutex_);
      available_ = true;
    }
    cv_.notify_one();
  }

private:
  mutex mutex_;
  condition_variable cv_;
  bool available_ = false;
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
 * parking: uses a throwaway `mutex` + `condition_variable` pair private to
 * this call, so it never consumes or is affected by the calling thread's
 * own park token. */
inline void sleep_for(duration timeout) noexcept {
  mutex sleep_mutex;
  condition_variable sleep_cv;
  std::unique_lock<mutex> lock(sleep_mutex);
  auto wait_result = sleep_cv.wait_for(lock, timeout, [] { return false; });
  RELOCO_ASSERT(wait_result.has_value(), "this_thread::sleep_for: condition_variable::wait_for failed");
}

} // namespace this_thread

} // namespace reloco
