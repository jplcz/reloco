// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file intrusive_ptr.hpp
 * @brief Reference-counted smart pointer whose refcount lives *inside* the
 * pointee (like Boost's `intrusive_ptr`, Linux's `kref`, or COM's
 * `AddRef`/`Release`), plus a ready-made counted base class.
 *
 * Compared with `shared_ptr`/`rc` (see `shared_ptr.hpp`/`rc.hpp`):
 *
 *  - `intrusive_ptr<T>` is a single word (no control block pointer) and
 *    needs no separate control-block allocation.
 *  - An `intrusive_ptr<T>` can be re-created from any `T *` that is already
 *    owned (`intrusive_ref_counted::intrusive_from_this()`), with no
 *    `enable_shared_from_this`-style back-pointer.
 *  - There is no weak pointer: the count is part of the object, so the
 *    storage must die with the last strong reference.
 *  - The refcount protocol is a customization point
 *    (`intrusive_ptr_traits<T>`), so legacy/C objects with their own
 *    retain/release functions can be managed without changing them.
 *
 * Two building blocks are provided:
 *
 *  - `intrusive_ptr<T>`: the smart pointer. It only ever calls
 *    `intrusive_ptr_traits<T>::add_ref`/`release`.
 *  - `intrusive_ref_counted<Derived, Counter>`: a CRTP base implementing the
 *    default protocol with either an atomic (`atomic_refcount`, default) or a
 *    plain (`local_refcount`) counter. Objects created through
 *    `try_allocate_intrusive`/`try_create_intrusive` remember the allocator
 *    they came from and are destroyed and freed when the last reference is
 *    released.
 *
 * Every fallible entry point returns `reloco::result<T>` (see `error.hpp`).
 *
 * Like `rc.hpp`, this file's `namespace reloco` body is wrapped in
 * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE`: the
 * factory placement-news into raw allocator storage, and the final release
 * destroys and deallocates it. The public `intrusive_ptr` API never exposes
 * caller-supplied storage; the only raw-pointer entry points
 * (`unsafe_adopt`, `unsafe_retain`, `unsafe_release`, `unsafe_get`) are
 * `RELOCO_UNSAFE_BUFFER_USAGE`.
 */

#include "construction_helpers.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"
#include "send_sync.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T> class intrusive_ptr;

/**
 * @brief Atomic reference counter for `intrusive_ref_counted` (the
 * default). Safe to share across threads. Starts at zero: the first
 * `intrusive_ptr` to adopt the object raises it to one.
 */
class atomic_refcount {
public:
  static constexpr bool is_atomic = true;

  atomic_refcount() noexcept = default;
  atomic_refcount(const atomic_refcount &) = delete;
  atomic_refcount &operator=(const atomic_refcount &) = delete;

  void add() noexcept {
    const std::size_t previous = count_.fetch_add(1, std::memory_order_relaxed);
    RELOCO_ASSERT(previous < std::numeric_limits<std::size_t>::max() / 2, "atomic_refcount: refcount overflow");
    (void)previous;
  }

  /** @return `true` when this call dropped the last reference. */
  [[nodiscard]] bool sub() noexcept { return count_.fetch_sub(1, std::memory_order_acq_rel) == 1; }

  /** @brief Adds a reference only if the count is not already zero. */
  [[nodiscard]] bool try_add() noexcept {
    auto current = count_.load(std::memory_order_relaxed);
    while (current != 0) {
      if (count_.compare_exchange_weak(current, current + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
        return true;
    }
    return false;
  }

  [[nodiscard]] std::size_t get() const noexcept { return count_.load(std::memory_order_relaxed); }

private:
  std::atomic<std::size_t> count_{0};
};

/**
 * @brief Plain (non-atomic) reference counter for `intrusive_ref_counted`.
 * Cheaper than `atomic_refcount` but strictly single-threaded, like `rc<T>`:
 * `intrusive_ptr<T>` is never `Send`/`Sync` for such a `T`.
 */
class local_refcount {
public:
  static constexpr bool is_atomic = false;

  constexpr local_refcount() noexcept = default;
  local_refcount(const local_refcount &) = delete;
  local_refcount &operator=(const local_refcount &) = delete;

  void add() noexcept {
    RELOCO_ASSERT(count_ < std::numeric_limits<std::size_t>::max() / 2, "local_refcount: refcount overflow");
    ++count_;
  }

  /** @return `true` when this call dropped the last reference. */
  [[nodiscard]] bool sub() noexcept { return --count_ == 0; }

  [[nodiscard]] bool try_add() noexcept {
    if (count_ == 0)
      return false;
    ++count_;
    return true;
  }

  [[nodiscard]] std::size_t get() const noexcept { return count_; }

private:
  std::size_t count_{0};
};

namespace detail {

template <typename T, typename = void> struct intrusive_member_atomic : std::false_type {};

template <typename T> struct intrusive_member_atomic<T, std::void_t<decltype(T::intrusive_is_atomic)>> {
  static constexpr bool value = T::intrusive_is_atomic;
};

struct intrusive_ref_counted_tag {};

} // namespace detail

/**
 * @brief Customization point describing how `intrusive_ptr<T>` retains and
 * releases a `T`. The primary template forwards to the members
 * `intrusive_add_ref()`, `intrusive_release()` and `intrusive_use_count()`
 * (all `const noexcept`), which `intrusive_ref_counted` provides.
 *
 * Specialize it in namespace `reloco` to manage a type that cannot derive
 * from `intrusive_ref_counted`, e.g. a C struct with its own retain/release:
 *
 * @code
 * template <> struct reloco::intrusive_ptr_traits<legacy_obj> {
 *   static void add_ref(const legacy_obj &o) noexcept { legacy_retain(&o); }
 *   static void release(const legacy_obj &o) noexcept { legacy_release(&o); }
 *   static constexpr bool thread_safe = true; // optional, defaults to false
 * };
 * @endcode
 *
 * `use_count` is optional (only needed if `intrusive_ptr::use_count()` is
 * called); `thread_safe` is optional and decides `is_send`/`is_sync`.
 * `T` is always passed without cv-qualifiers.
 */
template <typename T> struct intrusive_ptr_traits {
  static void add_ref(const T &obj) noexcept { obj.intrusive_add_ref(); }
  static void release(const T &obj) noexcept { obj.intrusive_release(); }
  static std::size_t use_count(const T &obj) noexcept { return obj.intrusive_use_count(); }
  static constexpr bool thread_safe = detail::intrusive_member_atomic<T>::value;
};

namespace detail {

template <typename T, typename = void> struct intrusive_thread_safe : std::false_type {};

template <typename T>
struct intrusive_thread_safe<T, std::void_t<decltype(intrusive_ptr_traits<T>::thread_safe)>>
    : std::bool_constant<intrusive_ptr_traits<T>::thread_safe> {};

} // namespace detail

/**
 * @brief Reference-counted pointer to an object that carries its own
 * refcount. Copying retains, destroying releases; the object's own
 * `intrusive_ptr_traits` decide what that means (usually: free at zero).
 *
 * Holds exactly one `T *`. Construct owned objects with
 * `try_allocate_intrusive`/`try_create_intrusive` rather than directly.
 */
template <typename T> class RELOCO_OWNER intrusive_ptr : private detail::requires_explicit_send_sync {
  using traits = intrusive_ptr_traits<std::remove_cv_t<T>>;

public:
  RELOCO_BLOCK_RVALUE_ACCESS(T);

  constexpr intrusive_ptr() noexcept = default;
  // mirrors the std smart pointers: implicit from nullptr_t and from convertible pointer types.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr intrusive_ptr(std::nullptr_t) noexcept {}

  intrusive_ptr(const intrusive_ptr &other) noexcept : ptr_(other.ptr_) { retain(); }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  // mirrors the std smart pointers: implicit from nullptr_t and from convertible pointer types.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  intrusive_ptr(const intrusive_ptr<U> &other) noexcept : ptr_(other.ptr_) {
    retain();
  }

  constexpr intrusive_ptr(intrusive_ptr &&other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  // mirrors the std smart pointers: implicit from nullptr_t and from convertible pointer types.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr intrusive_ptr(intrusive_ptr<U> &&other) noexcept : ptr_(other.ptr_) {
    other.ptr_ = nullptr;
  }

  ~intrusive_ptr() { drop(ptr_); }

  intrusive_ptr &operator=(const intrusive_ptr &other) noexcept {
    intrusive_ptr(other).swap(*this);
    return *this;
  }

  intrusive_ptr &operator=(intrusive_ptr &&other) noexcept {
    intrusive_ptr(std::move(other)).swap(*this);
    return *this;
  }

  /**
   * @brief Takes over one *existing* reference to `ptr` without touching the
   * count (the inverse of `unsafe_release()`).
   *
   * The caller must own exactly one reference to `*ptr` and must not release
   * it itself afterwards.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE static intrusive_ptr unsafe_adopt(T *ptr) noexcept {
    return intrusive_ptr(ptr);
  }

  /**
   * @brief Creates a new owning reference to `*ptr`, adding one to the
   * count. `ptr` must point at a live object (or be null).
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE static intrusive_ptr unsafe_retain(T *ptr) noexcept {
    intrusive_ptr result(ptr);
    result.retain();
    return result;
  }

  void swap(intrusive_ptr &other) noexcept { std::swap(ptr_, other.ptr_); }

  RELOCO_CHECKED [[nodiscard]] T &operator*() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(ptr_ != nullptr, "intrusive_ptr: dereference of null intrusive_ptr");
    return *ptr_;
  }

  RELOCO_CHECKED [[nodiscard]] T *operator->() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(ptr_ != nullptr, "intrusive_ptr: access of null intrusive_ptr");
    return ptr_;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }

  /** @brief Returns the raw pointer without transferring ownership. */
  [[nodiscard]] constexpr T *get() const & noexcept RELOCO_LIFETIMEBOUND { return ptr_; }

  /**
   * @brief Returns the raw pointer with only a `RELOCO_DEBUG_ASSERT` null
   * check (compiled out under `NDEBUG`). Use once non-null is established.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T *unsafe_get() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(ptr_ != nullptr, "intrusive_ptr: access of null intrusive_ptr");
    return ptr_;
  }

  /** @brief Checked pointer access: fails with `error::empty_pointer`. */
  [[nodiscard]] result<T *> try_get() const & noexcept RELOCO_LIFETIMEBOUND {
    if (!ptr_)
      return unexpected(error::empty_pointer);
    return ptr_;
  }
  result<T *> try_get() const && = delete;

  /**
   * @brief Number of references to the object (including this one), or `0`
   * for an empty pointer. Requires `intrusive_ptr_traits<T>::use_count`.
   */
  [[nodiscard]] std::size_t use_count() const noexcept { return ptr_ != nullptr ? traits::use_count(*ptr_) : 0; }

  /** @brief Releases this reference and resets the pointer to null. */
  void reset() noexcept {
    T *old = ptr_;
    ptr_ = nullptr;
    drop(old);
  }

  /**
   * @brief Gives up ownership without decrementing the count and returns the
   * raw pointer; the caller now owns that reference (hand it back with
   * `unsafe_adopt`, e.g. when passing ownership through a C API).
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T *unsafe_release() & noexcept { return std::exchange(ptr_, nullptr); }

private:
  constexpr explicit intrusive_ptr(T *ptr) noexcept : ptr_(ptr) {}

  void retain() const noexcept {
    if (ptr_ != nullptr)
      traits::add_ref(*ptr_);
  }

  static void drop(T *ptr) noexcept {
    if (ptr != nullptr)
      traits::release(*ptr);
  }

  T *ptr_{nullptr};

  template <typename U> friend class intrusive_ptr;
};

template <typename T> void swap(intrusive_ptr<T> &lhs, intrusive_ptr<T> &rhs) noexcept { lhs.swap(rhs); }

template <typename T> [[nodiscard]] bool operator==(const intrusive_ptr<T> &lhs, std::nullptr_t) noexcept {
  return lhs.get() == nullptr;
}

template <typename T> [[nodiscard]] bool operator==(std::nullptr_t, const intrusive_ptr<T> &rhs) noexcept {
  return rhs.get() == nullptr;
}

template <typename T> [[nodiscard]] bool operator!=(const intrusive_ptr<T> &lhs, std::nullptr_t) noexcept {
  return lhs.get() != nullptr;
}

template <typename T> [[nodiscard]] bool operator!=(std::nullptr_t, const intrusive_ptr<T> &rhs) noexcept {
  return rhs.get() != nullptr;
}

template <typename T, typename U>
[[nodiscard]] bool operator==(const intrusive_ptr<T> &lhs, const intrusive_ptr<U> &rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename T, typename U>
[[nodiscard]] bool operator!=(const intrusive_ptr<T> &lhs, const intrusive_ptr<U> &rhs) noexcept {
  return lhs.get() != rhs.get();
}

template <typename T, typename U>
[[nodiscard]] bool operator<(const intrusive_ptr<T> &lhs, const intrusive_ptr<U> &rhs) noexcept {
  return std::less<>{}(lhs.get(), rhs.get());
}

/**
 * @brief CRTP base implementing the default `intrusive_ptr_traits` protocol.
 *
 * `Derived` is the class deriving from this base (it is what
 * `intrusive_from_this()` returns a pointer to); `Counter` is
 * `atomic_refcount` (default, thread-safe) or `local_refcount`
 * (single-threaded, cheaper).
 *
 * Objects created by `try_allocate_intrusive`/`try_create_intrusive` record
 * their allocator and are destroyed + deallocated when the last
 * `intrusive_ptr` releases them. An object created any other way (stack,
 * member, static) is never freed by the count: reaching zero just does
 * nothing, so such objects may still be shared with `intrusive_ptr`
 * observers as long as they outlive every one of them.
 *
 * Copying/moving/assigning a counted object never copies its count or allocator:
 * the copy is a fresh, unowned object.
 */
template <typename Derived, typename Counter = atomic_refcount>
// NOLINTNEXTLINE(reloco-pointer-annotation): records the allocator it was created from, by design.
class intrusive_ref_counted : private detail::intrusive_ref_counted_tag {
public:
  /** @brief `true` when the count is atomic; drives `is_send`/`is_sync`. */
  static constexpr bool intrusive_is_atomic = Counter::is_atomic;

  /** @brief Protocol hook called by `intrusive_ptr_traits`. */
  void intrusive_add_ref() const noexcept { count_.add(); }

  /** @brief Protocol hook called by `intrusive_ptr_traits`; frees the object
   * at zero if it came from `try_allocate_intrusive`. */
  void intrusive_release() const noexcept {
    if (count_.sub() && destroy_ != nullptr)
      destroy_(*this);
  }

  [[nodiscard]] std::size_t intrusive_use_count() const noexcept { return count_.get(); }

  /**
   * @brief Returns an owning `intrusive_ptr` to this object, or
   * `error::pointer_expired` if no `intrusive_ptr` owns it right now (e.g.
   * called from the constructor, or on a stack object).
   */
  [[nodiscard]] result<intrusive_ptr<Derived>> intrusive_from_this() noexcept {
    if (!count_.try_add())
      return unexpected(error::pointer_expired);
    return intrusive_ptr<Derived>::unsafe_adopt(static_cast<Derived *>(this));
  }

  /** @brief `const` overload of `intrusive_from_this()`. */
  [[nodiscard]] result<intrusive_ptr<const Derived>> intrusive_from_this() const noexcept {
    if (!count_.try_add())
      return unexpected(error::pointer_expired);
    return intrusive_ptr<const Derived>::unsafe_adopt(static_cast<const Derived *>(this));
  }

protected:
  constexpr intrusive_ref_counted() noexcept = default;
  constexpr intrusive_ref_counted(const intrusive_ref_counted &) noexcept {}
  constexpr intrusive_ref_counted &operator=(const intrusive_ref_counted &) noexcept { return *this; }
  constexpr intrusive_ref_counted(intrusive_ref_counted &&) noexcept {}
  constexpr intrusive_ref_counted &operator=(intrusive_ref_counted &&) noexcept { return *this; }

  ~intrusive_ref_counted() {
    RELOCO_DEBUG_ASSERT(count_.get() == 0, "intrusive_ref_counted: destroyed while referenced");
  }

private:
  using destroy_fn = void (*)(const intrusive_ref_counted &) noexcept;

  // Runs on the last release: `T` is the most-derived type the factory
  // allocated, so the right destructor and size are used even through a base pointer.
  template <typename T> static void destroy_impl(const intrusive_ref_counted &self) noexcept {
    const allocator_ref alloc = self.alloc_;
    // NOLINTNEXTLINE(reloco-banned-construct): allocated mutable, seen const only via the release hook.
    T *obj = const_cast<T *>(static_cast<const T *>(&self));
    obj->~T();
    alloc.deallocate(obj, sizeof(T));
  }

  template <typename T> void bind_allocation(allocator_ref alloc) noexcept {
    alloc_ = alloc;
    destroy_ = &destroy_impl<T>;
  }

  // the count is mutated through const `intrusive_ptr<const T>` references.
  // NOLINTNEXTLINE(reloco-mutable-member)
  mutable Counter count_;
  allocator_ref alloc_;
  destroy_fn destroy_{nullptr};

  template <typename Tp, typename... Args>
  friend result<intrusive_ptr<Tp>> try_allocate_intrusive(allocator_ref alloc, Args &&...args) noexcept;
};

/**
 * @brief Allocates a `T` (which must derive from
 * `intrusive_ref_counted<...>`) with `alloc` and returns the first owning
 * `intrusive_ptr`. The object is destroyed and its storage returned to
 * `alloc` when the last reference is released.
 *
 * `T`'s constructor is run through `construction_helpers::try_construct`,
 * so a fallible `try_construct`/`try_allocate`/`try_create` is honoured.
 */
template <typename T, typename... Args>
[[nodiscard]] result<intrusive_ptr<T>> try_allocate_intrusive(allocator_ref alloc, Args &&...args) noexcept {
  static_assert(std::is_base_of_v<detail::intrusive_ref_counted_tag, T>,
                "try_allocate_intrusive: T must derive from intrusive_ref_counted<...>; types with a custom "
                "intrusive_ptr_traits manage their own storage");

  auto block = alloc.allocate(sizeof(T), alignof(T));
  if (!block)
    return unexpected(block.error());
  auto *raw_ptr = static_cast<T *>(block->ptr);

  auto ctor_res = construction_helpers::try_construct<T>(alloc, raw_ptr, std::forward<Args>(args)...);
  if (!ctor_res) {
    alloc.deallocate(block->ptr, block->size);
    return unexpected(ctor_res.error());
  }

  raw_ptr->template bind_allocation<T>(alloc);
  auto owner = intrusive_ptr<T>::unsafe_retain(raw_ptr);
  return owner;
}

/** @brief `try_allocate_intrusive` using the process-wide default allocator
 * (see `default_allocator()`). */
template <typename T, typename... Args>
[[nodiscard]] result<intrusive_ptr<T>> try_create_intrusive(Args &&...args) noexcept {
  return try_allocate_intrusive<T>(default_allocator(), std::forward<Args>(args)...);
}

template <typename T, typename U> intrusive_ptr<T> static_pointer_cast(const intrusive_ptr<U> &other) noexcept {
  return intrusive_ptr<T>::unsafe_retain(static_cast<T *>(other.get()));
}

template <typename T, typename U> intrusive_ptr<T> dynamic_pointer_cast(const intrusive_ptr<U> &other) noexcept {
  // NOLINTNEXTLINE(reloco-banned-construct): explicit dynamic_pointer_cast, the caller asked for it.
  if (auto *ptr = dynamic_cast<T *>(other.get()))
    return intrusive_ptr<T>::unsafe_retain(ptr);
  return intrusive_ptr<T>();
}

template <typename T, typename U> intrusive_ptr<T> const_pointer_cast(const intrusive_ptr<U> &other) noexcept {
  // NOLINTNEXTLINE(reloco-banned-construct): explicit const_pointer_cast, the caller asked for it.
  return intrusive_ptr<T>::unsafe_retain(const_cast<T *>(other.get()));
}

/** @brief `intrusive_ptr<T>` only holds a `T *`; relocating it never
 * invalidates the pointee. True regardless of `T`. */
template <typename T> struct is_trivially_relocatable<intrusive_ptr<T>> : std::true_type {};

/**
 * @brief `Send` only when the count is atomic (`intrusive_ptr_traits<T>::
 * thread_safe`) *and* `T` is itself both `Send` and `Sync`, matching
 * `shared_ptr<T>`: every live clone reaches the same `T` concurrently.
 */
template <typename T>
struct is_send<intrusive_ptr<T>>
    : std::bool_constant<detail::intrusive_thread_safe<std::remove_cv_t<T>>::value && is_send_v<T> && is_sync_v<T>> {};

/** @brief Same rationale/bound as `is_send<intrusive_ptr<T>>`. */
template <typename T>
struct is_sync<intrusive_ptr<T>>
    : std::bool_constant<detail::intrusive_thread_safe<std::remove_cv_t<T>>::value && is_send_v<T> && is_sync_v<T>> {};

} // namespace reloco

namespace std {

template <typename T> struct hash<reloco::intrusive_ptr<T>> {
  std::size_t operator()(const reloco::intrusive_ptr<T> &p) const noexcept { return hash<T *>{}(p.get()); }
};

} // namespace std

RELOCO_END_UNSAFE_BUFFER_USAGE
