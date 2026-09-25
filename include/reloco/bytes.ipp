// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file bytes.ipp @brief Out-of-line bodies for bytes/bytes_mut's heavier,
 * allocation/copy-driving methods (see bytes.hpp). Included from
 * bytes.hpp itself, guarded on RELOCO_SHARED_PROVIDE_DEFINITIONS (see
 * reloco/detail/compat.hpp). Never included directly.
 */

RELOCO_API result<bytes> bytes::try_copy_from(span<const std::byte> data, allocator_ref alloc) noexcept {
  if (data.empty())
    return bytes();

  auto block = alloc.allocate(data.size(), alignof(std::max_align_t));
  if (!block)
    return unexpected(block.error());
  auto *raw = static_cast<std::byte *>(block->ptr);
  std::memcpy(raw, data.data(), data.size());

  auto storage = try_allocate_combined_rc<detail::bytes_storage>(alloc, raw, block->size, alloc);
  if (!storage) {
    alloc.deallocate(raw, block->size);
    return unexpected(storage.error());
  }

  bytes result;
  result.storage_ = std::move(*storage);
  result.ptr_ = raw;
  result.len_ = data.size();
  return result;
}

RELOCO_API result<void> bytes_mut::try_reserve(std::size_t additional) noexcept {
  std::size_t required = len_ + additional;
  if (required < len_) // overflow
    return unexpected(error::integer_overflow);
  if (required <= capacity_)
    return {};

  std::size_t new_cap = capacity_ == 0 ? 64 : capacity_;
  while (new_cap < required) {
    std::size_t doubled = new_cap * 2;
    if (doubled < new_cap) { // overflow: clamp to exactly what's required
      new_cap = required;
      break;
    }
    new_cap = doubled;
  }
  return grow_to(new_cap);
}

RELOCO_API result<void> bytes_mut::try_put_slice(span<const std::byte> data) noexcept {
  if (data.empty())
    return {};
  if (auto res = try_reserve(data.size()); !res)
    return unexpected(res.error());
  std::memcpy(data_ + len_, data.data(), data.size());
  len_ += data.size();
  return {};
}

RELOCO_API result<bytes> bytes_mut::try_freeze() && noexcept {
  bytes result;
  if (data_ == nullptr || len_ == 0) {
    if (data_)
      alloc_.deallocate(data_, capacity_);
    data_ = nullptr;
    len_ = 0;
    capacity_ = 0;
    return result;
  }

  auto storage = try_allocate_combined_rc<detail::bytes_storage>(alloc_, data_, capacity_, alloc_);
  if (!storage)
    return unexpected(storage.error());

  result.storage_ = std::move(*storage);
  result.ptr_ = data_;
  result.len_ = len_;

  data_ = nullptr;
  len_ = 0;
  capacity_ = 0;
  return result;
}

RELOCO_API result<void> bytes_mut::grow_to(std::size_t new_cap) noexcept {
  if (data_ && alloc_.can_expand_in_place()) {
    if (auto res = alloc_.expand_in_place(data_, capacity_, new_cap)) {
      capacity_ = *res;
      return {};
    }
  }
  if (data_ && alloc_.can_reallocate()) {
    if (auto res = alloc_.reallocate(data_, capacity_, new_cap, alignof(std::max_align_t))) {
      data_ = static_cast<std::byte *>(res->ptr);
      capacity_ = res->size;
      return {};
    }
  }
  auto block = alloc_.allocate(new_cap, alignof(std::max_align_t));
  if (!block)
    return unexpected(block.error());
  if (data_) {
    std::memcpy(block->ptr, data_, len_);
    alloc_.deallocate(data_, capacity_);
  }
  data_ = static_cast<std::byte *>(block->ptr);
  capacity_ = block->size;
  return {};
}
