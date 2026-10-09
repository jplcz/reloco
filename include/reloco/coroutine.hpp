// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file coroutine.hpp
 * @brief `reloco::task<T>`: a lazy, move-only coroutine type whose result
 * is a `reloco::result<T>` and whose frame is allocated through
 * `reloco::default_allocator()`.
 *
 * C++20 and later only, GCC/Clang only; on anything older (or without
 * `-fcoroutines`-style support) this header is empty and
 * `RELOCO_HAS_COROUTINES` is 0.
 *
 * ## Design
 *
 * - **No exceptions.** Failure is carried by `reloco::result`, never by
 *   `throw`. A coroutine body must not let an exception escape
 *   (`unhandled_exception` traps), so build coroutine code with
 *   `-fno-exceptions` or keep it `noexcept`-clean.
 * - **No hidden heap.** The coroutine frame is allocated from
 *   `default_allocator()`, so a platform chooses the frame allocator once
 *   through the `RELOCO_DEFAULT_ALLOCATOR_CUSTOM` hook (see
 *   `default_allocator.hpp`); there is no per-coroutine allocator, no
 *   thread-local state and no per-frame header. Allocation failure never
 *   throws or crashes: the returned task is
 *   already finished and yields `error::allocation_failed`.
 * - **Errors propagate with `co_await`.** Awaiting a `result<U>` inside a
 *   `task` unwraps the value or, on error, ends the coroutine with that
 *   error and resumes the awaiting parent (the coroutine analogue of
 *   `RELOCO_TRY`; locals are destroyed normally).
 * - **Lazy.** A task does nothing until it is awaited or `resume()`d.
 */

#include "detail/compat.hpp"

#if RELOCO_CXX20 && defined(__cpp_impl_coroutine)
#define RELOCO_HAS_COROUTINES 1

#include "allocator.hpp"
#include "default_allocator.hpp"
#include "error.hpp"
#include "expected.hpp"

#include <coroutine>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

// Keeps the frame allocator out of the ramp function: once inlined, GCC sees
// the underlying free() paired with the class operator new and reports
// -Wmismatched-new-delete in user code, where it cannot be silenced.
#if defined(__GNUC__) || defined(__clang__)
#define RELOCO_CORO_NOINLINE __attribute__((noinline))
#else
#define RELOCO_CORO_NOINLINE
#endif

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T = void> class task;

namespace detail {

// Allocation and continuation bookkeeping shared by every task_promise<T>.
class coro_promise_base {
public:
  // Frames always come from default_allocator(). Only plain (non-template)
  // operator new/delete are declared on purpose: a variadic placement
  // operator new (needed to receive per-coroutine arguments such as an
  // allocator) makes GCC report a bogus -Wmismatched-new-delete against the
  // plain operator delete the compiler must call from the ramp's cleanup
  // path, and that diagnostic lands in user code where it cannot be silenced.
  RELOCO_CORO_NOINLINE static void *operator new(std::size_t size) noexcept {
    auto block = default_allocator().allocate(size, alignof(std::max_align_t));
    return block ? block.value().ptr : nullptr;
  }

  // Sized form: the compiler passes the frame size, so no header is stored.
  RELOCO_CORO_NOINLINE static void operator delete(void *frame, std::size_t size) noexcept {
    if (frame)
      default_allocator().deallocate(frame, size);
  }

  void set_continuation(std::coroutine_handle<> cont) noexcept { cont_ = cont; }

protected:
  [[nodiscard]] std::coroutine_handle<> continuation() const noexcept {
    return cont_ ? cont_ : std::noop_coroutine();
  }

private:
  std::coroutine_handle<> cont_{};
};

// Everything except `return_value`/`return_void`, which a promise must not
// declare both of and so live in the `task_promise<T>` / `task_promise<void>`
// wrappers below. `Derived` is that wrapper (the real promise type).
template <typename T, typename Derived> class task_promise_core : public coro_promise_base {
public:
  task_promise_core() noexcept {}
  task_promise_core(const task_promise_core &) = delete;
  task_promise_core &operator=(const task_promise_core &) = delete;
  ~task_promise_core() {
    if (has_)
      r_.~result<T>();
  }

  [[nodiscard]] task<T> get_return_object() noexcept;

  std::suspend_always initial_suspend() noexcept { return {}; }

  struct final_awaiter {
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Derived> self) const noexcept {
      return self.promise().continuation();
    }
    void await_resume() const noexcept {}
  };
  final_awaiter final_suspend() noexcept { return {}; }

  // Exceptions are out of scope for reloco.
  [[noreturn]] void unhandled_exception() noexcept { RELOCO_TRAP(); }

  // ---- co_await support -------------------------------------------------

  // Awaiting a result<U> yields its value; an error ends this coroutine with it.
  template <typename U> struct try_awaiter {
    result<U> r;
    task_promise_core *self;
    bool await_ready() const noexcept { return r.has_value(); }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept { return self->abandon(r.error()); }
    U await_resume() noexcept {
      if constexpr (!std::is_void_v<U>)
        return std::move(r).value();
    }
  };

  struct fail_awaiter {
    error err;
    task_promise_core *self;
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept { return self->abandon(err); }
    void await_resume() const noexcept {}
  };

  template <typename U> try_awaiter<U> await_transform(result<U> &&r) noexcept {
    return try_awaiter<U>{std::move(r), this};
  }

  fail_awaiter await_transform(unexpected<error> &&err) noexcept { return fail_awaiter{err.value(), this}; }

  // Any other awaitable (e.g. another task, a user event) is awaited as is.
  template <typename A> A &&await_transform(A &&a) noexcept { return static_cast<A &&>(a); }

  // ---- state ------------------------------------------------------------

  [[nodiscard]] bool finished() const noexcept { return has_; }

  result<T> take() noexcept {
    if (!has_)
      return unexpected(error::invalid_state);
    return std::move(r_);
  }

protected:
  template <typename... Args> void emplace_result(Args &&...args) noexcept {
    new (&r_) result<T>(std::forward<Args>(args)...);
    has_ = true;
  }

  void emplace_error(error e) noexcept { emplace_result(unexpected(e)); }

private:

  // Finishes the coroutine early from a suspension point with `e` and hands
  // control to whoever awaits it; the frame is destroyed by the owning task.
  std::coroutine_handle<> abandon(error e) noexcept {
    emplace_error(e);
    return continuation();
  }

  union {
    result<T> r_;
  };
  bool has_ = false;
};

template <typename T> class task_promise : public task_promise_core<T, task_promise<T>> {
public:
  template <typename U = T, std::enable_if_t<std::is_constructible_v<T, U &&>, int> = 0>
  void return_value(U &&value) noexcept {
    this->emplace_result(std::forward<U>(value));
  }

  void return_value(unexpected<error> &&err) noexcept { this->emplace_error(err.value()); }

  // Declared on the promise itself (not the CRTP base): some front ends only
  // accept the hook when it is a member of the exact promise type.
  static task<T> get_return_object_on_allocation_failure() noexcept;
};

template <> class task_promise<void> : public task_promise_core<void, task_promise<void>> {
public:
  void return_void() noexcept { emplace_result(); }

  static task<void> get_return_object_on_allocation_failure() noexcept;
};

} // namespace detail

/**
 * @brief Lazy, move-only coroutine returning `result<T>`.
 *
 * @code
 * reloco::task<int> parse(int v) {
 *   // A result<U> awaited here yields U, or ends this coroutine with the error.
 *   int checked = co_await validate(v);
 *   // `co_return` a plain T for success...
 *   if (checked > 100)
 *     co_return reloco::unexpected(reloco::error::out_of_range); // ...or an error.
 *   co_return checked;
 * }
 *
 * reloco::task<void> run() {
 *   // Awaiting a task yields its result<T>; await that again to propagate errors.
 *   auto r = co_await parse(7);
 *   int v = co_await std::move(r);
 *   (void)v;
 * }
 * @endcode
 *
 * Drive a top-level task with `resume()` until `done()`, then `take()`.
 * Dropping a task destroys its frame (including a suspended one).
 */
template <typename T> class [[nodiscard]] task {
public:
  using promise_type = detail::task_promise<T>;
  using value_type = T;

  task() noexcept = default;
  task(const task &) = delete;
  task &operator=(const task &) = delete;
  task(task &&other) noexcept : h_(std::exchange(other.h_, {})), err_(other.err_) {}
  task &operator=(task &&other) noexcept {
    if (this != &other) {
      reset();
      h_ = std::exchange(other.h_, {});
      err_ = other.err_;
    }
    return *this;
  }
  ~task() { reset(); }

  /** @brief True once a result (value or error) is available, or if frame allocation failed. */
  [[nodiscard]] bool done() const noexcept { return !h_ || h_.promise().finished(); }

  /** @brief Runs the coroutine until it next suspends or finishes. No-op if already done. */
  void resume() noexcept {
    if (!done())
      h_.resume();
  }

  /**
   * @brief Moves the outcome out; call after `done()`.
   * Fails with `error::allocation_failed` if the frame could not be allocated,
   * or `error::invalid_state` if the task has not finished.
   */
  [[nodiscard]] result<T> take() noexcept {
    if (!h_)
      return unexpected(err_);
    return h_.promise().take();
  }

  struct awaiter {
    task t;
    bool await_ready() const noexcept { return t.done(); }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent) noexcept {
      t.h_.promise().set_continuation(parent);
      return t.h_;
    }
    result<T> await_resume() noexcept { return t.take(); }
  };

  /** @brief Awaiting a task starts it and yields its `result<T>`. */
  [[nodiscard]] awaiter operator co_await() && noexcept { return awaiter{std::move(*this)}; }

private:
  friend class detail::task_promise_core<T, detail::task_promise<T>>;
  friend class detail::task_promise<T>;

  explicit task(std::coroutine_handle<promise_type> h) noexcept : h_(h) {}
  explicit task(error err) noexcept : err_(err) {}

  void reset() noexcept {
    if (h_) {
      h_.destroy();
      h_ = nullptr;
    }
  }

  std::coroutine_handle<promise_type> h_{};
  error err_ = error::invalid_state;
};

namespace detail {
template <typename T, typename D> task<T> task_promise_core<T, D>::get_return_object() noexcept {
  return task<T>(std::coroutine_handle<D>::from_promise(static_cast<D &>(*this)));
}
template <typename T> task<T> task_promise<T>::get_return_object_on_allocation_failure() noexcept {
  return task<T>(error::allocation_failed);
}
inline task<void> task_promise<void>::get_return_object_on_allocation_failure() noexcept {
  return task<void>(error::allocation_failed);
}
} // namespace detail

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE

#else
#define RELOCO_HAS_COROUTINES 0
#endif
