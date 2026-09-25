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

RELOCO_API result<void> unowned_vector_base::try_reserve_base(allocator_ref alloc, const type_metadata &type,
                                                              std::size_t new_cap, void *inline_storage,
                                                              std::size_t max_inline, std::size_t max_cap) noexcept {
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
        operations_->move_range(type, res->ptr, data_, size_);
      }
      // We don't have to "free" inline storage, just destroy the contents
      if (!type.is_trivially_relocatable) {
        // If type is trivially relocatable, then move_range has already killed source objects
        // so we can't destroy them
        destroy_elements_base(type);
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

  if (type.is_trivially_relocatable) {
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
      operations_->move_range(type, new_data, data_, size_);
    }

    if (data_)
      alloc.deallocate(data_, cap_ * elem_size);
    data_ = new_data;
    cap_ = res->size / elem_size;
  }
  return {};
}

result<void> unowned_vector_base::try_resize_base(allocator_ref alloc, const type_metadata &type, std::size_t count,
                                                  const void *value_ptr, void *inline_storage, std::size_t max_inline,
                                                  std::size_t max_cap) noexcept {
  if (count <= size_) {
    if (operations_->destroy_range) {
      operations_->destroy_range(type, data_, count, size_);
    }
    size_ = count;
    return {};
  }

  auto res = try_reserve_base(alloc, type, count, inline_storage, max_inline, max_cap);
  if (!res)
    return unexpected(res.error());

  if (operations_->copy_construct_range) {
    auto ctor_res = operations_->copy_construct_range(type, alloc, data_, size_, count, value_ptr);
    if (!ctor_res) {
      return unexpected(ctor_res.error());
    }
  }

  size_ = count;
  return {};
}

RELOCO_API result<void> unowned_vector_base::shrink_to_fit_base(allocator_ref alloc, const type_metadata &type,
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
        operations_->move_range(type, inline_storage, data_, size_);
      }
      // Destroy heap elements
      if (!type.is_trivially_relocatable) {
        // Only if they're not trivially relocatable. Trivial relocation prohibits source destruction
        // from being ran
        destroy_elements_base(type);
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

  if (type.is_trivially_relocatable) {
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
    operations_->move_range(type, new_data, data_, size_);

    alloc.deallocate(data_, cap_ * elem_size);
    data_ = new_data;
    cap_ = res->size / elem_size;
  }
  return {};
}

RELOCO_API result<void> unowned_vector_base::try_pop_back_base(const type_metadata &type) noexcept {
  if (size_ == 0)
    return unexpected(error::container_empty);

  --size_;
  if (operations_->destroy_range) {
    operations_->destroy_range(type, data_, size_, size_ + 1);
  }
  return {};
}

RELOCO_API result<void> unowned_vector_base::try_erase_at_base(const type_metadata &type, std::size_t index) noexcept {
  if (index >= size_)
    return unexpected(error::out_of_bounds);

  char *byte_data = static_cast<char *>(data_);
  const std::size_t elem_size = type.element_size;

  // Destroy the element being removed via operations table
  if (operations_->destroy_range) {
    operations_->destroy_range(type, data_, index, index + 1);
  }

  // Shift remaining elements down if any
  const std::size_t move_count = size_ - index - 1;
  if (move_count > 0) {
    void *dest = byte_data + index * elem_size;
    const void *src = byte_data + (index + 1) * elem_size;

    if (type.is_trivially_relocatable) {
      std::memmove(dest, src, move_count * elem_size);
    } else {
      // Reuse operations_->move_range to safely shift non-trivial elements down
      operations_->move_range(type, dest, src, move_count);
    }
  }

  --size_;
  return {};
}

RELOCO_API void unowned_vector_base::retain_base(const type_metadata &type,
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
        if (type.is_trivially_relocatable) {
          std::memmove(dest_ptr, current_ptr, elem_size);
        } else {
          operations_->move_range(type, dest_ptr, current_ptr, 1);
        }
      }
      ++write;
    } else {
      // Destroy the rejected element
      if (operations_->destroy_range) {
        operations_->destroy_range(type, data_, read, read + 1);
      }
    }
  }

  size_ = write;
}

RELOCO_API void unowned_vector_base::dedup_by_base(const type_metadata &type,
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
      if (operations_->destroy_range) {
        operations_->destroy_range(type, data_, read, read + 1);
      }
      continue;
    }

    // Otherwise, keep it and shift if necessary
    if (write != read) {
      void *write_ptr = byte_data + write * elem_size;
      if (type.is_trivially_relocatable) {
        std::memmove(write_ptr, curr_ptr, elem_size);
      } else {
        operations_->move_range(type, write_ptr, curr_ptr, 1);
      }
    }
    ++write;
  }

  size_ = write;
}

RELOCO_API result<void *> unowned_vector_base::try_insert_at_base(allocator_ref alloc, const type_metadata &type,
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
    } else {
      // Expand up to max storage
      new_cap = std::min(cap_ == 0 ? std::size_t(8) : cap_ + ((cap_ + 1) / 2), max_cap);
    }

    if (auto res = try_reserve_base(alloc, type, new_cap, inline_storage, max_inline, max_cap); !res) {
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

    if (type.is_trivially_relocatable) {
      std::memmove(dest, src, move_count * elem_size);
    } else {
      // Shift up using operations table move_range_up
      operations_->move_range_up(type, dest, src, move_count);
    }
  }

  // Construct the new element in-place using the callback
  void *dest_ptr = byte_data + index * elem_size;
  if (auto ctor_res = construct_fn(dest_ptr); !ctor_res) {
    // Optional rollback shift if construction fails
    if (move_count > 0) {
      void *dest = byte_data + index * elem_size;
      const void *src = byte_data + (index + 1) * elem_size;
      if (type.is_trivially_relocatable) {
        std::memmove(dest, src, move_count * elem_size);
      } else {
        operations_->move_range(type, dest, src, move_count);
      }
    }
    return unexpected(ctor_res.error());
  }

  ++size_;
  return dest_ptr;
}

RELOCO_API void heap_vector_base::destroy_elements(const type_metadata &type) noexcept {
  if (data_) {
    if (size_ > 0) {
      destroy_elements_base(type);
    }
    alloc_.deallocate(data_, cap_ * type.element_size);
    data_ = nullptr;
    size_ = 0;
    cap_ = 0;
  }
}

RELOCO_API void heap_vector_base::move_assign_from_base(const type_metadata &type, heap_vector_base &&other) noexcept {
  if (this == &other)
    return;

  // Clean up current resources using our new helper
  destroy_elements(type);

  operations_ = other.operations_;
  data_ = other.data_;
  size_ = other.size_;
  cap_ = other.cap_;
  alloc_ = other.alloc_;

  other.operations_ = nullptr;
  other.data_ = nullptr;
  other.size_ = 0;
  other.cap_ = 0;
}

RELOCO_API void inline_vector_base::destroy_elements(const type_metadata &type) noexcept {
  if (data_ && size_ > 0) {
    if (operations_->destroy_range) {
      operations_->destroy_range(type, data_, 0, size_);
    }
    size_ = 0;
  }
}

RELOCO_API void inline_vector_base::move_construct_from_base(const type_metadata &type,
                                                             inline_vector_base &&other) noexcept {
  operations_ = other.operations_;

  if (other.size_ > 0 && operations_->move_range) {
    operations_->move_range(type, data_, other.data_, other.size_);
    size_ = other.size_;
  } else {
    size_ = 0;
  }

  other.operations_ = nullptr;
  other.size_ = 0;
}

RELOCO_API void inline_vector_base::move_assign_from_base(const type_metadata &type,
                                                          inline_vector_base &&other) noexcept {
  if (this == &other)
    return;

  destroy_elements(type);
  move_construct_from_base(type, std::move(other));
}

RELOCO_API void mixed_vector_base::destroy_elements(const type_metadata &type) noexcept {
  if (data_) {
    if (size_ > 0 && operations_ && operations_->destroy_range) {
      operations_->destroy_range(type, data_, 0, size_);
    }
    if (!is_inline()) {
      alloc_.deallocate(data_, cap_ * type.element_size);
    }
    data_ = inline_storage_;
    size_ = 0;
    cap_ = inline_capacity_;
  }
}

RELOCO_API void mixed_vector_base::move_construct_from_base(const type_metadata &type,
                                                            mixed_vector_base &&other) noexcept {
  operations_ = other.operations_;
  alloc_ = other.alloc_;

  if (other.is_inline()) {
    // Our data_ is already initialized to our own inline_storage_ by constructor.
    if (other.size_ > 0 && operations_ && operations_->move_range) {
      operations_->move_range(type, data_, other.data_, other.size_);
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

RELOCO_API void mixed_vector_base::move_assign_from_base(const type_metadata &type,
                                                         mixed_vector_base &&other) noexcept {
  if (this == &other)
    return;

  // Clean up current resources (destroys elements and deallocates heap if needed)
  destroy_elements(type);

  // Perform the move transfer
  move_construct_from_base(type, std::move(other));
}

RELOCO_END_UNSAFE_BUFFER_USAGE
