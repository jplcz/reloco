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

class RELOCO_POINTER ipc_producer {
  reloco_ipc_producer ctx_{};

  constexpr ipc_producer() noexcept = default;

public:
  using size_type = uint32_t;

  /**
   * @brief Mounts a shared memory page for writing.
   * @param mapped_page Raw pointer to the shared memory region.
   * @param expected_capacity_bytes The capacity verified by the OS mapping size.
   * @return expected containing the mounted producer, or an error on ABI/tampering mismatch.
   */
  [[nodiscard]] static result<ipc_producer> create(void *mapped_page, uint32_t expected_capacity_bytes) noexcept {
    ipc_producer p;
    if (reloco_ipc_producer_init(&p.ctx_, static_cast<reloco_ipc_spsc_page *>(mapped_page), expected_capacity_bytes) !=
        0) {
      return unexpected(error::invalid_argument);
    }
    return p;
  }

  // Disable Copy
  ipc_producer(const ipc_producer &) = delete;
  ipc_producer &operator=(const ipc_producer &) = delete;

  // Enable Move (Required to return via reloco::result)
  ipc_producer(ipc_producer &&) noexcept = default;
  ipc_producer &operator=(ipc_producer &&) noexcept = default;

  // ---- Basic API ----

  [[nodiscard]] size_type try_write(span<const uint8_t> data) & noexcept {
    return reloco_ipc_try_write(&ctx_, data.data(), static_cast<uint32_t>(data.size()));
  }

  // ---- RAII Zero-Copy Write Transaction ----

  class RELOCO_POINTER RELOCO_CONSUMABLE(unconsumed) write_tx {
    ipc_producer *p_;
    std::pair<span<uint8_t>, span<uint8_t>> spans_;

    friend class ipc_producer;
    write_tx(ipc_producer *p, size_type min_bytes) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
        : p_(p), spans_(p->write_slices(min_bytes)) {}

  public:
    write_tx(const write_tx &) = delete;
    write_tx &operator=(const write_tx &) = delete;
    write_tx(write_tx &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : p_(other.p_), spans_(other.spans_) {
      other.p_ = nullptr;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) {
      return p_ != nullptr && (!spans_.first.empty());
    }

    [[nodiscard]] span<uint8_t> chunk1() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.first;
    }
    [[nodiscard]] span<uint8_t> chunk2() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.second;
    }

    void commit(size_type bytes) noexcept RELOCO_SET_TYPESTATE(consumed) {
      if (p_) {
        p_->commit(bytes);
        p_ = nullptr;
      }
    }
  };

  [[nodiscard]] write_tx begin_write(size_type min_bytes = 1) & noexcept RELOCO_LIFETIMEBOUND {
    return write_tx(this, min_bytes);
  }

private:
  [[nodiscard]] std::pair<span<uint8_t>, span<uint8_t>>
  write_slices(size_type min_bytes) & noexcept RELOCO_LIFETIMEBOUND {
    uint64_t w = RELOCO_IPC_LOAD_RELAXED(&ctx_.page->write_idx);
    uint64_t r = ctx_.cached_read_idx;
    uint32_t cap = ctx_.capacity;
    uint64_t in_use = w - r;

    if (in_use >= cap || cap - in_use < min_bytes) {
      r = RELOCO_IPC_LOAD_ACQUIRE(&ctx_.page->read_idx);
      ctx_.cached_read_idx = r;
      in_use = w - r;
    }

    if (in_use > cap || cap - in_use < min_bytes)
      return {};

    uint64_t available = cap - in_use;
    uint32_t physical_w = w & (cap - 1);
    uint32_t first_chunk =
        static_cast<uint32_t>(std::min<uint32_t>(static_cast<uint32_t>(available), cap - physical_w));

    uint8_t *payload = ctx_.page->payload;
    span<uint8_t> s1(payload + physical_w, first_chunk);
    span<uint8_t> s2;
    if (first_chunk < available) {
      s2 = span<uint8_t>(payload, available - first_chunk);
    }
    return {s1, s2};
  }

  void commit(size_type bytes) & noexcept {
    uint64_t w = RELOCO_IPC_LOAD_RELAXED(&ctx_.page->write_idx);
    RELOCO_IPC_STORE_RELEASE(&ctx_.page->write_idx, w + bytes);
  }
};

// =========================================================================
// IPC CONSUMER (Reader Domain)
// =========================================================================

class RELOCO_POINTER ipc_consumer {
  reloco_ipc_consumer ctx_{};

  // Private constructor
  ipc_consumer() noexcept = default;

public:
  using size_type = uint32_t;

  [[nodiscard]] static expected<ipc_consumer, error> create(void *mapped_page,
                                                            uint32_t expected_capacity_bytes) noexcept {
    ipc_consumer c;
    if (reloco_ipc_consumer_init(&c.ctx_, static_cast<reloco_ipc_spsc_page *>(mapped_page), expected_capacity_bytes) !=
        0) {
      return unexpected(error::invalid_argument);
    }
    return c;
  }

  // Disable Copy
  ipc_consumer(const ipc_consumer &) = delete;
  ipc_consumer &operator=(const ipc_consumer &) = delete;

  // Enable Move
  ipc_consumer(ipc_consumer &&) noexcept = default;
  ipc_consumer &operator=(ipc_consumer &&) noexcept = default;

  // ---- Basic API ----

  [[nodiscard]] size_type try_read(span<uint8_t> dest) & noexcept {
    return reloco_ipc_try_read(&ctx_, dest.data(), static_cast<uint32_t>(dest.size()));
  }

  // ---- RAII Zero-Copy Read Transaction ----

  class RELOCO_POINTER RELOCO_CONSUMABLE(unconsumed) read_tx {
    ipc_consumer *c_;
    std::pair<span<const uint8_t>, span<const uint8_t>> spans_;

    friend class ipc_consumer;
    read_tx(ipc_consumer *c, size_type min_bytes) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
        : c_(c), spans_(c->read_slices(min_bytes)) {}

  public:
    read_tx(const read_tx &) = delete;
    read_tx &operator=(const read_tx &) = delete;
    read_tx(read_tx &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : c_(other.c_), spans_(other.spans_) {
      other.c_ = nullptr;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) {
      return c_ != nullptr && (!spans_.first.empty());
    }

    [[nodiscard]] span<const uint8_t> chunk1() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.first;
    }
    [[nodiscard]] span<const uint8_t> chunk2() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.second;
    }

    void consume(size_type bytes) noexcept RELOCO_SET_TYPESTATE(consumed) {
      if (c_) {
        c_->consume(bytes);
        c_ = nullptr;
      }
    }
  };

  [[nodiscard]] read_tx begin_read(size_type min_bytes = 1) & noexcept RELOCO_LIFETIMEBOUND {
    return read_tx(this, min_bytes);
  }

private:
  [[nodiscard]] std::pair<span<const uint8_t>, span<const uint8_t>>
  read_slices(size_type min_bytes) & noexcept RELOCO_LIFETIMEBOUND {
    uint64_t r = RELOCO_IPC_LOAD_RELAXED(&ctx_.page->read_idx);
    uint64_t w = ctx_.cached_write_idx;
    uint32_t cap = ctx_.capacity;
    uint64_t available = w - r;

    if (available < min_bytes) {
      w = RELOCO_IPC_LOAD_ACQUIRE(&ctx_.page->write_idx);
      ctx_.cached_write_idx = w;
      available = w - r;
    }

    if (available > cap || available < min_bytes)
      return {};

    uint32_t physical_r = r & (cap - 1);
    uint32_t first_chunk = std::min<uint32_t>(static_cast<uint32_t>(available), cap - physical_r);

    const uint8_t *payload = ctx_.page->payload;
    span<const uint8_t> s1(payload + physical_r, first_chunk);
    span<const uint8_t> s2;
    if (first_chunk < available) {
      s2 = span<const uint8_t>(payload, available - first_chunk);
    }
    return {s1, s2};
  }

  void consume(size_type bytes) & noexcept {
    uint64_t r = RELOCO_IPC_LOAD_RELAXED(&ctx_.page->read_idx);
    RELOCO_IPC_STORE_RELEASE(&ctx_.page->read_idx, r + bytes);
  }
};

} // namespace reloco
RELOCO_END_UNSAFE_BUFFER_USAGE
