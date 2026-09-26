// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file ring_buffer.ipp
 * @brief Out-of-line bodies for `unowned_trivial_ring_base`'s heavier,
 * allocation/copy-driving methods (see `ring_buffer.hpp`). Included from
 * `ring_buffer.hpp` itself, guarded on `RELOCO_SHARED_PROVIDE_DEFINITIONS`
 * (see `reloco/detail/compat.hpp`). Never included directly.
 */

RELOCO_API result<void> unowned_trivial_ring_base::try_write_base(const void *src, std::size_t count,
                                                                  std::size_t elem_size) noexcept {
  if (len_ + count > cap_)
    return unexpected(error::capacity_exceeded);
  if (count == 0)
    return {};

  const char *src_bytes = static_cast<const char *>(src);
  char *dest_bytes = static_cast<char *>(data_);
  std::size_t tail = head_ + len_;
  if (tail >= cap_)
    tail -= cap_;

  std::size_t first_chunk = std::min(count, cap_ - tail);
  std::memcpy(dest_bytes + tail * elem_size, src_bytes, first_chunk * elem_size);

  if (first_chunk < count) {
    std::memcpy(dest_bytes, src_bytes + first_chunk * elem_size, (count - first_chunk) * elem_size);
  }

  len_ += count;
  return {};
}

RELOCO_API void unowned_trivial_ring_base::write_overwrite_base(const void *src, std::size_t count,
                                                                std::size_t elem_size) noexcept {
  if (count == 0 || cap_ == 0)
    return;

  const char *src_bytes = static_cast<const char *>(src);
  char *dest_bytes = static_cast<char *>(data_);

  // If writing more than capacity, we only care about the last `cap_` elements
  if (count >= cap_) {
    src_bytes += (count - cap_) * elem_size;
    count = cap_;
    head_ = 0;
    len_ = 0; // Reset state and fall through to a full write
  }

  std::size_t tail = head_ + len_;
  if (tail >= cap_)
    tail -= cap_;

  std::size_t first_chunk = std::min(count, cap_ - tail);
  std::memcpy(dest_bytes + tail * elem_size, src_bytes, first_chunk * elem_size);

  if (first_chunk < count) {
    std::memcpy(dest_bytes, src_bytes + first_chunk * elem_size, (count - first_chunk) * elem_size);
  }

  len_ += count;
  if (len_ > cap_) {
    // We overflowed. Advance head by the overflow amount.
    std::size_t overflow = len_ - cap_;
    head_ = (head_ + overflow) % cap_;
    len_ = cap_;
  }
}

RELOCO_API std::size_t unowned_trivial_ring_base::read_base(void *dest, std::size_t count,
                                                            std::size_t elem_size) noexcept {
  count = std::min(count, len_);
  if (count == 0)
    return 0;

  char *dest_bytes = static_cast<char *>(dest);
  const char *src_bytes = static_cast<const char *>(data_);

  std::size_t first_chunk = std::min(count, cap_ - head_);
  std::memcpy(dest_bytes, src_bytes + head_ * elem_size, first_chunk * elem_size);

  if (first_chunk < count) {
    std::memcpy(dest_bytes + first_chunk * elem_size, src_bytes, (count - first_chunk) * elem_size);
  }

  head_ = (head_ + count) % cap_;
  len_ -= count;
  return count;
}

RELOCO_API result<void> unowned_trivial_ring_base::try_reserve_base(allocator_ref alloc, std::size_t new_cap,
                                                                    std::size_t elem_size, std::size_t align,
                                                                    void *inline_storage, std::size_t max_inline,
                                                                    std::size_t max_cap) noexcept {
  if (new_cap <= cap_)
    return {};
  if (new_cap > max_cap)
    return unexpected(error::capacity_exceeded);

  const std::size_t required_bytes = new_cap * elem_size;

  // Inline Promotion
  if (inline_storage && data_ == inline_storage) {
    if (new_cap <= max_inline) {
      cap_ = max_inline;
      return {};
    }
    auto res = alloc.allocate(required_bytes, align);
    if (!res)
      return unexpected(res.error());

    char *new_data = static_cast<char *>(res->ptr);
    if (len_ > 0) {
      std::size_t first_chunk = std::min(len_, cap_ - head_);
      std::memcpy(new_data, static_cast<char *>(data_) + head_ * elem_size, first_chunk * elem_size);
      if (first_chunk < len_) {
        std::memcpy(new_data + first_chunk * elem_size, data_, (len_ - first_chunk) * elem_size);
      }
    }
    data_ = new_data;
    cap_ = res->size / elem_size;
    head_ = 0;
    return {};
  }

  // Expand in place (requires shifting if wrapped)
  if (data_) {
    if (auto res = alloc.expand_in_place(data_, cap_ * elem_size, required_bytes); res) {
      std::size_t newly_allocated_cap = *res / elem_size;
      if (len_ > 0 && head_ + len_ > cap_) {
        std::size_t added_cap = newly_allocated_cap - cap_;
        std::size_t head_len = cap_ - head_;
        char *byte_data = static_cast<char *>(data_);
        std::memmove(byte_data + (head_ + added_cap) * elem_size, byte_data + head_ * elem_size, head_len * elem_size);
        head_ += added_cap;
      }
      cap_ = newly_allocated_cap;
      return {};
    }
  }

  // Reallocate / Allocate
  auto res =
      data_ ? alloc.reallocate(data_, cap_ * elem_size, required_bytes, align) : alloc.allocate(required_bytes, align);
  if (!res)
    return unexpected(res.error());

  char *new_data = static_cast<char *>(res->ptr);
  std::size_t newly_allocated_cap = res->size / elem_size;

  if (data_) {
    if (len_ > 0 && head_ + len_ > cap_) {
      std::size_t added_cap = newly_allocated_cap - cap_;
      std::size_t head_len = cap_ - head_;
      std::memmove(new_data + (head_ + added_cap) * elem_size, new_data + head_ * elem_size, head_len * elem_size);
      head_ += added_cap;
    }
  }

  data_ = new_data;
  cap_ = newly_allocated_cap;
  return {};
}
