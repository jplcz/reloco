// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file commit.hpp
 * @brief Zero-allocation Software Transactional Memory (STM) and RAII guards for reloco.
 *
 * This header provides deterministic, zero-overhead transactional primitives
 * designed for hard real-time systems. They enforce safe state mutations,
 * hardware register rollbacks, and memory leak prevention at compile time.
 */

#pragma once
#include <reloco/detail/assert.hpp>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @class tx_guard
 * @brief A generic RAII transaction guard that automatically rolls back on early exit.
 *
 * @details
 * `tx_guard` acts as a Rust-style Drop Guard. It takes ownership of a payload
 * and a rollback callable. If the guard goes out of scope before `commit()`
 * is called, the rollback callable is automatically executed.
 *
 * @code
 * // Example: Safe memory allocation fallback
 * reloco::tx_guard tx{
 *     allocate_buffer(),
 *     [](Buffer* b) { free_buffer(b); } // Executes if we return early
 * };
 *
 * fill_buffer(tx.get());
 * if (hardware_fail()) return; // Automatically frees the buffer!
 *
 * Buffer* final_buf = tx.commit(); // Success, rollback disarmed.
 * @endcode
 *
 * @tparam T The type of the payload being guarded.
 * @tparam RollbackFn The callable type that executes the rollback logic.
 */
template <typename T, typename RollbackFn> class tx_guard {
public:
  /**
   * @brief Constructs a transaction guard.
   * @param payload The data to be protected and mutated.
   * @param rollback The closure executed if the transaction is aborted or dropped.
   */
  constexpr explicit tx_guard(T payload, RollbackFn rollback) noexcept
      : m_payload(std::move(payload)), m_rollback(std::move(rollback)) {}

  // Banned copying (Transactions are linear/affine types)
  tx_guard(const tx_guard &) = delete;
  tx_guard &operator=(const tx_guard &) = delete;

  // Move semantics transfer ownership and disarm the old transaction
  constexpr tx_guard(tx_guard &&other) noexcept
      : m_payload(std::move(other.m_payload)), m_rollback(std::move(other.m_rollback)), m_active(other.m_active) {
    other.m_active = false;
  }

  constexpr tx_guard &operator=(tx_guard &&other) noexcept {
    if (this != &other) {
      m_payload = std::move(other.m_payload);
      m_rollback = std::move(other.m_rollback);
      m_active = other.m_active;
      other.m_active = false;
    }
    return *this;
  }

  /**
   * @brief Destructor triggers the rollback if the transaction was not committed.
   */
  RELOCO_CONSTEXPR20 ~tx_guard() noexcept {
    if (m_active) {
      m_rollback(std::move(m_payload));
    }
  }

  /**
   * @brief Grants mutable access to the payload during the transaction.
   * @return A mutable reference to the payload.
   */
  constexpr T &get() noexcept { return m_payload; }

  /**
   * @brief Grants read-only access to the payload.
   * @return A const reference to the payload.
   */
  constexpr const T &get() const noexcept { return m_payload; }

  /**
   * @brief Commits the transaction, disarming the rollback and extracting the payload.
   * @pre The transaction must still be active (not already committed or rolled back).
   * @return The guarded payload by value.
   */
  constexpr T commit() noexcept {
    RELOCO_ASSERT(m_active, "Cannot commit an already consumed transaction");
    m_active = false;
    return std::move(m_payload);
  }

  /**
   * @brief Explicitly aborts the transaction before the scope naturally ends.
   * @pre The transaction must still be active.
   */
  constexpr void rollback() noexcept {
    RELOCO_ASSERT(m_active, "Cannot rollback an already consumed transaction");
    m_active = false;
    m_rollback(std::move(m_payload));
  }

private:
  T m_payload;
  RollbackFn m_rollback;
  bool m_active{true};
};

template <typename T, typename RollbackFn> tx_guard(T, RollbackFn) -> tx_guard<T, RollbackFn>;

/**
 * @class shadow_tx
 * @brief A deferred write-back transaction for safe global state mutation.
 *
 * @details
 * Creates a local stack copy (a "shadow") of a live target. Mutations occur
 * only on the shadow copy. The live target is overwritten via a single atomic
 * assignment only if `commit()` is called. If dropped, the shadow disappears
 * safely without modifying the live target.
 *
 * @code
 * // Example: Atomic configuration update
 * reloco::shadow_tx tx(global_config);
 * tx.get().baud_rate = 115200;
 * if (!validate(tx.get())) return; // global_config is left untouched!
 * tx.commit(); // global_config is safely updated.
 * @endcode
 *
 * @tparam T The trivially copyable type being shadowed.
 */
template <typename T> class shadow_tx {
  static_assert(std::is_trivially_copyable_v<T>, "Shadow targets must be trivially copyable");

public:
  /**
   * @brief Snapshots the live target into a local shadow copy.
   * @param live_target The active data to shadow.
   */
  explicit constexpr shadow_tx(T &live_target) noexcept : m_target(&live_target), m_shadow(live_target) {}

  /**
   * @brief Access the shadow copy for mutation.
   * @note Live data is completely isolated from these changes until commit().
   * @return A mutable reference to the shadow copy.
   */
  constexpr T &get() noexcept { return m_shadow; }
  constexpr const T &get() const noexcept { return m_shadow; }

  /**
   * @brief Overwrites the live data with the shadow copy and disarms the transaction.
   */
  constexpr void commit() noexcept {
    *m_target = m_shadow;
    m_target = nullptr; // Disarm
  }

private:
  T *m_target;
  T m_shadow;
};

template <typename T> shadow_tx(T &) -> shadow_tx<T>;

/**
 * @class checkpoint_guard
 * @brief An RAII state-restorer that reverts live data on failure.
 *
 * @details
 * Operates directly on the live target, but caches a backup upon creation.
 * If the guard goes out of scope without `commit()` being called, it instantly
 * restores the live target to the cached backup.
 *
 * @code
 * // Example: Hardware register rollback
 * reloco::checkpoint_guard tx(UART1_CONFIG_REG);
 * UART1_CONFIG_REG |= ENABLE_DMA;
 * if (dma_timeout()) return; // UART1_CONFIG_REG is automatically restored!
 * tx.commit(); // Change becomes permanent.
 * @endcode
 *
 * @tparam T The trivially copyable type being checkpointed.
 */
template <typename T> class checkpoint_guard {
  static_assert(std::is_trivially_copyable_v<T>, "Checkpoints require trivially copyable types");

public:
  /**
   * @brief Caches the initial state of the live target.
   * @param live_target The data to monitor and potentially restore.
   */
  explicit constexpr checkpoint_guard(T &live_target) noexcept
      : m_target(&live_target), m_backup(live_target) {} // Snapshot

  constexpr checkpoint_guard(checkpoint_guard &&other) noexcept
      : m_target(other.m_target), m_backup(other.m_backup), m_active(other.m_active) {
    other.m_active = false;
  }

  /**
   * @brief Destructor restores the live target to the backup state if not committed.
   */
  RELOCO_CONSTEXPR20 ~checkpoint_guard() noexcept {
    if (m_active && m_target) {
      *m_target = m_backup; // ROLLBACK: Restore the snapshot
    }
  }

  /**
   * @brief Commits the changes made to the live target, disarming the rollback.
   */
  constexpr void commit() noexcept { m_active = false; } // Disarm rollback

private:
  T *m_target;
  T m_backup;
  bool m_active{true};
};

template <typename T> checkpoint_guard(T &) -> checkpoint_guard<T>;

} // namespace reloco