// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file unique_ptr.hpp
 * @brief Move-only, allocator-backed smart pointer with fallible
 * construction.
 *
 * `unique_ptr<T>` owns a single heap allocation of `T`, obtained through a
 * `reloco::allocator_ref` (see `allocator.hpp`) rather than a virtual
 * allocator base: the handle is a cheap, two-word value stored directly by
 * the pointer, not a reference to an externally-owned object.
 *
 * Building the `T` inside that allocation is delegated entirely to
 * `construction_helpers::try_construct` (see `construction_helpers.hpp`),
 * which already resolves the full tiered fallible-construction protocol
 * (`try_construct`/`try_allocate`/`try_create`/plain nothrow construction)
 * for whatever `T` is instantiated with -- `unique_ptr` itself only owns
 * the box allocation around that.
 *
 * Every fallible entry point returns `reloco::result<T>` (see `error.hpp`),
 * the one error type every fallible reloco operation returns. The box
 * allocation itself can only fail with a `reloco::error`; if `T`'s own
 * construction tier fails instead, its `reloco::error` is forwarded as-is
 * (see @ref unique_ptr::try_allocate).
 *
 * `unique_ptr` owns a heap allocation (`allocator_ref::allocate`/
 * `deallocate`, `construction_helpers::try_construct`'s placement-new): raw
 * memory management with no bounds-tracked alternative, exactly like
 * `allocator_ref`'s own operations (see `docs/extending.md`). The file's
 * `namespace reloco` body is wrapped in
 * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE`,
 * matching `allocator.hpp`/`heap_allocator.hpp`/`array.hpp`; the public API
 * itself is not `RELOCO_UNSAFE_BUFFER_USAGE` since it never exposes a raw
 * pointer or caller-supplied storage -- only checked, ownership-safe
 * accessors.
 */

#include "construction_helpers.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"

#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief Move-only, allocator-backed smart pointer owning a single heap
 * allocation of `T`.
 *
 * Never copyable: use `T`'s own `try_clone`/`try_clone_at` protocol (see
 * `concepts.hpp`) explicitly if a deep copy is needed.
 */
template <typename T> class [[nodiscard]] RELOCO_OWNER unique_ptr {
public:
  RELOCO_BLOCK_RVALUE_ACCESS(T);

  /**
   * @brief Allocates and constructs a `T` using the given allocator,
   * choosing the most efficient construction strategy available for `T`
   * (see `construction_helpers::try_construct`).
   *
   * If the box allocation itself fails, returns that failure directly. If
   * `T`'s own construction tier fails instead, that failure -- already a
   * `reloco::error`, like every fallible reloco operation -- is forwarded
   * as-is.
   */
  template <typename... Args>
  [[nodiscard]] static result<unique_ptr> try_allocate(allocator_ref alloc, Args &&...args) noexcept {
    auto block = alloc.allocate(sizeof(T), alignof(T));
    if (!block)
      return unexpected(block.error());
    auto *storage = static_cast<T *>(block->ptr);
    auto ctor_res = construction_helpers::try_construct<T>(alloc, storage, std::forward<Args>(args)...);
    if (!ctor_res) {
      alloc.deallocate(block->ptr, block->size);
      return unexpected(ctor_res.error());
    }
    return unique_ptr(storage, alloc);
  }

  /**
   * @brief Allocates and constructs a `T` using the process-wide default
   * allocator (see `default_allocator()`).
   */
  template <typename... Args> [[nodiscard]] static result<unique_ptr> try_create(Args &&...args) noexcept {
    return try_allocate(default_allocator(), std::forward<Args>(args)...);
  }

  constexpr unique_ptr() noexcept = default;
  constexpr unique_ptr(std::nullptr_t) noexcept {}

  unique_ptr(const unique_ptr &) = delete;
  unique_ptr &operator=(const unique_ptr &) = delete;

  constexpr unique_ptr(unique_ptr &&other) noexcept : ptr_(other.ptr_), alloc_(other.alloc_) {
    other.ptr_ = nullptr;
  }

  unique_ptr &operator=(unique_ptr &&other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = other.ptr_;
      alloc_ = other.alloc_;
      other.ptr_ = nullptr;
    }
    return *this;
  }

  ~unique_ptr() noexcept { reset(); }

  [[nodiscard]] T &operator*() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(ptr_ != nullptr, "unique_ptr: dereference of null unique_ptr");
    return *ptr_;
  }

  [[nodiscard]] T *operator->() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(ptr_ != nullptr, "unique_ptr: access of null unique_ptr");
    return ptr_;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }

  /**
   * @brief Returns the raw pointer without transferring ownership.
   */
  [[nodiscard]] constexpr T *get() const & noexcept RELOCO_LIFETIMEBOUND { return ptr_; }

  /**
   * @brief Returns the raw pointer without the always-on null check that
   * `operator*`/`operator->` perform.
   *
   * Explicitly-unsafe tier: only a `RELOCO_DEBUG_ASSERT`, so it is compiled
   * out under `NDEBUG` (unless `RELOCO_DEBUG` is also defined). Use only
   * once non-null has already been established via `operator bool()`.
   *
   * Marked `RELOCO_UNSAFE_BUFFER_USAGE`: under Clang's
   * `-Wunsafe-buffer-usage`, every call site must be wrapped in
   * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE`,
   * making the opt-out to the unsafe tier explicit and greppable at each
   * use.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T *unsafe_get() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(ptr_ != nullptr, "unique_ptr: access of null unique_ptr");
    return ptr_;
  }

  /**
   * @brief Destroys and deallocates the owned object, if any, and resets
   * this wrapper to the null state.
   */
  void reset() noexcept {
    if (ptr_) {
      ptr_->~T();
      alloc_.deallocate(ptr_, sizeof(T));
      ptr_ = nullptr;
    }
  }

private:
  constexpr unique_ptr(T *ptr, allocator_ref alloc) noexcept : ptr_(ptr), alloc_(alloc) {}

  T *ptr_{nullptr};
  allocator_ref alloc_{};
};

/**
 * @brief `unique_ptr<T>` only holds a `T *` and an `allocator_ref` (itself
 * two words with no self-reference); relocating those bytes to a new
 * address and abandoning the old one never invalidates the pointee, which
 * `unique_ptr` never points back to itself. True regardless of `T` (the
 * pointee is never relocated, only re-pointed-to).
 */
template <typename T> struct is_trivially_relocatable<unique_ptr<T>> : std::true_type {};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
