// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boxed_slice.hpp
 * @brief Fixed-size, allocator-backed owned array, matching Rust's
 * `Box<[T]>`.
 *
 * `boxed_slice<T>` owns exactly `size()` heap-allocated elements of `T`,
 * with no spare capacity and no ability to grow/shrink after construction
 * -- the allocation-free-of-slack counterpart to `vector<T>` (see
 * `vector.hpp`), exactly like Rust's `Box<[T]>` versus `Vec<T>`. Prefer
 * `boxed_slice<T>` over `vector<T>` once a collection's size is finalized
 * and it will be stored for a while: it never wastes capacity headroom
 * and is one word smaller (no separate capacity field).
 *
 * Construct via `try_allocate`/`try_create` (default-constructs every
 * element) or their `(count, value)` overloads (copy-constructs `value`
 * into every element), or convert an already-built `vector<T>` via
 * `try_from_vector` (Rust's `Vec::into_boxed_slice`), which moves each
 * element into an exactly-sized allocation and releases the source
 * vector's own (possibly larger) buffer.
 *
 * Move-only, checked/fallible/unsafe tri-tier element access matching
 * every other reloco container (see `docs/hardened-containers.md`).
 */

#include "construction_helpers.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"
#include "vector.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T> class RELOCO_OWNER boxed_slice {
public:
  using value_type = T;
  using size_type = std::size_t;
  using iterator = T *;
  using const_iterator = const T *;

  constexpr boxed_slice() noexcept = default;

  boxed_slice(const boxed_slice &) = delete;
  boxed_slice &operator=(const boxed_slice &) = delete;

  boxed_slice(boxed_slice &&other) noexcept : ptr_(other.ptr_), size_(other.size_), alloc_(other.alloc_) {
    other.ptr_ = nullptr;
    other.size_ = 0;
  }

  boxed_slice &operator=(boxed_slice &&other) noexcept {
    if (this != &other) {
      destroy();
      ptr_ = other.ptr_;
      size_ = other.size_;
      alloc_ = other.alloc_;
      other.ptr_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }

  ~boxed_slice() { destroy(); }

  /**
   * @brief Allocates room for @p count elements and default-constructs
   * each one, using @p alloc explicitly. Returns an empty (null, zero-size)
   * `boxed_slice` for `count == 0` without allocating.
   */
  [[nodiscard]] static result<boxed_slice> try_allocate(allocator_ref alloc, size_type count) noexcept {
    static_assert(std::is_default_constructible_v<T>,
                  "try_allocate(alloc, count) requires T to be default-constructible; "
                  "use try_allocate(alloc, count, value) instead.");
    if (count == 0)
      return boxed_slice();

    auto block = alloc.allocate(count * sizeof(T), alignof(T));
    if (!block)
      return unexpected(block.error());
    auto *ptr = static_cast<T *>(block->ptr);

    size_type constructed = 0;
    for (; constructed < count; ++constructed) {
      auto ctor_res = construction_helpers::try_construct<T>(alloc, ptr + constructed);
      if (!ctor_res) {
        for (size_type i = 0; i < constructed; ++i)
          ptr[i].~T();
        alloc.deallocate(ptr, count * sizeof(T));
        return unexpected(ctor_res.error());
      }
    }
    return boxed_slice(ptr, count, alloc);
  }

  /**
   * @brief Allocates room for @p count elements and copy-constructs
   * @p value into each one, using @p alloc explicitly.
   */
  [[nodiscard]] static result<boxed_slice> try_allocate(allocator_ref alloc, size_type count, const T &value) noexcept {
    if (count == 0)
      return boxed_slice();

    auto block = alloc.allocate(count * sizeof(T), alignof(T));
    if (!block)
      return unexpected(block.error());
    auto *ptr = static_cast<T *>(block->ptr);

    size_type constructed = 0;
    for (; constructed < count; ++constructed) {
      auto ctor_res = construction_helpers::try_construct<T>(alloc, ptr + constructed, value);
      if (!ctor_res) {
        for (size_type i = 0; i < constructed; ++i)
          ptr[i].~T();
        alloc.deallocate(ptr, count * sizeof(T));
        return unexpected(ctor_res.error());
      }
    }
    return boxed_slice(ptr, count, alloc);
  }

  /** @brief `try_allocate(default_allocator(), count)`. */
  [[nodiscard]] static result<boxed_slice> try_create(size_type count) noexcept {
    return try_allocate(default_allocator(), count);
  }

  /** @brief `try_allocate(default_allocator(), count, value)`. */
  [[nodiscard]] static result<boxed_slice> try_create(size_type count, const T &value) noexcept {
    return try_allocate(default_allocator(), count, value);
  }

  /**
   * @brief Rust `Vec::into_boxed_slice` equivalent: consumes @p v, moving
   * its elements into a freshly, exactly-sized allocation (dropping any
   * spare capacity `v` was holding) using `v`'s own allocator.
   */
  [[nodiscard]] static result<boxed_slice> try_from_vector(vector<T> &&v) noexcept {
    static_assert(std::is_move_constructible_v<T>, "try_from_vector requires T to be move-constructible");
    const size_type count = v.size();
    if (count == 0)
      return boxed_slice();

    allocator_ref alloc = v.get_allocator();
    auto block = alloc.allocate(count * sizeof(T), alignof(T));
    if (!block)
      return unexpected(block.error());
    auto *ptr = static_cast<T *>(block->ptr);

    for (size_type i = 0; i < count; ++i)
      new (static_cast<void *>(ptr + i)) T(std::move(v[i]));

    return boxed_slice(ptr, count, alloc);
  }

  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr allocator_ref get_allocator() const noexcept { return alloc_; }

  [[nodiscard]] T &operator[](size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < size_, "boxed_slice index out of bounds");
    return ptr_[index];
  }

  [[nodiscard]] const T &operator[](size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < size_, "boxed_slice index out of bounds");
    return ptr_[index];
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= size_)
      return unexpected(error::out_of_bounds);
    return std::ref(ptr_[index]);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= size_)
      return unexpected(error::out_of_bounds);
    return std::cref(ptr_[index]);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < size_, "boxed_slice index out of bounds");
    return ptr_[index];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < size_, "boxed_slice index out of bounds");
    return ptr_[index];
  }

  [[nodiscard]] T &front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "boxed_slice is empty");
    return ptr_[0];
  }

  [[nodiscard]] const T &front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "boxed_slice is empty");
    return ptr_[0];
  }

  [[nodiscard]] T &back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "boxed_slice is empty");
    return ptr_[size_ - 1];
  }

  [[nodiscard]] const T &back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "boxed_slice is empty");
    return ptr_[size_ - 1];
  }

  [[nodiscard]] T *data() & noexcept RELOCO_LIFETIMEBOUND { return ptr_; }
  [[nodiscard]] const T *data() const & noexcept RELOCO_LIFETIMEBOUND { return ptr_; }

  [[nodiscard]] iterator begin() & noexcept RELOCO_LIFETIMEBOUND { return ptr_; }
  [[nodiscard]] iterator end() & noexcept RELOCO_LIFETIMEBOUND { return ptr_ + size_; }
  [[nodiscard]] const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND { return ptr_; }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return ptr_ + size_; }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return ptr_; }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return ptr_ + size_; }

private:
  boxed_slice(T *ptr, size_type size, allocator_ref alloc) noexcept : ptr_(ptr), size_(size), alloc_(alloc) {}

  void destroy() noexcept {
    if (ptr_) {
      if constexpr (!std::is_trivially_destructible_v<T>)
        for (size_type i = 0; i < size_; ++i)
          ptr_[i].~T();
      alloc_.deallocate(ptr_, size_ * sizeof(T));
      ptr_ = nullptr;
      size_ = 0;
    }
  }

  T *ptr_{nullptr};
  size_type size_{0};
  allocator_ref alloc_{};
};

/**
 * @brief `boxed_slice<T>` holds only a `T *`, a size, and an
 * `allocator_ref` -- none of which are self-referential -- so relocating
 * those bytes to a new address and abandoning the old one is always sound.
 */
template <typename T> struct is_trivially_relocatable<boxed_slice<T>> : std::true_type {};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
