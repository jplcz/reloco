// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file wait_group.hpp
 * @brief `wait_group`, matching crossbeam-utils's `WaitGroup` (Rust
 * ecosystem, not `std`): waits for an unknown-in-advance number of
 * cloned handles to all be dropped, unlike `barrier.hpp`'s `barrier`
 * (which needs the exact participant count up front).
 *
 * A `wait_group` is `Clone` (an ordinary copy constructor, like
 * `rc<T>`/`shared_ptr<T>`'s own copy-is-clone convention): every live
 * copy -- across every thread -- represents one unit of outstanding
 * work. Create one, `wait_group other = wg;` (or move a copy into a
 * closure) once per task about to start, and let each task's copy be
 * destroyed (going out of scope, or an explicit `reset()`/reassignment)
 * when that task finishes:
 *
 * @code
 * auto wg_result = reloco::wait_group::try_create();
 * RELOCO_ASSERT(wg_result.has_value(), "wait_group allocation failed");
 * reloco::wait_group wg = std::move(*wg_result);
 *
 * for (auto &task : tasks) {
 *   reloco::wait_group clone = wg; // one outstanding unit per task
 *   reloco::spawn([clone = std::move(clone), &task]() mutable {
 *     task.run();
 *     // clone's destructor here signals this task's completion
 *   });
 * }
 *
 * std::move(wg).wait(); // blocks until every clone above has been dropped
 * @endcode
 *
 * `wait()` is `&&`-qualified -- called via `std::move(wg).wait()` --
 * matching Rust's `WaitGroup::wait(self)`, which takes `self` by value
 * (consuming/dropping the caller's own handle as part of the call): the
 * calling thread's own copy is itself one of the outstanding units, so it
 * must be relinquished before waiting for the count to reach zero, or the
 * wait could never observe zero at all. A `wait_group` that has already
 * had `wait()` called on it (or been moved from) is empty and must not be
 * copied, waited on again, or have anything but its destructor called on
 * it (`RELOCO_ASSERT`-checked).
 *
 * Built on a `shared_ptr<detail::wait_group_state>` (an atomic refcount
 * plus a `futex_word`, see `futex.hpp`) -- copying a `wait_group`
 * increments the shared count, dropping one decrements it and wakes any
 * blocked `wait()` once it reaches zero, and `wait()` itself spins on
 * (via `futex_wait`) the same word. No mutex/condition_variable involved,
 * exactly like `barrier`/`once`/`once_lock<T>`.
 *
 * `wait_group` carries no user data of its own (unlike `once_lock<T>`),
 * so it is unconditionally `Send`/`Sync` like `barrier`/`once` -- no
 * `is_send`/`is_sync` specialization is needed.
 */

#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "futex.hpp"
#include "shared_ptr.hpp"

#include <atomic>
#include <cstdint>
#include <utility>

namespace reloco {

namespace detail {

/** @brief Shared state behind every clone of one `wait_group`: an atomic
 * count of outstanding handles (starting at 1, for the handle returned by
 * `wait_group::try_create`) plus the `futex_word` used to block/wake
 * `wait()`. */
struct wait_group_state {
  wait_group_state() noexcept = default;
  wait_group_state(const wait_group_state &) = delete;
  wait_group_state &operator=(const wait_group_state &) = delete;

  futex_word count{1};
};

} // namespace detail

/**
 * @brief Waits for an unknown-in-advance number of cloned handles to all
 * be dropped, matching crossbeam-utils's `WaitGroup`. See the file-level
 * documentation above.
 */
class wait_group {
public:
  /** @brief Allocates a fresh `wait_group` with one outstanding handle
   * (this one). Fails with the allocator's own error on allocation
   * failure. */
  [[nodiscard]] static result<wait_group> try_create(allocator_ref alloc = default_allocator()) noexcept {
    auto state = try_allocate_combined_shared<detail::wait_group_state>(alloc);
    if (!state)
      return unexpected(state.error());
    return wait_group(std::move(*state));
  }

  /** @brief Clones the handle: one more outstanding unit of work, sharing
   * the same underlying count as every other clone. */
  wait_group(const wait_group &other) noexcept : state_(other.state_) {
    RELOCO_ASSERT(state_ != nullptr, "wait_group: copying an empty/already-waited-on wait_group");
    state_->count.fetch_add(1, std::memory_order_relaxed);
  }

  wait_group &operator=(const wait_group &other) noexcept {
    if (this != &other) {
      wait_group(other).swap(*this);
    }
    return *this;
  }

  wait_group(wait_group &&other) noexcept : state_(std::exchange(other.state_, shared_ptr<detail::wait_group_state>())) {}

  wait_group &operator=(wait_group &&other) noexcept {
    if (this != &other) {
      drop();
      state_ = std::exchange(other.state_, shared_ptr<detail::wait_group_state>());
    }
    return *this;
  }

  /** @brief Drops this handle: one fewer outstanding unit of work, waking
   * a concurrent `wait()` if this was the last one remaining. */
  ~wait_group() { drop(); }

  void swap(wait_group &other) noexcept { state_.swap(other.state_); }

  /**
   * @brief Consumes this handle (dropping its own outstanding unit, like
   * every other clone's destructor) and blocks until every other clone
   * has also been dropped, matching Rust's `WaitGroup::wait(self)`. Call
   * via `std::move(wg).wait()`. `RELOCO_ASSERT`-checked against calling
   * this on an already-empty (moved-from, or already-waited-on)
   * `wait_group`.
   */
  void wait() && noexcept {
    RELOCO_ASSERT(state_ != nullptr, "wait_group::wait: called on an empty/already-waited-on wait_group");
    // Keep the shared state alive locally for the duration of the wait,
    // independent of this handle's own lifetime (which ends -- logically
    // -- the moment its own outstanding unit is released just below).
    shared_ptr<detail::wait_group_state> state = std::exchange(state_, shared_ptr<detail::wait_group_state>());

    auto remaining = state->count.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) {
      futex_wake_all(state->count);
      return;
    }

    for (;;) {
      auto current = state->count.load(std::memory_order_acquire);
      if (current == 0)
        return;
      futex_wait(state->count, current);
    }
  }

private:
  explicit wait_group(shared_ptr<detail::wait_group_state> state) noexcept : state_(std::move(state)) {}

  void drop() noexcept {
    if (!state_)
      return;
    auto remaining = state_->count.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0)
      futex_wake_all(state_->count);
    state_ = nullptr;
  }

  shared_ptr<detail::wait_group_state> state_;
};

} // namespace reloco
