/**
 * @file epoch.hpp
 * @brief Zero-allocation generation counters for ABA (stale pointer) prevention.
 *
 * @details
 * This module provides an O(1) weak-pointer equivalent designed for hard RTOS
 * environments. It uses Clang typestates (`test_typestate`) to mathematically
 * force the developer to verify an object's generation before accessing it,
 * eliminating Use-After-Free bugs in statically allocated memory pools.
 *
 * @attention **CONCURRENCY & RACE CONDITION ARCHITECTURE**
 * Generation counters provide **staleness detection**, not **mutual exclusion**.
 * Calling `handle.lock()` and `is_alive()` guarantees you do not *start* an
 * operation on a dead object, but it does **NOT** prevent an Interrupt Service
 * Routine (ISR) or another thread from preempting you and recycling the object
 * *while* you are actively reading it.
 *
 * @warning
 * To prevent "Check-Then-Act" race conditions in a concurrent environment,
 * this generation counter **MUST be structurally coupled** with an external
 * synchronization primitive or execution barrier.
 *
 * You must use one of the following three architectural patterns:
 *
 * **1. External Locking (Multi-Core / SMP)**
 * Wrap the `epoch_trackable` object in a `reloco::mutex`, `spinlock`, or `rwlock`.
 * You must acquire the lock *before* checking the epoch to ensure the memory pool
 * cannot recycle the slot while you hold the lock.
 * @code
 * auto guard = slot_mutex.lock();       // 1. Block the recycler thread
 * if (guard->current_epoch() == expected) { // 2. Verify the generation
 *     guard->do_work();                 // 3. Safe concurrent access
 * }
 * @endcode
 *
 * **2. Global IRQ Block (Single-Core Bare Metal)**
 * If reading from a timer or ISR, disable preemptions around the resolution
 * block so the RTOS scheduler cannot swap to the recycler thread.
 * @code
 * auto irq_block = reloco::system_irq_lock(); // Suspends scheduler/interrupts
 * auto guard = handle.lock();
 * if (guard.is_alive()) { guard.get().cancel(); }
 * @endcode
 *
 * **3. Thread Confinement / Deferred Action (Lock-Free)**
 * Do not resolve the handle across thread boundaries. Instead, pass the 8-byte
 * `epoch_handle` through a lock-free SPSC queue. Resolve the handle only inside
 * the worker thread that exclusively owns (and recycles) the memory pool.
 *
 * @note By intentionally decoupling the epoch (liveliness) from the lock (exclusion),
 * `reloco` ensures zero-cost abstractions—you do not pay the CPU penalty of a
 * spinlock if your architecture uses lock-free thread confinement.
 */

#pragma once
#include <atomic>
#include <cstdint>
#include <reloco/lifetime.hpp>
#include <type_traits>

namespace reloco {

using epoch_t = uint32_t;

/**
 * @class epoch_trackable
 * @brief A 4-byte mixin that tracks the memory slot's generation.
 */
class epoch_trackable {
protected:
  std::atomic<epoch_t> m_epoch;

public:
  /**
   * @brief Constructs the trackable object.
   * @param initial_epoch Allows an RTOS pool to preserve the generation across placement-new recycling.
   */
  constexpr explicit epoch_trackable(const epoch_t initial_epoch = 1) noexcept : m_epoch(initial_epoch) {}

  // Banned copy/move: The epoch is mathematically tied to the physical RAM address.
  epoch_trackable(const epoch_trackable &) = delete;
  epoch_trackable &operator=(const epoch_trackable &) = delete;

  /**
   * @brief Safely reads the current generation.
   * @note std::memory_order_acquire prevents the compiler from reading the
   * underlying data *before* the epoch is verified.
   */
  [[nodiscard]] epoch_t current_epoch() const noexcept { return m_epoch.load(std::memory_order_acquire); }

  /**
   * @brief Increments the generation. Called by the memory pool when recycling the slot.
   * @note std::memory_order_release guarantees that all object destruction
   * is committed to RAM *before* the epoch officially changes.
   */
  void bump_epoch() noexcept {
    // Read the current state
    epoch_t expected = m_epoch.load(std::memory_order_relaxed);
    epoch_t next;

    // Loop until we successfully swap expected with next
    do {
      next = expected + 1;
      if (next == 0) {
        next = 1; // Skip 0 (invalid state)
      }

      // compare_exchange_weak will update 'expected' with the true RAM value
      // if it fails, allowing the loop to immediately try again.
    } while (!m_epoch.compare_exchange_weak(expected,                  // What we think it is
                                            next,                      // What we want it to be
                                            std::memory_order_release, // Success memory order
                                            std::memory_order_relaxed  // Failure memory order
                                            ));
  }
};

template <typename T> class epoch_handle;

/**
 * @class epoch_guard
 * @brief Typestate-enforced access guard for generation handles.
 *
 * @details
 * Starts in the 'unverified' state. It is mathematically impossible to access
 * the underlying pointer unless `is_alive()` is called and returns `true`,
 * which transitions the compiler state to 'verified'.
 */
template <typename T> class RELOCO_OWNER RELOCO_CONSUMABLE(unverified) [[nodiscard]] epoch_guard {
  friend class epoch_handle<T>;
  T *m_ptr;

  constexpr explicit epoch_guard(T *p) noexcept RELOCO_RETURN_TYPESTATE(unverified) : m_ptr(p) {}

public:
  epoch_guard(const epoch_guard &) = delete;
  epoch_guard &operator=(const epoch_guard &) = delete;

  // Moving transfers the typestate gracefully
  constexpr epoch_guard(epoch_guard &&other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }

  /**
   * @brief Verifies if the object hasn't been recycled.
   * @note CLANG MAGIC: If this returns true, the object transitions to the 'verified' state.
   */
  [[nodiscard]] bool is_alive() const noexcept RELOCO_TEST_TYPESTATE(verified) { return m_ptr != nullptr; }

  /**
   * @brief Safe mutable access to the underlying object.
   * @pre You must check `if (guard.is_alive())` first!
   */
  RELOCO_UNSAFE_BUFFER_USAGE T &get() noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(verified) { return *m_ptr; }

  /**
   * @brief Safe read-only access to the underlying object.
   * @pre You must check `if (guard.is_alive())` first!
   */
  RELOCO_UNSAFE_BUFFER_USAGE const T &get() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(verified) {
    return *m_ptr;
  }

  // Syntactic sugar for direct pointer access
  RELOCO_UNSAFE_BUFFER_USAGE T *operator->() noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(verified) {
    return m_ptr;
  }

  RELOCO_UNSAFE_BUFFER_USAGE const T *operator->() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(verified) {
    return m_ptr;
  }
};

/**
 * @class epoch_handle
 * @brief An 8-byte safe handle (Weak Pointer) that tracks a specific generation of an object.
 *
 * @tparam T The object type (must inherit from epoch_trackable).
 */
template <typename T> class [[nodiscard]] epoch_handle {
  static_assert(std::is_base_of_v<epoch_trackable, T>, "T must inherit from reloco::epoch_trackable");

public:
  /** @brief Creates an empty/invalid handle. */
  constexpr epoch_handle() noexcept = default;

  /**
   * @brief Snapshots the memory address and its exact generation at this moment in time.
   */
  constexpr explicit epoch_handle(T &obj) noexcept : m_target(&obj), m_expected_epoch(obj.current_epoch()) {}

  /**
   * @brief Attempts to safely resolve the handle.
   * @return An epoch_guard which enforces a liveliness check at compile time.
   */
  [[nodiscard]] epoch_guard<T> lock() const noexcept {
    // Deterministic O(1) Check: Is the memory still holding OUR generation?
    if (m_target && m_target->current_epoch() == m_expected_epoch) {
      return epoch_guard<T>{m_target};
    }
    return epoch_guard<T>{nullptr}; // Stale/Recycled!
  }

  /** @brief Clears the handle. */
  constexpr void reset() noexcept {
    m_target = nullptr;
    m_expected_epoch = 0;
  }

  /** @brief Checks if the handle is inherently empty (ignoring object lifetime). */
  [[nodiscard]] constexpr bool is_empty() const noexcept { return m_target == nullptr; }

private:
  T *m_target{nullptr};
  epoch_t m_expected_epoch{0};
};

} // namespace reloco