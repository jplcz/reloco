// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file rc.hpp
 * @brief Single-threaded, allocator-backed reference-counted smart pointer,
 * matching Rust's `std::rc::Rc<T>`/`std::rc::Weak<T>`.
 *
 * `rc<T>` is structurally identical to `shared_ptr<T>` (see
 * `shared_ptr.hpp`) -- shared ownership, split object/control-block
 * lifetimes, the same two allocation strategies (separate vs. combined) --
 * except its refcounts are plain `std::size_t` increments/decrements
 * instead of `std::atomic<std::size_t>` operations. That makes `rc<T>`
 * strictly single-threaded (like Rust's `Rc<T>`, which is deliberately
 * `!Send`/`!Sync`) but noticeably cheaper to copy/drop than `shared_ptr<T>`
 * (Rust's `Arc<T>`) when the sharing never crosses a thread boundary.
 *
 * Reaching for `rc<T>` over `shared_ptr<T>` is purely a performance choice:
 * pick `rc<T>` whenever the shared object only ever lives on one thread,
 * and `shared_ptr<T>` the moment it might be handed to another thread.
 * Mixing the two for the same object is not possible (they are unrelated
 * types with independent control blocks).
 *
 * Every fallible entry point returns `reloco::result<T>` (see `error.hpp`).
 *
 * Like `shared_ptr.hpp`, this file's `namespace reloco` body is wrapped in
 * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE`: the
 * control blocks placement-new/placement-destroy directly into raw
 * allocator storage, which has no bounds-tracked alternative. The public
 * `rc`/`weak_rc` API itself never exposes a raw pointer or caller-supplied
 * storage and is not `RELOCO_UNSAFE_BUFFER_USAGE`, except for
 * `unsafe_get()`.
 */

#include "construction_helpers.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"
#include "send_sync.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T> class rc;
template <typename T> class weak_rc;
template <typename T> class enable_rc_from_this;

namespace detail {

/**
 * @brief Type-erased base for an `rc`/`weak_rc` control block.
 *
 * Splits release into two independent, non-atomic refcounts: `shared_count_`
 * gates the object's lifetime (`destroy_object`), `weak_count_` gates the
 * control block's own storage (`destroy_self`). The block always starts
 * with one implicit weak reference, released once `shared_count_` reaches
 * zero, so the control block never outlives the last owner of either kind.
 * Never touched from more than one thread -- unlike `sp_control_block`,
 * plain (non-atomic) arithmetic is sound here.
 */
struct RELOCO_EXPORT rc_control_block {
  std::size_t shared_count_{1};
  std::size_t weak_count_{1};

  rc_control_block() noexcept = default;
  virtual ~rc_control_block() = default;

  rc_control_block(const rc_control_block &) = delete;
  rc_control_block &operator=(const rc_control_block &) = delete;

  virtual void destroy_object() noexcept = 0;
  virtual void destroy_self() noexcept = 0;

  void release_shared() noexcept {
    if (--shared_count_ == 0) {
      destroy_object();
      release_weak();
    }
  }

  void release_weak() noexcept {
    if (--weak_count_ == 0)
      destroy_self();
  }

  bool try_add_shared() noexcept {
    if (shared_count_ == 0)
      return false;
    ++shared_count_;
    return true;
  }
};

// Object and control block live in two separate allocator blocks: the
// object's storage is freed as soon as the last `rc` releases it,
// independently of any surviving `weak_rc`.
template <typename T> struct rc_control_block_separate final : rc_control_block {
  T *ptr_;
  allocator_ref alloc_;

  rc_control_block_separate(T *ptr, allocator_ref alloc) noexcept : ptr_(ptr), alloc_(alloc) {}

  void destroy_object() noexcept override {
    ptr_->~T();
    alloc_.deallocate(ptr_, sizeof(T));
  }

  void destroy_self() noexcept override {
    allocator_ref alloc = alloc_;
    this->~rc_control_block_separate();
    alloc.deallocate(this, sizeof(*this));
  }
};

// Object storage is embedded directly in the control block: a single
// allocation, but the whole block (object bytes included) stays alive until
// the last `weak_rc` also releases it.
template <typename T> struct rc_control_block_combined final : rc_control_block {
  alignas(T) std::byte storage_[sizeof(T)];
  allocator_ref alloc_;

  explicit rc_control_block_combined(allocator_ref alloc) noexcept : alloc_(alloc) {}

  T *object() noexcept { return reinterpret_cast<T *>(storage_); }

  void destroy_object() noexcept override { object()->~T(); }

  void destroy_self() noexcept override {
    allocator_ref alloc = alloc_;
    this->~rc_control_block_combined();
    alloc.deallocate(this, sizeof(*this));
  }
};

struct RELOCO_EXPORT enable_rc_from_this_base {};

} // namespace detail

/**
 * @brief Single-threaded, reference-counted, allocator-backed smart
 * pointer. Matches Rust's `std::rc::Rc<T>`.
 *
 * Copyable (increments the shared refcount) and movable. Construct via
 * `try_allocate_rc`/`try_create_rc` (separate control block) or
 * `try_allocate_combined_rc`/`try_create_combined_rc` (single allocation,
 * recommended default) rather than directly.
 */
template <typename T> class RELOCO_OWNER rc {
public:
  RELOCO_BLOCK_RVALUE_ACCESS(T);

  constexpr rc() noexcept = default;
  constexpr rc(std::nullptr_t) noexcept {}

  rc(const rc &other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    if (block_)
      ++block_->shared_count_;
  }

  /** @brief Converting copy from a compatible `rc<U>`. */
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  rc(const rc<U> &other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    if (block_)
      ++block_->shared_count_;
  }

  /** @brief Aliasing constructor: shares ownership with `other` but points
   * at `ptr` instead (used by the `*_pointer_cast` helpers below). */
  template <typename U> rc(const rc<U> &other, T *ptr) noexcept : ptr_(ptr), block_(other.block_) {
    if (block_)
      ++block_->shared_count_;
  }

  constexpr rc(rc &&other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    other.ptr_ = nullptr;
    other.block_ = nullptr;
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  constexpr rc(rc<U> &&other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    other.ptr_ = nullptr;
    other.block_ = nullptr;
  }

  ~rc() {
    if (block_)
      block_->release_shared();
  }

  rc &operator=(const rc &other) noexcept {
    if (this != &other) {
      rc(other).swap(*this);
    }
    return *this;
  }

  rc &operator=(rc &&other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = other.ptr_;
      block_ = other.block_;
      other.ptr_ = nullptr;
      other.block_ = nullptr;
    }
    return *this;
  }

  void swap(rc &other) noexcept {
    std::swap(ptr_, other.ptr_);
    std::swap(block_, other.block_);
  }

  [[nodiscard]] T &operator*() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(ptr_ != nullptr, "rc: dereference of null rc");
    return *ptr_;
  }

  [[nodiscard]] T *operator->() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(ptr_ != nullptr, "rc: access of null rc");
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
    RELOCO_DEBUG_ASSERT(ptr_ != nullptr, "rc: access of null rc");
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

  /** @brief Number of `rc`s (including this one) sharing ownership, or `0`
   * for an empty pointer. */
  [[nodiscard]] std::size_t use_count() const noexcept { return block_ ? block_->shared_count_ : 0; }

  /** @brief Releases ownership, destroying the object if this was the last
   * owner, and resets this pointer to the null state. */
  void reset() noexcept {
    if (block_) {
      block_->release_shared();
      block_ = nullptr;
      ptr_ = nullptr;
    }
  }

  template <typename U> bool owner_before(const rc<U> &other) const noexcept { return block_ < other.block_; }

  template <typename U> bool owner_before(const weak_rc<U> &other) const noexcept { return block_ < other.block_; }

private:
  constexpr rc(detail::rc_control_block *block, T *ptr) noexcept : ptr_(ptr), block_(block) {}

  T *ptr_{nullptr};
  detail::rc_control_block *block_{nullptr};

  template <typename U> friend class rc;
  template <typename U> friend class weak_rc;
  template <typename U> friend class enable_rc_from_this;

  template <typename Tp, typename... Args>
  friend result<rc<Tp>> try_allocate_rc(allocator_ref alloc, Args &&...args) noexcept;

  template <typename Tp, typename... Args>
  friend result<rc<Tp>> try_allocate_combined_rc(allocator_ref alloc, Args &&...args) noexcept;
};

template <typename T> void swap(rc<T> &lhs, rc<T> &rhs) noexcept { lhs.swap(rhs); }

template <typename T> [[nodiscard]] bool operator==(const rc<T> &lhs, std::nullptr_t) noexcept {
  return lhs.get() == nullptr;
}

template <typename T> [[nodiscard]] bool operator==(std::nullptr_t, const rc<T> &rhs) noexcept {
  return rhs.get() == nullptr;
}

template <typename T> [[nodiscard]] bool operator!=(const rc<T> &lhs, std::nullptr_t) noexcept {
  return lhs.get() != nullptr;
}

template <typename T> [[nodiscard]] bool operator!=(std::nullptr_t, const rc<T> &rhs) noexcept {
  return rhs.get() != nullptr;
}

template <typename T, typename U> [[nodiscard]] bool operator==(const rc<T> &lhs, const rc<U> &rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename T, typename U> [[nodiscard]] bool operator!=(const rc<T> &lhs, const rc<U> &rhs) noexcept {
  return lhs.get() != rhs.get();
}

template <typename T, typename U> [[nodiscard]] bool operator<(const rc<T> &lhs, const rc<U> &rhs) noexcept {
  return lhs.get() < rhs.get();
}

/**
 * @brief Non-owning observer of an object managed by `rc`. Matches Rust's
 * `std::rc::Weak<T>`.
 *
 * Does not keep the object alive; call `lock()` to obtain an `rc` (or
 * `error::pointer_expired` if the object is already gone).
 */
template <typename T> class RELOCO_POINTER weak_rc {
public:
  constexpr weak_rc() noexcept = default;

  weak_rc(const rc<T> &other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    if (block_)
      ++block_->weak_count_;
  }

  weak_rc(const weak_rc &other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    if (block_)
      ++block_->weak_count_;
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  weak_rc(const weak_rc<U> &other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    if (block_)
      ++block_->weak_count_;
  }

  constexpr weak_rc(weak_rc &&other) noexcept : ptr_(other.ptr_), block_(other.block_) {
    other.ptr_ = nullptr;
    other.block_ = nullptr;
  }

  ~weak_rc() {
    if (block_)
      block_->release_weak();
  }

  weak_rc &operator=(const weak_rc &other) noexcept {
    if (this != &other) {
      weak_rc(other).swap(*this);
    }
    return *this;
  }

  weak_rc &operator=(const rc<T> &other) noexcept {
    weak_rc(other).swap(*this);
    return *this;
  }

  weak_rc &operator=(weak_rc &&other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = other.ptr_;
      block_ = other.block_;
      other.ptr_ = nullptr;
      other.block_ = nullptr;
    }
    return *this;
  }

  void swap(weak_rc &other) noexcept {
    std::swap(ptr_, other.ptr_);
    std::swap(block_, other.block_);
  }

  /** @brief `true` once the last `rc` has released the object (or this
   * observer was never bound to one). */
  [[nodiscard]] bool expired() const noexcept { return !block_ || block_->shared_count_ == 0; }

  /** @brief Attempts to promote this observer to an `rc`, failing with
   * `error::pointer_expired` if the object is already gone. */
  [[nodiscard]] result<rc<T>> lock() const noexcept {
    if (!block_)
      return unexpected(error::empty_pointer);
    if (!block_->try_add_shared())
      return unexpected(error::pointer_expired);
    return rc<T>(block_, ptr_);
  }

  [[nodiscard]] std::size_t use_count() const noexcept { return block_ ? block_->shared_count_ : 0; }

  void reset() noexcept {
    if (block_) {
      block_->release_weak();
      block_ = nullptr;
      ptr_ = nullptr;
    }
  }

  template <typename U> bool owner_before(const weak_rc<U> &other) const noexcept { return block_ < other.block_; }

  template <typename U> bool owner_before(const rc<U> &other) const noexcept { return block_ < other.block_; }

private:
  T *ptr_{nullptr};
  detail::rc_control_block *block_{nullptr};

  template <typename U> friend class weak_rc;
  template <typename U> friend class rc;
  template <typename U> friend class enable_rc_from_this;

  template <typename Tp, typename... Args>
  friend result<rc<Tp>> try_allocate_rc(allocator_ref alloc, Args &&...args) noexcept;

  template <typename Tp, typename... Args>
  friend result<rc<Tp>> try_allocate_combined_rc(allocator_ref alloc, Args &&...args) noexcept;
};

template <typename T> void swap(weak_rc<T> &lhs, weak_rc<T> &rhs) noexcept { lhs.swap(rhs); }

/**
 * @brief Lets a type obtain an `rc<T>` to itself from within one of its own
 * member functions, without storing an `rc` and creating a reference
 * cycle. Matches Rust's (external-crate) `Rc`-flavored `shared_from_this`
 * idiom, mirroring `enable_shared_from_this<T>`.
 *
 * `T` must publicly derive from `enable_rc_from_this<T>` and must only ever
 * be owned by `rc<T>` obtained through
 * `try_allocate_rc`/`try_create_rc`/`try_allocate_combined_rc`/
 * `try_create_combined_rc` -- those entry points populate the internal
 * `weak_rc` this relies on; calling `rc_from_this()` before that (e.g. from
 * the constructor) or on an object not owned by any `rc` fails with
 * `error::pointer_expired`/`error::empty_pointer` rather than invoking
 * undefined behavior.
 */
template <typename T> class enable_rc_from_this : detail::enable_rc_from_this_base {
public:
  [[nodiscard]] result<rc<T>> rc_from_this() const noexcept { return weak_this_.lock(); }

  [[nodiscard]] weak_rc<T> weak_from_this() const noexcept { return weak_this_; }

protected:
  constexpr enable_rc_from_this() noexcept = default;
  constexpr enable_rc_from_this(const enable_rc_from_this &) noexcept {}
  enable_rc_from_this &operator=(const enable_rc_from_this &) noexcept { return *this; }
  ~enable_rc_from_this() = default;

private:
  mutable weak_rc<T> weak_this_;

  template <typename Tp, typename... Args>
  friend result<rc<Tp>> try_allocate_rc(allocator_ref alloc, Args &&...args) noexcept;

  template <typename Tp, typename... Args>
  friend result<rc<Tp>> try_allocate_combined_rc(allocator_ref alloc, Args &&...args) noexcept;
};

/**
 * @brief Allocates the object and the control block separately, using
 * `alloc` explicitly.
 *
 * Prefer @ref try_allocate_combined_rc unless you specifically need the
 * object's storage released as soon as the last `rc` (not `weak_rc`)
 * releases it.
 */
template <typename T, typename... Args>
[[nodiscard]] result<rc<T>> try_allocate_rc(allocator_ref alloc, Args &&...args) noexcept {
  auto object_block = alloc.allocate(sizeof(T), alignof(T));
  if (!object_block)
    return unexpected(object_block.error());
  auto *raw_ptr = static_cast<T *>(object_block->ptr);

  auto ctor_res = construction_helpers::try_construct<T>(alloc, raw_ptr, std::forward<Args>(args)...);
  if (!ctor_res) {
    alloc.deallocate(object_block->ptr, object_block->size);
    return unexpected(ctor_res.error());
  }

  using control_block_t = detail::rc_control_block_separate<T>;
  auto cb_block = alloc.allocate(sizeof(control_block_t), alignof(control_block_t));
  if (!cb_block) {
    raw_ptr->~T();
    alloc.deallocate(object_block->ptr, object_block->size);
    return unexpected(cb_block.error());
  }
  auto *cb = new (cb_block->ptr) control_block_t(raw_ptr, alloc);

  if constexpr (std::is_base_of_v<detail::enable_rc_from_this_base, T>) {
    raw_ptr->weak_this_.ptr_ = raw_ptr;
    raw_ptr->weak_this_.block_ = cb;
    ++cb->weak_count_;
  }

  return rc<T>(cb, raw_ptr);
}

/**
 * @brief Allocates the object and its control block in a single allocation,
 * like `try_allocate_combined_shared`. Recommended default.
 */
template <typename T, typename... Args>
[[nodiscard]] result<rc<T>> try_allocate_combined_rc(allocator_ref alloc, Args &&...args) noexcept {
  using control_block_t = detail::rc_control_block_combined<T>;

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

  if constexpr (std::is_base_of_v<detail::enable_rc_from_this_base, T>) {
    raw_ptr->weak_this_.ptr_ = raw_ptr;
    raw_ptr->weak_this_.block_ = cb;
    ++cb->weak_count_;
  }

  return rc<T>(cb, raw_ptr);
}

/**
 * @brief `try_allocate_rc` using the process-wide default allocator (see
 * `default_allocator()`).
 */
template <typename T, typename... Args> [[nodiscard]] result<rc<T>> try_create_rc(Args &&...args) noexcept {
  return try_allocate_rc<T>(default_allocator(), std::forward<Args>(args)...);
}

/**
 * @brief `try_allocate_combined_rc` using the process-wide default
 * allocator (see `default_allocator()`). Recommended default entry point.
 */
template <typename T, typename... Args> [[nodiscard]] result<rc<T>> try_create_combined_rc(Args &&...args) noexcept {
  return try_allocate_combined_rc<T>(default_allocator(), std::forward<Args>(args)...);
}

template <typename T, typename U> rc<T> static_pointer_cast(const rc<U> &other) noexcept {
  return rc<T>(other, static_cast<T *>(other.get()));
}

template <typename T, typename U> rc<T> dynamic_pointer_cast(const rc<U> &other) noexcept {
  if (auto *ptr = dynamic_cast<T *>(other.get()))
    return rc<T>(other, ptr);
  return rc<T>();
}

template <typename T, typename U> rc<T> const_pointer_cast(const rc<U> &other) noexcept {
  return rc<T>(other, const_cast<T *>(other.get()));
}

template <typename T, typename U> rc<T> reinterpret_pointer_cast(const rc<U> &other) noexcept {
  return rc<T>(other, reinterpret_cast<T *>(other.get()));
}

/**
 * @brief `rc<T>` only holds a `T *` and a control-block pointer (itself
 * never self-referential): relocating those two words to a new address and
 * abandoning the old one never invalidates the pointee or the control
 * block. True regardless of `T`.
 */
template <typename T> struct is_trivially_relocatable<rc<T>> : std::true_type {};

/** @brief Same rationale as `is_trivially_relocatable<rc<T>>`: `weak_rc<T>`
 * only holds a `T *` and a control-block pointer. */
template <typename T> struct is_trivially_relocatable<weak_rc<T>> : std::true_type {};

/**
 * @brief `rc<T>`'s refcount is a plain, non-atomic `std::size_t`,
 * matching Rust's `Rc<T>` -- never `Send`, regardless of `T`: even
 * transferring a single handle to another thread is unsound while a
 * clone might still be dropped concurrently from the original thread.
 */
template <typename T> struct is_send<rc<T>> : std::false_type {};

/** @brief Same rationale as `is_send<rc<T>>`, and for the same reason
 * never `Sync` either: sharing `&rc<T>` across threads still exposes the
 * same non-atomic refcount to concurrent clone/drop. */
template <typename T> struct is_sync<rc<T>> : std::false_type {};

/** @brief `weak_rc<T>` shares `rc<T>`'s non-atomic control block, so it is
 * never `Send` either, matching Rust's `Weak<T>`. */
template <typename T> struct is_send<weak_rc<T>> : std::false_type {};

/** @brief Same rationale as `is_send<weak_rc<T>>`. */
template <typename T> struct is_sync<weak_rc<T>> : std::false_type {};

} // namespace reloco

namespace std {

template <typename T> struct hash<reloco::rc<T>> {
  std::size_t operator()(const reloco::rc<T> &p) const noexcept { return hash<T *>{}(p.get()); }
};

} // namespace std

RELOCO_END_UNSAFE_BUFFER_USAGE
