// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file vector_base.ipp
 * @brief Out-of-line bodies for `trivial_operator_set` and the
 * `*_base` growth/mutation primitives declared in `vector_base.hpp` (see
 * `unowned_vector_base`, `heap_vector_base`, `inline_vector_base`,
 * `mixed_vector_base`). Included from `vector_base.hpp` itself, guarded on
 * `RELOCO_SHARED_PROVIDE_DEFINITIONS` (see `reloco/detail/compat.hpp`).
 * Never included directly.
 */

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

RELOCO_API result<void> trivial_operator_set::clone_range(const type_metadata &type, const void *src, void *dest,
                                                          std::size_t size, allocator_ref) noexcept {
  if (size > 0) {
    std::memcpy(dest, src, size * type.element_size);
  }
  return {};
}

RELOCO_API result<void> trivial_operator_set::copy_construct_range(const type_metadata &type, allocator_ref, void *data,
                                                                   std::size_t from, std::size_t to,
                                                                   const void *value_ptr) noexcept {
  char *byte_data = static_cast<char *>(data);
  if (!value_ptr) {
    std::memset(byte_data + from * type.element_size, 0, (to - from) * type.element_size);
  } else {
    for (std::size_t i = from; i < to; ++i) {
      std::memcpy(byte_data + i * type.element_size, value_ptr, type.element_size);
    }
  }
  return {};
}

RELOCO_API void trivial_operator_set::move_range(const type_metadata &type, void *to, const void *from,
                                                 std::size_t count) {
  if (count > 0) {
    std::memmove(to, from, count * type.element_size);
  }
}

RELOCO_API void trivial_operator_set::move_range_up(const type_metadata &type, void *to, const void *from,
                                                    std::size_t count) {
  if (count > 0) {
    std::memmove(to, from, count * type.element_size);
  }
}

RELOCO_API result<void> unowned_vector_base::try_reserve_base(const vector_operations *operations, allocator_ref alloc,
                                                              const type_metadata &type, std::size_t new_cap,
                                                              void *inline_storage, std::size_t max_inline,
                                                              std::size_t max_cap) noexcept {
  if (new_cap <= cap_)
    return {};
  if (new_cap > max_cap)
    return unexpected(error::capacity_exceeded);

  const std::size_t elem_size = type.element_size;
  const std::size_t required_bytes = new_cap * elem_size;

  RELOCO_DEBUG_ASSERT(max_inline <= max_cap, "Inline capacity larger than heap capacity");

  // We need special code path if container has either only or optional inline storage
  if (inline_storage) {
    RELOCO_DEBUG_ASSERT(max_inline != 0, "Non-empty inline storage with zero capacity");

    // If we're in inline storage world now
    if (data_ == inline_storage) {
      // Grow to max inline capacity
      if (new_cap <= max_inline) {
        cap_ = max_inline;
        return {};
      }
      // We have to relocate to heap world
      auto res = alloc.allocate(required_bytes, type.element_alignment);
      if (!res)
        return unexpected(res.error());
      if (size_ > 0) {
        operations->move_range(type, res->ptr, data_, size_);
      }
      // We don't have to "free" inline storage, just destroy the contents
      if (!type.is_trivially_relocatable()) {
        // If type is trivially relocatable, then move_range has already killed source objects
        // so we can't destroy them
        destroy_elements_base(operations, type);
      }
      data_ = res->ptr;
      cap_ = res->size / elem_size;
      return {};
    }
  } else {
    RELOCO_DEBUG_ASSERT(inline_storage == nullptr, "Non-empty inline storage with zero capacity");
  }

  if (data_) {
    if (auto res = alloc.expand_in_place(data_, cap_ * elem_size, required_bytes); res) {
      cap_ = *res / elem_size;
      return {};
    }
  }

  if (type.is_trivially_relocatable()) {
    auto res = data_ ? alloc.reallocate(data_, cap_ * elem_size, required_bytes, type.element_alignment)
                     : alloc.allocate(required_bytes, type.element_alignment);
    if (!res)
      return unexpected(res.error());
    data_ = res->ptr;
    cap_ = res->size / elem_size;
  } else {
    auto res = alloc.allocate(required_bytes, type.element_alignment);
    if (!res)
      return unexpected(res.error());

    void *new_data = res->ptr;
    if (size_ > 0) {
      operations->move_range(type, new_data, data_, size_);
    }

    if (data_)
      alloc.deallocate(data_, cap_ * elem_size);
    data_ = new_data;
    cap_ = res->size / elem_size;
  }
  return {};
}

result<void> unowned_vector_base::try_resize_base(const vector_operations *operations, allocator_ref alloc,
                                                  const type_metadata &type, std::size_t count, const void *value_ptr,
                                                  void *inline_storage, std::size_t max_inline,
                                                  std::size_t max_cap) noexcept {
  if (count <= size_) {
    if (operations->destroy_range) {
      operations->destroy_range(type, data_, count, size_);
    }
    size_ = count;
    return {};
  }

  auto res = try_reserve_base(operations, alloc, type, count, inline_storage, max_inline, max_cap);
  if (!res)
    return unexpected(res.error());

  if (operations->copy_construct_range) {
    auto ctor_res = operations->copy_construct_range(type, alloc, data_, size_, count, value_ptr);
    if (!ctor_res) {
      return unexpected(ctor_res.error());
    }
  }

  size_ = count;
  return {};
}

RELOCO_API result<void> unowned_vector_base::shrink_to_fit_base(const vector_operations *operations,
                                                                allocator_ref alloc, const type_metadata &type,
                                                                void *inline_storage, std::size_t max_inline) noexcept {
  if (cap_ <= size_)
    return {};

  const std::size_t elem_size = type.element_size;

  // Special handling for case where inline storage exists
  if (inline_storage) {
    RELOCO_DEBUG_ASSERT(max_inline > 0, "Zero capacity inline storage");

    // If we're already in inline storage world, then no need to do anything
    if (data_ == inline_storage) {
      RELOCO_DEBUG_ASSERT(cap_ <= max_inline, "Inline storage with invalid capacity");
      return {};
    }

    // We might need to migrate to inline storage
    if (size_ <= max_inline) {
      if (size_ > 0) {
        operations->move_range(type, inline_storage, data_, size_);
      }
      // Destroy heap elements
      if (!type.is_trivially_relocatable()) {
        // Only if they're not trivially relocatable. Trivial relocation prohibits source destruction
        // from being ran
        destroy_elements_base(operations, type);
      }
      if (data_) {
        alloc.deallocate(data_, cap_ * elem_size);
      }
      data_ = inline_storage;
      cap_ = max_inline;
      return {};
    }
  } else {
    RELOCO_DEBUG_ASSERT(inline_storage == nullptr, "No inline storage, but non-empty inline capacity");
  }

  if (size_ == 0) {
    if (data_) {
      alloc.deallocate(data_, cap_ * elem_size);
      data_ = nullptr;
      cap_ = 0;
    }
    return {};
  }

  const std::size_t target_bytes = size_ * elem_size;

  if (type.is_trivially_relocatable()) {
    auto res = alloc.reallocate(data_, cap_ * elem_size, target_bytes, type.element_alignment);
    if (!res)
      return unexpected(res.error());
    data_ = res->ptr;
    cap_ = res->size / elem_size;
  } else {
    auto res = alloc.allocate(target_bytes, type.element_alignment);
    if (!res)
      return unexpected(res.error());

    void *new_data = res->ptr;

    // Reuse the operations table's move_range to safely move elements
    // and destruct old ones without code duplication
    operations->move_range(type, new_data, data_, size_);

    alloc.deallocate(data_, cap_ * elem_size);
    data_ = new_data;
    cap_ = res->size / elem_size;
  }
  return {};
}

RELOCO_API result<void> unowned_vector_base::try_pop_back_base(const vector_operations *operations,
                                                               const type_metadata &type) noexcept {
  if (size_ == 0)
    return unexpected(error::container_empty);

  --size_;
  if (operations->destroy_range) {
    operations->destroy_range(type, data_, size_, size_ + 1);
  }
  return {};
}

RELOCO_API result<void> unowned_vector_base::try_erase_at_base(const vector_operations *operations,
                                                               const type_metadata &type, std::size_t index) noexcept {
  if (index >= size_)
    return unexpected(error::out_of_bounds);

  char *byte_data = static_cast<char *>(data_);
  const std::size_t elem_size = type.element_size;

  // Destroy the element being removed via operations table
  if (operations->destroy_range) {
    operations->destroy_range(type, data_, index, index + 1);
  }

  // Shift remaining elements down if any
  const std::size_t move_count = size_ - index - 1;
  if (move_count > 0) {
    void *dest = byte_data + index * elem_size;
    const void *src = byte_data + (index + 1) * elem_size;

    if (type.is_trivially_relocatable()) {
      std::memmove(dest, src, move_count * elem_size);
    } else {
      // Reuse operations->move_range to safely shift non-trivial elements down
      operations->move_range(type, dest, src, move_count);
    }
  }

  --size_;
  return {};
}

RELOCO_API void unowned_vector_base::retain_base(const vector_operations *operations, const type_metadata &type,
                                                 function_ref<bool(const void *)> pred) noexcept {
  if (size_ == 0)
    return;

  char *byte_data = static_cast<char *>(data_);
  const std::size_t elem_size = type.element_size;
  std::size_t write = 0;

  for (std::size_t read = 0; read < size_; ++read) {
    void *current_ptr = byte_data + read * elem_size;

    if (pred(current_ptr)) {
      if (write != read) {
        void *dest_ptr = byte_data + write * elem_size;
        if (type.is_trivially_relocatable()) {
          std::memmove(dest_ptr, current_ptr, elem_size);
        } else {
          operations->move_range(type, dest_ptr, current_ptr, 1);
        }
      }
      ++write;
    } else {
      // Destroy the rejected element
      if (operations->destroy_range) {
        operations->destroy_range(type, data_, read, read + 1);
      }
    }
  }

  size_ = write;
}

RELOCO_API void unowned_vector_base::dedup_by_base(const vector_operations *operations, const type_metadata &type,
                                                   function_ref<bool(const void *, const void *)> same) noexcept {
  if (size_ < 2)
    return;

  char *byte_data = static_cast<char *>(data_);
  const std::size_t elem_size = type.element_size;
  std::size_t write = 1;

  for (std::size_t read = 1; read < size_; ++read) {
    void *prev_ptr = byte_data + (write - 1) * elem_size;
    void *curr_ptr = byte_data + read * elem_size;

    // If adjacent elements match according to the predicate
    if (same(prev_ptr, curr_ptr)) {
      if (operations->destroy_range) {
        operations->destroy_range(type, data_, read, read + 1);
      }
      continue;
    }

    // Otherwise, keep it and shift if necessary
    if (write != read) {
      void *write_ptr = byte_data + write * elem_size;
      if (type.is_trivially_relocatable()) {
        std::memmove(write_ptr, curr_ptr, elem_size);
      } else {
        operations->move_range(type, write_ptr, curr_ptr, 1);
      }
    }
    ++write;
  }

  size_ = write;
}

RELOCO_API result<void *> unowned_vector_base::try_insert_at_base(const vector_operations *operations,
                                                                  allocator_ref alloc, const type_metadata &type,
                                                                  std::size_t index,
                                                                  function_ref<result<void>(void *dest)> construct_fn,
                                                                  void *inline_storage, std::size_t max_inline,
                                                                  std::size_t max_cap) noexcept {

  if (index > size_)
    return unexpected(error::out_of_bounds);

  // Ensure capacity first
  if (size_ == cap_) {
    // Can't exceed max capacity if we're already full
    if (cap_ >= max_cap)
      return unexpected(error::capacity_exceeded);

    std::size_t new_cap;

    if (max_inline && cap_ < max_inline) {
      // Expand up to inline storage if we're below inline
      new_cap = max_inline;
    } else if (cap_ == 0) {
      // Bootstrap from zero
      new_cap = std::min(std::size_t(8), max_cap);
    } else {
      // cap_ < max_cap, so `cap_ + 1` cannot overflow.
      std::size_t growth = (cap_ + 1) / 2;

      // Safe outer addition check
      if (max_cap - cap_ < growth)
        RELOCO_UNLIKELY { new_cap = max_cap; }
      else {
        new_cap = cap_ + growth;
      }
    }

    if (auto res = try_reserve_base(operations, alloc, type, new_cap, inline_storage, max_inline, max_cap); !res) {
      return unexpected(res.error());
    }
  }

  char *byte_data = static_cast<char *>(data_);
  const std::size_t elem_size = type.element_size;

  // Shift existing elements up to make room at `index`
  const std::size_t move_count = size_ - index;
  if (move_count > 0) {
    void *dest = byte_data + (index + 1) * elem_size;
    const void *src = byte_data + index * elem_size;

    if (type.is_trivially_relocatable()) {
      std::memmove(dest, src, move_count * elem_size);
    } else {
      // Shift up using operations table move_range_up
      operations->move_range_up(type, dest, src, move_count);
    }
  }

  // Construct the new element in-place using the callback
  void *dest_ptr = byte_data + index * elem_size;
  if (auto ctor_res = construct_fn(dest_ptr); !ctor_res) {
    // Optional rollback shift if construction fails
    if (move_count > 0) {
      void *dest = byte_data + index * elem_size;
      const void *src = byte_data + (index + 1) * elem_size;
      if (type.is_trivially_relocatable()) {
        std::memmove(dest, src, move_count * elem_size);
      } else {
        operations->move_range(type, dest, src, move_count);
      }
    }
    return unexpected(ctor_res.error());
  }

  ++size_;
  return dest_ptr;
}

RELOCO_API void heap_vector_base::destroy_elements(const vector_operations *operations,
                                                   const type_metadata &type) noexcept {
  if (data_) {
    if (size_ > 0) {
      destroy_elements_base(operations, type);
    }
    alloc_.deallocate(data_, cap_ * type.element_size);
    data_ = nullptr;
    size_ = 0;
    cap_ = 0;
  }
}

RELOCO_API void heap_vector_base::move_assign_from_base(const vector_operations *operations, const type_metadata &type,
                                                        heap_vector_base &&other) noexcept {
  if (this == &other)
    return;

  // Clean up current resources using our new helper
  destroy_elements(operations, type);

  data_ = other.data_;
  size_ = other.size_;
  cap_ = other.cap_;
  alloc_ = other.alloc_;

  other.data_ = nullptr;
  other.size_ = 0;
  other.cap_ = 0;
}

RELOCO_API void inline_vector_base::destroy_elements(const vector_operations *operations,
                                                     const type_metadata &type) noexcept {
  if (data_ && size_ > 0) {
    if (operations->destroy_range) {
      operations->destroy_range(type, data_, 0, size_);
    }
    size_ = 0;
  }
}

RELOCO_API void inline_vector_base::move_construct_from_base(const vector_operations *operations,
                                                             const type_metadata &type,
                                                             inline_vector_base &&other) noexcept {
  if (other.size_ > 0 && operations->move_range) {
    operations->move_range(type, data_, other.data_, other.size_);
    size_ = other.size_;
  } else {
    size_ = 0;
  }

  other.size_ = 0;
}

RELOCO_API void inline_vector_base::move_assign_from_base(const vector_operations *operations,
                                                          const type_metadata &type,
                                                          inline_vector_base &&other) noexcept {
  if (this == &other)
    return;

  destroy_elements(operations, type);
  move_construct_from_base(operations, type, std::move(other));
}

RELOCO_API void outline_vector_base::destroy_elements(const vector_operations *operations,
                                                      const type_metadata &type) noexcept {
  if (data_ && size_ > 0) {
    if (operations->destroy_range) {
      operations->destroy_range(type, data_, 0, size_);
    }
    size_ = 0;
  }
}

RELOCO_API void mixed_vector_base::destroy_elements(const vector_operations *operations,
                                                    const type_metadata &type) noexcept {
  if (data_) {
    if (size_ > 0 && operations && operations->destroy_range) {
      operations->destroy_range(type, data_, 0, size_);
    }
    if (!is_inline()) {
      alloc_.deallocate(data_, cap_ * type.element_size);
    }
    data_ = inline_storage_;
    size_ = 0;
    cap_ = inline_capacity_;
  }
}

RELOCO_API void mixed_vector_base::move_construct_from_base(const vector_operations *operations,
                                                            const type_metadata &type,
                                                            mixed_vector_base &&other) noexcept {
  alloc_ = other.alloc_;

  if (other.is_inline()) {
    // Our data_ is already initialized to our own inline_storage_ by constructor.
    if (other.size_ > 0 && operations && operations->move_range) {
      operations->move_range(type, data_, other.data_, other.size_);
      size_ = other.size_;
    } else {
      size_ = 0;
    }
    cap_ = inline_capacity_;
    other.size_ = 0;
  } else {
    // Steal heap ownership directly with zero memory overhead
    data_ = other.data_;
    size_ = other.size_;
    cap_ = other.cap_;

    // Reset other back to its safe inline state
    other.data_ = other.inline_storage_;
    other.size_ = 0;
    other.cap_ = other.inline_capacity_;
  }
}

RELOCO_API void mixed_vector_base::move_assign_from_base(const vector_operations *operations, const type_metadata &type,
                                                         mixed_vector_base &&other) noexcept {
  if (this == &other)
    return;

  // Clean up current resources (destroys elements and deallocates heap if needed)
  destroy_elements(operations, type);

  // Perform the move transfer
  move_construct_from_base(operations, type, std::move(other));
}

RELOCO_API result<void> unowned_deque_base::try_reserve_base(const vector_operations *ops, allocator_ref alloc,
                                                             const type_metadata &type, std::size_t new_cap,
                                                             void *inline_storage, std::size_t max_inline,
                                                             std::size_t max_cap) noexcept {
  if (new_cap <= cap_)
    return {};
  if (new_cap > max_cap)
    return unexpected(error::capacity_exceeded);

  const std::size_t elem_size = type.element_size;
  const std::size_t required_bytes = new_cap * elem_size;

  RELOCO_DEBUG_ASSERT(max_inline <= max_cap, "Inline capacity larger than heap capacity");

  // Handle embedded SSO/inline storage promotion
  if (inline_storage) {
    RELOCO_DEBUG_ASSERT(max_inline != 0, "Non-empty inline storage with zero capacity");
    if (data_ == inline_storage) {
      if (new_cap <= max_inline) {
        cap_ = max_inline;
        return {};
      }

      // Promote from inline to heap
      auto res = alloc.allocate(required_bytes, type.element_alignment);
      if (!res)
        return unexpected(res.error());

      const auto new_data = static_cast<char *>(res->ptr);
      if (len_ > 0) {
        // Linearize the ring buffer into [0, len_) during the move
        const std::size_t first_chunk = std::min(len_, cap_ - head_);
        ops->move_range(type, new_data, static_cast<char *>(data_) + head_ * elem_size, first_chunk);
        if (first_chunk < len_) {
          ops->move_range(type, new_data + first_chunk * elem_size, data_, len_ - first_chunk);
        }
      }

      data_ = new_data;
      cap_ = res->size / elem_size;
      head_ = 0; // Buffer is now flat
      return {};
    }
  }

  // Try zero-cost in-place expansion first
  if (data_) {
    if (auto res = alloc.expand_in_place(data_, cap_ * elem_size, required_bytes); res) {
      const std::size_t newly_allocated_cap = *res / elem_size;

      // If the buffer was wrapped, expanding it creates a hole in the middle.
      // We fix this by shifting the head chunk to the right, flush against the new capacity.
      if (len_ > 0 && head_ + len_ > cap_) {
        const std::size_t added_cap = newly_allocated_cap - cap_;
        const std::size_t head_len = cap_ - head_;
        const auto byte_data = static_cast<char *>(data_);

        // Because we shift to the right, src and dest may overlap, so move_range_up is required.
        // It's mathematically impossible for this shift to overwrite the tail chunk.
        ops->move_range_up(type, byte_data + (head_ + added_cap) * elem_size, byte_data + head_ * elem_size, head_len);
        head_ += added_cap;
      }

      cap_ = newly_allocated_cap;
      return {};
    }
  }

  // Fallback: Allocate or Reallocate
  auto res = data_ && type.is_trivially_relocatable()
                 ? alloc.reallocate(data_, cap_ * elem_size, required_bytes, type.element_alignment)
                 : alloc.allocate(required_bytes, type.element_alignment);

  if (!res)
    return unexpected(res.error());

  const auto new_data = static_cast<char *>(res->ptr);
  const std::size_t newly_allocated_cap = res->size / elem_size;

  if (data_ && type.is_trivially_relocatable()) {
    // `reallocate` perfectly preserved the bytes, including the logical wrap-around.
    // We fix the wrap exactly the same way as `expand_in_place`.
    if (len_ > 0 && head_ + len_ > cap_) {
      const std::size_t added_cap = newly_allocated_cap - cap_;
      const std::size_t head_len = cap_ - head_;
      ops->move_range_up(type, new_data + (head_ + added_cap) * elem_size, new_data + head_ * elem_size, head_len);
      head_ += added_cap;
    }
  } else {
    // Standard `allocate` path: linearize the chunks into [0, len_) while moving.
    if (len_ > 0) {
      std::size_t first_chunk = std::min(len_, cap_ - head_);
      ops->move_range(type, new_data, static_cast<char *>(data_) + head_ * elem_size, first_chunk);
      if (first_chunk < len_) {
        ops->move_range(type, new_data + first_chunk * elem_size, data_, len_ - first_chunk);
      }
    }
    if (data_)
      alloc.deallocate(data_, cap_ * elem_size);
    head_ = 0; // Buffer is now flat
  }

  data_ = new_data;
  cap_ = newly_allocated_cap;
  return {};
}

RELOCO_API void heap_deque_base::deallocate_elements(const vector_operations *ops, const type_metadata &type) noexcept {
  if (data_) {
    if (len_ > 0) {
      unowned_deque_base::destroy_elements(ops, type);
    }
    alloc_.deallocate(data_, cap_ * type.element_size);
    data_ = nullptr;
    head_ = 0;
    len_ = 0;
    cap_ = 0;
  }
}

RELOCO_API void inline_deque_base::move_construct_from_base(const vector_operations *ops, const type_metadata &type,
                                                            inline_deque_base &&other) noexcept {
  if (other.len_ > 0 && ops->move_range) {
    char *dest = static_cast<char *>(data_);
    char *src = static_cast<char *>(other.data_);
    const std::size_t elem_size = type.element_size;

    // Linearize the wrapped buffer into the new target starting at index 0
    std::size_t first_chunk = std::min(other.len_, other.cap_ - other.head_);
    ops->move_range(type, dest, src + other.head_ * elem_size, first_chunk);

    if (first_chunk < other.len_) {
      ops->move_range(type, dest + first_chunk * elem_size, src, other.len_ - first_chunk);
    }

    len_ = other.len_;
    head_ = 0; // Linearized!
  } else {
    len_ = 0;
    head_ = 0;
  }

  other.len_ = 0;
  other.head_ = 0;
}

RELOCO_API void mixed_deque_base::deallocate_elements(const vector_operations *ops,
                                                      const type_metadata &type) noexcept {
  if (data_) {
    if (len_ > 0)
      unowned_deque_base::destroy_elements(ops, type);
    if (!is_inline())
      alloc_.deallocate(data_, cap_ * type.element_size);

    // Restore the inline state baseline
    data_ = inline_storage_;
    head_ = 0;
    len_ = 0;
    cap_ = inline_capacity_;
  }
}

RELOCO_API void mixed_deque_base::move_construct_from_base(const vector_operations *ops, const type_metadata &type,
                                                           mixed_deque_base &&other) noexcept {
  alloc_ = other.alloc_;

  if (other.is_inline()) {
    if (other.len_ > 0 && ops && ops->move_range) {
      char *dest = static_cast<char *>(data_);
      char *src = static_cast<char *>(other.data_);
      const std::size_t elem_size = type.element_size;

      std::size_t first_chunk = std::min(other.len_, other.cap_ - other.head_);
      ops->move_range(type, dest, src + other.head_ * elem_size, first_chunk);

      if (first_chunk < other.len_) {
        ops->move_range(type, dest + first_chunk * elem_size, src, other.len_ - first_chunk);
      }

      len_ = other.len_;
      head_ = 0; // Linearized on the target's inline stack
    } else {
      len_ = 0;
      head_ = 0;
    }
    cap_ = inline_capacity_;

    other.len_ = 0;
    other.head_ = 0;
  } else {
    // Steal heap allocation via O(1) swap
    data_ = other.data_;
    head_ = other.head_;
    len_ = other.len_;
    cap_ = other.cap_;

    // Reset source to its safe, empty inline baseline
    other.data_ = other.inline_storage_;
    other.head_ = 0;
    other.len_ = 0;
    other.cap_ = other.inline_capacity_;
  }
}

RELOCO_API result<void> unowned_deque_base::try_make_contiguous_base(const vector_operations *ops, allocator_ref alloc,
                                                                     const type_metadata &type, void *inline_storage,
                                                                     std::size_t max_inline,
                                                                     std::size_t max_cap) noexcept {
  std::size_t tail = head_ + len_;
  // Already contiguous (should be caught by the wrapper, but checked for safety)
  if (tail <= cap_)
    return {};

  const std::size_t elem_size = type.element_size;
  char *byte_data = static_cast<char *>(data_);

  // We are wrapped. Break into Chunk 1 (head to cap) and Chunk 2 (0 to tail).
  std::size_t L1 = cap_ - head_;
  std::size_t L2 = tail - cap_;
  std::size_t F = cap_ - len_; // Free space

  // Right-Shift Strategy: Free space is large enough to absorb Chunk 1
  if (F >= L1) {
    // Shift C2 right by L1
    ops->move_range_up(type, byte_data + L1 * elem_size, byte_data, L2);
    // Shift C1 left to 0
    ops->move_range(type, byte_data, byte_data + head_ * elem_size, L1);

    head_ = 0;
    return {};
  }

  // Left-Shift Strategy: Free space is large enough to absorb Chunk 2
  if (F >= L2) {
    // Shift C1 left by L2
    ops->move_range(type, byte_data + (head_ - L2) * elem_size, byte_data + head_ * elem_size, L1);
    // Shift C2 right to the end
    ops->move_range_up(type, byte_data + (cap_ - L2) * elem_size, byte_data, L2);

    head_ -= L2;
    return {};
  }

  // Hard Case (F < L1 && F < L2): Very little free space (or completely full).
  auto temp_res = alloc.allocate(L2 * elem_size, type.element_alignment);

  if (!temp_res) {
    // Fallback: forcefully expand capacity by 1, which inherently calls
    // try_reserve_base and linearizes the array securely during reallocation.
    // If cap_ == max_cap, this will fail with capacity_exceeded.
    return try_reserve_base(ops, alloc, type, cap_ + 1, inline_storage, max_inline, max_cap);
  }

  char *temp = static_cast<char *>(temp_res->ptr);

  // Move Chunk 2 out to temp
  ops->move_range(type, temp, byte_data, L2);
  // Move Chunk 1 to index 0 (safe left-shift)
  ops->move_range(type, byte_data, byte_data + head_ * elem_size, L1);
  // Move Chunk 2 back into the middle
  ops->move_range(type, byte_data + L1 * elem_size, temp, L2);

  alloc.deallocate(temp_res->ptr, temp_res->size);

  head_ = 0;
  return {};
}

RELOCO_API void unowned_deque_base::rotate_left_base(const vector_operations *ops, const type_metadata &type,
                                                     std::size_t mid) noexcept {
  if (len_ <= 1 || mid == 0 || mid == len_)
    return;
  mid %= len_;

  // O(1) rotation for fully packed ring buffers (zero physical moves!)
  if (len_ == cap_) {
    head_ = (head_ + mid) % cap_;
    return;
  }

  const std::size_t right_rot = len_ - mid;
  const std::size_t elem_size = type.element_size;
  char *byte_data = static_cast<char *>(data_);

  if (mid <= right_rot) {
    // Shift left: move `mid` elements from the front of the deque to the back.
    std::size_t remaining = mid;
    while (remaining > 0) {
      std::size_t chunk = std::min(remaining, cap_ - len_); // Bounded by free space

      // Contiguous source starting at `head_`
      std::size_t src_chunk = cap_ - head_;

      // Contiguous free space starting at `tail`
      std::size_t tail = (head_ + len_) % cap_;
      std::size_t dest_chunk = cap_ - tail;

      chunk = std::min({chunk, src_chunk, dest_chunk});

      ops->move_range(type, byte_data + tail * elem_size, byte_data + head_ * elem_size, chunk);
      head_ = (head_ + chunk) % cap_;
      remaining -= chunk;
    }
  } else {
    // Shift right: move `right_rot` elements from the back of the deque to the front.
    std::size_t remaining = right_rot;
    while (remaining > 0) {
      std::size_t chunk = std::min(remaining, cap_ - len_); // Bounded by free space

      std::size_t tail = (head_ + len_) % cap_;

      // The contiguous block of source elements ending at `tail`
      std::size_t src_chunk = (tail == 0) ? cap_ : tail;

      // The contiguous block of free space ending at `head_`
      std::size_t dest_chunk = (head_ == 0) ? cap_ : head_;

      chunk = std::min({chunk, src_chunk, dest_chunk});

      std::size_t src_start = (tail == 0) ? cap_ - chunk : tail - chunk;
      std::size_t dest_start = (head_ == 0) ? cap_ - chunk : head_ - chunk;

      ops->move_range(type, byte_data + dest_start * elem_size, byte_data + src_start * elem_size, chunk);
      head_ = dest_start;
      remaining -= chunk;
    }
  }
}

RELOCO_API result<void> unowned_deque_base::try_erase_at_base(const vector_operations *ops, const type_metadata &type,
                                                              std::size_t index) noexcept {
  if (index >= len_)
    return unexpected(error::out_of_bounds);

  char *byte_data = static_cast<char *>(data_);
  const std::size_t elem_size = type.element_size;

  // Calculate physical index and destroy the target element
  std::size_t physical_idx = head_ + index;
  if (physical_idx >= cap_)
    physical_idx -= cap_;

  if (ops->destroy_range) {
    ops->destroy_range(type, data_, physical_idx, physical_idx + 1);
  }

  // Shortest Shift Optimization
  if (index < len_ / 2) {
    // Shift logical [0, index) RIGHT by 1
    if (index > 0) {
      const std::size_t src_start = head_;
      const std::size_t src_end = head_ + index;

      if (src_end <= cap_) {
        if (src_end < cap_) {
          // Completely contiguous shift right
          ops->move_range_up(type, byte_data + (src_start + 1) * elem_size, byte_data + src_start * elem_size, index);
        } else {
          // Exactly hits capacity: the last element wraps to 0
          ops->move_range(type, byte_data, byte_data + (cap_ - 1) * elem_size, 1);
          if (index > 1) {
            ops->move_range_up(type, byte_data + (src_start + 1) * elem_size, byte_data + src_start * elem_size,
                               index - 1);
          }
        }
      } else {
        // Source chunk is physically wrapped across the boundary
        const std::size_t len2 = src_end - cap_;
        ops->move_range_up(type, byte_data + 1 * elem_size, byte_data, len2);

        ops->move_range(type, byte_data, byte_data + (cap_ - 1) * elem_size, 1);

        const std::size_t len1 = cap_ - head_ - 1;
        if (len1 > 0) {
          ops->move_range_up(type, byte_data + (head_ + 1) * elem_size, byte_data + head_ * elem_size, len1);
        }
      }
    }
    // Update head
    head_ = (head_ + 1 == cap_) ? 0 : head_ + 1;
  } else {
    // Shift logical [index + 1, len_) LEFT by 1
    const std::size_t to_move = len_ - index - 1;
    if (to_move > 0) {
      std::size_t src_start = physical_idx + 1;
      if (src_start >= cap_)
        src_start -= cap_;
      const std::size_t src_end = src_start + to_move;

      if (src_end <= cap_) {
        if (src_start > 0) {
          // Completely contiguous shift left
          ops->move_range(type, byte_data + (src_start - 1) * elem_size, byte_data + src_start * elem_size, to_move);
        } else {
          // Exactly starts at 0: the first element wraps to cap_ - 1
          ops->move_range(type, byte_data + (cap_ - 1) * elem_size, byte_data, 1);
          if (to_move > 1) {
            ops->move_range(type, byte_data, byte_data + 1 * elem_size, to_move - 1);
          }
        }
      } else {
        // Source chunk is physically wrapped across the boundary
        const std::size_t len1 = cap_ - src_start;
        ops->move_range(type, byte_data + (src_start - 1) * elem_size, byte_data + src_start * elem_size, len1);

        ops->move_range(type, byte_data + (cap_ - 1) * elem_size, byte_data, 1);

        const std::size_t len2 = src_end - cap_ - 1;
        if (len2 > 0) {
          ops->move_range(type, byte_data, byte_data + 1 * elem_size, len2);
        }
      }
    }
  }

  --len_;
  return {};
}

RELOCO_END_UNSAFE_BUFFER_USAGE
