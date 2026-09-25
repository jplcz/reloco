// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file barrier.hpp
 * @brief `barrier`, matching Rust's `std::sync::Barrier`/
 * `BarrierWaitResult`: a rendezvous point that blocks every calling
 * thread until exactly `num_threads` of them have arrived, then releases
 * them all together.
 *
 * Unlike `std::barrier` (C++20, not always available -- reloco targets
 * C++17), `barrier` is reusable across an unbounded number of waves
 * (matching both `std::sync::Barrier` and `std::barrier`, unlike a
 * one-shot latch): a *generation* counter (`futex.hpp`'s `futex_word`)
 * distinguishes the wave a waiting thread belongs to, so a thread that
 * arrives immediately after the barrier releases the previous wave is
 * correctly counted into the next one instead of racing with (or being
 * woken by) the wave it just missed.
 *
 * `wait()` returns `true` for exactly one arbitrarily-chosen thread per
 * wave (matching Rust's `BarrierWaitResult::is_leader()`) and `false` for
 * every other one -- useful for having exactly one thread perform
 * once-per-wave cleanup/setup work without any extra synchronization.
 *
 * Built directly on `futex.hpp`'s `futex_word`/`futex_wait`/
 * `futex_wake_all` (arrival count and generation counter are both plain
 * atomics) rather than a `mutex` + `condition_variable` pair -- see
 * `futex.hpp` for its own backend selection (`RELOCO_FUTEX_BACKEND_*`);
 * the portable default backend already falls back to a `mutex`/
 * `condition_variable`-based "parking lot" internally, so this class
 * itself never needs a fallback of its own.
 */

#include "futex.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace reloco {

/**
 * @brief A reusable rendezvous point for exactly `num_threads` threads,
 * matching Rust's `std::sync::Barrier`.
 */
class barrier {
public:
  /**
   * @brief Constructs a barrier that releases a wave of waiting threads
   * once exactly @p num_threads of them have called `wait()`. A
   * `num_threads` of `0` behaves like `1`: every `wait()` call would
   * otherwise release immediately without ever being able to count a
   * "generation" of arrivals, matching `std::sync::Barrier::new(0)`'s own
   * documented behavior of still requiring one arrival per wave.
   */
  explicit barrier(std::size_t num_threads) noexcept : threshold_(num_threads == 0 ? 1 : num_threads) {}

  barrier(const barrier &) = delete;
  barrier &operator=(const barrier &) = delete;

  /**
   * @brief Blocks the calling thread until `threshold_` threads (across
   * the lifetime of this barrier, one wave at a time) have called
   * `wait()`, then releases every thread in that wave together. Returns
   * `true` for exactly one arbitrarily-chosen thread per wave (the
   * "leader"), matching Rust's `BarrierWaitResult::is_leader()`; every
   * other thread in the same wave observes `false`.
   */
  [[nodiscard]] bool wait() & noexcept {
    std::uint32_t generation = generation_.load(std::memory_order_acquire);
    if (arrived_.fetch_add(1, std::memory_order_acq_rel) + 1 == threshold_) {
      // Last arrival in this wave: become the leader, reset for the next
      // wave, and wake everyone else waiting on the generation we just
      // completed.
      arrived_.store(0, std::memory_order_relaxed);
      generation_.fetch_add(1, std::memory_order_release);
      futex_wake_all(generation_);
      return true;
    }

    while (generation_.load(std::memory_order_acquire) == generation) {
      futex_wait(generation_, generation);
    }
    return false;
  }

private:
  std::size_t threshold_;
  std::atomic<std::size_t> arrived_ = 0;
  futex_word generation_ = 0;
};

} // namespace reloco
