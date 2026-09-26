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
