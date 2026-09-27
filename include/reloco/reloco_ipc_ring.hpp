#pragma once
#include "error.hpp"
#include "expected.hpp"
#include "span.hpp"
#include <cstdlib>
#include <type_traits>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
#include "reloco_ipc_ring.h"

namespace reloco {
// =========================================================================
// IPC PRODUCER (Writer Domain)
// =========================================================================

template <typename T> class RELOCO_POINTER ipc_producer {
  static_assert(std::is_trivially_copyable_v<T>, "IPC queue requires trivially copyable types");

  reloco_ipc_producer ctx_{};

public:
  using size_type = uint32_t;

  /**
   * @brief Mounts a shared memory page for writing.
   * @param mapped_page Raw pointer to the shared memory region.
   * @param expected_capacity The capacity verified by the OS/Hypervisor mapping size.
   */
  explicit ipc_producer(void *mapped_page, const uint32_t expected_capacity) noexcept {
    if (reloco_ipc_producer_init(&ctx_, static_cast<reloco_ipc_spsc_page *>(mapped_page), sizeof(T),
                                 expected_capacity) != 0) {
      RELOCO_ASSERT(false, "Security boundary violation");
    }
  }

  // Moving/Copying a mounted connection breaks local cached indices
  ipc_producer(const ipc_producer &) = delete;
  ipc_producer &operator=(const ipc_producer &) = delete;

  // ---- Basic API ----

  [[nodiscard]] size_type try_write(span<const T> data) & noexcept {
    return reloco_ipc_try_write(&ctx_, data.data(), static_cast<uint32_t>(data.size()));
  }

  // ---- RAII Zero-Copy Write Transaction (Typestate Protected) ----

  class RELOCO_POINTER RELOCO_CONSUMABLE(unconsumed) write_tx {
    ipc_producer *p_;
    std::pair<span<T>, span<T>> spans_;

    friend class ipc_producer;
    write_tx(ipc_producer *p, size_type min_space) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
        : p_(p), spans_(p->write_slices(min_space)) {}

  public:
    write_tx(const write_tx &) = delete;
    write_tx &operator=(const write_tx &) = delete;
    write_tx(write_tx &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : p_(other.p_), spans_(other.spans_) {
      other.p_ = nullptr;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) {
      return p_ != nullptr && (spans_.first.size() > 0);
    }

    [[nodiscard]] span<T> chunk1() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.first;
    }
    [[nodiscard]] span<T> chunk2() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.second;
    }

    void commit(size_type count) noexcept RELOCO_SET_TYPESTATE(consumed) {
      if (p_) {
        p_->commit(count);
        p_ = nullptr;
      }
    }
  };

  [[nodiscard]] write_tx begin_write(size_type min_space = 1) & noexcept RELOCO_LIFETIMEBOUND {
    return write_tx(this, min_space);
  }

private:
  // Internal Zero-Copy Math (Using the C context bounds)
  [[nodiscard]] std::pair<span<T>, span<T>> write_slices(size_type min_space) & noexcept RELOCO_LIFETIMEBOUND {
    uint64_t w = RELOCO_IPC_LOAD_RELAXED(&ctx_.page->write_idx);
    uint64_t r = ctx_.cached_read_idx;
    uint32_t cap = ctx_.capacity;
    uint64_t in_use = w - r;

    if (in_use >= cap || cap - in_use < min_space) {
      r = RELOCO_IPC_LOAD_ACQUIRE(&ctx_.page->read_idx);
      ctx_.cached_read_idx = r;
      in_use = w - r;
    }

    if (in_use > cap || cap - in_use < min_space)
      return {}; // Abort or queue full

    uint64_t available = cap - in_use;
    uint32_t physical_w = w & (cap - 1);
    uint32_t first_chunk = std::min<uint32_t>(static_cast<uint32_t>(available), cap - physical_w);

    T *data = reinterpret_cast<T *>(ctx_.page->payload);
    span<T> s1(data + physical_w, first_chunk);
    span<T> s2;
    if (first_chunk < available) {
      s2 = span<T>(data, available - first_chunk);
    }
    return {s1, s2};
  }

  void commit(const size_type count) & noexcept {
    const uint64_t w = RELOCO_IPC_LOAD_RELAXED(&ctx_.page->write_idx);
    RELOCO_IPC_STORE_RELEASE(&ctx_.page->write_idx, w + count);
  }
};

// =========================================================================
// IPC CONSUMER (Reader Domain)
// =========================================================================

template <typename T> class RELOCO_POINTER ipc_consumer {
  static_assert(std::is_trivially_copyable_v<T>, "IPC queue requires trivially copyable types");

  reloco_ipc_consumer ctx_{};

public:
  using size_type = uint32_t;

  explicit ipc_consumer(void *mapped_page, uint32_t expected_capacity) noexcept {
    if (reloco_ipc_consumer_init(&ctx_, static_cast<reloco_ipc_spsc_page *>(mapped_page), sizeof(T),
                                 expected_capacity) != 0) {
      RELOCO_ASSERT(false, "Security boundary violation");
    }
  }

  ipc_consumer(const ipc_consumer &) = delete;
  ipc_consumer &operator=(const ipc_consumer &) = delete;

  // ---- Basic API ----

  [[nodiscard]] size_type try_read(span<T> dest) & noexcept {
    return reloco_ipc_try_read(&ctx_, dest.data(), static_cast<uint32_t>(dest.size()));
  }

  // ---- RAII Zero-Copy Read Transaction (Typestate Protected) ----

  class RELOCO_POINTER RELOCO_CONSUMABLE(unconsumed) read_tx {
    ipc_consumer *c_;
    std::pair<span<const T>, span<const T>> spans_;

    friend class ipc_consumer;
    read_tx(ipc_consumer *c, size_type min_elems) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
        : c_(c), spans_(c->read_slices(min_elems)) {}

  public:
    read_tx(const read_tx &) = delete;
    read_tx &operator=(const read_tx &) = delete;
    read_tx(read_tx &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : c_(other.c_), spans_(other.spans_) {
      other.c_ = nullptr;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) {
      return c_ != nullptr && (spans_.first.size() > 0);
    }

    [[nodiscard]] span<const T> chunk1() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.first;
    }
    [[nodiscard]] span<const T> chunk2() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.second;
    }

    void consume(size_type count) noexcept RELOCO_SET_TYPESTATE(consumed) {
      if (c_) {
        c_->consume(count);
        c_ = nullptr;
      }
    }
  };

  [[nodiscard]] read_tx begin_read(size_type min_elems = 1) & noexcept RELOCO_LIFETIMEBOUND {
    return read_tx(this, min_elems);
  }

private:
  [[nodiscard]] std::pair<span<const T>, span<const T>>
  read_slices(size_type min_elems) & noexcept RELOCO_LIFETIMEBOUND {
    uint64_t r = RELOCO_IPC_LOAD_RELAXED(&ctx_.page->read_idx);
    uint64_t w = ctx_.cached_write_idx;
    uint32_t cap = ctx_.capacity;
    uint64_t available = w - r;

    if (available < min_elems) {
      w = RELOCO_IPC_LOAD_ACQUIRE(&ctx_.page->write_idx);
      ctx_.cached_write_idx = w;
      available = w - r;
    }

    if (available > cap || available < min_elems)
      return {}; // Abort or insufficient data

    uint32_t physical_r = r & (cap - 1);
    uint32_t first_chunk = std::min<uint32_t>(static_cast<uint32_t>(available), cap - physical_r);

    const T *data = reinterpret_cast<const T *>(ctx_.page->payload);
    span<const T> s1(data + physical_r, first_chunk);
    span<const T> s2;
    if (first_chunk < available) {
      s2 = span<const T>(data, available - first_chunk);
    }
    return {s1, s2};
  }

  void consume(size_type count) & noexcept {
    const uint64_t r = RELOCO_IPC_LOAD_RELAXED(&ctx_.page->read_idx);
    RELOCO_IPC_STORE_RELEASE(&ctx_.page->read_idx, r + count);
  }
};

} // namespace reloco
RELOCO_END_UNSAFE_BUFFER_USAGE
