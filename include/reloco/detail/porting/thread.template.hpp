// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file thread.template.hpp
 * @brief Documentation-only scaffold for `RELOCO_THREAD_BACKEND_CUSTOM`.
 *
 * Never `#include`d by anything -- copy this file to
 * `detail/porting/thread.hpp` (dropping `.template`), fill it in for your
 * actual target, and define `RELOCO_THREAD_BACKEND_CUSTOM` (see
 * `reloco/thread.hpp`/`reloco/reloco_config.hpp`), or point the
 * `JPLCZ_RELOCO_PORTING_HEADERS` CMake variable at a directory containing
 * your finished `thread.hpp` and let the build do both for you.
 *
 * Sketches, loosely, what a **FreeBSD kernel** port might look like --
 * backed by `kthread_add(9)` (kernel thread creation) instead of
 * `pthread_create`/`std::thread`. This is illustrative, not exact or
 * complete (real kernel-thread lifetime/teardown coordination needs more
 * care than shown here -- e.g. a real `join()` would want the spawned
 * thread to call `kthread_exit(9)` and the joiner to wait on that via its
 * own synchronization, not the bare polling sketched below), and is not
 * compiled or exercised by this repository (which targets hosted
 * userspace, not the FreeBSD kernel proper).
 *
 * `reloco::spawn`/`reloco::join_handle<R>` (see `reloco/thread.hpp`) are
 * built generically on top of just this `thread`/`thread_id`/
 * `this_thread` surface, via `function<void()>`, so a custom backend
 * needs no changes to either. `reloco::thread_builder` is deliberately
 * *not* part of this customization point at all -- see `reloco/
 * thread.hpp`'s own doc comment for why (naming/stack-sizing has no
 * portable shape to standardize over kernel task-creation APIs); a kernel
 * port that wants the same ergonomic layer defines its own
 * `thread_builder`-shaped type directly against `kthread_add(9)`'s own
 * name/stack-size parameters instead.
 */

#include <sys/param.h>

#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/systm.h>

namespace reloco {

/** @brief Opaque per-thread identity, `EqualityComparable`, default-
 * constructs to "no thread". */
class thread_id {
public:
  constexpr thread_id() noexcept = default;
  explicit constexpr thread_id(struct thread *td) noexcept : td_(td) {}

  friend constexpr bool operator==(thread_id lhs, thread_id rhs) noexcept { return lhs.td_ == rhs.td_; }
  friend constexpr bool operator!=(thread_id lhs, thread_id rhs) noexcept { return !(lhs == rhs); }

private:
  struct thread *td_ = nullptr;
};

/** @brief Raw kernel-thread handle, matching `reloco::thread`'s built-in
 * `PTHREAD`/`STD` backends' public surface. */
class thread {
public:
  using native_handle_type = struct thread *;

  /** @brief Attempts to spawn a kernel thread running @p entry.
   * @p allocator is unused here (kthread_add's own struct thread
   * allocation comes from the kernel's own zone allocator, not @p alloc)
   * but kept in the signature to match `reloco::thread::try_spawn`'s
   * shape exactly. */
  [[nodiscard]] static result<thread> try_spawn(function<void()> &&entry, allocator_ref alloc) noexcept {
    auto *boxed = /* box `entry` on the heap via `alloc`, matching the
                     PTHREAD backend's own detail::thread_entry_box --
                     omitted here, see reloco/thread_pthread.ipp */
        nullptr;
    struct thread *td = nullptr;
    int error = kthread_add(&thread_trampoline, boxed, nullptr, &td, 0, 0, "reloco-thread");
    if (error != 0)
      return unexpected(error::resource_exhausted);
    return thread(td);
  }

  thread() noexcept = default;
  explicit thread(native_handle_type td) noexcept : td_(td) {}

  thread(const thread &) = delete;
  thread &operator=(const thread &) = delete;
  thread(thread &&other) noexcept : td_(other.td_) { other.td_ = nullptr; }

  [[nodiscard]] bool joinable() const noexcept { return td_ != nullptr; }

  /** @brief A real port needs its own rendezvous here (e.g. an
   * `sema(9)`/`cv(9)` the spawned thread posts to right before
   * `kthread_exit(9)`) -- kthread_add(9) itself has no built-in "join". */
  void join() noexcept { /* wait for td_'s completion signal, then td_ = nullptr; */ }

  void detach() noexcept { td_ = nullptr; }

  [[nodiscard]] thread_id get_id() const noexcept { return thread_id(td_); }

  [[nodiscard]] native_handle_type native_handle() noexcept { return td_; }

private:
  static void thread_trampoline(void *arg) noexcept {
    // Unbox and run the entry callable (matching detail::thread_entry_box
    // in reloco/thread_pthread.ipp), then kthread_exit(9) -- never return
    // from a kthread_add(9) entry point normally.
    (void)arg;
    kthread_exit();
  }

  native_handle_type td_ = nullptr;
};

namespace this_thread {

[[nodiscard]] inline thread_id get_id() noexcept { return thread_id(curthread); }

inline void yield() noexcept { kern_yield(PRI_UNCHANGED); }

} // namespace this_thread

} // namespace reloco
