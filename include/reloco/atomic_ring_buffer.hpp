// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file atomic_ring_buffer.hpp
 * @brief Lock-free, single-producer/single-consumer (SPSC) ring buffer for
 * handing trivially copyable data between exactly two threads without a
 * mutex.
 *
 * `spsc_ring_buffer<T>` splits its state across two cache lines
 * (`write_idx_`/`cached_read_idx_` for the producer, `read_idx_`/
 * `cached_write_idx_` for the consumer) to prevent false sharing, and
 * requires a power-of-two capacity so every index computation can use
 * `& mask_` instead of `% cap_`. Unlike `ring_buffer<T>` (see
 * `ring_buffer.hpp`), every container in this family is unconditionally
 * non-copyable and non-movable: moving or copying an object containing
 * `std::atomic` members while another thread may be reading/writing it is
 * inherently unsound.
 *
 * `inline_spsc_ring_buffer<T, Capacity>` (embedded, zero-allocation),
 * `outline_spsc_ring_buffer<T>` (non-owning, over a caller-supplied span),
 * and `heap_spsc_ring_buffer<T>` (allocator-backed, fallible
 * `try_initialize`) are the three public leaf containers.
 *
 * See `docs/atomic-ring-buffer.md` for the full design writeup, and
 * `docs/ring-buffer.md` / `ring_buffer.hpp` for the single-threaded,
 * growable counterpart this type's frame-parsing API mirrors.
 */

#include "allocator.hpp"
#include "default_allocator.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "span.hpp"
#include <atomic>
#include <cstring>
#include <new>
#include <type_traits>

namespace reloco {

// Detect CPU cache line size to prevent False Sharing.
// C++17 provides this, but we fallback to 64 bytes for older compilers.
#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t cache_line_size = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t cache_line_size = 64;
#endif

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

/**
 * @brief The core, allocation-free algorithmic engine for the lock-free queue.
 */
template <typename T> class RELOCO_EXPORT RELOCO_POINTER spsc_ring_buffer {
  static_assert(std::is_trivially_copyable_v<T>, "Lock-free buffer requires trivially copyable types");

protected:
  // ---- PRODUCER CACHE LINE ----
  alignas(cache_line_size) std::atomic<std::size_t> write_idx_{0};
  std::size_t cached_read_idx_{0};

  // ---- CONSUMER CACHE LINE ----
  alignas(cache_line_size) std::atomic<std::size_t> read_idx_{0};
  std::size_t cached_write_idx_{0};

  // ---- SHARED CONSTANT STATE ----
  alignas(cache_line_size) T *data_{nullptr};
  std::size_t cap_{0};
  std::size_t mask_{0};

  constexpr spsc_ring_buffer(T *data, std::size_t capacity) noexcept
      : data_(data), cap_(capacity), mask_(capacity - 1) {
    if (data || capacity) {
      RELOCO_ASSERT(capacity >= 2, "SPSC capacity must be at least 2");
      RELOCO_ASSERT((capacity & (capacity - 1)) == 0, "SPSC capacity must be a power of 2");
    }
  }

public:
  using size_type = std::size_t;

  // Moving or copying an atomic structure in memory destroys thread safety.
  spsc_ring_buffer(const spsc_ring_buffer &) = delete;
  spsc_ring_buffer &operator=(const spsc_ring_buffer &) = delete;
  spsc_ring_buffer(spsc_ring_buffer &&) = delete;
  spsc_ring_buffer &operator=(spsc_ring_buffer &&) = delete;

  /**
   * @brief Constructs a trivially copyable object in-place directly inside the ring buffer.
   * Bypasses the stack entirely. (Similar to Rust's MaybeUninit placement).
   *
   * @note Marked unsafe so it's not executed without explicit approval
   */
  template <typename U, typename... Args>
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE result<U *> try_emplace(Args &&...args) & noexcept {
    static_assert(std::is_trivially_copyable_v<U>, "Object must be trivially copyable");
    static_assert(sizeof(U) % sizeof(T) == 0, "Size must align");

    const std::size_t req_elems = sizeof(U) / sizeof(T);
    auto tx = begin_write(req_elems); // Force cache refresh if needed!

    auto chunk1 = tx.chunk1();

    if (chunk1.size() < req_elems) {
      return unexpected(error::capacity_exceeded);
    }

    U *obj = new (chunk1.data()) U{std::forward<Args>(args)...};
    tx.commit(req_elems);
    return obj;
  }

  // ---- Math Helpers for Derived Classes ----
  static constexpr std::size_t round_up_power_of_2(std::size_t v) noexcept {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    if constexpr (sizeof(std::size_t) == 8)
      v |= v >> 32;
    return v + 1;
  }

  static constexpr std::size_t round_down_power_of_2(std::size_t v) noexcept {
    if (v == 0)
      return 0;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    if constexpr (sizeof(std::size_t) == 8)
      v |= v >> 32;
    return v - (v >> 1);
  }

  [[nodiscard]] size_type capacity() const noexcept { return cap_; }

  // =========================================================================
  // PRODUCER API (Only call from the Writer Thread)
  // =========================================================================

  [[nodiscard]] std::pair<span<T>, span<T>> write_slices(size_type min_space = 1) & noexcept RELOCO_LIFETIMEBOUND {
    const size_type w = write_idx_.load(std::memory_order_relaxed);
    size_type r = cached_read_idx_;

    // Demand-driven cache refresh!
    if (cap_ - (w - r) < min_space) {
      r = read_idx_.load(std::memory_order_acquire);
      cached_read_idx_ = r;
    }

    const size_type available = cap_ - (w - r);
    if (available == 0)
      return {};

    const size_type physical_w = w & mask_;
    const size_type first_chunk = std::min(available, cap_ - physical_w);

    span<T> s1(data_ + physical_w, first_chunk);
    span<T> s2;
    if (first_chunk < available) {
      s2 = span<T>(data_, available - first_chunk);
    }
    return {s1, s2};
  }

  void commit(const size_type count) & noexcept {
    const size_type w = write_idx_.load(std::memory_order_relaxed);
    write_idx_.store(w + count, std::memory_order_release);
  }

  [[nodiscard]] result<void> try_write(span<const T> data) & noexcept {
    auto [s1, s2] = write_slices(data.size()); // Request exact size!
    if (s1.size() + s2.size() < data.size()) {
      return unexpected(error::capacity_exceeded);
    }

    size_type s1_write = std::min(s1.size(), data.size());
    std::memcpy(s1.data(), data.data(), s1_write * sizeof(T));

    if (s1_write < data.size()) {
      std::memcpy(s2.data(), data.data() + s1_write, (data.size() - s1_write) * sizeof(T));
    }

    commit(data.size());
    return {};
  }

  /**
   * @brief Single-element push (Rust: `push`). Fails if queue is full.
   */
  [[nodiscard]] result<void> try_push(T value) & noexcept {
    auto [s1, s2] = write_slices(1);
    if (s1.empty())
      return unexpected(error::capacity_exceeded);
    s1[0] = value;
    commit(1);
    return {};
  }

  /**
   * @brief Writes a trivially copyable struct zero-copy.
   */
  template <typename U> [[nodiscard]] result<void> try_write_object(const U &obj) & noexcept {
    static_assert(std::is_trivially_copyable_v<U>, "Object must be trivially copyable");
    static_assert(sizeof(U) % sizeof(T) == 0, "Size must align");
    return try_write(span<const T>(reinterpret_cast<const T *>(&obj), sizeof(U) / sizeof(T)));
  }

  // ---- RAII Zero-Copy Write Transaction (Typestate Protected) ----

  class RELOCO_POINTER RELOCO_CONSUMABLE(unconsumed) write_tx {
    spsc_ring_buffer *buf_;
    std::pair<span<T>, span<T>> spans_;

    friend class spsc_ring_buffer;
    write_tx(spsc_ring_buffer *buf, size_type min_space) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
        : buf_(buf), spans_(buf->write_slices(min_space)) {}

  public:
    write_tx(const write_tx &) = delete;
    write_tx &operator=(const write_tx &) = delete;

    write_tx(write_tx &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : buf_(other.buf_), spans_(other.spans_) {
      other.buf_ = nullptr;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) {
      return buf_ != nullptr && (spans_.first.size() > 0);
    }

    [[nodiscard]] span<T> chunk1() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.first;
    }
    [[nodiscard]] span<T> chunk2() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.second;
    }
    [[nodiscard]] std::size_t available() const noexcept RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.first.size() + spans_.second.size();
    }

    void commit(size_type count) noexcept RELOCO_SET_TYPESTATE(consumed) {
      if (buf_) {
        buf_->commit(count);
        buf_ = nullptr;
      }
    }
  };

  /**
   * @brief Begins a safe, rollback-ready scatter-gather write transaction.
   */
  [[nodiscard]] write_tx begin_write(size_type min_space = 1) & noexcept RELOCO_LIFETIMEBOUND {
    return write_tx(this, min_space);
  }

  /**
   * @brief Rust-like closure injection. The callable is provided the two writable spans.
   * It must return the number of elements it successfully wrote.
   * @param func A callable matching signature: `size_type(span<T>, span<T>)`
   */
  template <typename Func> void write_with(Func &&func) & noexcept(noexcept(func(span<T>{}, span<T>{}))) {
    auto tx = begin_write();
    if (!tx)
      return;
    size_type written = func(tx.chunk1(), tx.chunk2());
    tx.commit(written);
  }

  // =========================================================================
  // CONSUMER API (Only call from the Reader Thread)
  // =========================================================================

  /**
   * @param min_elements The minimum data required. Updates the atomic cache if needed.
   */
  [[nodiscard]] std::pair<span<const T>, span<const T>>
  read_slices(size_type min_elements = 1) & noexcept RELOCO_LIFETIMEBOUND {
    const size_type r = read_idx_.load(std::memory_order_relaxed);
    size_type w = cached_write_idx_;

    // Demand-driven cache refresh!
    if (w - r < min_elements) {
      w = write_idx_.load(std::memory_order_acquire);
      cached_write_idx_ = w;
    }

    const size_type available = w - r;
    if (available == 0)
      return {};

    const size_type physical_r = r & mask_;
    const size_type first_chunk = std::min(available, cap_ - physical_r);

    span<const T> s1(data_ + physical_r, first_chunk);
    span<const T> s2;
    if (first_chunk < available) {
      s2 = span<const T>(data_, available - first_chunk);
    }
    return {s1, s2};
  }

  void consume(size_type count) & noexcept {
    const size_type r = read_idx_.load(std::memory_order_relaxed);
    read_idx_.store(r + count, std::memory_order_release);
  }

  size_type read(span<T> dest) & noexcept {
    auto [s1, s2] = read_slices(dest.size()); // Request exact size!
    if (s1.empty())
      return 0;

    size_type total_read = std::min(dest.size(), s1.size() + s2.size());
    size_type s1_read = std::min(s1.size(), total_read);

    std::memcpy(dest.data(), s1.data(), s1_read * sizeof(T));

    if (s1_read < total_read) {
      std::memcpy(dest.data() + s1_read, s2.data(), (total_read - s1_read) * sizeof(T));
    }

    consume(total_read);
    return total_read;
  }

  /**
   * @brief Single-element pop (Rust: `pop`). Fails if queue is empty.
   */
  [[nodiscard]] result<T> try_pop() & noexcept {
    auto [s1, s2] = read_slices(1);
    if (s1.empty())
      return unexpected(error::container_empty);
    T val = s1[0];
    consume(1);
    return val;
  }

  /**
   * @brief Safely reads a trivially copyable object, consuming it.
   */
  template <typename U> [[nodiscard]] result<U> try_read_object() & noexcept {
    static_assert(std::is_trivially_copyable_v<U>, "Object must be trivially copyable");
    static_assert(sizeof(U) % sizeof(T) == 0, "Size must align");

    const std::size_t req_elems = sizeof(U) / sizeof(T);
    auto [s1, s2] = read_slices(req_elems); // Force cache refresh!

    if (s1.size() + s2.size() < req_elems) {
      return unexpected(error::out_of_bounds);
    }

    U obj;
    char *dest = reinterpret_cast<char *>(&obj);
    size_type s1_read = std::min(s1.size(), req_elems);

    std::memcpy(dest, s1.data(), s1_read * sizeof(T));
    if (s1_read < req_elems) {
      std::memcpy(dest + (s1_read * sizeof(T)), s2.data(), (req_elems - s1_read) * sizeof(T));
    }

    consume(req_elems);
    return obj;
  }

  /**
   * @brief Safely reads a struct WITHOUT consuming it.
   * Perfect for checking packet headers safely over the wrap boundary.
   */
  template <typename U> [[nodiscard]] result<U> try_peek_object(size_type offset = 0) & noexcept {
    static_assert(std::is_trivially_copyable_v<U>, "Object must be trivially copyable");
    static_assert(sizeof(U) % sizeof(T) == 0, "Size must align");

    const std::size_t req_elems = sizeof(U) / sizeof(T);
    auto [s1, s2] = read_slices(offset + req_elems); // Ensure data is present!

    const std::size_t total_avail = s1.size() + s2.size();
    if (total_avail < offset + req_elems) {
      return unexpected(error::out_of_bounds);
    }

    U obj;
    char *dest = reinterpret_cast<char *>(&obj);

    if (offset < s1.size()) {
      size_type s1_avail = s1.size() - offset;
      size_type s1_read = std::min(s1_avail, req_elems);

      std::memcpy(dest, s1.data() + offset, s1_read * sizeof(T));
      if (s1_read < req_elems) {
        std::memcpy(dest + (s1_read * sizeof(T)), s2.data(), (req_elems - s1_read) * sizeof(T));
      }
    } else {
      std::memcpy(dest, s2.data() + (offset - s1.size()), req_elems * sizeof(T));
    }
    return obj;
  }

  /**
   * @brief Decodes a structured frame atomically from the lock-free queue.
   *
   * @param validator Evaluates the peeked header. Returns {is_valid, total_size}.
   * @param processor Executes if the full frame is ready.
   * @return true if a frame was consumed, false if waiting for data.
   */
  template <typename Header, typename Validator, typename Processor>
  [[nodiscard]] result<bool> try_consume_frame(Validator &&validator, Processor &&processor) & noexcept {
    static_assert(std::is_trivially_copyable_v<Header>, "Header must be trivially copyable");
    const std::size_t header_elems = sizeof(Header) / sizeof(T);

    auto hdr_res = try_peek_object<Header>();
    if (!hdr_res)
      return false;

    auto [is_valid, total_size] = validator(*hdr_res);

    if (!is_valid || total_size < header_elems) {
      return unexpected(error::invalid_argument);
    }

    // Explicitly demand the FULL frame from the cache!
    auto [s1, s2] = read_slices(total_size);
    if (s1.size() + s2.size() < total_size) {
      return false; // Still waiting for payload
    }

    std::size_t payload_elems = total_size - header_elems;
    span<const T> p1, p2;
    if (payload_elems > 0) {
      if (header_elems < s1.size()) {
        std::size_t p1_avail = s1.size() - header_elems;
        std::size_t p1_read = std::min(p1_avail, payload_elems);
        p1 = span<const T>(s1.data() + header_elems, p1_read);
        if (p1_read < payload_elems) {
          p2 = span<const T>(s2.data(), payload_elems - p1_read);
        }
      } else {
        std::size_t s2_offset = header_elems - s1.size();
        p1 = span<const T>(s2.data() + s2_offset, payload_elems);
      }
    }

    processor(*hdr_res, p1, p2);
    consume(total_size);
    return true;
  }

  // ---- RAII Zero-Copy Read Transaction (Typestate Protected) ----

  class RELOCO_POINTER RELOCO_CONSUMABLE(unconsumed) read_tx {
    spsc_ring_buffer *buf_;
    std::pair<span<const T>, span<const T>> spans_;

    friend class spsc_ring_buffer;
    explicit read_tx(spsc_ring_buffer *buf, size_type min_elements) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
        : buf_(buf), spans_(buf->read_slices(min_elements)) {}

  public:
    read_tx(const read_tx &) = delete;
    read_tx &operator=(const read_tx &) = delete;

    read_tx(read_tx &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : buf_(other.buf_), spans_(other.spans_) {
      other.buf_ = nullptr;
    }

    ~read_tx() = default;

    [[nodiscard]] constexpr explicit operator bool() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) {
      return buf_ != nullptr && (!spans_.first.empty());
    }

    [[nodiscard]] span<const T> chunk1() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.first;
    }
    [[nodiscard]] span<const T> chunk2() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.second;
    }
    [[nodiscard]] std::size_t available() const noexcept RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.first.size() + spans_.second.size();
    }

    void consume(size_type count) noexcept RELOCO_SET_TYPESTATE(consumed) {
      if (buf_) {
        buf_->consume(count);
        buf_ = nullptr;
      }
    }
  };

  /**
   * @brief Begins a safe, rollback-ready scatter-gather read transaction.
   */
  [[nodiscard]] read_tx begin_read(size_type min_elements = 1) & noexcept RELOCO_LIFETIMEBOUND {
    return read_tx(this, min_elements);
  }

  /**
   * @brief Rust-like closure extraction. The callable is provided the two readable spans.
   * It must return the number of elements it successfully consumed.
   */
  template <typename Func> void read_with(Func &&func) & noexcept(noexcept(func(span<const T>{}, span<const T>{}))) {
    auto tx = begin_read();
    if (!tx)
      return;
    size_type consumed = func(tx.chunk1(), tx.chunk2());
    tx.consume(consumed);
  }

  /**
   * @brief Drops elements from the front of the queue as long as the predicate returns true.
   * The first element that fails the predicate stops the consumption.
   * @param pred A callable `bool(const T&)`
   * @return Number of elements dropped.
   */
  template <typename Predicate> size_type consume_while(Predicate &&pred) & noexcept {
    auto tx = begin_read();
    if (!tx)
      return 0;

    size_type dropped = 0;
    for (const auto &elem : tx.chunk1()) {
      if (!pred(elem)) {
        tx.consume(dropped);
        return dropped;
      }
      ++dropped;
    }
    for (const auto &elem : tx.chunk2()) {
      if (!pred(elem))
        break;
      ++dropped;
    }
    tx.consume(dropped);
    return dropped;
  }
};

/**
 * @brief An SPSC queue allocated entirely on the stack (zero-allocation).
 * The requested capacity MUST be a power of 2, enforced at compile time.
 */
template <typename T, std::size_t Capacity> class inline_spsc_ring_buffer : public spsc_ring_buffer<T> {
  static_assert((Capacity >= 2) && ((Capacity & (Capacity - 1)) == 0), "Inline SPSC capacity must be a power of 2");

  alignas(T) std::byte storage_[Capacity * sizeof(T)];

public:
  // ReSharper disable once CppPossiblyUninitializedMember
  constexpr inline_spsc_ring_buffer() noexcept // NOLINT(*-pro-type-member-init)
      : spsc_ring_buffer<T>(reinterpret_cast<T *>(storage_), Capacity) {}

  // STRICTLY DELETED: Moving stack arrays / atomics destroys thread safety.
  inline_spsc_ring_buffer(const inline_spsc_ring_buffer &) = delete;
  inline_spsc_ring_buffer &operator=(const inline_spsc_ring_buffer &) = delete;
  inline_spsc_ring_buffer(inline_spsc_ring_buffer &&) = delete;
  inline_spsc_ring_buffer &operator=(inline_spsc_ring_buffer &&) = delete;
};

/**
 * @brief An SPSC queue mapped over raw, unowned external memory (e.g. DMA, IPC).
 * Automatically rounds the byte length DOWN to the nearest power of 2 for safety.
 */
template <typename T> class outline_spsc_ring_buffer : public spsc_ring_buffer<T> {
public:
  template <typename U>
  constexpr explicit outline_spsc_ring_buffer(span<U> memory) noexcept
      : spsc_ring_buffer<T>(const_cast<T *>(reinterpret_cast<const T *>(memory.data())),
                            spsc_ring_buffer<T>::round_down_power_of_2((memory.size() * sizeof(U)) / sizeof(T))) {
    static_assert(!std::is_const_v<U>, "Requires a mutable span for writing");
  }

  // STRICTLY DELETED: Moving control blocks destroys thread safety.
  outline_spsc_ring_buffer(const outline_spsc_ring_buffer &) = delete;
  outline_spsc_ring_buffer &operator=(const outline_spsc_ring_buffer &) = delete;
  outline_spsc_ring_buffer(outline_spsc_ring_buffer &&) = delete;
  outline_spsc_ring_buffer &operator=(outline_spsc_ring_buffer &&) = delete;
};

/**
 * @brief An SPSC queue dynamically allocated on the heap.
 * Capacity is automatically rounded up to the nearest power of 2.
 */
template <typename T> class heap_spsc_ring_buffer : public spsc_ring_buffer<T> {
  allocator_ref alloc_;

  // Private constructor invoked ONLY when allocation succeeds
  constexpr heap_spsc_ring_buffer(T *data, std::size_t cap, allocator_ref alloc) noexcept
      : spsc_ring_buffer<T>(data, cap), alloc_(alloc) {}

public:
  using size_type = typename spsc_ring_buffer<T>::size_type;

  // ---- Fallible Allocation Factories ----

  /**
   * @brief Constructs an uninitialized, inert lock-free queue.
   * You MUST call `try_initialize` before using it.
   */
  constexpr explicit heap_spsc_ring_buffer(allocator_ref alloc = default_allocator()) noexcept
      : spsc_ring_buffer<T>(nullptr, 0), alloc_(alloc) {}

  /**
   * @brief Fallibly allocates the internal memory for the queue.
   * @param requested_capacity The minimum capacity required. Will round up to power of 2.
   * @return error::invalid_argument if already initialized, or allocation error.
   */
  [[nodiscard]] result<void> try_initialize(size_type requested_capacity) noexcept {
    if (this->data_ != nullptr) {
      return unexpected(error::invalid_argument); // Prevent double initialization
    }

    size_type cap = this->round_up_power_of_2(std::max<size_type>(requested_capacity, 2));

    auto res = alloc_.allocate(cap * sizeof(T), alignof(T));
    if (!res) {
      return unexpected(res.error());
    }

    // Safely write the immutable state
    this->data_ = static_cast<T *>(res->ptr);
    this->cap_ = cap;
    this->mask_ = cap - 1;

    return {};
  }

  // ---- Lifecycle ----

  ~heap_spsc_ring_buffer() noexcept {
    if (this->data_) {
      alloc_.deallocate(this->data_, this->cap_ * sizeof(T));
    }
  }

  // STRICTLY DELETED: Moving control blocks destroys thread safety.
  heap_spsc_ring_buffer(const heap_spsc_ring_buffer &) = delete;
  heap_spsc_ring_buffer &operator=(const heap_spsc_ring_buffer &) = delete;
  heap_spsc_ring_buffer(heap_spsc_ring_buffer &&) = delete;
  heap_spsc_ring_buffer &operator=(heap_spsc_ring_buffer &&) = delete;
};

RELOCO_END_UNSAFE_BUFFER_USAGE
} // namespace reloco
