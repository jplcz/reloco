// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file once_lock.hpp
 * @brief `once_lock<T>`, matching Rust's `std::sync::OnceLock<T>`: a cell
 * that can be written at most once and read many times after that.
 *
 * Complements `fallible_singleton.hpp`/`atomic_fallible_singleton.hpp`
 * rather than replacing them: those provide exactly one, static,
 * process-wide instance per `T`, constructed lazily and living until
 * program termination. `once_lock<T>` is an ordinary value type instead --
 * usable as a struct field, a local, or an element of another container --
 * so a program can have as many independently-initialized `once_lock<T>`
 * cells as it needs, each write-once-then-read-many, exactly like Rust's
 * `OnceLock<T>`.
 *
 * An `std::atomic<int>` state (`empty`/`initializing`/`ready`) gives every
 * `get()`/`get_mut()` call, and the fast path of every `try_set`/
 * `get_or_try_init` call, a lock-free acquire-load once initialization has
 * completed. The slow path (the first write, or any call contending with
 * an in-progress one) is serialized by one `mutex` + `condition_variable`
 * pair (see `mutex.hpp`), matching `channel.hpp`'s own locking approach.
 *
 * - `try_set(T)` -> `result<void>`: fails with `error::already_exists` if
 *   the cell is already initialized (matching Rust's `OnceLock::set`,
 *   minus recovering the rejected value -- reloco's single `error` enum
 *   carries no payload).
 * - `get_or_try_init(F)` -> `result<T *>`, where `F` is invocable as
 *   `result<T>()`: returns the existing value if already initialized,
 *   otherwise blocks concurrent callers while exactly one of them runs
 *   `F` and stores its result. If `F` fails, the cell reverts to empty so
 *   a later call (from any thread) may retry -- matching Rust's
 *   `OnceLock::get_or_try_init`.
 * - `get()`/`get_mut()` -> `T *`/`const T *`: `nullptr` if not yet
 *   initialized, never blocking.
 * - `take()` -> `result<T>`: resets the cell to empty and returns the
 *   previous value, failing with `error::not_initialized` if the cell was
 *   already empty (matching Rust's `OnceLock::take(&mut self)`, which
 *   returns `Option<T>` -- reloco represents "nothing to take" as this
 *   file's own error case instead, consistent with every other fallible
 *   reloco operation returning `result<T>`, see `error.hpp`).
 *
 * `T` must be `std::is_nothrow_move_constructible_v`, like every other
 * reloco container element requirement.
 *
 * `once_lock<T>` is neither copyable nor movable (it embeds a `mutex` +
 * `condition_variable`, matching `guarded_mutex<T>`/`mutex`/
 * `condition_variable`'s own restriction).
 *
 * `is_send<once_lock<T>>` forwards to `is_send<T>` (moving the whole,
 * empty-or-initialized cell to another thread is fine exactly when moving
 * a bare `T` would be -- though note the cell itself cannot actually be
 * moved once constructed, see above; this only matters for, e.g.,
 * `is_send<once_lock<T> *>`-style composition). `is_sync<once_lock<T>>`
 * requires both `is_send<T>` and `is_sync<T>`, matching Rust's `unsafe impl
 * <T: Send + Sync> Sync for OnceLock<T>`: unlike `guarded_mutex<T>`
 * (`Mutex<T>`, only ever reached through an exclusive lock), a `const
 * once_lock<T> &` hands out a bare `const T *` via `get()`/
 * `get_or_try_init()` once ready, so concurrent readers need `T` itself to
 * tolerate concurrent shared access.
 */

#include "alignment.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "mutex.hpp"
#include "send_sync.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief A cell that can be written at most once and read many times
 * after that, matching Rust's `std::sync::OnceLock<T>`. See the
 * file-level documentation above.
 */
template <typename T> class once_lock {
  static_assert(std::is_nothrow_move_constructible_v<T>, "once_lock<T>: T must be nothrow move constructible");
  static_assert(is_send_v<T>, "once_lock<T>: T must be Send (see send_sync.hpp) -- the cell hands ownership of T to "
                              "whichever thread wins the race to initialize it");

public:
  constexpr once_lock() noexcept = default;

  once_lock(const once_lock &) = delete;
  once_lock &operator=(const once_lock &) = delete;
  once_lock(once_lock &&) = delete;
  once_lock &operator=(once_lock &&) = delete;

  ~once_lock() noexcept {
    if (state_.load(std::memory_order_acquire) == ready)
      ptr()->~T();
  }

  /**
   * @brief Returns a pointer to the stored value, or `nullptr` if the cell
   * has not been initialized yet. Never blocks.
   */
  [[nodiscard]] const T *get() const & noexcept RELOCO_LIFETIMEBOUND {
    if (state_.load(std::memory_order_acquire) == ready)
      return ptr();
    return nullptr;
  }

  /**
   * @brief Same as `get() const`, non-`const`. The caller is responsible
   * for ensuring no other thread concurrently reads/writes the cell while
   * the returned pointer is used to mutate it, exactly like Rust's
   * `OnceLock::get_mut(&mut self)`.
   */
  [[nodiscard]] T *get_mut() & noexcept RELOCO_LIFETIMEBOUND {
    if (state_.load(std::memory_order_acquire) == ready)
      return ptr();
    return nullptr;
  }

  /**
   * @brief Initializes the cell with `value` if it is currently empty.
   *
   * Fails with `error::already_exists` if the cell is already initialized,
   * or already being initialized by a racing `try_set`/`get_or_try_init`
   * call on another thread (this call still waits for that race to
   * settle first, so it only reports `already_exists` once the outcome is
   * actually known).
   */
  [[nodiscard]] result<void> try_set(T value) noexcept {
    std::unique_lock<mutex> lock(mutex_);
    auto wait_result = cv_.wait(lock, [this] { return state_.load(std::memory_order_relaxed) != initializing; });
    if (!wait_result)
      return unexpected(wait_result.error());
    if (state_.load(std::memory_order_relaxed) == ready)
      return unexpected(error::already_exists);

    ::new (static_cast<void *>(ptr())) T(std::move(value));
    state_.store(ready, std::memory_order_release);
    lock.unlock();
    cv_.notify_all();
    return {};
  }

  /**
   * @brief Returns the stored value, initializing it first via `f()` if
   * the cell is currently empty.
   *
   * `f` must be invocable as `result<T>()`. Concurrent callers on other
   * threads block until the winning call's `f()` returns; if it fails,
   * the cell reverts to empty (this call returns `f`'s error, and a later
   * call from any thread may retry initialization), matching Rust's
   * `OnceLock::get_or_try_init`.
   */
  template <typename F> [[nodiscard]] result<T *> get_or_try_init(F &&f) noexcept(std::is_nothrow_invocable_v<F &>) {
    if (state_.load(std::memory_order_acquire) == ready)
      return ptr();

    std::unique_lock<mutex> lock(mutex_);
    auto wait_result = cv_.wait(lock, [this] { return state_.load(std::memory_order_relaxed) != initializing; });
    if (!wait_result)
      return unexpected(wait_result.error());

    if (state_.load(std::memory_order_relaxed) == ready)
      return ptr();

    state_.store(initializing, std::memory_order_relaxed);
    lock.unlock();

    result<T> init_result = f();

    lock.lock();
    if (!init_result) {
      state_.store(empty, std::memory_order_relaxed);
      lock.unlock();
      cv_.notify_all();
      return unexpected(init_result.error());
    }

    ::new (static_cast<void *>(ptr())) T(std::move(*init_result));
    state_.store(ready, std::memory_order_release);
    lock.unlock();
    cv_.notify_all();
    return ptr();
  }

  /**
   * @brief Resets the cell to empty, returning the previous value.
   * Fails with `error::not_initialized` if the cell was already empty.
   * See the file-level documentation above for the exclusivity
   * requirement this carries, matching Rust's `OnceLock::take(&mut
   * self)`.
   */
  [[nodiscard]] result<T> take() noexcept {
    std::unique_lock<mutex> lock(mutex_);
    auto wait_result = cv_.wait(lock, [this] { return state_.load(std::memory_order_relaxed) != initializing; });
    if (!wait_result)
      return unexpected(wait_result.error());
    if (state_.load(std::memory_order_relaxed) != ready)
      return unexpected(error::not_initialized);

    result<T> taken(std::move(*ptr()));
    ptr()->~T();
    state_.store(empty, std::memory_order_relaxed);
    return taken;
  }

private:
  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) T *ptr() noexcept {
    return std::addressof(storage_.value_);
  }

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) const T *ptr() const noexcept {
    return std::addressof(storage_.value_);
  }

  // See `fallible_singleton::storage_type` for why this union needs its
  // own no-op destructor: `T` may be non-trivially destructible, and a
  // union with a non-trivially-destructible alternative still needs
  // *some* user-provided destructor to remain usable as a data member at
  // all. `once_lock<T>`'s own destructor destroys `value_` explicitly
  // when `state_` is `ready`, so this destructor stays a deliberate no-op.
  union storage_type {
    constexpr storage_type() noexcept : dummy_('\0') {}
    ~storage_type() noexcept {}

    char dummy_;
    T value_;
  };

  static constexpr int empty = 0;
  static constexpr int initializing = 1;
  static constexpr int ready = 2;

  alignas(effective_alignment_v<T>) storage_type storage_{};
  mutex mutex_;
  condition_variable cv_;
  std::atomic<int> state_{empty};
};

/**
 * @brief `is_send<once_lock<T>>` forwards to `is_send<T>` -- see the
 * file-level documentation above.
 */
template <typename T> struct is_send<once_lock<T>> : is_send<T> {};

/**
 * @brief `is_sync<once_lock<T>>` requires both `is_send<T>` and
 * `is_sync<T>` -- see the file-level documentation above.
 */
template <typename T> struct is_sync<once_lock<T>> : std::bool_constant<is_send_v<T> && is_sync_v<T>> {};

} // namespace reloco
