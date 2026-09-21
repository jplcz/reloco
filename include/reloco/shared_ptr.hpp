// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file shared_ptr.hpp
 * @brief Reference-counted, allocator-backed smart pointer with fallible
 * construction.
 *
 * `shared_ptr<T>` is reloco's shared-ownership counterpart to `unique_ptr<T>`
 * (see `unique_ptr.hpp`): multiple `shared_ptr<T>` instances may jointly own
 * the same `T`, which is destroyed once the last one releases it, and
 * `weak_ptr<T>` observers may outlive the object without extending its
 * lifetime.
 *
 * Ownership is split across two independently-refcounted lifetimes, exactly
 * like `std::shared_ptr`/`std::weak_ptr`: the *object* is destroyed once the
 * last `shared_ptr` releases it, while the *control block* (refcounts plus
 * enough bookkeeping to release the object's storage) survives until the
 * last `weak_ptr` also releases it. Two allocation strategies are offered,
 * mirroring `std::allocate_shared` vs. a hand-rolled `unique_ptr` plus a
 * separate control block:
 *
 * - @ref try_allocate_combined_shared / @ref try_create_combined_shared:
 *   one allocation holds both the object and the control block (like
 *   `std::make_shared`). Cheaper (a single allocate/deallocate pair) but
 *   keeps the object's storage alive for as long as *any* `weak_ptr`
 *   survives, even after every `shared_ptr` has released it.
 * - @ref try_allocate_shared / @ref try_create_shared: the object and the
 *   control block are allocated separately, so the object's storage is
 *   freed as soon as the last `shared_ptr` releases it; only the small
 *   control block itself waits on outstanding `weak_ptr`s.
 *
 * Building the `T` inside either layout is delegated to
 * `construction_helpers::try_construct` (see `construction_helpers.hpp`),
 * exactly like `unique_ptr::try_allocate`, so the same tiered
 * `try_construct`/`try_allocate`/`try_create`/plain-nothrow protocol applies.
 *
 * Every fallible entry point returns `reloco::result<T>` (see `error.hpp`).
 *
 * Like `allocator.hpp`/`unique_ptr.hpp`, this file's `namespace reloco` body
 * is wrapped in `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/
 * `RELOCO_END_UNSAFE_BUFFER_USAGE`: the control blocks placement-new/
 * placement-destroy directly into raw allocator storage, which has no
 * bounds-tracked alternative. The public `shared_ptr`/`weak_ptr` API itself
 * never exposes a raw pointer or caller-supplied storage and is not
 * `RELOCO_UNSAFE_BUFFER_USAGE`, except for `unsafe_get()`.
 */

#include "construction_helpers.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T> class shared_ptr;
template <typename T> class weak_ptr;
template <typename T> class enable_shared_from_this;

namespace detail {

/**
 * @brief Type-erased base for a `shared_ptr`/`weak_ptr` control block.
 *
 * Splits release into two independent atomic refcounts: `shared_count_`
 * gates the object's lifetime (`destroy_object`), `weak_count_` gates the
 * control block's own storage (`destroy_self`). The block always starts
 * with one implicit weak reference, released once `shared_count_` reaches
 * zero, so the control block never outlives the last owner of either kind.
 */
struct sp_control_block {
  std::atomic<std::size_t> shared_count_{1};
  std::atomic<std::size_t> weak_count_{1};

  sp_control_block() noexcept = default;
  virtual ~sp_control_block() = default;

  sp_control_block(const sp_control_block &) = delete;
  sp_control_block &operator=(const sp_control_block &) = delete;

  virtual void destroy_object() noexcept = 0;
  virtual void destroy_self() noexcept = 0;

  void release_shared() noexcept {
    if (shared_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      destroy_object();
      release_weak();
    }
  }

  void release_weak() noexcept {
    if (weak_count_.fetch_sub(1, std::memory_order_acq_rel) == 1)
      destroy_self();
  }

  bool try_add_shared() noexcept {
    auto count = shared_count_.load(std::memory_order_relaxed);
    while (count != 0) {
      if (shared_count_.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel,
                                              std::memory_order_relaxed))
        return true;
    }
    return false;
  }
};

// Object and control block live in two separate allocator blocks: the
// object's storage is freed as soon as the last `shared_ptr` releases it,
// independently of any surviving `weak_ptr`.
template <typename T> struct sp_control_block_separate final : sp_control_block {
  T *ptr_;
  allocator_ref alloc_;

  sp_control_block_separate(T *ptr, allocator_ref alloc) noexcept : ptr_(ptr), alloc_(alloc) {}

  void destroy_object() noexcept override {
    ptr_->~T();
    alloc_.deallocate(ptr_, sizeof(T));
  }

  void destroy_self() noexcept override {
    allocator_ref alloc = alloc_;
    this->~sp_control_block_separate();
    alloc.deallocate(this, sizeof(*this));
  }
};

// Object storage is embedded directly in the control block: a single
// allocation, but the whole block (object bytes included) stays alive until
// the last `weak_ptr` also releases it.
template <typename T> struct sp_control_block_combined final : sp_control_block {
  alignas(T) std::byte storage_[sizeof(T)];
  allocator_ref alloc_;

  explicit sp_control_block_combined(allocator_ref alloc) noexcept : alloc_(alloc) {}

  T *object() noexcept { return reinterpret_cast<T *>(storage_); }

  void destroy_object() noexcept override { object()->~T(); }

  void destroy_self() noexcept override {
    allocator_ref alloc = alloc_;
    this->~sp_control_block_combined();
    alloc.deallocate(this, sizeof(*this));
  }
};

struct enable_shared_from_this_base {};

} // namespace detail

/**
 * @brief Reference-counted, allocator-backed smart pointer.
 *
 * Copyable (increments the shared refcount) and movable. Construct via
 * `try_allocate_shared`/`try_create_shared` (separate control block) or
 * `try_allocate_combined_shared`/`try_create_combined_shared` (single
 * allocation, recommended default) rather than directly.
 */
template <typename T> class RELOCO_OWNER shared_ptr {
public:
  RELOCO_BLOCK_RVALUE_ACCESS(T);

  constexpr shared_ptr() noexcept = default;
  constexpr shared_ptr(std::nullptr_t) noexcept {}

  shared_ptr(const shared_ptr &other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    if (block_)
      block_->shared_count_.fetch_add(1, std::memory_order_relaxed);
  }

  /** @brief Converting copy from a compatible `shared_ptr<U>`. */
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  shared_ptr(const shared_ptr<U> &other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    if (block_)
      block_->shared_count_.fetch_add(1, std::memory_order_relaxed);
  }

  /** @brief Aliasing constructor: shares ownership with `other` but points
   * at `ptr` instead (used by the `*_pointer_cast` helpers below). */
  template <typename U>
  shared_ptr(const shared_ptr<U> &other, T *ptr) noexcept : ptr_(ptr), block_(other.block_) {
    if (block_)
      block_->shared_count_.fetch_add(1, std::memory_order_relaxed);
  }

  constexpr shared_ptr(shared_ptr &&other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    other.ptr_ = nullptr;
    other.block_ = nullptr;
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  constexpr shared_ptr(shared_ptr<U> &&other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    other.ptr_ = nullptr;
    other.block_ = nullptr;
  }

  ~shared_ptr() {
    if (block_)
      block_->release_shared();
  }

  shared_ptr &operator=(const shared_ptr &other) noexcept {
    if (this != &other) {
      shared_ptr(other).swap(*this);
    }
    return *this;
  }

  shared_ptr &operator=(shared_ptr &&other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = other.ptr_;
      block_ = other.block_;
      other.ptr_ = nullptr;
      other.block_ = nullptr;
    }
    return *this;
  }

  void swap(shared_ptr &other) noexcept {
    std::swap(ptr_, other.ptr_);
    std::swap(block_, other.block_);
  }

  [[nodiscard]] T &operator*() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(ptr_ != nullptr, "shared_ptr: dereference of null shared_ptr");
    return *ptr_;
  }

  [[nodiscard]] T *operator->() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(ptr_ != nullptr, "shared_ptr: access of null shared_ptr");
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
   * Explicitly-unsafe tier: only a `RELOCO_DEBUG_ASSERT`, compiled out under
   * `NDEBUG` (unless `RELOCO_DEBUG` is also defined). Use only once
   * non-null has already been established via `operator bool()`.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T *unsafe_get() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(ptr_ != nullptr, "shared_ptr: access of null shared_ptr");
    return ptr_;
  }

  /**
   * @brief Checked pointer access: fails with `error::empty_pointer` instead
   * of asserting.
   */
  [[nodiscard]] result<T *> try_get() const & noexcept RELOCO_LIFETIMEBOUND {
    if (!ptr_)
      return unexpected(error::empty_pointer);
    return ptr_;
  }
  result<T *> try_get() const && = delete;

  /** @brief Number of `shared_ptr`s (including this one) sharing ownership,
   * or `0` for an empty pointer. */
  [[nodiscard]] std::size_t use_count() const noexcept {
    return block_ ? block_->shared_count_.load(std::memory_order_relaxed) : 0;
  }

  /** @brief Releases ownership, destroying the object if this was the last
   * owner, and resets this pointer to the null state. */
  void reset() noexcept {
    if (block_) {
      block_->release_shared();
      block_ = nullptr;
      ptr_ = nullptr;
    }
  }

  template <typename U> bool owner_before(const shared_ptr<U> &other) const noexcept {
    return block_ < other.block_;
  }

  template <typename U> bool owner_before(const weak_ptr<U> &other) const noexcept {
    return block_ < other.block_;
  }

private:
  constexpr shared_ptr(detail::sp_control_block *block, T *ptr) noexcept : ptr_(ptr), block_(block) {}

  T *ptr_{nullptr};
  detail::sp_control_block *block_{nullptr};

  template <typename U> friend class shared_ptr;
  template <typename U> friend class weak_ptr;
  template <typename U> friend class enable_shared_from_this;

  template <typename Tp, typename... Args>
  friend result<shared_ptr<Tp>> try_allocate_shared(allocator_ref alloc, Args &&...args) noexcept;

  template <typename Tp, typename... Args>
  friend result<shared_ptr<Tp>> try_allocate_combined_shared(allocator_ref alloc, Args &&...args) noexcept;
};

template <typename T> void swap(shared_ptr<T> &lhs, shared_ptr<T> &rhs) noexcept { lhs.swap(rhs); }

template <typename T>
[[nodiscard]] bool operator==(const shared_ptr<T> &lhs, std::nullptr_t) noexcept {
  return lhs.get() == nullptr;
}

template <typename T>
[[nodiscard]] bool operator==(std::nullptr_t, const shared_ptr<T> &rhs) noexcept {
  return rhs.get() == nullptr;
}

template <typename T>
[[nodiscard]] bool operator!=(const shared_ptr<T> &lhs, std::nullptr_t) noexcept {
  return lhs.get() != nullptr;
}

template <typename T>
[[nodiscard]] bool operator!=(std::nullptr_t, const shared_ptr<T> &rhs) noexcept {
  return rhs.get() != nullptr;
}

template <typename T, typename U>
[[nodiscard]] bool operator==(const shared_ptr<T> &lhs, const shared_ptr<U> &rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename T, typename U>
[[nodiscard]] bool operator!=(const shared_ptr<T> &lhs, const shared_ptr<U> &rhs) noexcept {
  return lhs.get() != rhs.get();
}

template <typename T, typename U>
[[nodiscard]] bool operator<(const shared_ptr<T> &lhs, const shared_ptr<U> &rhs) noexcept {
  return lhs.get() < rhs.get();
}

/**
 * @brief Non-owning observer of an object managed by `shared_ptr`.
 *
 * Does not keep the object alive; call `lock()` to obtain a `shared_ptr`
 * (or `error::pointer_expired` if the object is already gone).
 */
template <typename T> class RELOCO_POINTER weak_ptr {
public:
  constexpr weak_ptr() noexcept = default;

  weak_ptr(const shared_ptr<T> &other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    if (block_)
      block_->weak_count_.fetch_add(1, std::memory_order_relaxed);
  }

  weak_ptr(const weak_ptr &other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    if (block_)
      block_->weak_count_.fetch_add(1, std::memory_order_relaxed);
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  weak_ptr(const weak_ptr<U> &other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    if (block_)
      block_->weak_count_.fetch_add(1, std::memory_order_relaxed);
  }

  constexpr weak_ptr(weak_ptr &&other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    other.ptr_ = nullptr;
    other.block_ = nullptr;
  }

  ~weak_ptr() {
    if (block_)
      block_->release_weak();
  }

  weak_ptr &operator=(const weak_ptr &other) noexcept {
    if (this != &other) {
      weak_ptr(other).swap(*this);
    }
    return *this;
  }

  weak_ptr &operator=(const shared_ptr<T> &other) noexcept {
    weak_ptr(other).swap(*this);
    return *this;
  }

  weak_ptr &operator=(weak_ptr &&other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = other.ptr_;
      block_ = other.block_;
      other.ptr_ = nullptr;
      other.block_ = nullptr;
    }
    return *this;
  }

  void swap(weak_ptr &other) noexcept {
    std::swap(ptr_, other.ptr_);
    std::swap(block_, other.block_);
  }

  /** @brief `true` once the last `shared_ptr` has released the object (or
   * this observer was never bound to one). */
  [[nodiscard]] bool expired() const noexcept {
    return !block_ || block_->shared_count_.load(std::memory_order_relaxed) == 0;
  }

  /** @brief Attempts to promote this observer to a `shared_ptr`, atomically
   * failing with `error::pointer_expired` if the object is already gone. */
  [[nodiscard]] result<shared_ptr<T>> lock() const noexcept {
    if (!block_)
      return unexpected(error::empty_pointer);
    if (!block_->try_add_shared())
      return unexpected(error::pointer_expired);
    return shared_ptr<T>(block_, ptr_);
  }

  [[nodiscard]] std::size_t use_count() const noexcept {
    return block_ ? block_->shared_count_.load(std::memory_order_relaxed) : 0;
  }

  void reset() noexcept {
    if (block_) {
      block_->release_weak();
      block_ = nullptr;
      ptr_ = nullptr;
    }
  }

  template <typename U> bool owner_before(const weak_ptr<U> &other) const noexcept {
    return block_ < other.block_;
  }

  template <typename U> bool owner_before(const shared_ptr<U> &other) const noexcept {
    return block_ < other.block_;
  }

private:
  T *ptr_{nullptr};
  detail::sp_control_block *block_{nullptr};

  template <typename U> friend class weak_ptr;
  template <typename U> friend class shared_ptr;
  template <typename U> friend class enable_shared_from_this;

  template <typename Tp, typename... Args>
  friend result<shared_ptr<Tp>> try_allocate_shared(allocator_ref alloc, Args &&...args) noexcept;

  template <typename Tp, typename... Args>
  friend result<shared_ptr<Tp>> try_allocate_combined_shared(allocator_ref alloc, Args &&...args) noexcept;
};

template <typename T> void swap(weak_ptr<T> &lhs, weak_ptr<T> &rhs) noexcept { lhs.swap(rhs); }

/**
 * @brief Lets a type obtain a `shared_ptr<T>` to itself from within one of
 * its own member functions, without storing a `shared_ptr` and creating a
 * reference cycle.
 *
 * `T` must publicly derive from `enable_shared_from_this<T>` and must only
 * ever be owned by `shared_ptr<T>` obtained through
 * `try_allocate_shared`/`try_create_shared`/`try_allocate_combined_shared`/
 * `try_create_combined_shared` -- those entry points populate the internal
 * `weak_ptr` this relies on; calling `shared_from_this()` before that (e.g.
 * from the constructor) or on an object not owned by any `shared_ptr`
 * fails with `error::pointer_expired`/`error::empty_pointer` rather than
 * invoking undefined behavior.
 */
template <typename T> class enable_shared_from_this : detail::enable_shared_from_this_base {
public:
  [[nodiscard]] result<shared_ptr<T>> shared_from_this() const noexcept { return weak_this_.lock(); }

  [[nodiscard]] weak_ptr<T> weak_from_this() const noexcept { return weak_this_; }

protected:
  constexpr enable_shared_from_this() noexcept = default;
  constexpr enable_shared_from_this(const enable_shared_from_this &) noexcept {}
  enable_shared_from_this &operator=(const enable_shared_from_this &) noexcept { return *this; }
  ~enable_shared_from_this() = default;

private:
  mutable weak_ptr<T> weak_this_;

  template <typename Tp, typename... Args>
  friend result<shared_ptr<Tp>> try_allocate_shared(allocator_ref alloc, Args &&...args) noexcept;

  template <typename Tp, typename... Args>
  friend result<shared_ptr<Tp>> try_allocate_combined_shared(allocator_ref alloc, Args &&...args) noexcept;
};

/**
 * @brief Allocates the object and the control block separately, using
 * `alloc` explicitly.
 *
 * Prefer @ref try_allocate_combined_shared unless you specifically need the
 * object's storage released as soon as the last `shared_ptr` (not
 * `weak_ptr`) releases it.
 */
template <typename T, typename... Args>
[[nodiscard]] result<shared_ptr<T>> try_allocate_shared(allocator_ref alloc, Args &&...args) noexcept {
  auto object_block = alloc.allocate(sizeof(T), alignof(T));
  if (!object_block)
    return unexpected(object_block.error());
  auto *raw_ptr = static_cast<T *>(object_block->ptr);

  auto ctor_res = construction_helpers::try_construct<T>(alloc, raw_ptr, std::forward<Args>(args)...);
  if (!ctor_res) {
    alloc.deallocate(object_block->ptr, object_block->size);
    return unexpected(ctor_res.error());
  }

  using control_block_t = detail::sp_control_block_separate<T>;
  auto cb_block = alloc.allocate(sizeof(control_block_t), alignof(control_block_t));
  if (!cb_block) {
    raw_ptr->~T();
    alloc.deallocate(object_block->ptr, object_block->size);
    return unexpected(cb_block.error());
  }
  auto *cb = new (cb_block->ptr) control_block_t(raw_ptr, alloc);

  if constexpr (std::is_base_of_v<detail::enable_shared_from_this_base, T>) {
    raw_ptr->weak_this_.ptr_ = raw_ptr;
    raw_ptr->weak_this_.block_ = cb;
    cb->weak_count_.fetch_add(1, std::memory_order_relaxed);
  }

  return shared_ptr<T>(cb, raw_ptr);
}

/**
 * @brief Allocates the object and its control block in a single allocation,
 * like `std::make_shared`. Recommended default.
 */
template <typename T, typename... Args>
[[nodiscard]] result<shared_ptr<T>> try_allocate_combined_shared(allocator_ref alloc, Args &&...args) noexcept {
  using control_block_t = detail::sp_control_block_combined<T>;

  auto block = alloc.allocate(sizeof(control_block_t), alignof(control_block_t));
  if (!block)
    return unexpected(block.error());

  auto *cb = new (block->ptr) control_block_t(alloc);
  T *raw_ptr = cb->object();

  auto ctor_res = construction_helpers::try_construct<T>(alloc, raw_ptr, std::forward<Args>(args)...);
  if (!ctor_res) {
    allocator_ref alloc_copy = alloc;
    cb->~control_block_t();
    alloc_copy.deallocate(block->ptr, block->size);
    return unexpected(ctor_res.error());
  }

  if constexpr (std::is_base_of_v<detail::enable_shared_from_this_base, T>) {
    raw_ptr->weak_this_.ptr_ = raw_ptr;
    raw_ptr->weak_this_.block_ = cb;
    cb->weak_count_.fetch_add(1, std::memory_order_relaxed);
  }

  return shared_ptr<T>(cb, raw_ptr);
}

/**
 * @brief `try_allocate_shared` using the process-wide default allocator
 * (see `default_allocator()`).
 */
template <typename T, typename... Args>
[[nodiscard]] result<shared_ptr<T>> try_create_shared(Args &&...args) noexcept {
  return try_allocate_shared<T>(default_allocator(), std::forward<Args>(args)...);
}

/**
 * @brief `try_allocate_combined_shared` using the process-wide default
 * allocator (see `default_allocator()`). Recommended default entry point.
 */
template <typename T, typename... Args>
[[nodiscard]] result<shared_ptr<T>> try_create_combined_shared(Args &&...args) noexcept {
  return try_allocate_combined_shared<T>(default_allocator(), std::forward<Args>(args)...);
}

template <typename T, typename U> shared_ptr<T> static_pointer_cast(const shared_ptr<U> &other) noexcept {
  return shared_ptr<T>(other, static_cast<T *>(other.get()));
}

template <typename T, typename U> shared_ptr<T> dynamic_pointer_cast(const shared_ptr<U> &other) noexcept {
  if (auto *ptr = dynamic_cast<T *>(other.get()))
    return shared_ptr<T>(other, ptr);
  return shared_ptr<T>();
}

template <typename T, typename U> shared_ptr<T> const_pointer_cast(const shared_ptr<U> &other) noexcept {
  return shared_ptr<T>(other, const_cast<T *>(other.get()));
}

template <typename T, typename U> shared_ptr<T> reinterpret_pointer_cast(const shared_ptr<U> &other) noexcept {
  return shared_ptr<T>(other, reinterpret_cast<T *>(other.get()));
}

/**
 * @brief `shared_ptr<T>` only holds a `T *` and a control-block pointer
 * (itself never self-referential): relocating those two words to a new
 * address and abandoning the old one never invalidates the pointee or the
 * control block. True regardless of `T`.
 */
template <typename T> struct is_trivially_relocatable<shared_ptr<T>> : std::true_type {};

/** @brief Same rationale as `is_trivially_relocatable<shared_ptr<T>>`:
 * `weak_ptr<T>` only holds a `T *` and a control-block pointer. */
template <typename T> struct is_trivially_relocatable<weak_ptr<T>> : std::true_type {};

} // namespace reloco

namespace std {

template <typename T> struct hash<reloco::shared_ptr<T>> {
  std::size_t operator()(const reloco::shared_ptr<T> &p) const noexcept { return hash<T *>{}(p.get()); }
};

} // namespace std

RELOCO_END_UNSAFE_BUFFER_USAGE
