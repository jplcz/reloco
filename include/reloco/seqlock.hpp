#pragma once
#include <atomic>
#include <cstring>
#include <reloco/guarded_mutex.hpp>
#include <reloco/hint.hpp>
#include <reloco/lifetime.hpp>
#include <type_traits>

namespace reloco {

/**
 * @class guarded_seqlock
 * @brief Fuses `guarded_mutex` ownership with `seqlock` lock-free typestates.
 *
 * @details
 * - Writers acquire the hardware `MutexT`, blocking other writers, and automatically
 *   bump a sequence counter to warn readers.
 * - Readers bypass the `MutexT` entirely, spinning lock-free on the sequence counter
 *   using typestate transactions.
 */
template <typename T, typename MutexT = mutex> class RELOCO_CAPABILITY("mutex") guarded_seqlock {
  static_assert(std::is_trivially_copyable_v<T>, "Seqlock payload must be trivially copyable");
  static_assert(is_send_v<T>, "T must be Send (safe to transfer across threads)");

public:
  // ==========================================================================
  // WRITER API (Hardware Exclusion + Sequence Signaling)
  // ==========================================================================
  /** @brief Exclusive writer guard for a guarded seqlock. */
  class RELOCO_SCOPED_CAPABILITY write_guard {
  public:
    write_guard(write_guard &&other) noexcept RELOCO_NO_THREAD_SAFETY_ANALYSIS : cell_(other.cell_) {
      other.cell_ = nullptr;
    }

    write_guard(const write_guard &) = delete;
    write_guard &operator=(write_guard &&) = delete;
    write_guard &operator=(const write_guard &) = delete;

    ~write_guard() noexcept RELOCO_RELEASE() RELOCO_NO_THREAD_SAFETY_ANALYSIS {
      if (cell_ != nullptr) {
        // Release seqlock: Publish payload to readers and transition seq to EVEN
        cell_->seq_.fetch_add(1, std::memory_order_release);
        // Release hardware mutex: Allow the next writer to proceed
        cell_->mutex_.unlock();
      }
    }

    [[nodiscard]] T &operator*() const & noexcept RELOCO_LIFETIMEBOUND {
      RELOCO_ASSERT(cell_ != nullptr, "guard used after being moved from");
      return cell_->payload_;
    }

    [[nodiscard]] T *operator->() const & noexcept RELOCO_LIFETIMEBOUND {
      RELOCO_ASSERT(cell_ != nullptr, "guard used after being moved from");
      return &cell_->payload_;
    }

  private:
    friend class guarded_seqlock;
    explicit write_guard(const guarded_seqlock *cell) noexcept : cell_(cell) {}
    const guarded_seqlock *cell_;
  };

  /**
   * @brief Blocks until the mutex is acquired, then initiates a write transaction.
   */
  [[nodiscard]] write_guard write_lock() const & noexcept RELOCO_ACQUIRE() {
    mutex_.lock();
    seq_.fetch_add(1, std::memory_order_acquire); // Signal readers: Sequence is now ODD
    return write_guard(this);
  }

  /**
   * @brief Attempts to acquire the write lock without blocking.
   */
  [[nodiscard]] result<write_guard> try_write_lock() const & noexcept RELOCO_TRY_ACQUIRE(true) {
    if (!mutex_.try_lock())
      return unexpected(error::busy);
    seq_.fetch_add(1, std::memory_order_acquire);
    return write_guard(this);
  }

  // ==========================================================================
  // READER API (Lock-Free Typestate Transactions)
  // ==========================================================================
  /** @brief Lock-free reader transaction for a guarded seqlock snapshot. */
  class RELOCO_CONSUMABLE(unconsumed) read_tx {
  public:
    // TSA is explicitly disabled here because readers mathematically bypass the mutex.
    explicit read_tx(const guarded_seqlock &lock) noexcept
        RELOCO_RETURN_TYPESTATE(unconsumed) RELOCO_NO_THREAD_SAFETY_ANALYSIS : cell_(&lock) {
      start_seq_ = cell_->seq_.load(std::memory_order_acquire);

      // Early bailout: Only copy if the sequence is EVEN (no writer is active)
      if (start_seq_ % 2 == 0) {
        // `memcpy` of a fixed, statically-known `sizeof(T)` between two
        // `T`-sized objects (guaranteed trivially copyable by the
        // static_assert above) can't overrun either object; safe despite
        // Clang's blanket "libc call" flag.
        RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
        std::memcpy(&snapshot_, &cell_->payload_, sizeof(T));
        RELOCO_END_UNSAFE_BUFFER_USAGE
      }
    }

    [[nodiscard]] bool verify() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) {
      if (start_seq_ % 2 != 0)
        return false; // Writer was active when we started

      std::atomic_thread_fence(std::memory_order_acquire);

      // Did a writer acquire the lock while we were copying?
      return start_seq_ == cell_->seq_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] T extract() const noexcept RELOCO_CALLABLE_WHEN(unconsumed) { return snapshot_; }

  private:
    const guarded_seqlock *cell_;
    uint32_t start_seq_;
    T snapshot_;
  };

  /**
   * @brief High-level spin-read API. Automatically spins until a clean read is acquired.
   */
  [[nodiscard]] T read() const & noexcept {
    while (true) {
      if (read_tx tx(*this); tx.verify()) {
        return tx.extract();
      }
      reloco::hint::spin_loop(); // Safe CPU/Compiler yield
    }
  }

private:
  // Payload is TSA-guarded to ensure WRITERS never access it without the mutex.
  // Readers bypass this statically via NO_THREAD_SAFETY_ANALYSIS.
  mutable T payload_ RELOCO_GUARDED_BY(this);

  mutable std::atomic<uint32_t> seq_{0};
  mutable MutexT mutex_;
};

} // namespace reloco