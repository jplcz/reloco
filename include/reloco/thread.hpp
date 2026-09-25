// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file thread.hpp
 * @brief OS thread creation and Rust-like `thread::spawn`/`JoinHandle<T>`.
 *
 * Two layers, exactly like `mutex.hpp`/`guarded_mutex.hpp`:
 *
 * - **`thread`**: a raw, backend-selected OS thread handle. Constructed
 *   only through the fallible `try_spawn(function<void()> &&entry,
 *   allocator_ref)`, which reports a creation failure (e.g. a
 *   `pthread_create` `EAGAIN`, or `std::thread`'s constructor throwing
 *   `std::system_error`) as `result<thread>` instead of throwing or
 *   asserting -- unlike lock/unlock misuse elsewhere in `mutex.hpp`,
 *   running out of OS thread resources is a normal runtime condition, not
 *   a programming bug.
 * - **`spawn(F &&, allocator_ref)` / `join_handle<R>`**: the Rust-facing
 *   layer, matching `std::thread::spawn`/`std::thread::JoinHandle<T>`.
 *   Built generically on top of `thread` + `function<void()>` (see
 *   `function.hpp`), so it needs no backend-specific code of its own and
 *   works unchanged under a custom backend too.
 *
 * Backend selection mirrors `mutex.hpp` exactly:
 *
 * - **`RELOCO_THREAD_BACKEND_STD`**: wraps `<thread>`.
 * - **`RELOCO_THREAD_BACKEND_PTHREAD`**: wraps `<pthread.h>` directly.
 * - Neither defined: auto-selected -- `RELOCO_THREAD_BACKEND_PTHREAD` when
 *   `<pthread.h>` is available (`RELOCO_HAS_INCLUDE`), otherwise
 *   `RELOCO_THREAD_BACKEND_STD`.
 * - **`RELOCO_THREAD_BACKEND_CUSTOM`**: suppresses both built-in backends.
 *   An application/kernel targeting a platform with neither pthread nor a
 *   hosted `<thread>` -- an RTOS with its own task API, a freestanding
 *   target, ... -- supplies its own `reloco::thread`/`reloco::thread_id`/
 *   `reloco::this_thread::get_id()`/`reloco::this_thread::yield()`
 *   matching the same public API, in its own header, included by the
 *   application through the normal path, exactly like
 *   `RELOCO_MUTEX_BACKEND_CUSTOM` (see `mutex.hpp`):
 *
 * @code
 * // reloco_user_config.hpp
 * #define RELOCO_THREAD_BACKEND_CUSTOM
 *
 * // my_platform_thread.hpp, included normally elsewhere by the app.
 * namespace reloco {
 * class thread_id { ... };  // EqualityComparable, default-constructs to
 *                            // "no thread" (matches a default-constructed
 *                            // std::thread::id).
 * class thread {            // native_handle_type, try_spawn(function<void()>&&,
 *   ...                     // allocator_ref) -> result<thread>, joinable(),
 * };                        // join(), detach(), get_id(), native_handle().
 * namespace this_thread {
 * thread_id get_id() noexcept;
 * void yield() noexcept;
 * }
 * } // namespace reloco
 * @endcode
 *
 * `spawn`/`join_handle<R>` only ever call that same small surface, so a
 * custom backend needs no changes to either.
 *
 * Every fallible entry point returns `reloco::result<T>` (see `error.hpp`).
 *
 * Like `unique_ptr.hpp`/`function.hpp`/`rc.hpp`, this file's `namespace
 * reloco` body is wrapped in `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/
 * `RELOCO_END_UNSAFE_BUFFER_USAGE`: `try_spawn`'s bodies placement-new/
 * placement-destroy directly into raw allocator storage (the `PTHREAD`
 * backend additionally boxes `function<void()>` on the heap to cross
 * `pthread_create`'s `void *` boundary), which has no bounds-tracked
 * alternative. The public API itself never exposes a raw pointer or
 * caller-supplied storage.
 */

#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "function.hpp"
#include "lifetime.hpp"
#include "optional.hpp"
#include "send_sync.hpp"
#include "unique_ptr.hpp"

#include <new>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

#if !defined(RELOCO_THREAD_BACKEND_CUSTOM)

#if !defined(RELOCO_THREAD_BACKEND_STD) && !defined(RELOCO_THREAD_BACKEND_PTHREAD)
#if RELOCO_HAS_INCLUDE(<pthread.h>)
#define RELOCO_THREAD_BACKEND_PTHREAD 1
#else
#define RELOCO_THREAD_BACKEND_STD 1
#endif
#endif

#if defined(RELOCO_THREAD_BACKEND_PTHREAD)

#include <pthread.h>
#include <sched.h>

/**
 * @brief Opaque, `EqualityComparable` thread identity, matching
 * `std::thread::id`. Default-constructed means "no associated thread"
 * (never equal to any real thread's id, including another default-
 * constructed `thread_id`'s -- matches `std::thread::id`'s own rule).
 */
class thread_id {
public:
  constexpr thread_id() noexcept = default;

  [[nodiscard]] static thread_id current() noexcept { return thread_id(pthread_self()); }

  [[nodiscard]] friend bool operator==(const thread_id &lhs, const thread_id &rhs) noexcept {
    if (lhs.has_value_ != rhs.has_value_)
      return false;
    if (!lhs.has_value_)
      return false; // Two "no thread" ids are never equal (matches std::thread::id).
    return pthread_equal(lhs.handle_, rhs.handle_) != 0;
  }

  [[nodiscard]] friend bool operator!=(const thread_id &lhs, const thread_id &rhs) noexcept { return !(lhs == rhs); }

private:
  friend class thread;
  explicit thread_id(pthread_t handle) noexcept : has_value_(true), handle_(handle) {}

  bool has_value_ = false;
  pthread_t handle_{};
};

namespace detail {

/** @brief Heap-boxed `function<void()>` passed through `pthread_create`'s
 * single `void *` argument, freed by the trampoline once the callable has
 * run. */
struct thread_entry_box {
  function<void()> entry;
  allocator_ref alloc;
};

} // namespace detail

/**
 * @brief Raw OS thread handle backed by `pthread_t`. Move-only; asserts if
 * destroyed/reassigned while still joinable, matching `std::thread`'s own
 * "must join or detach first" contract (as an assertion trap rather than
 * an unconditional `std::terminate`, matching the rest of reloco's
 * hardened style).
 */
class thread {
public:
  using native_handle_type = pthread_t;

  constexpr thread() noexcept = default;

  thread(thread &&other) noexcept : handle_(other.handle_), joinable_(other.joinable_) { other.joinable_ = false; }

  thread &operator=(thread &&other) noexcept {
    if (this != &other) {
      RELOCO_ASSERT(!joinable_, "thread: reassigned while still joinable (call join()/detach() first)");
      handle_ = other.handle_;
      joinable_ = other.joinable_;
      other.joinable_ = false;
    }
    return *this;
  }

  thread(const thread &) = delete;
  thread &operator=(const thread &) = delete;

  ~thread() noexcept {
    RELOCO_ASSERT(!joinable_, "thread: destroyed while still joinable (call join()/detach() first)");
  }

  /**
   * @brief Spawns a new OS thread running `entry` to completion. `alloc`
   * only backs the small internal box needed to pass `entry` through
   * `pthread_create`'s `void *` argument, freed by the spawned thread
   * itself once `entry` has run.
   */
  [[nodiscard]] static RELOCO_API result<thread> try_spawn(function<void()> &&entry,
                                                           allocator_ref alloc = default_allocator()) noexcept;

  [[nodiscard]] bool joinable() const noexcept { return joinable_; }

  RELOCO_API void join() & noexcept;

  RELOCO_API void detach() & noexcept;

  [[nodiscard]] thread_id get_id() const noexcept { return joinable_ ? thread_id(handle_) : thread_id(); }

  [[nodiscard]] native_handle_type native_handle() noexcept { return handle_; }

private:
  pthread_t handle_{};
  bool joinable_ = false;
};

namespace this_thread {

[[nodiscard]] inline thread_id get_id() noexcept { return thread_id::current(); }

inline void yield() noexcept { sched_yield(); }

} // namespace this_thread

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "thread_pthread.ipp"
#endif

#elif defined(RELOCO_THREAD_BACKEND_STD)

#include <system_error>
#include <thread>

/**
 * @brief Opaque, `EqualityComparable` thread identity wrapping
 * `std::thread::id` directly.
 */
class thread_id {
public:
  constexpr thread_id() noexcept = default;

  [[nodiscard]] friend bool operator==(const thread_id &lhs, const thread_id &rhs) noexcept {
    return lhs.id_ == rhs.id_;
  }

  [[nodiscard]] friend bool operator!=(const thread_id &lhs, const thread_id &rhs) noexcept {
    return lhs.id_ != rhs.id_;
  }

private:
  friend class thread;
  explicit thread_id(std::thread::id id) noexcept : id_(id) {}

  std::thread::id id_{};
};

/**
 * @brief Raw OS thread handle wrapping `std::thread`. Move-only; asserts if
 * destroyed/reassigned while still joinable, matching `std::thread`'s own
 * "must join or detach first" contract (as an assertion trap rather than
 * an unconditional `std::terminate`, matching the rest of reloco's
 * hardened style).
 */
class thread {
public:
  using native_handle_type = std::thread::native_handle_type;

  constexpr thread() noexcept = default;

  thread(thread &&other) noexcept = default;

  thread &operator=(thread &&other) noexcept {
    if (this != &other) {
      RELOCO_ASSERT(!handle_.joinable(), "thread: reassigned while still joinable (call join()/detach() first)");
      handle_ = std::move(other.handle_);
    }
    return *this;
  }

  thread(const thread &) = delete;
  thread &operator=(const thread &) = delete;

  ~thread() noexcept {
    RELOCO_ASSERT(!handle_.joinable(), "thread: destroyed while still joinable (call join()/detach() first)");
  }

  /**
   * @brief Spawns a new OS thread running `entry` to completion. `alloc`
   * is unused by this backend (`std::thread` manages its own internal
   * storage); accepted only to keep the same signature as the `PTHREAD`
   * backend, so `spawn`/`join_handle<R>` need no backend-specific code.
   */
  [[nodiscard]] static RELOCO_API result<thread> try_spawn(function<void()> &&entry,
                                                           allocator_ref alloc = default_allocator()) noexcept;

  [[nodiscard]] bool joinable() const noexcept { return handle_.joinable(); }

  void join() & noexcept {
    RELOCO_ASSERT(handle_.joinable(), "thread: join() called on a non-joinable thread");
    handle_.join();
  }

  void detach() & noexcept {
    RELOCO_ASSERT(handle_.joinable(), "thread: detach() called on a non-joinable thread");
    handle_.detach();
  }

  [[nodiscard]] thread_id get_id() const noexcept { return thread_id(handle_.get_id()); }

  [[nodiscard]] native_handle_type native_handle() noexcept { return handle_.native_handle(); }

private:
  std::thread handle_;
};

namespace this_thread {

[[nodiscard]] inline thread_id get_id() noexcept { return thread_id(std::this_thread::get_id()); }

inline void yield() noexcept { std::this_thread::yield(); }

} // namespace this_thread

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "thread_std.ipp"
#endif

#endif // RELOCO_THREAD_BACKEND_*

#endif // !RELOCO_THREAD_BACKEND_CUSTOM

namespace detail {

template <typename R> struct thread_result_slot {
  optional<R> value;
};

} // namespace detail

template <typename R> class join_handle;

template <typename F, typename R = std::invoke_result_t<std::decay_t<F> &>>
[[nodiscard]] result<join_handle<R>> spawn(F &&f, allocator_ref alloc = default_allocator()) noexcept;

/**
 * @brief Owned handle to a spawned thread's eventual result, matching
 * Rust's `std::thread::JoinHandle<T>`. Move-only.
 *
 * Unlike Rust (where dropping a `JoinHandle` silently detaches the
 * thread), the destructor here blocks and joins if still joinable --
 * matching C++20 `std::jthread`'s safer default instead: reloco would
 * rather a caller notice an unexpectedly long-blocking destructor than
 * silently leak a still-running, unreachable thread. Call `detach()`
 * explicitly to opt in to Rust's/`std::thread`'s original behavior.
 */
template <typename R> class [[nodiscard]] RELOCO_OWNER join_handle {
public:
  constexpr join_handle() noexcept = default;

  join_handle(join_handle &&) noexcept = default;
  join_handle &operator=(join_handle &&) noexcept = default;
  join_handle(const join_handle &) = delete;
  join_handle &operator=(const join_handle &) = delete;

  ~join_handle() noexcept {
    if (thread_.joinable())
      thread_.join();
  }

  [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }

  [[nodiscard]] thread_id get_id() const noexcept { return thread_.get_id(); }

  /**
   * @brief Blocks until the thread finishes, then moves its result out.
   * Consumes `*this`. Matches Rust's `JoinHandle::join()` (minus the
   * `Result<T, Box<dyn Any + Send>>` wrapping for a panicking thread: an
   * exception escaping `F` -- or `F` itself not being `noexcept` -- calls
   * `std::terminate` here exactly like an uncaught exception escaping any
   * other thread's entry function would).
   */
  [[nodiscard]] R join() && noexcept {
    RELOCO_ASSERT(thread_.joinable(), "join_handle: already joined/detached");
    thread_.join();
    RELOCO_ASSERT(slot_->value.has_value(), "join_handle: internal error, thread produced no result");
    return std::move(slot_->value).value();
  }

  /** @brief Detaches the underlying thread, matching Rust's drop-without-
   * join behavior. Consumes `*this`; the result (if any) is never
   * retrievable afterward. */
  void detach() && noexcept {
    RELOCO_ASSERT(thread_.joinable(), "join_handle: already joined/detached");
    thread_.detach();
  }

private:
  template <typename F, typename R2> friend result<join_handle<R2>> spawn(F &&, allocator_ref) noexcept;

  thread thread_;
  unique_ptr<detail::thread_result_slot<R>> slot_;
};

/** @brief `void`-returning specialization: no result slot to retrieve. */
template <> class [[nodiscard]] RELOCO_OWNER join_handle<void> {
public:
  constexpr join_handle() noexcept = default;

  join_handle(join_handle &&) noexcept = default;
  join_handle &operator=(join_handle &&) noexcept = default;
  join_handle(const join_handle &) = delete;
  join_handle &operator=(const join_handle &) = delete;

  ~join_handle() noexcept {
    if (thread_.joinable())
      thread_.join();
  }

  [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }

  [[nodiscard]] thread_id get_id() const noexcept { return thread_.get_id(); }

  /** @brief Blocks until the thread finishes. Consumes `*this`. */
  void join() && noexcept {
    RELOCO_ASSERT(thread_.joinable(), "join_handle: already joined/detached");
    thread_.join();
  }

  /** @brief Detaches the underlying thread. Consumes `*this`. */
  void detach() && noexcept {
    RELOCO_ASSERT(thread_.joinable(), "join_handle: already joined/detached");
    thread_.detach();
  }

private:
  template <typename F, typename R2> friend result<join_handle<R2>> spawn(F &&, allocator_ref) noexcept;

  thread thread_;
};

/**
 * @brief Spawns `f` on a new OS thread, matching Rust's
 * `std::thread::spawn`. `F` (and everything it captures) must be
 * `is_send_v` (see `send_sync.hpp`); if `F`'s return type `R` is not
 * `void`, `R` must be `is_send_v` too, since `join_handle<R>::join()`
 * moves it back to the joining thread. Both are checked with
 * `static_assert` -- matching Rust's `F: Send + 'static, F::Output: Send`
 * bound on `thread::spawn` (reloco has no lifetime tracking to enforce the
 * `'static` half; do not capture a reference to a value that might not
 * outlive the spawned thread).
 */
template <typename F, typename R> [[nodiscard]] result<join_handle<R>> spawn(F &&f, allocator_ref alloc) noexcept {
  static_assert(is_send_v<std::decay_t<F>>,
                "reloco::spawn: F must be Send (see send_sync.hpp) -- it (and everything it captures) will run on "
                "another thread");
  static_assert(is_send_v<R>,
                "reloco::spawn: F's return type must be Send -- it is moved back to the joining thread by join()");

  if constexpr (std::is_void_v<R>) {
    auto entry = function<void()>::try_allocate(alloc, std::forward<F>(f));
    if (!entry)
      return unexpected(entry.error());
    auto spawned = thread::try_spawn(std::move(*entry), alloc);
    if (!spawned)
      return unexpected(spawned.error());
    join_handle<void> handle;
    handle.thread_ = std::move(*spawned);
    return handle;
  } else {
    auto slot = unique_ptr<detail::thread_result_slot<R>>::try_allocate(alloc);
    if (!slot)
      return unexpected(slot.error());
    auto *raw_slot = slot->get();
    auto entry = function<void()>::try_allocate(
        alloc, [raw_slot, captured_f = std::forward<F>(f)]() noexcept { raw_slot->value.emplace(captured_f()); });
    if (!entry)
      return unexpected(entry.error());
    auto spawned = thread::try_spawn(std::move(*entry), alloc);
    if (!spawned)
      return unexpected(spawned.error());
    join_handle<R> handle;
    handle.thread_ = std::move(*spawned);
    handle.slot_ = std::move(*slot);
    return handle;
  }
}

/**
 * @brief `join_handle<R>` is `Send` exactly when `R` is (it is moved back
 * to the joining thread by `join()`), matching Rust's `impl<T: Send> Send
 * for JoinHandle<T>`.
 */
template <typename R> struct is_send<join_handle<R>> : is_send<R> {};

/** @brief `join_handle<R>` is always `Sync`, regardless of `R` -- its own
 * queries (`joinable()`/`get_id()`) never touch `R` -- matching Rust's
 * `impl<T> Sync for JoinHandle<T>`. */
template <typename R> struct is_sync<join_handle<R>> : std::true_type {};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
