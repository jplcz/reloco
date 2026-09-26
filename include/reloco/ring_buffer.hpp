// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "allocator.hpp"
#include "default_allocator.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "span.hpp"
#include <algorithm>
#include <cstring>
#include <type_traits>

namespace reloco {
namespace detail {

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

class RELOCO_EXPORT unowned_trivial_ring_base {
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

protected:
  void *data_ = nullptr;
  std::size_t head_ = 0;
  std::size_t len_ = 0;
  std::size_t cap_ = 0;

  constexpr unowned_trivial_ring_base() noexcept = default;

  constexpr unowned_trivial_ring_base(void *storage, const size_type capacity) noexcept
      : data_(storage), cap_(capacity) {}

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

class RELOCO_EXPORT heap_trivial_ring_base : public unowned_trivial_ring_base {
protected:
  constexpr explicit heap_trivial_ring_base(const allocator_ref alloc = default_allocator()) noexcept
      : unowned_trivial_ring_base(), alloc_(alloc) {}

  ~heap_trivial_ring_base() noexcept = default;

  void destroy_elements(std::size_t elem_size) noexcept {
    if (data_) {
      alloc_.deallocate(data_, cap_ * elem_size);
      data_ = nullptr;
      head_ = 0;
      len_ = 0;
      cap_ = 0;
    }
  }

  constexpr void move_construct_from_base(heap_trivial_ring_base &&other) noexcept {
    data_ = other.data_;
    head_ = other.head_;
    len_ = other.len_;
    cap_ = other.cap_;
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

class RELOCO_EXPORT inline_trivial_ring_base : public unowned_trivial_ring_base {
protected:
  constexpr inline_trivial_ring_base(void *storage, const std::size_t capacity) noexcept
      : unowned_trivial_ring_base(storage, capacity) {}

  ~inline_trivial_ring_base() noexcept = default;

  void destroy_elements(std::size_t) noexcept {
    len_ = 0;
    head_ = 0;
  }

  void move_construct_from_base(std::size_t elem_size, inline_trivial_ring_base &&other) noexcept {
    if (other.len_ > 0) {
      char *dest = static_cast<char *>(data_);
      char *src = static_cast<char *>(other.data_);
      std::size_t first_chunk = std::min(other.len_, other.cap_ - other.head_);
      std::memcpy(dest, src + other.head_ * elem_size, first_chunk * elem_size);
      if (first_chunk < other.len_) {
        std::memcpy(dest + first_chunk * elem_size, src, (other.len_ - first_chunk) * elem_size);
      }
      len_ = other.len_;
      head_ = 0;
    } else {
      len_ = 0;
      head_ = 0;
    }
    other.len_ = 0;
    other.head_ = 0;
  }

  void move_assign_from_base(std::size_t elem_size, inline_trivial_ring_base &&other) noexcept {
    if (this == &other)
      return;
    move_construct_from_base(elem_size, std::move(other));
  }

  [[nodiscard]] constexpr void *get_inline_storage() const noexcept { return data_; }
  [[nodiscard]] constexpr std::size_t max_capacity(std::size_t) const noexcept { return cap_; }

public:
  [[nodiscard]] constexpr static bool is_inline() noexcept { return true; }
  [[nodiscard]] static allocator_ref get_allocator() noexcept { return default_allocator(); }
  [[nodiscard]] std::size_t inline_capacity() const noexcept { return cap_; }
};

class RELOCO_EXPORT outline_trivial_ring_base : public unowned_trivial_ring_base {
protected:
  constexpr outline_trivial_ring_base(void *storage, std::size_t capacity) noexcept
      : unowned_trivial_ring_base(storage, capacity) {}
  ~outline_trivial_ring_base() noexcept = default;
  void destroy_elements(std::size_t) noexcept {
    len_ = 0;
    head_ = 0;
  }
  [[nodiscard]] constexpr void *get_inline_storage() const noexcept { return data_; }
  [[nodiscard]] constexpr std::size_t max_capacity(std::size_t) const noexcept { return cap_; }

public:
  [[nodiscard]] constexpr static bool is_inline() noexcept { return false; }
  [[nodiscard]] static allocator_ref get_allocator() noexcept { return default_allocator(); }
  [[nodiscard]] std::size_t inline_capacity() const noexcept { return cap_; }
};

class RELOCO_EXPORT mixed_trivial_ring_base : public unowned_trivial_ring_base {
private:
  void *inline_storage_;
  std::size_t inline_capacity_;
  allocator_ref alloc_;

protected:
  constexpr mixed_trivial_ring_base(void *inline_storage, std::size_t inline_capacity,
                                    const allocator_ref alloc) noexcept
      : unowned_trivial_ring_base(inline_storage, inline_capacity), inline_storage_(inline_storage),
        inline_capacity_(inline_capacity), alloc_(alloc) {}

  ~mixed_trivial_ring_base() noexcept = default;

  void destroy_elements(std::size_t elem_size) noexcept {
    if (data_) {
      if (!is_inline())
        alloc_.deallocate(data_, cap_ * elem_size);
      data_ = inline_storage_;
      head_ = 0;
      len_ = 0;
      cap_ = inline_capacity_;
    }
  }

  void move_construct_from_base(std::size_t elem_size, mixed_trivial_ring_base &&other) noexcept {
    alloc_ = other.alloc_;
    if (other.is_inline()) {
      if (other.len_ > 0) {
        char *dest = static_cast<char *>(data_);
        char *src = static_cast<char *>(other.data_);
        std::size_t first_chunk = std::min(other.len_, other.cap_ - other.head_);
        std::memcpy(dest, src + other.head_ * elem_size, first_chunk * elem_size);
        if (first_chunk < other.len_)
          std::memcpy(dest + first_chunk * elem_size, src, (other.len_ - first_chunk) * elem_size);
        len_ = other.len_;
        head_ = 0;
      } else {
        len_ = 0;
        head_ = 0;
      }
      cap_ = inline_capacity_;
      other.len_ = 0;
      other.head_ = 0;
    } else {
      data_ = other.data_;
      head_ = other.head_;
      len_ = other.len_;
      cap_ = other.cap_;
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
  [[nodiscard]] constexpr bool is_inline() const noexcept { return data_ == inline_storage_; }
  [[nodiscard]] allocator_ref get_allocator() const noexcept { return alloc_; }
  [[nodiscard]] std::size_t inline_capacity() const noexcept { return inline_capacity_; }
};

template <typename T, typename Base> class RELOCO_EXPORT typed_ring_buffer : public Base {
  static_assert(std::is_trivially_copyable_v<T>, "ring_buffer is strictly for trivial types (bytes, PODs)");

public:
  using size_type = typename Base::size_type;

  // Inherit base constructors (binds to the specific storage policy)
  template <typename... Args>
  constexpr explicit typed_ring_buffer(Args &&...args) noexcept : Base(std::forward<Args>(args)...) {}

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

  // ---- Bulk Mutation ----

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
   * @brief Returns the contiguous slices of data available to read.
   * Typically passed directly to `writev` / `WSASend` / socket APIs.
   */
  [[nodiscard]] std::pair<span<const T>, span<const T>> read_slices() const & noexcept {
    if (this->len_ == 0)
      return {span<const T>(), span<const T>()};

    const T *typed_data = static_cast<const T *>(this->data_);
    std::size_t tail = this->head_ + this->len_;

    if (tail <= this->cap_) {
      return {span<const T>(typed_data + this->head_, this->len_), span<const T>()};
    } else {
      return {span<const T>(typed_data + this->head_, this->cap_ - this->head_),
              span<const T>(typed_data, tail - this->cap_)};
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

  /**
   * @brief After manually writing into the spans returned by `write_slices()`,
   * call this to logically commit the written bytes to the buffer.
   */
  void commit_written(size_type count) & noexcept {
    RELOCO_ASSERT(count <= this->free_space(), "Committed more data than free space");
    this->len_ += count;
  }

  // ---- Single Element Mutation ----

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
      if (this->len_ == 1)
        this->head_ = 0; // Trivial reset
      return span<T>(static_cast<T *>(this->data_) + this->head_, this->len_);
    }

    std::size_t tail = this->head_ + this->len_;

    // Already contiguous
    if (tail <= this->cap_) {
      return span<T>(static_cast<T *>(this->data_) + this->head_, this->len_);
    }

    // Wrapped. We have two chunks: [head_, cap_) and [0, tail - cap_).
    // Because elements are trivial, we can just use std::rotate on the raw bytes!
    T *typed_data = static_cast<T *>(this->data_);

    // If there is free space, it's faster to do block shifts, but std::rotate
    // is highly optimized in standard libraries for contiguous memory and requires 0 extra memory.
    // We rotate the entire array so the head chunk comes first.
    std::rotate(typed_data, typed_data + this->head_, typed_data + this->cap_);

    this->head_ = 0;
    return span<T>(typed_data, this->len_);
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
  template <typename DestBase>
  size_type transfer_to(typed_ring_buffer<T, DestBase> &dest,
                        size_type max_count = static_cast<size_type>(-1)) & noexcept {
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
  template <typename Header, typename DestBase, typename Validator>
  [[nodiscard]] result<bool> try_transfer_frame_to(typed_ring_buffer<T, DestBase> &dest,
                                                   Validator &&validator) & noexcept {

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
};

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "ring_buffer.ipp"
#endif

RELOCO_END_UNSAFE_BUFFER_USAGE

} // namespace detail

template <typename T> using ring_buffer = detail::typed_ring_buffer<T, detail::heap_trivial_ring_base>;

template <typename T> using outline_ring_buffer = detail::typed_ring_buffer<T, detail::outline_trivial_ring_base>;

template <typename T, std::size_t Capacity>
class inline_ring_buffer : public detail::typed_ring_buffer<T, detail::inline_trivial_ring_base> {
  alignas(T) std::byte storage_[Capacity * sizeof(T)];

public:
  // ReSharper disable once CppPossiblyUninitializedMember
  constexpr inline_ring_buffer() noexcept // NOLINT(*-pro-type-member-init)
      : detail::typed_ring_buffer<T, detail::inline_trivial_ring_base>(storage_, Capacity) {}
};

template <typename T, std::size_t InlineCapacity>
class sso_ring_buffer : public detail::typed_ring_buffer<T, detail::mixed_trivial_ring_base> {
  alignas(T) std::byte storage_[InlineCapacity * sizeof(T)];

public:
  // ReSharper disable once CppPossiblyUninitializedMember
  constexpr explicit sso_ring_buffer( // NOLINT(*-pro-type-member-init)
      allocator_ref alloc = default_allocator()) noexcept
      : detail::typed_ring_buffer<T, detail::mixed_trivial_ring_base>(storage_, InlineCapacity, alloc) {}
};

} // namespace reloco
