// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file bytes.hpp
 * @brief `bytes`/`bytes_mut`: an immutable, cheaply-cloneable, shareable
 * byte buffer and its growable, exclusively-owned mutable counterpart,
 * matching the shape (not the exact internals) of Rust's `bytes` crate
 * `Bytes`/`BytesMut`.
 *
 * `bytes_mut` is a move-only, allocator-backed, growable `std::byte`
 * buffer -- structurally a hand-written specialization of `vector<T>` for
 * `T = std::byte` (see `vector.hpp`), kept as its own small, non-template
 * class rather than composing `vector<std::byte>` because `try_freeze()`
 * needs to transfer its raw allocation into a fresh `bytes` without
 * copying the payload, and `vector<T>` exposes no such ownership-transfer
 * primitive. Growth follows the exact same strategy `vector<T>` documents
 * for trivially relocatable `T` (`std::byte` always is): prefer
 * `allocator_ref::expand_in_place`, then `allocator_ref::reallocate`, and
 * only fall back to a fresh `allocate` + `std::memcpy` + `deallocate` when
 * neither is supported by the bound allocator.
 *
 * `bytes` is a reference-counted, immutable view: cloning it
 * (copy-construction) is an `rc<detail::bytes_storage>` refcount bump, not
 * a payload copy, and `slice()`/`split_to()`/`split_off()` are all O(1),
 * sharing the same backing allocation via `rc<T>`'s existing copy
 * machinery (see `rc.hpp`) while only adjusting a local pointer/length
 * pair -- exactly Rust's `Bytes::slice`/`split_to`/`split_off` semantics.
 *
 * `bytes_mut::try_freeze()` converts a `bytes_mut` into a `bytes` without
 * copying the byte payload: it moves the existing heap pointer/capacity
 * into a freshly allocated `rc<detail::bytes_storage>` control block. That
 * control block allocation itself is small and unavoidable (it is what
 * makes the buffer refcounted/shareable in the first place) -- only the
 * potentially large byte payload is guaranteed not to be copied, matching
 * the spirit (if not the single-allocation, tagged-pointer implementation
 * trick) of the real `bytes` crate's zero-copy freeze.
 *
 * Unlike the real `bytes` crate, `bytes_mut` itself does not support
 * `split_to`/`split_off`/`unsplit` (those require the same shared,
 * refcounted storage `bytes` uses, which would make every `bytes_mut`
 * mutation check for other live aliases first) -- `bytes_mut` stays a
 * plain, exclusively-owned growable buffer, matching how the rest of
 * reloco keeps mutable and shared ownership as two entirely separate
 * types (`vector<T>` vs. `rc<T>`) rather than one type that does both.
 * `bytes` also has no `Buf`-style advancing-cursor reader (`get_u8` and
 * friends) -- only the `BufMut`-style `try_put_*` write helpers on
 * `bytes_mut` -- since a stateful reader is an orthogonal feature that can
 * be layered on top of `as_span()` if/when it is needed.
 */

#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "rc.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"
#include "span.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

// Raw byte-buffer plumbing throughout (allocator calls, memcpy, pointer
// arithmetic): a single checked boundary, matching vector.hpp/allocator.hpp.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

class bytes;
class bytes_mut;

namespace detail {

/**
 * @brief Control-block payload for `bytes`: owns the single heap
 * allocation shared by every `bytes` sliced/cloned from the same root.
 * Freed through `alloc` once the last `rc<bytes_storage>` releases it.
 */
struct RELOCO_EXPORT bytes_storage {
  std::byte *data;
  std::size_t capacity;
  allocator_ref alloc;

  constexpr bytes_storage(std::byte *d, std::size_t cap, allocator_ref a) noexcept : data(d), capacity(cap), alloc(a) {}

  bytes_storage(const bytes_storage &) = delete;
  bytes_storage &operator=(const bytes_storage &) = delete;

  ~bytes_storage() noexcept {
    if (data)
      alloc.deallocate(data, capacity);
  }
};

} // namespace detail

/**
 * @brief Immutable, reference-counted, cheaply-cloneable byte buffer.
 * Matches Rust's `bytes::Bytes`.
 *
 * Default-constructed as empty (no allocation). Every non-empty instance
 * is produced by `try_copy_from`, `slice`/`try_slice`, `split_to`,
 * `split_off`, or `bytes_mut::try_freeze`.
 */
class RELOCO_POINTER bytes {
public:
  constexpr bytes() noexcept = default;

  bytes(const bytes &) = default;
  bytes(bytes &&) noexcept = default;
  bytes &operator=(const bytes &) = default;
  bytes &operator=(bytes &&) noexcept = default;

  /**
   * @brief Allocates a new buffer and copies `data` into it. Returns an
   * empty `bytes` (no allocation) when `data` is empty.
   */
  [[nodiscard]] static RELOCO_API result<bytes> try_copy_from(span<const std::byte> data,
                                                              allocator_ref alloc = default_allocator()) noexcept;

  [[nodiscard]] constexpr std::size_t size() const noexcept { return len_; }
  [[nodiscard]] constexpr bool is_empty() const noexcept { return len_ == 0; }
  [[nodiscard]] constexpr const std::byte *data() const noexcept RELOCO_LIFETIMEBOUND { return ptr_; }

  [[nodiscard]] constexpr span<const std::byte> as_span() const noexcept RELOCO_LIFETIMEBOUND {
    return span<const std::byte>(ptr_, len_);
  }

  [[nodiscard]] std::byte operator[](std::size_t index) const noexcept {
    RELOCO_ASSERT(index < len_, "bytes: index out of range");
    return ptr_[index];
  }

  [[nodiscard]] result<std::byte> try_at(std::size_t index) const noexcept {
    if (index >= len_)
      return unexpected(error::out_of_bounds);
    return ptr_[index];
  }

  /**
   * @brief Returns a `[offset, offset + len)` sub-view sharing the same
   * backing allocation (O(1): an `rc` refcount bump plus a pointer/length
   * adjustment, never a payload copy).
   */
  [[nodiscard]] bytes slice(std::size_t offset, std::size_t len) const noexcept {
    RELOCO_ASSERT(offset <= len_ && len <= len_ - offset, "bytes: slice out of range");
    bytes result;
    result.storage_ = storage_;
    result.ptr_ = ptr_ + offset;
    result.len_ = len;
    return result;
  }

  [[nodiscard]] result<bytes> try_slice(std::size_t offset, std::size_t len) const noexcept {
    if (offset > len_ || len > len_ - offset)
      return unexpected(error::out_of_bounds);
    return slice(offset, len);
  }

  /**
   * @brief Splits off `[0, at)`, keeping `[at, size())` in `*this`. Matches
   * Rust's `Bytes::split_to`.
   */
  [[nodiscard]] bytes split_to(std::size_t at) noexcept {
    RELOCO_ASSERT(at <= len_, "bytes: split_to out of range");
    bytes front = slice(0, at);
    ptr_ += at;
    len_ -= at;
    return front;
  }

  [[nodiscard]] result<bytes> try_split_to(std::size_t at) noexcept {
    if (at > len_)
      return unexpected(error::out_of_bounds);
    return split_to(at);
  }

  /**
   * @brief Splits off `[at, size())`, keeping `[0, at)` in `*this`. Matches
   * Rust's `Bytes::split_off`.
   */
  [[nodiscard]] bytes split_off(std::size_t at) noexcept {
    RELOCO_ASSERT(at <= len_, "bytes: split_off out of range");
    bytes back = slice(at, len_ - at);
    len_ = at;
    return back;
  }

  [[nodiscard]] result<bytes> try_split_off(std::size_t at) noexcept {
    if (at > len_)
      return unexpected(error::out_of_bounds);
    return split_off(at);
  }

  void clear() noexcept {
    storage_.reset();
    ptr_ = nullptr;
    len_ = 0;
  }

private:
  rc<detail::bytes_storage> storage_;
  const std::byte *ptr_ = nullptr;
  std::size_t len_ = 0;

  friend class bytes_mut;
};

[[nodiscard]] inline bool operator==(const bytes &lhs, const bytes &rhs) noexcept {
  return lhs.size() == rhs.size() && (lhs.size() == 0 || std::memcmp(lhs.data(), rhs.data(), lhs.size()) == 0);
}

[[nodiscard]] inline bool operator!=(const bytes &lhs, const bytes &rhs) noexcept { return !(lhs == rhs); }

/**
 * @brief Move-only, allocator-backed, growable `std::byte` buffer. Matches
 * Rust's `bytes::BytesMut` (minus `split_to`/`split_off`/`unsplit`; see the
 * file-level doc comment).
 */
class RELOCO_OWNER bytes_mut {
public:
  constexpr explicit bytes_mut(allocator_ref alloc = default_allocator()) noexcept : alloc_(alloc) {}

  ~bytes_mut() noexcept {
    if (data_)
      alloc_.deallocate(data_, capacity_);
  }

  bytes_mut(bytes_mut &&other) noexcept
      : alloc_(other.alloc_), data_(other.data_), len_(other.len_), capacity_(other.capacity_) {
    other.data_ = nullptr;
    other.len_ = 0;
    other.capacity_ = 0;
  }

  bytes_mut &operator=(bytes_mut &&other) noexcept {
    if (this != &other) {
      if (data_)
        alloc_.deallocate(data_, capacity_);
      alloc_ = other.alloc_;
      data_ = other.data_;
      len_ = other.len_;
      capacity_ = other.capacity_;
      other.data_ = nullptr;
      other.len_ = 0;
      other.capacity_ = 0;
    }
    return *this;
  }

  bytes_mut(const bytes_mut &) = delete;
  bytes_mut &operator=(const bytes_mut &) = delete;

  [[nodiscard]] static result<bytes_mut> try_allocate(allocator_ref alloc, std::size_t initial_cap = 0) noexcept {
    bytes_mut buf(alloc);
    if (initial_cap > 0) {
      if (auto res = buf.try_reserve(initial_cap); !res)
        return unexpected(res.error());
    }
    return buf;
  }

  [[nodiscard]] static result<bytes_mut> try_create(std::size_t initial_cap = 0) noexcept {
    return try_allocate(default_allocator(), initial_cap);
  }

  /**
   * @brief Ensures at least `additional` more bytes can be pushed/put
   * without a further allocation. Prefers growing in place; see the
   * file-level doc comment for the full fallback chain.
   */
  [[nodiscard]] RELOCO_API result<void> try_reserve(std::size_t additional) noexcept;

  [[nodiscard]] result<void> try_push(std::byte value) noexcept {
    if (auto res = try_reserve(1); !res)
      return unexpected(res.error());
    data_[len_++] = value;
    return {};
  }

  /** @brief Appends a copy of `data`. Matches Rust's `BufMut::put_slice`. */
  [[nodiscard]] RELOCO_API result<void> try_put_slice(span<const std::byte> data) noexcept;

  [[nodiscard]] result<void> try_extend_from_slice(span<const std::byte> data) noexcept { return try_put_slice(data); }

  [[nodiscard]] result<void> try_put_u8(std::uint8_t value) noexcept { return try_push(static_cast<std::byte>(value)); }

  [[nodiscard]] result<void> try_put_u16_le(std::uint16_t value) noexcept {
    std::byte buf[2] = {static_cast<std::byte>(value & 0xff), static_cast<std::byte>((value >> 8) & 0xff)};
    return try_put_slice(span<const std::byte>(buf, 2));
  }

  [[nodiscard]] result<void> try_put_u16_be(std::uint16_t value) noexcept {
    std::byte buf[2] = {static_cast<std::byte>((value >> 8) & 0xff), static_cast<std::byte>(value & 0xff)};
    return try_put_slice(span<const std::byte>(buf, 2));
  }

  [[nodiscard]] result<void> try_put_u32_le(std::uint32_t value) noexcept {
    std::byte buf[4];
    for (std::size_t i = 0; i < 4; ++i)
      buf[i] = static_cast<std::byte>((value >> (8 * i)) & 0xff);
    return try_put_slice(span<const std::byte>(buf, 4));
  }

  [[nodiscard]] result<void> try_put_u32_be(std::uint32_t value) noexcept {
    std::byte buf[4];
    for (std::size_t i = 0; i < 4; ++i)
      buf[i] = static_cast<std::byte>((value >> (8 * (3 - i))) & 0xff);
    return try_put_slice(span<const std::byte>(buf, 4));
  }

  [[nodiscard]] result<void> try_put_u64_le(std::uint64_t value) noexcept {
    std::byte buf[8];
    for (std::size_t i = 0; i < 8; ++i)
      buf[i] = static_cast<std::byte>((value >> (8 * i)) & 0xff);
    return try_put_slice(span<const std::byte>(buf, 8));
  }

  [[nodiscard]] result<void> try_put_u64_be(std::uint64_t value) noexcept {
    std::byte buf[8];
    for (std::size_t i = 0; i < 8; ++i)
      buf[i] = static_cast<std::byte>((value >> (8 * (7 - i))) & 0xff);
    return try_put_slice(span<const std::byte>(buf, 8));
  }

  [[nodiscard]] constexpr std::size_t len() const noexcept { return len_; }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return len_; }
  [[nodiscard]] constexpr std::size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] constexpr bool is_empty() const noexcept { return len_ == 0; }
  [[nodiscard]] constexpr allocator_ref get_allocator() const noexcept { return alloc_; }

  void clear() noexcept { len_ = 0; }

  [[nodiscard]] std::byte &operator[](std::size_t index) noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < len_, "bytes_mut: index out of range");
    return data_[index];
  }

  [[nodiscard]] const std::byte &operator[](std::size_t index) const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < len_, "bytes_mut: index out of range");
    return data_[index];
  }

  [[nodiscard]] constexpr std::byte *data() noexcept RELOCO_LIFETIMEBOUND { return data_; }
  [[nodiscard]] constexpr const std::byte *data() const noexcept RELOCO_LIFETIMEBOUND { return data_; }

  [[nodiscard]] constexpr span<std::byte> as_span() noexcept RELOCO_LIFETIMEBOUND {
    return span<std::byte>(data_, len_);
  }

  [[nodiscard]] constexpr span<const std::byte> as_span() const noexcept RELOCO_LIFETIMEBOUND {
    return span<const std::byte>(data_, len_);
  }

  /**
   * @brief Converts `*this` into an immutable `bytes` without copying the
   * byte payload (see the file-level doc comment for exactly what "zero
   * copy" means here). Consumes `*this`: the moved-from `bytes_mut` is
   * left empty, owning nothing.
   */
  [[nodiscard]] RELOCO_API result<bytes> try_freeze() && noexcept;

private:
  [[nodiscard]] RELOCO_API result<void> grow_to(std::size_t new_cap) noexcept;

  allocator_ref alloc_;
  std::byte *data_ = nullptr;
  std::size_t len_ = 0;
  std::size_t capacity_ = 0;
};

/**
 * @brief `bytes` only holds an `rc<detail::bytes_storage>` plus a pointer
 * and a size, with no self-reference into its own storage.
 */
template <> struct is_trivially_relocatable<bytes> : std::true_type {};

/**
 * @brief `bytes_mut` only holds an `allocator_ref` plus a pointer and two
 * sizes, with no self-reference into its own storage.
 */
template <> struct is_trivially_relocatable<bytes_mut> : std::true_type {};

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "bytes.ipp"
#endif

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
