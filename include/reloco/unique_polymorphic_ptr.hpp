// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file unique_polymorphic_ptr.hpp
 * @brief Move-only, allocator-backed owner of a derived object held through
 * a base-typed handle.
 *
 * `unique_ptr<T>` is deliberately minimal and can only own exactly `T`.
 * `unique_polymorphic_ptr<Base>` pays for the extra bookkeeping needed to
 * own any `Derived` through a `Base` handle: the address of the complete
 * object, the allocator-returned block size, and a type-erased destructor
 * captured when the `Derived` type is still known. Consequently:
 *
 *  - `Base` does not need a virtual destructor;
 *  - virtual and multiple inheritance work (the complete object address is
 *    kept, so no downcast is ever needed);
 *  - the block is released with the size the allocator returned, not
 *    `sizeof(Base)` or `sizeof(Derived)`.
 *
 * Like `unique_ptr`, the file's body is wrapped in
 * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE` because
 * it manages raw allocations; only `adopt` is itself marked unsafe.
 */

#include "construction_helpers.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"
#include "unique_ptr.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief Move-only owner of a (possibly derived) object accessed as `Base`.
 */
template <typename Base> class [[nodiscard]] RELOCO_OWNER unique_polymorphic_ptr {
public:
  RELOCO_BLOCK_RVALUE_ACCESS(Base);

  /**
   * @brief Allocates and constructs a `Derived` (default: `Base`) using
   * `alloc`, owned through a `Base` handle. Construction follows
   * `construction_helpers::try_construct`; failures are returned as-is.
   */
  template <
      typename Derived = Base, typename... Args,
      typename = std::enable_if_t<std::is_convertible_v<Derived *, Base *> && is_try_constructible_v<Derived, Args...>>>
  [[nodiscard]] static result<unique_polymorphic_ptr> try_allocate(allocator_ref alloc, Args &&...args) noexcept {
    auto block = alloc.allocate(sizeof(Derived), alignof(Derived));
    if (!block)
      return unexpected(block.error());
    auto *storage = static_cast<Derived *>(block->ptr);
    auto ctor_res = construction_helpers::try_construct<Derived>(alloc, storage, std::forward<Args>(args)...);
    if (!ctor_res) {
      alloc.deallocate(block->ptr, block->size);
      return unexpected(ctor_res.error());
    }
    return unique_polymorphic_ptr(storage, storage, block->size, destroy_as<Derived>, alloc);
  }

  /** @brief Same as `try_allocate`, using `default_allocator()`. */
  template <
      typename Derived = Base, typename... Args,
      typename = std::enable_if_t<std::is_convertible_v<Derived *, Base *> && is_try_constructible_v<Derived, Args...>>>
  [[nodiscard]] static result<unique_polymorphic_ptr> try_create(Args &&...args) noexcept {
    return try_allocate<Derived>(default_allocator(), std::forward<Args>(args)...);
  }

  /**
   * @brief Takes ownership of an object already constructed at the start of
   * a block obtained from `alloc`.
   *
   * @param obj   The complete `Derived` object, at the start of its block.
   * @param bytes `mem_block::size` the allocator returned for that block.
   */
  template <typename Derived, typename = std::enable_if_t<std::is_convertible_v<Derived *, Base *>>>
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE static unique_polymorphic_ptr adopt(allocator_ref alloc, Derived *obj,
                                                                               std::size_t bytes) noexcept {
    return unique_polymorphic_ptr(obj, obj, bytes, destroy_as<Derived>, alloc);
  }

  constexpr unique_polymorphic_ptr() noexcept = default;
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr unique_polymorphic_ptr(std::nullptr_t) noexcept {}

  /**
   * @brief Upcasts a `unique_ptr<Derived>`; its block is freed with
   * `sizeof(Derived)`, as `unique_ptr` itself would.
   */
  template <typename Derived, typename = std::enable_if_t<std::is_convertible_v<Derived *, Base *>>>
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  unique_polymorphic_ptr(unique_ptr<Derived> &&other) noexcept
      : ptr_(other.ptr_), self_(other.ptr_), size_(sizeof(Derived)), destroy_(&destroy_as<Derived>),
        alloc_(other.alloc_) {
    other.ptr_ = nullptr;
  }

  unique_polymorphic_ptr(const unique_polymorphic_ptr &) = delete;
  unique_polymorphic_ptr &operator=(const unique_polymorphic_ptr &) = delete;

  constexpr unique_polymorphic_ptr(unique_polymorphic_ptr &&other) noexcept
      : ptr_(other.ptr_), self_(other.self_), size_(other.size_), destroy_(other.destroy_), alloc_(other.alloc_) {
    other.ptr_ = nullptr;
  }

  unique_polymorphic_ptr &operator=(unique_polymorphic_ptr &&other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = other.ptr_;
      self_ = other.self_;
      size_ = other.size_;
      destroy_ = other.destroy_;
      alloc_ = other.alloc_;
      other.ptr_ = nullptr;
    }
    return *this;
  }

  ~unique_polymorphic_ptr() noexcept { reset(); }

  RELOCO_CHECKED [[nodiscard]] Base &operator*() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(ptr_ != nullptr, "unique_polymorphic_ptr: dereference of null pointer");
    return *ptr_;
  }

  RELOCO_CHECKED [[nodiscard]] Base *operator->() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(ptr_ != nullptr, "unique_polymorphic_ptr: access of null pointer");
    return ptr_;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }

  [[nodiscard]] constexpr Base *get() const & noexcept RELOCO_LIFETIMEBOUND { return ptr_; }

  /**
   * @brief Unchecked raw access (debug-assert only); see
   * `unique_ptr::unsafe_get`.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE Base *unsafe_get() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(ptr_ != nullptr, "unique_polymorphic_ptr: access of null pointer");
    return ptr_;
  }

  /** @brief Destroys the owned object and frees its block. */
  void reset() noexcept {
    if (ptr_) {
      destroy_(self_);
      alloc_.deallocate(self_, size_);
      ptr_ = nullptr;
    }
  }

private:
  template <typename Derived> static void destroy_as(void *self) noexcept { static_cast<Derived *>(self)->~Derived(); }

  constexpr unique_polymorphic_ptr(Base *ptr, void *self, std::size_t size, void (*destroy)(void *) noexcept,
                                   allocator_ref alloc) noexcept
      : ptr_(ptr), self_(self), size_(size), destroy_(destroy), alloc_(alloc) {}

  Base *ptr_{nullptr};
  void *self_{nullptr}; // complete object == start of the allocation
  std::size_t size_{0}; // allocator-returned block size
  void (*destroy_)(void *) noexcept {nullptr};
  allocator_ref alloc_{};
};

/** @brief Plain data (pointers, size, function pointer, allocator_ref); never self-referential. */
template <typename Base> struct is_trivially_relocatable<unique_polymorphic_ptr<Base>> : std::true_type {};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
