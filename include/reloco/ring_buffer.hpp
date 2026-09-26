// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file ring_buffer.hpp
 * @brief Byte/POD-oriented circular buffer for streaming I/O: framing
 * protocol parsers, log ring buffers, and socket/pipe staging areas.
 *
 * `ring_buffer<T>` owns a heap allocation obtained through a
 * `reloco::allocator_ref` (see `allocator.hpp`), exactly like `vector<T>`
 * (see `vector.hpp`). `T` must be `std::is_trivially_copyable`; the API is
 * built around bulk byte transfer (`try_write`/`read`/`read_slices`/
 * `write_slices`) and zero-copy framing (`try_consume_frame`/
 * `try_write_frame_evicting`) rather than per-element construction --
 * see `vec_deque.hpp` for a per-element deque of arbitrary (non-trivial)
 * `T`.
 *
 * `unowned_trivial_ring_base` (the untyped, `void*`-based engine) and
 * `unowned_ring_base<T>` (the typed layer) are shared, out-of-line-bodied
 * (`ring_buffer.ipp`) engines behind four storage policies --
 * `heap_trivial_ring_base`, `inline_trivial_ring_base`,
 * `outline_trivial_ring_base`, `mixed_trivial_ring_base` -- backing the
 * four public containers `ring_buffer<T>`, `inline_ring_buffer<T,
 * Capacity>`, `outline_ring_buffer<T>`, `sso_ring_buffer<T,
 * InlineCapacity>`, mirroring `vector.hpp`/`inline_vector.hpp`/
 * `outline_vector.hpp`/`sso_vector.hpp` exactly (see
 * `detail/vector_base.hpp`). `ring_buffer_ref<T>` is an independent,
 * copyable cursor snapshotting another ring buffer's current state.
 *
 * See `docs/ring-buffer.md` for the full design writeup, and
 * `docs/atomic-ring-buffer.md` / `atomic_ring_buffer.hpp` for the
 * lock-free, cross-thread SPSC counterpart.
 */

#include "allocator.hpp"
#include "default_allocator.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "rvalue_safety.hpp"
#include "span.hpp"
#include <algorithm>
#include <cstring>
#include <type_traits>

namespace reloco {
namespace detail {

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

class RELOCO_EXPORT RELOCO_POINTER unowned_trivial_ring_base {
public:
  using size_type = std::size_t;

  [[nodiscard]] constexpr size_type size() const noexcept { return this->len_; }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return this->cap_; }
  [[nodiscard]] constexpr size_type free_space() const noexcept { return this->cap_ - this->len_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return this->len_ == 0; }
  [[nodiscard]] constexpr bool full() const noexcept { return this->len_ == this->cap_; }

  RELOCO_REINITIALIZES void clear() noexcept {
    this->len_ = 0;
    this->head_ = 0;
  }

  /**
   * @brief Discards the next `count` elements from the front.
   */
  void consume(size_type count) & noexcept {
    count = std::min(count, this->len_);
    this->head_ = (this->head_ + count) % this->cap_;
    this->len_ -= count;
  }

  /**
   * @brief Shrinks the buffer logically to `new_len` by dropping elements from the back.
   * If `new_len` >= `size()`, does nothing. (Rust: `truncate`)
   */
  void truncate(size_type new_len) & noexcept {
    if (new_len < this->len_) {
      this->len_ = new_len;
    }
  }

  /**
   * @brief Discards the last `count` elements from the back of the buffer.
   */
  void unwrite(size_type count) & noexcept {
    count = std::min(count, this->len_);
    this->len_ -= count;
  }

  unowned_trivial_ring_base &operator=(const unowned_trivial_ring_base &) noexcept = delete;
  unowned_trivial_ring_base(const unowned_trivial_ring_base &) noexcept = delete;

public:
  [[nodiscard]] constexpr void *raw_data() const noexcept { return this->data_; }
  [[nodiscard]] constexpr size_type raw_head() const noexcept { return this->head_; }

protected:
  void *data_ = nullptr;
  std::size_t head_ = 0;
  std::size_t len_ = 0;
  std::size_t cap_ = 0;

  constexpr unowned_trivial_ring_base() noexcept = default;

  constexpr unowned_trivial_ring_base(void *storage, const size_type capacity) noexcept
      : data_(storage), cap_(capacity) {}

  // Explicit state adoption for the cursor
  constexpr unowned_trivial_ring_base(void *storage, const size_type capacity, const size_type head,
                                      const size_type len) noexcept
      : data_(storage), head_(head), len_(len), cap_(capacity) {}

  [[nodiscard]] RELOCO_API result<void> try_reserve_base(allocator_ref alloc, std::size_t new_cap,
                                                         std::size_t elem_size, std::size_t align, void *inline_storage,
                                                         std::size_t max_inline, std::size_t max_cap) noexcept;

  // Bulk write (lossless). Fails if there isn't enough capacity.
  [[nodiscard]] RELOCO_API result<void> try_write_base(const void *src, std::size_t count,
                                                       std::size_t elem_size) noexcept;

  // Bulk write (lossy). Overwrites the oldest elements if capacity is exceeded.
  RELOCO_API void write_overwrite_base(const void *src, std::size_t count, std::size_t elem_size) noexcept;

  // Bulk read (consuming). Reads up to `count` elements into `dest`. Returns elements read.
  RELOCO_API std::size_t read_base(void *dest, std::size_t count, std::size_t elem_size) noexcept;
};

template <typename T> class RELOCO_POINTER unowned_ring_base : public unowned_trivial_ring_base {
  static_assert(std::is_trivially_copyable_v<T>, "ring_buffer is strictly for trivial types (bytes, PODs)");

protected:
  constexpr unowned_ring_base() noexcept = default;

  constexpr unowned_ring_base(void *storage, const size_type capacity) noexcept
      : unowned_trivial_ring_base(storage, capacity) {}

  constexpr unowned_ring_base(void *storage, const size_type capacity, const size_type head,
                              const size_type len) noexcept
      : unowned_trivial_ring_base(storage, capacity, head, len) {}

public:
  using value_type = std::remove_cv_t<T>;
  using reference = T &;
  using const_reference = const T &;

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  // ---- Bulk Mutation ----

  unowned_ring_base &operator=(const unowned_ring_base &) noexcept = delete;
  unowned_ring_base(const unowned_ring_base &) noexcept = delete;

  /**
   * @brief Attempts to write the entire span into the buffer.
   * Fails if there is not enough free space.
   */
  [[nodiscard]] result<void> try_write(span<const T> data) & noexcept {
    return this->try_write_base(data.data(), data.size(), sizeof(T));
  }

  /**
   * @brief Writes data into the buffer. If the buffer fills up, it overwrites
   * the oldest elements and advances the head. Perfect for streaming logs.
   */
  void write_overwrite(span<const T> data) & noexcept {
    this->write_overwrite_base(data.data(), data.size(), sizeof(T));
  }

  /**
   * @brief Reads and consumes up to `dest.size()` elements from the buffer.
   * Returns the actual number of elements read.
   */
  size_type read(span<T> dest) & noexcept { return this->read_base(dest.data(), dest.size(), sizeof(T)); }

  // ---- Direct Buffer Access (Zero-Copy Networking/IO) ----

  /**
   * @brief Returns the contiguous slices of data available to read, up to a specified limit.
   * Typically passed directly to `writev` / `WSASend` / socket APIs.
   *
   * @param limit Maximum number of elements to expose (defaults to all available data).
   */
  [[nodiscard]] std::pair<span<const T>, span<const T>>
  read_slices(size_type limit = static_cast<size_type>(-1)) const & noexcept {
    std::size_t to_read = std::min(this->len_, limit);

    if (to_read == 0) {
      return {};
    }

    const T *typed_data = static_cast<const T *>(this->data_);
    std::size_t logical_tail = this->head_ + to_read;

    if (logical_tail <= this->cap_) {
      // The requested length fits entirely within the first physical chunk
      return {span<const T>(typed_data + this->head_, to_read), {}};
    } else {
      // The requested length crosses the wrap-around boundary
      std::size_t first_chunk = this->cap_ - this->head_;
      std::size_t second_chunk = logical_tail - this->cap_;

      return {span<const T>(typed_data + this->head_, first_chunk), span<const T>(typed_data, second_chunk)};
    }
  }

  /**
   * @brief Returns the contiguous slices of free space available to write into.
   * Pass these to `readv` / `WSARecv` to DMA directly into the ring buffer!
   */
  [[nodiscard]] std::pair<span<T>, span<T>> write_slices() & noexcept {
    std::size_t free = this->free_space();
    if (free == 0)
      return {span<T>(), span<T>()};

    T *typed_data = static_cast<T *>(this->data_);
    std::size_t tail = this->head_ + this->len_;
    if (tail >= this->cap_)
      tail -= this->cap_;

    if (tail + free <= this->cap_) {
      return {span<T>(typed_data + tail, free), span<T>()};
    } else {
      return {span<T>(typed_data + tail, this->cap_ - tail), span<T>(typed_data, free - (this->cap_ - tail))};
    }
  }

  // ---- Single Element Mutation ----

  /**
   * @brief Pushes a single element, overwriting the oldest if the buffer is full.
   * Does NOT allocate.
   */
  void push_back_overwrite(T value) & noexcept {
    if (this->cap_ == 0)
      return;

    std::size_t tail = this->head_ + this->len_;
    if (tail >= this->cap_)
      tail -= this->cap_;

    static_cast<T *>(this->data_)[tail] = value;

    if (this->len_ < this->cap_) {
      ++this->len_;
    } else {
      // Buffer was full; advance head to drop the oldest element
      this->head_ = (this->head_ + 1 == this->cap_) ? 0 : this->head_ + 1;
    }
  }

  /**
   * @brief Removes and returns the first element if the buffer is not empty.
   * (Rust: `pop_front`)
   */
  [[nodiscard]] result<T> try_pop_front() & noexcept {
    if (this->len_ == 0)
      return unexpected(error::container_empty);
    T val = static_cast<const T *>(this->data_)[this->head_];
    this->head_ = (this->head_ + 1 == this->cap_) ? 0 : this->head_ + 1;
    --this->len_;
    return val;
  }

  /**
   * @brief Removes and returns the last element if the buffer is not empty.
   * (Rust: `pop_back`)
   */
  [[nodiscard]] result<T> try_pop_back() & noexcept {
    if (this->len_ == 0)
      return unexpected(error::container_empty);
    std::size_t tail = this->head_ + this->len_ - 1;
    if (tail >= this->cap_)
      tail -= this->cap_;
    T val = static_cast<const T *>(this->data_)[tail];
    --this->len_;
    return val;
  }

  /**
   * @brief Pushes to the front. Overwrites the NEWEST (back) element if full.
   * Useful for "keep the most recent N elements" streams where pushing front
   * drops the oldest from the back.
   */
  void push_front_overwrite(T value) & noexcept {
    if (this->cap_ == 0)
      return;

    this->head_ = (this->head_ == 0) ? this->cap_ - 1 : this->head_ - 1;
    static_cast<T *>(this->data_)[this->head_] = value;

    if (this->len_ < this->cap_) {
      ++this->len_;
    }
    // If full, moving head backwards inherently drops the element at the tail!
  }

  // ---- Element Access ----

  [[nodiscard]] T &operator[](size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < this->len_, "ring_buffer index out of bounds");
    std::size_t physical = this->head_ + index;
    if (physical >= this->cap_)
      physical -= this->cap_;
    return static_cast<T *>(this->data_)[physical];
  }

  [[nodiscard]] const T &operator[](size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < this->len_, "ring_buffer index out of bounds");
    std::size_t physical = this->head_ + index;
    if (physical >= this->cap_)
      physical -= this->cap_;
    return static_cast<const T *>(this->data_)[physical];
  }

  [[nodiscard]] T &front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(this->len_ > 0, "ring_buffer is empty");
    return static_cast<T *>(this->data_)[this->head_];
  }

  [[nodiscard]] const T &front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(this->len_ > 0, "ring_buffer is empty");
    return static_cast<const T *>(this->data_)[this->head_];
  }

  [[nodiscard]] T &back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(this->len_ > 0, "ring_buffer is empty");
    std::size_t tail = this->head_ + this->len_ - 1;
    if (tail >= this->cap_)
      tail -= this->cap_;
    return static_cast<T *>(this->data_)[tail];
  }

  [[nodiscard]] const T &back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(this->len_ > 0, "ring_buffer is empty");
    std::size_t tail = this->head_ + this->len_ - 1;
    if (tail >= this->cap_)
      tail -= this->cap_;
    return static_cast<const T *>(this->data_)[tail];
  }

  /**
   * @brief Bounds-checked access to the element at `index`.
   * Returns a pointer to the element on success, or `error::out_of_bounds`.
   */
  [[nodiscard]] result<T *> try_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= this->len_)
      return unexpected(error::out_of_bounds);
    std::size_t physical = this->head_ + index;
    if (physical >= this->cap_)
      physical -= this->cap_;
    return static_cast<T *>(this->data_) + physical;
  }

  [[nodiscard]] result<const T *> try_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= this->len_)
      return unexpected(error::out_of_bounds);
    std::size_t physical = this->head_ + index;
    if (physical >= this->cap_)
      physical -= this->cap_;
    return static_cast<const T *>(this->data_) + physical;
  }

  [[nodiscard]] result<T *> try_front() & noexcept RELOCO_LIFETIMEBOUND {
    if (this->len_ == 0)
      return unexpected(error::container_empty);
    return static_cast<T *>(this->data_) + this->head_;
  }

  [[nodiscard]] result<T *> try_back() & noexcept RELOCO_LIFETIMEBOUND {
    if (this->len_ == 0)
      return unexpected(error::container_empty);
    std::size_t tail = this->head_ + this->len_ - 1;
    if (tail >= this->cap_)
      tail -= this->cap_;
    return static_cast<T *>(this->data_) + tail;
  }

  /**
   * @brief Reorders the physical buffer so that all elements are contiguous,
   * returning a single span. Uses zero allocations. (Rust: `make_contiguous`)
   */
  span<T> make_contiguous() & noexcept RELOCO_LIFETIMEBOUND {
    if (this->len_ <= 1) {
      if (this->len_ == 1 && this->head_ > 0) {
        T *typed_data = static_cast<T *>(this->data_);
        typed_data[0] = typed_data[this->head_];
      }
      this->head_ = 0;
      return span<T>(static_cast<T *>(this->data_), this->len_);
    }

    std::size_t tail = this->head_ + this->len_;
    T *typed_data = static_cast<T *>(this->data_);

    // Already contiguous, but not at the front.
    // Slide it left to unify all free space at the back.
    if (tail <= this->cap_) {
      if (this->head_ > 0) {
        std::memmove(typed_data, typed_data + this->head_, this->len_ * sizeof(T));
        this->head_ = 0;
      }
      return span<T>(typed_data, this->len_);
    }

    // Wrapped. We have two chunks: [head_, cap_) and [0, tail - cap_).
    // std::rotate perfectly glues Chunk 2 right after Chunk 1 and shifts the head to 0.
    std::rotate(typed_data, typed_data + this->head_, typed_data + this->cap_);

    this->head_ = 0;
    return span<T>(typed_data, this->len_);
  }

  // ---- Heterogeneous Object I/O ----

  /**
   * @brief Writes a trivially copyable object (like a struct) directly into the buffer.
   * Fails if there is not enough free space. Does NOT allocate.
   */
  template <typename U> [[nodiscard]] result<void> try_write_object(const U &obj) & noexcept {
    static_assert(std::is_trivially_copyable_v<U>, "Object must be trivially copyable to serialize");
    static_assert(sizeof(U) % sizeof(T) == 0, "Object size must be a multiple of buffer element size");
    return this->try_write_base(&obj, sizeof(U) / sizeof(T), sizeof(T));
  }

  /**
   * @brief Writes a trivially copyable object, overwriting the oldest data if the buffer is full.
   */
  template <typename U> void write_object_overwrite(const U &obj) & noexcept {
    static_assert(std::is_trivially_copyable_v<U>, "Object must be trivially copyable to serialize");
    static_assert(sizeof(U) % sizeof(T) == 0, "Object size must be a multiple of buffer element size");
    this->write_overwrite_base(&obj, sizeof(U) / sizeof(T), sizeof(T));
  }

  /**
   * @brief Writes a span of a different trivially copyable type into the buffer.
   * e.g., Pushing `span<const uint32_t>` into a `ring_buffer<uint8_t>`.
   */
  template <typename U> [[nodiscard]] result<void> try_write_span(span<const U> data) & noexcept {
    static_assert(std::is_trivially_copyable_v<U>, "Span elements must be trivially copyable");
    static_assert(sizeof(U) % sizeof(T) == 0, "Span element size must be a multiple of buffer element size");
    return this->try_write_base(data.data(), (data.size() * sizeof(U)) / sizeof(T), sizeof(T));
  }

  /**
   * @brief Reads a trivially copyable object from the buffer.
   * Consumes the bytes and returns the instantiated object. Fails if there aren't enough bytes.
   * This safely avoids strict-aliasing and alignment UB by using memcpy internally.
   */
  template <typename U> [[nodiscard]] result<U> try_read_object() & noexcept {
    static_assert(std::is_trivially_copyable_v<U>, "Object must be trivially copyable to deserialize");
    static_assert(sizeof(U) % sizeof(T) == 0, "Object size must be a multiple of buffer element size");

    const std::size_t required_elements = sizeof(U) / sizeof(T);
    if (this->len_ < required_elements)
      return unexpected(error::out_of_bounds);

    U obj;
    this->read_base(&obj, required_elements, sizeof(T));
    return obj;
  }

  /**
   * @brief Reads a trivially copyable object from the buffer without consuming it.
   * Useful for reading packet headers to determine payload sizes before extracting.
   * @param element_offset The logical index to start peeking from (defaults to 0 / front).
   */
  template <typename U> [[nodiscard]] result<U> try_peek_object(size_type element_offset = 0) const & noexcept {
    static_assert(std::is_trivially_copyable_v<U>, "Object must be trivially copyable to peek");
    static_assert(sizeof(U) % sizeof(T) == 0, "Object size must be a multiple of buffer element size");

    const std::size_t required_elements = sizeof(U) / sizeof(T);
    if (this->len_ < element_offset + required_elements) {
      return unexpected(error::out_of_bounds);
    }

    U obj;
    char *dest_bytes = reinterpret_cast<char *>(&obj);
    const char *src_bytes = static_cast<const char *>(this->data_);

    std::size_t read_head = this->head_ + element_offset;
    if (read_head >= this->cap_)
      read_head %= this->cap_;

    std::size_t first_chunk = std::min(required_elements, this->cap_ - read_head);
    std::memcpy(dest_bytes, src_bytes + read_head * sizeof(T), first_chunk * sizeof(T));

    if (first_chunk < required_elements) {
      std::memcpy(dest_bytes + first_chunk * sizeof(T), src_bytes, (required_elements - first_chunk) * sizeof(T));
    }

    return obj;
  }

  // ---- Stream Parsing & Frame Decoding ----

  /**
   * @brief Probes for a structured frame, validates it, and extracts the payload if complete.
   *
   * @param validator A callable `std::pair<bool, size_type> (const Header&)`:
   *        - On success: returns `{true, total_frame_elements}`.
   *        - On failure (corrupted): returns `{false, elements_to_skip}` to frame-hunt.
   * @param processor A callable `void (const Header&, span<const T> chunk1, span<const T> chunk2)`
   *        executed ONLY if the entire frame has arrived.
   *
   * @return `result<bool>`:
   *         - `true` if the buffer state advanced (a frame was processed OR corrupted bytes skipped).
   *         - `false` if we are waiting for more data.
   *         - `error::invalid_argument` if the validator approved an impossibly small frame size.
   */
  template <typename Header, typename Validator, typename Processor>
  [[nodiscard]] result<bool> try_consume_frame(Validator &&validator, Processor &&processor) & noexcept {
    static_assert(std::is_trivially_copyable_v<Header>, "Header must be trivially copyable");
    const std::size_t header_elems = sizeof(Header) / sizeof(T);

    if (this->len_ < header_elems)
      return false;

    // Peek the header safely
    auto hdr_res = this->try_peek_object<Header>();
    if (!hdr_res)
      return false;

    // Validate integrity and get expected size
    auto [is_valid, size_or_skip] = validator(*hdr_res);

    if (!is_valid) {
      // Header is corrupted or magic mismatched. Skip penalty bytes.
      this->clear();
      return reloco::unexpected(error::invalid_argument);
    }

    // Catch malicious/corrupted length fields without asserting
    if (size_or_skip < header_elems) {
      this->clear(); // Wipe on impossible lengths too
      return reloco::unexpected(error::invalid_argument);
    }

    if (this->len_ < size_or_skip) {
      return false; // Header is valid, but payload hasn't fully arrived yet. Wait.
    }

    // We have the full frame! Calculate the payload slices.
    std::size_t payload_elems = size_or_skip - header_elems;
    std::size_t payload_head = (this->head_ + header_elems) % this->cap_;

    span<const T> chunk1, chunk2;
    if (payload_elems > 0) {
      const T *typed_data = static_cast<const T *>(this->data_);
      std::size_t first_chunk = std::min(payload_elems, this->cap_ - payload_head);

      chunk1 = span<const T>(typed_data + payload_head, first_chunk);
      if (first_chunk < payload_elems) {
        chunk2 = span<const T>(typed_data, payload_elems - first_chunk);
      }
    }

    // Dispatch the un-consumed payload to the user
    processor(*hdr_res, chunk1, chunk2);

    // Consume the entire frame automatically
    this->consume(size_or_skip);
    return true;
  }

  /**
   * @brief Probes for a structured frame and extracts the payload WITHOUT consuming it.
   * Because this is a const operation, if corruption is detected, it returns an error
   * but cannot automatically clear the buffer.
   *
   * @param validator A callable `std::pair<bool, size_type> (const Header&)`
   * @param processor A callable `void (const Header&, span<const T> chunk1, span<const T> chunk2)`
   *
   * @return `result<bool>`:
   *         - `true` if a frame was successfully peeked.
   *         - `false` if we are waiting for more data.
   *         - `error::invalid_argument` if corruption is detected.
   */
  template <typename Header, typename Validator, typename Processor>
  [[nodiscard]] result<bool> try_peek_frame(Validator &&validator, Processor &&processor) const & noexcept {
    static_assert(std::is_trivially_copyable_v<Header>, "Header must be trivially copyable");
    const std::size_t header_elems = sizeof(Header) / sizeof(T);

    if (this->len_ < header_elems)
      return false;

    // Peek the header safely
    auto hdr_res = this->try_peek_object<Header>();
    if (!hdr_res)
      return false;

    // Validate integrity and get expected size
    auto [is_valid, size_or_skip] = validator(*hdr_res);

    if (!is_valid || size_or_skip < header_elems) {
      // Corruption detected! Cannot self-heal because this method is const.
      return reloco::unexpected(error::invalid_argument);
    }

    // Check if the full frame is here
    if (this->len_ < size_or_skip) {
      return false; // Wait for more data
    }

    // We have the full frame! Calculate the payload slices.
    std::size_t payload_elems = size_or_skip - header_elems;
    std::size_t payload_head = (this->head_ + header_elems) % this->cap_;

    span<const T> chunk1, chunk2;
    if (payload_elems > 0) {
      const T *typed_data = static_cast<const T *>(this->data_);
      std::size_t first_chunk = std::min(payload_elems, this->cap_ - payload_head);

      chunk1 = span<const T>(typed_data + payload_head, first_chunk);
      if (first_chunk < payload_elems) {
        chunk2 = span<const T>(typed_data, payload_elems - first_chunk);
      }
    }

    // Dispatch the payload to the user
    processor(*hdr_res, chunk1, chunk2);

    // Return success WITHOUT consuming!
    return true;
  }

  /**
   * @brief Atomically writes a header and payload. If space is insufficient, it cleanly
   * evicts COMPLETE older frames from the front using the provided validator, avoiding shredded data.
   *
   * @param validator The exact same callable used in `try_consume_frame` to determine frame sizes.
   */
  template <typename Header, typename PayloadType, typename Validator>
  [[nodiscard]] result<void> try_write_frame_evicting(const Header &header, span<const PayloadType> payload,
                                                      Validator &&validator) & noexcept {

    static_assert(std::is_trivially_copyable_v<Header>, "Header must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<PayloadType>, "Payload must be trivially copyable");
    static_assert(sizeof(Header) % sizeof(T) == 0, "Header size must align with buffer element size");
    static_assert(sizeof(PayloadType) % sizeof(T) == 0, "Payload size must align with buffer element size");

    const std::size_t header_elems = sizeof(Header) / sizeof(T);
    const std::size_t payload_elems = (payload.size() * sizeof(PayloadType)) / sizeof(T);
    const std::size_t total_elems = header_elems + payload_elems;

    // A frame physically cannot fit if it's larger than the entire ring buffer capacity
    if (total_elems > this->cap_) {
      return unexpected(error::capacity_exceeded);
    }

    // Evict complete frames until we have enough contiguous free space
    while (this->free_space() < total_elems) {
      if (this->len_ < header_elems) {
        // Less than a header remains, must be garbage. Wipe it.
        this->clear();
        break;
      }

      auto hdr_res = this->try_peek_object<Header>();
      if (!hdr_res) {
        this->clear();
        break;
      }

      auto [is_valid, size_or_skip] = validator(*hdr_res);

      if (!is_valid || size_or_skip < header_elems) {
        // Corrupted frame at the front! Don't try to hunt.
        // Wipe the entire buffer so the new frame starts on a clean slate.
        this->clear();
        break;
      }

      // Valid frame! Safely evict the ENTIRE frame.
      std::size_t to_drop = std::min<std::size_t>(this->len_, size_or_skip);
      this->consume(to_drop);
    }

    // Space is now guaranteed. Write losslessly!
    // Using try_write_base here instead of overwrite_base guarantees we don't accidentally shred data.
    std::ignore = this->try_write_base(&header, header_elems, sizeof(T));
    if (payload_elems > 0) {
      std::ignore = this->try_write_base(payload.data(), payload_elems, sizeof(T));
    }

    return {};
  }

  // ---- Delimiter Searching ----

  /**
   * @brief Searches for a specific value (delimiter) in the buffer.
   * @param value The element to search for (e.g., '\n').
   * @param offset Logical index to start searching from (defaults to 0).
   * @return The logical index of the value if found, or empty if not found.
   */
  [[nodiscard]] std::optional<size_type> find(const T &value, size_type offset = 0) const & noexcept {
    if (offset >= this->len_)
      return std::nullopt;

    const T *typed_data = static_cast<const T *>(this->data_);
    std::size_t physical_start = (this->head_ + offset) % this->cap_;
    std::size_t remaining = this->len_ - offset;

    std::size_t first_chunk = std::min(remaining, this->cap_ - physical_start);
    const T *match1 = std::find(typed_data + physical_start, typed_data + physical_start + first_chunk, value);
    if (match1 != typed_data + physical_start + first_chunk) {
      return offset + static_cast<size_type>(std::distance(typed_data + physical_start, match1));
    }

    if (first_chunk < remaining) {
      std::size_t second_chunk = remaining - first_chunk;
      const T *match2 = std::find(typed_data, typed_data + second_chunk, value);
      if (match2 != typed_data + second_chunk) {
        return offset + first_chunk + static_cast<size_type>(std::distance(typed_data, match2));
      }
    }

    return std::nullopt;
  }

  // ---- Bulk Peeking ----

  /**
   * @brief Copies up to `dest.size()` elements from the buffer into `dest` WITHOUT consuming them.
   * @param dest The destination span.
   * @param offset Logical index to start peeking from (defaults to 0).
   * @return The actual number of elements copied.
   */
  size_type peek(span<T> dest, size_type offset = 0) const & noexcept {
    if (offset >= this->len_ || dest.empty())
      return 0;

    std::size_t count = std::min(dest.size(), this->len_ - offset);
    char *dest_bytes = reinterpret_cast<char *>(dest.data());
    const char *src_bytes = static_cast<const char *>(this->data_);

    std::size_t read_head = (this->head_ + offset) % this->cap_;
    std::size_t first_chunk = std::min(count, this->cap_ - read_head);

    std::memcpy(dest_bytes, src_bytes + read_head * sizeof(T), first_chunk * sizeof(T));

    if (first_chunk < count) {
      std::memcpy(dest_bytes + first_chunk * sizeof(T), src_bytes, (count - first_chunk) * sizeof(T));
    }

    return count;
  }

  // ---- Cascading Transfers ----

  /**
   * @brief Drains up to `max_count` elements from this buffer directly into `dest` buffer.
   * Moves bytes in maximum two chunked memcpys. Extremely fast.
   * @return The number of elements successfully transferred.
   */
  size_type transfer_to(unowned_ring_base &dest, size_type max_count = static_cast<size_type>(-1)) & noexcept {
    std::size_t elements_to_move = std::min({max_count, this->len_, dest.free_space()});
    if (elements_to_move == 0)
      return 0;

    auto [s1, s2] = this->read_slices();

    std::size_t s1_move = std::min(s1.size(), elements_to_move);
    std::ignore = dest.try_write(span<const T>(s1.data(), s1_move));

    std::size_t remaining = elements_to_move - s1_move;
    if (remaining > 0) {
      std::ignore = dest.try_write(span<const T>(s2.data(), remaining));
    }

    this->consume(elements_to_move);
    return elements_to_move;
  }

  // ---- Frame-Based Transfers ----

  /**
   * @brief Probes for a complete, valid structured frame and transfers it atomically
   * to a destination ring buffer.
   *
   * @param dest The destination buffer.
   * @param validator A callable `std::pair<bool, size_type> (const Header&)`
   *
   * @return `result<bool>`:
   *         - `true` if a full frame was successfully transferred.
   *         - `false` if waiting for more data in the source buffer.
   *         - `error::capacity_exceeded` if the destination buffer lacks free space.
   *         - `error::invalid_argument` if corruption is detected (source buffer is cleared).
   */
  template <typename Header, typename Validator>
  [[nodiscard]] result<bool> try_transfer_frame_to(unowned_ring_base &dest, Validator &&validator) & noexcept {

    static_assert(std::is_trivially_copyable_v<Header>, "Header must be trivially copyable");
    const std::size_t header_elems = sizeof(Header) / sizeof(T);

    if (this->len_ < header_elems)
      return false;

    // Peek the header safely
    auto hdr_res = this->try_peek_object<Header>();
    if (!hdr_res)
      return false;

    // Validate integrity and get expected size
    auto [is_valid, size_or_skip] = validator(*hdr_res);

    if (!is_valid || size_or_skip < header_elems) {
      // Corruption detected! Clear source to stop cascading errors.
      this->clear();
      return reloco::unexpected(error::invalid_argument);
    }

    // Check if the full frame has arrived
    if (this->len_ < size_or_skip) {
      return false; // Wait for more data
    }

    // Ensure the destination can hold the entire frame atomically
    if (dest.free_space() < size_or_skip) {
      return reloco::unexpected(error::capacity_exceeded);
    }

    // Transfer the verified frame flawlessly reusing our byte-transfer API!
    this->transfer_to(dest, size_or_skip);

    return true;
  }

  /**
   * @brief Safely reads a POD struct from the buffer without consuming it.
   * Seamlessly copies bytes even if the struct is split across the wrap boundary.
   *
   * @tparam U The struct type to read (must be trivially copyable).
   * @param offset Logical byte offset to read from (default 0).
   * @return The struct by value, or std::nullopt if not enough bytes exist.
   */
  template <typename U> [[nodiscard]] optional<U> peek_struct(size_type offset = 0) const noexcept {
    static_assert(sizeof(T) == 1, "peek_struct requires a byte-oriented buffer (char, uint8_t, std::byte)");
    static_assert(std::is_trivially_copyable_v<U>, "Can only peek trivially copyable structs");

    if (this->len_ - offset < sizeof(U)) {
      return nullopt; // Not enough data
    }

    U result;
    std::copy_n(this->begin() + static_cast<std::ptrdiff_t>(offset), sizeof(U), reinterpret_cast<T *>(&result));

    return result;
  }

  /**
   * @brief Attempts to extract a complete length-prefixed packet.
   *
   * @tparam Header The struct type representing the packet header.
   * @tparam LengthFunc A callable `std::size_t(const Header&)` that returns the total frame size.
   * @return A contiguous span of the entire frame, or std::nullopt if incomplete.
   */
  template <typename Header, typename LengthFunc>
  [[nodiscard]] std::optional<span<const T>> try_read_frame(LengthFunc get_total_size) & noexcept RELOCO_LIFETIMEBOUND {
    // Do we have enough data to even read the header?
    auto hdr = this->peek_struct<Header>();
    if (!hdr)
      return std::nullopt;

    // Ask the user's lambda how big this entire packet is supposed to be
    std::size_t total_size = get_total_size(*hdr);

    // Has the whole packet arrived from the network yet?
    if (this->len_ < total_size)
      return std::nullopt;

    // We have the full packet! Make sure it sits contiguously in memory.
    // (If the buffer is fragmented, this slides it to index 0 using memmove)
    this->make_contiguous();

    // Return the span. (The user should call consume() after parsing it).
    return span<const T>(static_cast<const T *>(this->data_) + this->head_, total_size);
  }

  /**
   * @brief Advances the write head by inserting padding bytes until the
   * next available write index satisfies the requested alignment.
   *
   * @param alignment The required byte alignment (e.g., alignof(MyStruct)).
   */
  void align_write_head(std::size_t alignment) & noexcept {
    static_assert(sizeof(T) == 1, "Alignment padding requires a byte-oriented buffer");

    const std::size_t physical_tail = (this->head_ + this->len_) % this->cap_;
    const auto current_addr = reinterpret_cast<std::size_t>(static_cast<const T *>(this->data_) + physical_tail);

    const std::size_t remainder = current_addr % alignment;
    if (remainder == 0)
      return; // Already aligned

    const std::size_t padding_needed = alignment - remainder;

    // Just fake-write padding bytes by increasing len_
    if (this->free_space() >= padding_needed) {
      this->commit(padding_needed);
    }
  }

  // ---- Zero-Copy Allocation & Commit ----

  /**
   * @brief Returns a contiguous span of writable free space.
   * Because memory is circular, this returns at most the contiguous free space
   * up to the physical end of the buffer.
   *
   * @param limit Maximum number of elements to allocate (default: all available contiguous space).
   * @return A span pointing to writable memory. Call `commit()` after writing.
   */
  [[nodiscard]] span<T>
  allocate_contiguous(size_type limit = static_cast<size_type>(-1)) & noexcept RELOCO_LIFETIMEBOUND {
    if (this->len_ == this->cap_)
      return {}; // Full

    std::size_t tail = this->head_ + this->len_;
    std::size_t available;

    if (tail < this->cap_) {
      // Free space is contiguous from tail to the physical end of the buffer
      available = this->cap_ - tail;
    } else {
      // Free space is wrapped, existing between the physical start (tail - cap_) and head_
      tail -= this->cap_;
      available = this->head_ - tail;
    }

    std::size_t to_allocate = std::min(limit, available);
    return span<T>(static_cast<T *>(this->data_) + tail, to_allocate);
  }

  /**
   * @brief Allocates uninitialized contiguous space for a trivially copyable object.
   * Allows direct mutation (e.g., placement-new or direct struct assignment).
   */
  template <typename U> [[nodiscard]] result<U *> try_allocate_object() & noexcept RELOCO_LIFETIMEBOUND {
    static_assert(std::is_trivially_copyable_v<U>, "Object must be trivially copyable");
    static_assert(sizeof(U) % sizeof(T) == 0, "Object size must align with buffer element size");

    const std::size_t required_elems = sizeof(U) / sizeof(T);

    span<T> chunk = this->allocate_contiguous(required_elems);
    if (chunk.size() < required_elems) {
      return reloco::unexpected(error::capacity_exceeded);
    }

    return reinterpret_cast<U *>(chunk.data());
  }

  /**
   * @brief Commits elements written directly into the allocated space, officially adding them to the buffer.
   * @param count The number of elements successfully written.
   */
  void commit(size_type count) & noexcept {
    RELOCO_ASSERT(count <= this->free_space(), "Cannot commit more elements than free space available");
    this->len_ += count;
  }

  // ---- Scatter-Gather (Non-Contiguous) Allocation ----

  /**
   * @brief Returns up to `limit` elements of writable free space as one or two spans.
   * Bypasses the need for `make_contiguous()` by exposing the wrap-around boundary directly.
   * Perfect for POSIX `readv` or scatter-gather network I/O.
   *
   * @param limit Maximum number of elements to allocate (default: all free space).
   * @return A pair of spans. The second span is empty if the allocated space didn't wrap.
   *         Call `commit(total_written)` after writing to these spans.
   */
  [[nodiscard]] std::pair<span<T>, span<T>>
  allocate_slices(size_type limit = static_cast<size_type>(-1)) & noexcept RELOCO_LIFETIMEBOUND {
    std::size_t available = this->cap_ - this->len_;
    std::size_t to_allocate = std::min(limit, available);

    if (to_allocate == 0)
      return {};

    std::size_t tail = this->head_ + this->len_;
    if (tail >= this->cap_)
      tail -= this->cap_;

    std::size_t first_chunk = std::min(to_allocate, this->cap_ - tail);

    span<T> s1(static_cast<T *>(this->data_) + tail, first_chunk);
    span<T> s2;

    if (first_chunk < to_allocate) {
      s2 = span<T>(static_cast<T *>(this->data_), to_allocate - first_chunk);
    }

    return {s1, s2};
  }

  /**
   * @brief Searches for a multi-element sequence (e.g., a magic word or "\r\n\r\n").
   * Seamlessly handles sequences split across the physical wrap-around boundary.
   *
   * @param seq The sequence to search for.
   * @param offset Logical index to start searching from.
   * @return Logical index of the start of the sequence, or std::nullopt.
   */
  [[nodiscard]] std::optional<size_type> find_sequence(span<const T> seq, size_type offset = 0) const & noexcept {
    if (seq.empty() || this->len_ - offset < seq.size())
      return std::nullopt;

    // Cast the unsigned offset to a signed difference type for safe iterator arithmetic
    auto it_begin = this->begin() + static_cast<std::ptrdiff_t>(offset);
    auto it_end = this->end();

    auto match = std::search(it_begin, it_end, seq.begin(), seq.end());

    if (match != it_end) {
      return static_cast<size_type>(match - this->begin());
    }
    return std::nullopt;
  }

  /**
   * @brief Consumes and drops elements from the front of the buffer until the
   * predicate returns true. The element that matches the predicate is NOT consumed.
   *
   * @param pred A callable `bool(const T&)`
   * @return The number of elements dropped.
   */
  template <typename Predicate> size_type consume_until(Predicate &&pred) & noexcept {
    size_type dropped = 0;

    auto it = std::find_if(this->begin(), this->end(), std::forward<Predicate>(pred));
    dropped = static_cast<size_type>(it - this->begin());

    if (dropped > 0) {
      this->consume(dropped);
    }

    return dropped;
  }

  // ---- Iterators ----

  template <bool IsConst> class RELOCO_POINTER ring_iterator {
  public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = std::remove_cv_t<T>;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<IsConst, const T *, T *>;
    using reference = std::conditional_t<IsConst, const T &, T &>;

  private:
    using BufferPtr = std::conditional_t<IsConst, const unowned_ring_base *, unowned_ring_base *>;
    BufferPtr buf_{nullptr};
    size_type logical_idx_{0};

    friend class unowned_ring_base;
    ring_iterator(BufferPtr buf, size_type idx) noexcept : buf_(buf), logical_idx_(idx) {}

  public:
    ring_iterator() = default;

    reference operator*() const noexcept RELOCO_LIFETIMEBOUND {
      RELOCO_ASSERT(buf_ != nullptr, "Dereferencing an uninitialized ring_iterator");
      RELOCO_ASSERT(logical_idx_ < buf_->len_, "Out-of-bounds ring_iterator dereference (likely dereferenced end())");

      std::size_t physical_idx = (buf_->head_ + logical_idx_) % buf_->cap_;
      return static_cast<pointer>(buf_->data_)[physical_idx];
    }

    pointer operator->() const noexcept RELOCO_LIFETIMEBOUND { return &(**this); }

    ring_iterator &operator++() noexcept {
      ++logical_idx_;
      RELOCO_ASSERT(buf_ != nullptr, "Incrementing uninitialized ring_iterator");
      RELOCO_ASSERT(logical_idx_ <= buf_->len_, "ring_iterator incremented past end()");
      return *this;
    }

    ring_iterator operator++(int) noexcept {
      auto tmp = *this;
      ++(*this); // Safely calls the hardened prefix operator
      return tmp;
    }

    ring_iterator &operator--() noexcept {
      --logical_idx_;
      RELOCO_ASSERT(buf_ != nullptr, "Decrementing uninitialized ring_iterator");
      // Because logical_idx_ is unsigned, 0 - 1 wraps to SIZE_MAX,
      // which safely triggers this exact same assert!
      RELOCO_ASSERT(logical_idx_ <= buf_->len_, "ring_iterator decremented before begin()");
      return *this;
    }

    ring_iterator operator--(int) noexcept {
      auto tmp = *this;
      --(*this); // Safely calls the hardened prefix operator
      return tmp;
    }

    ring_iterator &operator+=(difference_type n) noexcept {
      if (n >= 0) {
        logical_idx_ += static_cast<size_type>(n);
      } else {
        logical_idx_ -= static_cast<size_type>(-n);
      }

      RELOCO_ASSERT(buf_ != nullptr, "Arithmetic on uninitialized ring_iterator");
      // Catches both going past end() AND underflowing below begin() (due to unsigned wrap)
      RELOCO_ASSERT(logical_idx_ <= buf_->len_, "ring_iterator arithmetic out of bounds");

      return *this;
    }

    ring_iterator &operator-=(difference_type n) noexcept {
      if (n >= 0) {
        logical_idx_ -= static_cast<size_type>(n);
      } else {
        logical_idx_ += static_cast<size_type>(-n);
      }

      RELOCO_ASSERT(buf_ != nullptr, "Arithmetic on uninitialized ring_iterator");
      RELOCO_ASSERT(logical_idx_ <= buf_->len_, "ring_iterator arithmetic out of bounds");

      return *this;
    }

    friend ring_iterator operator+(ring_iterator it, difference_type n) noexcept { return it += n; }
    friend ring_iterator operator+(difference_type n, ring_iterator it) noexcept { return it += n; }
    friend ring_iterator operator-(ring_iterator it, difference_type n) noexcept { return it -= n; }
    friend difference_type operator-(const ring_iterator &a, const ring_iterator &b) noexcept {
      return static_cast<difference_type>(a.logical_idx_) - static_cast<difference_type>(b.logical_idx_);
    }

    reference operator[](difference_type n) const noexcept { return *(*this + n); }

    bool operator==(const ring_iterator &other) const noexcept {
      return logical_idx_ == other.logical_idx_ && buf_ == other.buf_;
    }
    bool operator!=(const ring_iterator &other) const noexcept { return !(*this == other); }
    bool operator<(const ring_iterator &other) const noexcept { return logical_idx_ < other.logical_idx_; }
    bool operator>(const ring_iterator &other) const noexcept { return logical_idx_ > other.logical_idx_; }
    bool operator<=(const ring_iterator &other) const noexcept { return logical_idx_ <= other.logical_idx_; }
    bool operator>=(const ring_iterator &other) const noexcept { return logical_idx_ >= other.logical_idx_; }
  };

  using iterator = ring_iterator<false>;
  using const_iterator = ring_iterator<true>;

  [[nodiscard]] iterator begin() & noexcept RELOCO_LIFETIMEBOUND { return iterator(this, 0); }
  [[nodiscard]] iterator end() & noexcept RELOCO_LIFETIMEBOUND { return iterator(this, this->len_); }

  [[nodiscard]] const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND { return const_iterator(this, 0); }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return const_iterator(this, this->len_); }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return begin(); }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return end(); }

  // ---- RAII Transactions ----

  class RELOCO_POINTER RELOCO_CONSUMABLE(unconsumed) write_tx {
    unowned_ring_base *buf_;
    std::pair<span<T>, span<T>> spans_;

    friend class unowned_ring_base;
    write_tx(unowned_ring_base *buf RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS, size_type limit) noexcept
        RELOCO_RETURN_TYPESTATE(unconsumed)
        : buf_(buf), spans_(buf->allocate_slices(limit)) {}

  public:
    write_tx(const write_tx &) = delete;
    write_tx &operator=(const write_tx &) = delete;

    write_tx(write_tx &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : buf_(other.buf_), spans_(other.spans_) {
      other.buf_ = nullptr; // Steal ownership
    }

    ~write_tx() = default; // Zero overhead rollback on destruction

    [[nodiscard]] constexpr bool is_valid() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) { return buf_ != nullptr; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) {
      return buf_ != nullptr;
    }

    [[nodiscard]] span<T> chunk1() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.first;
    }
    [[nodiscard]] span<T> chunk2() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.second;
    }

    [[nodiscard]] std::size_t total_allocated() const noexcept RELOCO_CALLABLE_WHEN(unconsumed) {
      return spans_.first.size() + spans_.second.size();
    }

    /**
     * @brief Commits the written data to the buffer and invalidates the transaction.
     */
    void commit(size_type count) noexcept RELOCO_SET_TYPESTATE(consumed) {
      if (buf_) {
        buf_->commit(count);
        buf_ = nullptr; // Prevent double commits
      }
    }

    write_tx &as_unconsumed() noexcept RELOCO_SET_TYPESTATE(unconsumed) {
      RELOCO_ASSERT(buf_ != nullptr, "Transaction already consumed");
      return *this;
    }
  };

  /**
   * @brief Begins a safe, rollback-ready scatter-gather write transaction.
   */
  [[nodiscard]] write_tx begin_write(size_type limit = static_cast<size_type>(-1)) & noexcept RELOCO_LIFETIMEBOUND {
    return write_tx(this, limit);
  }

  // ---- STL Back Inserter Support ----

  /**
   * @brief Pushes a single element to the back of the buffer.
   * If the buffer is full, it automatically overwrites the oldest element (lossy FIFO).
   * Required to support standard library std::back_inserter.
   */
  void push_back(const_reference value) & noexcept {
    // Write to the current physical tail
    std::size_t physical_tail = (this->head_ + this->len_) % this->cap_;
    static_cast<T *>(this->data_)[physical_tail] = value;

    // Adjust state based on capacity
    if (this->len_ < this->cap_) {
      ++this->len_; // Just grow the length
    } else {
      // Buffer is full. The tail just overwrote the old head!
      // We must advance the head to drop the oldest element.
      this->head_ = (this->head_ + 1) % this->cap_;
    }
  }
};

template <typename T> class heap_trivial_ring_base : public unowned_ring_base<T> {
protected:
  constexpr explicit heap_trivial_ring_base(const allocator_ref alloc = default_allocator()) noexcept
      : unowned_ring_base<T>(), alloc_(alloc) {}

  ~heap_trivial_ring_base() noexcept = default;

  void destroy_elements(std::size_t elem_size) noexcept {
    if (this->data_) {
      alloc_.deallocate(this->data_, this->cap_ * elem_size);
      this->data_ = nullptr;
      this->head_ = 0;
      this->len_ = 0;
      this->cap_ = 0;
    }
  }

  constexpr void move_construct_from_base(heap_trivial_ring_base &&other) noexcept {
    this->data_ = other.data_;
    this->head_ = other.head_;
    this->len_ = other.len_;
    this->cap_ = other.cap_;
    alloc_ = other.alloc_;
    other.data_ = nullptr;
    other.head_ = 0;
    other.len_ = 0;
    other.cap_ = 0;
  }

  void move_assign_from_base(std::size_t elem_size, heap_trivial_ring_base &&other) noexcept {
    if (this == &other)
      return;
    destroy_elements(elem_size);
    move_construct_from_base(std::move(other));
  }

  [[nodiscard]] static constexpr void *get_inline_storage() noexcept { return nullptr; }
  [[nodiscard]] static constexpr std::size_t max_capacity(std::size_t elem_size) noexcept {
    return std::numeric_limits<std::size_t>::max() / elem_size;
  }

public:
  [[nodiscard]] constexpr static bool is_inline() noexcept { return false; }
  [[nodiscard]] allocator_ref get_allocator() const noexcept { return alloc_; }
  [[nodiscard]] static constexpr std::size_t inline_capacity() noexcept { return 0; }

private:
  allocator_ref alloc_;
};

template <typename T> class inline_trivial_ring_base : public unowned_ring_base<T> {
protected:
  constexpr inline_trivial_ring_base(void *storage, const std::size_t capacity) noexcept
      : unowned_ring_base<T>(storage, capacity) {}

  ~inline_trivial_ring_base() noexcept = default;

  void destroy_elements(std::size_t) noexcept {
    this->len_ = 0;
    this->head_ = 0;
  }

  void move_construct_from_base(std::size_t elem_size, inline_trivial_ring_base &&other) noexcept {
    if (other.len_ > 0) {
      char *dest = static_cast<char *>(this->data_);
      char *src = static_cast<char *>(other.data_);
      std::size_t first_chunk = std::min(other.len_, other.cap_ - other.head_);
      std::memcpy(dest, src + other.head_ * elem_size, first_chunk * elem_size);
      if (first_chunk < other.len_) {
        std::memcpy(dest + first_chunk * elem_size, src, (other.len_ - first_chunk) * elem_size);
      }
      this->len_ = other.len_;
      this->head_ = 0;
    } else {
      this->len_ = 0;
      this->head_ = 0;
    }
    other.len_ = 0;
    other.head_ = 0;
  }

  void move_assign_from_base(std::size_t elem_size, inline_trivial_ring_base &&other) noexcept {
    if (this == &other)
      return;
    move_construct_from_base(elem_size, std::move(other));
  }

  [[nodiscard]] constexpr void *get_inline_storage() const noexcept { return this->data_; }
  [[nodiscard]] constexpr std::size_t max_capacity(std::size_t) const noexcept { return this->cap_; }

public:
  [[nodiscard]] constexpr static bool is_inline() noexcept { return true; }
  [[nodiscard]] static allocator_ref get_allocator() noexcept { return default_allocator(); }
  [[nodiscard]] std::size_t inline_capacity() const noexcept { return this->cap_; }
};

template <typename T> class outline_trivial_ring_base : public unowned_ring_base<T> {
protected:
  constexpr outline_trivial_ring_base(void *storage, std::size_t capacity) noexcept
      : unowned_trivial_ring_base(storage, capacity) {}

  ~outline_trivial_ring_base() noexcept = default;

  /**
   * @brief Cross-casting constructor. Accepts a span of any type U.
   * Automatically calculates the correct byte offset and truncates any trailing
   * bytes that don't fit perfectly into a multiple of sizeof(T).
   */
  template <typename U>
  constexpr explicit outline_trivial_ring_base(span<U> memory) noexcept
      : unowned_ring_base<T>(const_cast<void *>(static_cast<const void *>(memory.data())),
                             (memory.size() * sizeof(U)) / sizeof(T)) {
    static_assert(!std::is_const_v<U>, "outline_ring_buffer writes to its storage and requires a mutable span");
  }

  void destroy_elements(std::size_t) noexcept {
    this->len_ = 0;
    this->head_ = 0;
  }
  [[nodiscard]] constexpr void *get_inline_storage() const noexcept { return this->data_; }
  [[nodiscard]] constexpr std::size_t max_capacity(std::size_t) const noexcept { return this->cap_; }

public:
  [[nodiscard]] constexpr static bool is_inline() noexcept { return false; }
  [[nodiscard]] static allocator_ref get_allocator() noexcept { return default_allocator(); }
  [[nodiscard]] std::size_t inline_capacity() const noexcept { return this->cap_; }
};

template <typename T> class mixed_trivial_ring_base : public unowned_ring_base<T> {
private:
  void *inline_storage_;
  std::size_t inline_capacity_;
  allocator_ref alloc_;

protected:
  constexpr mixed_trivial_ring_base(void *inline_storage, std::size_t inline_capacity,
                                    const allocator_ref alloc) noexcept
      : unowned_ring_base<T>(inline_storage, inline_capacity), inline_storage_(inline_storage),
        inline_capacity_(inline_capacity), alloc_(alloc) {}

  ~mixed_trivial_ring_base() noexcept = default;

  void destroy_elements(std::size_t elem_size) noexcept {
    if (this->data_) {
      if (!is_inline())
        alloc_.deallocate(this->data_, this->cap_ * elem_size);
      this->data_ = inline_storage_;
      this->head_ = 0;
      this->len_ = 0;
      this->cap_ = inline_capacity_;
    }
  }

  void move_construct_from_base(std::size_t elem_size, mixed_trivial_ring_base &&other) noexcept {
    alloc_ = other.alloc_;
    if (other.is_inline()) {
      if (other.len_ > 0) {
        char *dest = static_cast<char *>(this->data_);
        char *src = static_cast<char *>(other.data_);
        std::size_t first_chunk = std::min(other.len_, other.cap_ - other.head_);
        std::memcpy(dest, src + other.head_ * elem_size, first_chunk * elem_size);
        if (first_chunk < other.len_)
          std::memcpy(dest + first_chunk * elem_size, src, (other.len_ - first_chunk) * elem_size);
        this->len_ = other.len_;
        this->head_ = 0;
      } else {
        this->len_ = 0;
        this->head_ = 0;
      }
      this->cap_ = inline_capacity_;
      other.len_ = 0;
      other.head_ = 0;
    } else {
      this->data_ = other.data_;
      this->head_ = other.head_;
      this->len_ = other.len_;
      this->cap_ = other.cap_;
      other.data_ = other.inline_storage_;
      other.head_ = 0;
      other.len_ = 0;
      other.cap_ = other.inline_capacity_;
    }
  }

  void move_assign_from_base(std::size_t elem_size, mixed_trivial_ring_base &&other) noexcept {
    if (this == &other)
      return;
    destroy_elements(elem_size);
    move_construct_from_base(elem_size, std::move(other));
  }

  [[nodiscard]] constexpr void *get_inline_storage() const noexcept { return inline_storage_; }
  [[nodiscard]] static constexpr std::size_t max_capacity(std::size_t elem_size) noexcept {
    return std::numeric_limits<std::size_t>::max() / elem_size;
  }

public:
  [[nodiscard]] constexpr bool is_inline() const noexcept { return this->data_ == inline_storage_; }
  [[nodiscard]] allocator_ref get_allocator() const noexcept { return alloc_; }
  [[nodiscard]] std::size_t inline_capacity() const noexcept { return inline_capacity_; }
};

template <typename T, typename Base> class RELOCO_EXPORT RELOCO_POINTER typed_ring_buffer : public Base {
  static_assert(std::is_trivially_copyable_v<T>, "ring_buffer is strictly for trivial types (bytes, PODs)");

public:
  using size_type = typename Base::size_type;
  using value_type = std::remove_cv_t<T>;
  using reference = T &;
  using const_reference = const T &;

protected:
  // Inherit base constructors (binds to the specific storage policy)
  template <typename... Args>
  constexpr explicit typed_ring_buffer(Args &&...args) noexcept : Base(std::forward<Args>(args)...) {}

public:
  [[nodiscard]] constexpr std::size_t max_capacity() const noexcept { return Base::max_capacity(sizeof(T)); }

  // ---- Capacity Management ----

  /**
   * @brief Attempts to increase the capacity of the ring buffer to at least `new_cap`.
   * For fixed-capacity storage policies (inline, outline), this fails if `new_cap`
   * exceeds the fixed capacity.
   */
  [[nodiscard]] result<void> try_reserve(size_type new_cap) & noexcept {
    return this->try_reserve_base(Base::get_allocator(), new_cap, sizeof(T), alignof(T), Base::get_inline_storage(),
                                  Base::inline_capacity(), Base::max_capacity(sizeof(T)));
  }

  /**
   * @brief Pushes a single element into the ring buffer, growing capacity if necessary.
   */
  [[nodiscard]] result<void> try_push_back(T value) & noexcept {
    if (this->len_ == this->cap_)
      RELOCO_UNLIKELY {
        std::size_t new_cap = this->cap_ == 0 ? 8 : (this->cap_ * 3 + 1) / 2;
        auto res = try_reserve(new_cap);
        if (!res)
          return unexpected(res.error());
      }

    std::size_t tail = this->head_ + this->len_;
    if (tail >= this->cap_)
      tail -= this->cap_;

    static_cast<T *>(this->data_)[tail] = value;
    ++this->len_;

    return {};
  }

  /**
   * @brief Pushes a single element to the front, growing capacity if necessary.
   */
  [[nodiscard]] result<void> try_push_front(T value) & noexcept {
    if (this->len_ == this->cap_)
      RELOCO_UNLIKELY {
        std::size_t new_cap = this->cap_ == 0 ? 8 : (this->cap_ * 3 + 1) / 2;
        auto res = try_reserve(new_cap);
        if (!res)
          return unexpected(res.error());
      }

    this->head_ = (this->head_ == 0) ? this->cap_ - 1 : this->head_ - 1;
    static_cast<T *>(this->data_)[this->head_] = value;
    ++this->len_;

    return {};
  }

  // ---- Fallible Resizing ----

  /**
   * @brief Resizes the buffer to `new_len`. If it grows, new elements are initialized to `value`.
   * Fails if `new_len` exceeds maximum capacity and cannot be allocated.
   */
  [[nodiscard]] result<void> try_resize(size_type new_len, T value = T{}) & noexcept {
    if (new_len <= this->len_) {
      this->len_ = new_len; // Trivial truncation
      return {};
    }

    auto res = try_reserve(new_len);
    if (!res)
      return unexpected(res.error());

    std::size_t to_add = new_len - this->len_;
    for (std::size_t i = 0; i < to_add; ++i) {
      // Safe to blindly push because we just reserved enough capacity
      std::size_t tail = this->head_ + this->len_;
      if (tail >= this->cap_)
        tail -= this->cap_;

      static_cast<T *>(this->data_)[tail] = value;
      ++this->len_;
    }

    return {};
  }

  /**
   * @brief Deep-copies the ring buffer using the provided allocator.
   * Linearizes the layout into the new clone automatically!
   */
  [[nodiscard]] result<typed_ring_buffer> try_clone(allocator_ref alloc = Base::get_allocator()) const noexcept {
    typed_ring_buffer clone(alloc);

    if (this->len_ > 0) {
      auto res = clone.try_reserve(this->len_);
      if (!res)
        return unexpected(res.error());

      // Because T is trivial, we can just use our own read_slices!
      auto [s1, s2] = this->read_slices();

      // We can bypass try_write because we know capacity is exact
      std::memcpy(static_cast<T *>(clone.data_), s1.data(), s1.size() * sizeof(T));
      if (!s2.empty()) {
        std::memcpy(static_cast<T *>(clone.data_) + s1.size(), s2.data(), s2.size() * sizeof(T));
      }

      clone.len_ = this->len_;
    }

    return clone;
  }
};

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "ring_buffer.ipp"

#endif

RELOCO_END_UNSAFE_BUFFER_USAGE

} // namespace detail

template <typename T>
class RELOCO_OWNER ring_buffer : public detail::typed_ring_buffer<T, detail::heap_trivial_ring_base<T>> {
public:
  constexpr explicit ring_buffer(allocator_ref alloc = default_allocator()) noexcept
      : detail::typed_ring_buffer<T, detail::heap_trivial_ring_base<T>>(alloc) {}

  // Not copyable
  ring_buffer(const ring_buffer &) noexcept = delete;
  ring_buffer &operator=(const ring_buffer &) noexcept = delete;

  // Moveable (Steals the heap pointer)
  constexpr ring_buffer(ring_buffer &&other) noexcept
      : detail::typed_ring_buffer<T, detail::heap_trivial_ring_base<T>>(other.get_allocator()) {
    this->move_construct_from_base(std::move(other));
  }

  ring_buffer &operator=(ring_buffer &&other) noexcept {
    this->move_assign_from_base(sizeof(T), std::move(other));
    return *this;
  }
};

template <typename T>
class RELOCO_POINTER outline_ring_buffer : public detail::typed_ring_buffer<T, detail::outline_trivial_ring_base<T>> {
public:
  template <typename U>
  constexpr explicit outline_ring_buffer(span<U> memory) noexcept
      : detail::typed_ring_buffer<T, detail::outline_trivial_ring_base<T>>(memory) {}

  // Never copyable and never movable (It doesn't own the memory it views)
  outline_ring_buffer(const outline_ring_buffer &) noexcept = delete;
  outline_ring_buffer &operator=(const outline_ring_buffer &) noexcept = delete;
  outline_ring_buffer(outline_ring_buffer &&) noexcept = delete;
  outline_ring_buffer &operator=(outline_ring_buffer &&) noexcept = delete;
};

template <typename T, std::size_t Capacity>
class RELOCO_OWNER inline_ring_buffer : public detail::typed_ring_buffer<T, detail::inline_trivial_ring_base<T>> {
  alignas(T) std::byte storage_[Capacity * sizeof(T)];

public:
  // ReSharper disable once CppPossiblyUninitializedMember
  constexpr inline_ring_buffer() noexcept // NOLINT(*-pro-type-member-init)
      : detail::typed_ring_buffer<T, detail::inline_trivial_ring_base<T>>(storage_, Capacity) {}

  // Copy Constructor (Deep copies data into the new local array)
  constexpr inline_ring_buffer(const inline_ring_buffer &other) noexcept // NOLINT(*-pro-type-member-init)
      : detail::typed_ring_buffer<T, detail::inline_trivial_ring_base<T>>(storage_, Capacity) {
    if (other.size() > 0) {
      auto [s1, s2] = other.read_slices();
      std::ignore = this->try_write(s1);
      if (!s2.empty())
        std::ignore = this->try_write(s2);
    }
  }

  // Copy Assignment
  inline_ring_buffer &operator=(const inline_ring_buffer &other) noexcept {
    if (this != &other) {
      this->clear();
      if (other.size() > 0) {
        auto [s1, s2] = other.read_slices();
        std::ignore = this->try_write(s1);
        if (!s2.empty())
          std::ignore = this->try_write(s2);
      }
    }
    return *this;
  }

  // Move Constructor (Uses base logic to shift elements zero-copy if possible)
  constexpr inline_ring_buffer(inline_ring_buffer &&other) noexcept // NOLINT(*-pro-type-member-init)
      : detail::typed_ring_buffer<T, detail::inline_trivial_ring_base<T>>(storage_, Capacity) {
    this->move_construct_from_base(sizeof(T), std::move(other));
  }

  // Move Assignment
  inline_ring_buffer &operator=(inline_ring_buffer &&other) noexcept {
    this->move_assign_from_base(sizeof(T), std::move(other));
    return *this;
  }
};

template <typename T, std::size_t InlineCapacity>
class RELOCO_OWNER sso_ring_buffer : public detail::typed_ring_buffer<T, detail::mixed_trivial_ring_base<T>> {
  alignas(T) std::byte storage_[InlineCapacity * sizeof(T)];

public:
  // ReSharper disable once CppPossiblyUninitializedMember
  constexpr explicit sso_ring_buffer( // NOLINT(*-pro-type-member-init)
      allocator_ref alloc = default_allocator()) noexcept
      : detail::typed_ring_buffer<T, detail::mixed_trivial_ring_base<T>>(storage_, InlineCapacity, alloc) {}

  sso_ring_buffer(const sso_ring_buffer &other) = delete;
  sso_ring_buffer &operator=(const sso_ring_buffer &other) = delete;

  // Move Constructor (Steals heap pointer if spilled, else copies inline elements)
  constexpr sso_ring_buffer(sso_ring_buffer &&other) noexcept // NOLINT(*-pro-type-member-init)
      : detail::typed_ring_buffer<T, detail::mixed_trivial_ring_base<T>>(storage_, InlineCapacity,
                                                                         other.get_allocator()) {
    this->move_construct_from_base(sizeof(T), std::move(other));
  }

  // Move Assignment
  sso_ring_buffer &operator=(sso_ring_buffer &&other) noexcept {
    this->move_assign_from_base(sizeof(T), std::move(other));
    return *this;
  }
};

/**
 * @brief An independent, lightweight cursor over a ring buffer.
 * Initiates as an immutable borrow of the source buffer's state (copying its
 * data pointer, capacity, head, and length).
 * Because it holds its own internal pointers, calling `consume()` or `read()`
 * on this reference advances its own cursor without modifying the original buffer.
 */
template <typename T> class RELOCO_POINTER ring_buffer_ref : public detail::unowned_ring_base<T> {
public:
  // ---- Sourcing from an Immutable Borrow ----

  /**
   * @brief Takes an immutable borrow of an existing ring buffer, explicitly copying
   * its state to create an independent read/write cursor.
   */
  constexpr explicit ring_buffer_ref(const detail::unowned_ring_base<T> &source RELOCO_LIFETIMEBOUND) noexcept
      : detail::unowned_ring_base<T>(source.raw_data(), source.capacity(), source.raw_head(), source.size()) {}

  constexpr ring_buffer_ref(const ring_buffer_ref &other) noexcept
      : detail::unowned_ring_base<T>(other.raw_data(), other.capacity(), other.raw_head(), other.size()) {}

  ring_buffer_ref &operator=(const ring_buffer_ref &other) noexcept {
    if (this != &other) {
      // Protected members are accessible here because 'other' is the same derived type
      this->data_ = other.data_;
      this->cap_ = other.cap_;
      this->head_ = other.head_;
      this->len_ = other.len_;
    }
    return *this;
  }

  constexpr ring_buffer_ref(ring_buffer_ref &&other) noexcept
      : ring_buffer_ref(static_cast<const ring_buffer_ref &>(other)) {}

  ring_buffer_ref &operator=(ring_buffer_ref &&other) noexcept {
    return *this = static_cast<const ring_buffer_ref &>(other);
  }

  // ---- Wrapping Raw Memory ----

  constexpr explicit ring_buffer_ref(span<T> memory RELOCO_LIFETIMEBOUND) noexcept
      : detail::unowned_ring_base<T>(memory.data(), memory.size(), 0, 0) {}

  constexpr ring_buffer_ref(span<T> memory RELOCO_LIFETIMEBOUND, std::size_t initial_head,
                            std::size_t initial_len) noexcept
      : detail::unowned_ring_base<T>(memory.data(), memory.size(), initial_head, initial_len) {
    RELOCO_ASSERT(initial_head < memory.size(), "Invalid initial head");
    RELOCO_ASSERT(initial_len <= memory.size(), "Invalid initial length");
  }
};

} // namespace reloco
