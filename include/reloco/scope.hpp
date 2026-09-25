// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file scope.hpp
 * @brief `reloco::scope`, matching Rust's `std::thread::scope`: run a
 * closure that may spawn threads borrowing data from the enclosing stack
 * frame, guaranteeing every such thread has finished running before
 * `scope()` itself returns.
 *
 * `thread.hpp`'s `spawn(F, allocator_ref)` requires `F: Send + 'static`
 * (documented, not statically enforced -- reloco has no lifetime tracking,
 * so nothing actually stops a captured reference from outliving the
 * spawned thread if the caller gets it wrong). `scope()` closes that gap
 * exactly the way Rust does it: `scope(body)` calls `body(thread_scope&)`,
 * and every `thread_scope::spawn()` call made from inside `body` is
 * guaranteed to have fully returned by the time `scope()` itself returns
 * -- so a stack local captured by reference from the enclosing frame is
 * provably still alive for the whole lifetime of every spawned closure,
 * without reloco needing to prove it via any borrow-checker-like
 * machinery. `F` (and everything it captures) passed to
 * `thread_scope::spawn` therefore only needs to be `Send`, not `'static`
 * -- exactly like Rust's own `Scope::spawn` bound (`F: Send + 'scope`,
 * not `F: Send + 'static`).
 *
 * Internally this is a completion counter (`detail::scope_data::
 * running_count`) guarded by one `mutex` + `condition_variable` pair (see
 * `mutex.hpp`), shared via `shared_ptr` so it outlives any individual
 * `spawn()` call. Each `thread_scope::spawn()` call increments the
 * counter before handing the closure to `reloco::spawn()`, and wraps it
 * so the counter is decremented (and the condition variable notified)
 * immediately after the closure returns, still running on the spawned
 * thread -- matching Rust's own `std::thread::scope` implementation,
 * which also does not literally join every spawned thread to know when
 * it is safe to return, just waits for this kind of completion signal.
 * `~thread_scope()` blocks until the counter reaches zero, which is what
 * makes `scope()` itself not return until every spawned closure has
 * finished running. Any `scoped_join_handle<R>` the caller keeps and
 * never explicitly `.join()`s is still safely joined on its own
 * destruction, exactly like `join_handle<R>` (see `thread.hpp`) -- that
 * OS-level join is a separate, purely resource-reclamation concern from
 * the borrow-safety guarantee above.
 */

#include "default_allocator.hpp"
#include "detail/compat.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "mutex.hpp"
#include "send_sync.hpp"
#include "shared_ptr.hpp"
#include "thread.hpp"

#include <cstddef>
#include <mutex>
#include <type_traits>
#include <utility>

namespace reloco {

class thread_scope;

/**
 * @brief Runs `body(s)` with a fresh `thread_scope &s`, blocking until
 * every thread spawned via `s.spawn()` has finished before returning.
 * Matching Rust's `std::thread::scope`. See the file-level documentation
 * above.
 *
 * Fails only if allocating the internal shared completion-tracking state
 * fails; `body` itself is otherwise invoked unconditionally, and its
 * result (if not `void`) is forwarded as-is.
 */
template <typename F, typename R = std::invoke_result_t<F &, thread_scope &>>
[[nodiscard]] result<R> scope(F &&body, allocator_ref alloc = default_allocator()) noexcept;

namespace detail {

struct scope_data {
  mutex guard;
  condition_variable done;
  std::size_t running_count = 0;
};

} // namespace detail

/**
 * @brief Handle to a thread spawned via `thread_scope::spawn`, matching
 * Rust's `std::thread::ScopedJoinHandle<'scope, T>`. Move-only.
 */
template <typename R> class [[nodiscard]] RELOCO_OWNER scoped_join_handle {
public:
  scoped_join_handle(scoped_join_handle &&) noexcept = default;
  scoped_join_handle &operator=(scoped_join_handle &&) noexcept = default;
  scoped_join_handle(const scoped_join_handle &) = delete;
  scoped_join_handle &operator=(const scoped_join_handle &) = delete;

  [[nodiscard]] bool joinable() const noexcept { return inner_.joinable(); }

  [[nodiscard]] thread_id get_id() const noexcept { return inner_.get_id(); }

  /**
   * @brief Blocks until the thread finishes, then moves its result out.
   * Consumes `*this`. Matches Rust's `ScopedJoinHandle::join()`.
   */
  [[nodiscard]] R join() && noexcept { return std::move(inner_).join(); }

private:
  friend class thread_scope;

  explicit scoped_join_handle(join_handle<R> inner) noexcept : inner_(std::move(inner)) {}

  join_handle<R> inner_;
};

/** @brief `void`-returning specialization: no result to retrieve. */
template <> class [[nodiscard]] RELOCO_OWNER scoped_join_handle<void> {
public:
  scoped_join_handle(scoped_join_handle &&) noexcept = default;
  scoped_join_handle &operator=(scoped_join_handle &&) noexcept = default;
  scoped_join_handle(const scoped_join_handle &) = delete;
  scoped_join_handle &operator=(const scoped_join_handle &) = delete;

  [[nodiscard]] bool joinable() const noexcept { return inner_.joinable(); }

  [[nodiscard]] thread_id get_id() const noexcept { return inner_.get_id(); }

  /** @brief Blocks until the thread finishes. Consumes `*this`. */
  void join() && noexcept { std::move(inner_).join(); }

private:
  friend class thread_scope;

  explicit scoped_join_handle(join_handle<void> inner) noexcept : inner_(std::move(inner)) {}

  join_handle<void> inner_;
};

/**
 * @brief Passed by reference to a `scope()` closure. `spawn()` calls made
 * through it are guaranteed to have fully returned before the enclosing
 * `scope()` call returns -- see the file-level documentation above.
 * Neither copyable nor movable.
 */
class thread_scope {
public:
  thread_scope(const thread_scope &) = delete;
  thread_scope &operator=(const thread_scope &) = delete;
  thread_scope(thread_scope &&) = delete;
  thread_scope &operator=(thread_scope &&) = delete;

  ~thread_scope() noexcept {
    std::unique_lock<mutex> lock(data_->guard);
    static_cast<void>(data_->done.wait(lock, [this] { return data_->running_count == 0; }));
  }

  /**
   * @brief Spawns `f` on a new OS thread, matching Rust's `Scope::spawn`.
   * `F` (and everything it captures) must be `is_send_v` (see
   * `send_sync.hpp`), same as `reloco::spawn` -- but, unlike
   * `reloco::spawn`, `F` may capture a reference to any value that
   * outlives this `scope()` call, since every thread spawned through
   * `*this` is guaranteed to have finished before `scope()` returns.
   */
  template <typename F, typename R = std::invoke_result_t<std::decay_t<F> &>>
  [[nodiscard]] result<scoped_join_handle<R>> spawn(F &&f, allocator_ref alloc = default_allocator()) noexcept {
    static_assert(is_send_v<std::decay_t<F>>,
                  "thread_scope::spawn: F must be Send (see send_sync.hpp) -- it (and everything it captures) will "
                  "run on another thread");
    static_assert(is_send_v<R>,
                  "thread_scope::spawn: F's return type must be Send -- it is moved back to the joining thread by "
                  "join()");

    {
      std::lock_guard<mutex> lock(data_->guard);
      ++data_->running_count;
    }

    auto data = data_; // shared_ptr copy: keeps scope_data alive for the wrapped closure below.
    auto wrapped = [captured_f = std::forward<F>(f), data]() noexcept -> R {
      // Runs on the spawned thread, right after captured_f() returns (in
      // either branch below): decrements the shared completion counter
      // and wakes up a `~thread_scope()` blocked waiting on it.
      struct completion_guard {
        shared_ptr<detail::scope_data> shared_data;

        ~completion_guard() noexcept {
          std::lock_guard<mutex> lock(shared_data->guard);
          if (--shared_data->running_count == 0)
            shared_data->done.notify_all();
        }
      } guard{data};

      return captured_f();
    };

    auto handle = reloco::spawn(std::move(wrapped), alloc);
    if (!handle) {
      std::lock_guard<mutex> lock(data_->guard);
      if (--data_->running_count == 0)
        data_->done.notify_all();
      return unexpected(handle.error());
    }
    return scoped_join_handle<R>(std::move(*handle));
  }

private:
  template <typename F, typename R> friend result<R> scope(F &&, allocator_ref) noexcept;

  explicit thread_scope(shared_ptr<detail::scope_data> data) noexcept : data_(std::move(data)) {}

  shared_ptr<detail::scope_data> data_;
};

template <typename F, typename R> [[nodiscard]] result<R> scope(F &&body, allocator_ref alloc) noexcept {
  auto data = try_allocate_combined_shared<detail::scope_data>(alloc);
  if (!data)
    return unexpected(data.error());

  thread_scope s(std::move(*data));
  if constexpr (std::is_void_v<R>) {
    body(s);
    return {};
  } else {
    return result<R>(body(s));
  }
}

/**
 * @brief `scoped_join_handle<R>` is `Send` exactly when `R` is (it is
 * moved back to the joining thread by `join()`), matching
 * `join_handle<R>`'s own specialization.
 */
template <typename R> struct is_send<scoped_join_handle<R>> : is_send<R> {};

/** @brief `scoped_join_handle<R>` is always `Sync`, regardless of `R`,
 * matching `join_handle<R>`'s own specialization. */
template <typename R> struct is_sync<scoped_join_handle<R>> : std::true_type {};

} // namespace reloco
