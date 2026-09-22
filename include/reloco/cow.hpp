// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file cow.hpp
 * @brief Clone-on-write wrapper, matching Rust's `std::borrow::Cow<'a, T>`.
 *
 * `cow<T>` holds either a borrowed `const T &` (lifetime-bound to the
 * caller) or an owned `T` value, transparently cloning the borrowed data
 * into owned storage the moment mutable access is actually needed
 * (`to_mut()`), or on request (`into_owned()`). This defers an allocation/
 * deep-copy for as long as the data is only ever read.
 *
 * Unlike Rust's `Cow<'a, B>` (which requires `B: ToOwned` and is
 * infallible to clone since Rust panics/aborts on allocation failure),
 * reloco's clone step is fallible: `to_mut()`/`into_owned()` return
 * `result<...>` and propagate an allocator failure instead of aborting.
 *
 * How to clone `T` is a customization point: `cow_traits<T>::try_clone`
 * defaults to `construction_helpers::try_clone<T>` (see
 * `construction_helpers.hpp`), the same tiered `try_clone(alloc)`/
 * `try_clone()`/`try_allocate`/`try_create`/nothrow-copy dispatch every
 * other reloco container's own `try_clone` uses. Specialize
 * `cow_traits<T>` for a type that needs different clone semantics inside
 * a `cow<T>` specifically (without changing that type's own `try_clone`
 * used elsewhere).
 *
 * ```cpp
 * reloco::string original;
 * // ... populate original ...
 * reloco::cow<reloco::string> view(original); // borrowed, no clone yet
 * assert(view.is_borrowed());
 * auto mut_ref = view.to_mut(); // clones into owned storage on first mutation
 * assert(mut_ref.has_value() && view.is_owned());
 * ```
 */

#include "construction_helpers.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief Customization point for how `cow<T>` clones a borrowed `T` into
 * owned storage.
 *
 * The default forwards to `construction_helpers::try_clone<T>`, which
 * already resolves `try_clone(alloc)`/`try_clone()`/`try_allocate`/
 * `try_create`/nothrow-copy in that order. Specialize this trait to
 * override cloning behavior for a specific `T` used inside a `cow<T>`
 * without touching that type's own clone protocol used elsewhere (e.g. to
 * make a `cow` share/refcount its owned copy instead of deep-copying it,
 * or to reject cloning outright for a type that should only ever be
 * borrowed).
 */
template <typename T> struct cow_traits {
  [[nodiscard]] static result<T> try_clone(allocator_ref alloc, const T &source) noexcept {
    return construction_helpers::try_clone<T>(alloc, source);
  }
};

/**
 * @brief Clone-on-write wrapper over a borrowed-or-owned `T`.
 *
 * Move-only: an owned `cow<T>` may hold a `T` that is itself move-only
 * (e.g. `vector<T>`/`string`), and copying a borrowed-vs-owned union
 * implicitly would either silently clone (surprising, and fallible) or
 * silently alias (unsound once the borrowed source is gone). Use
 * `try_clone()` for an explicit, fallible deep copy.
 */
template <typename T> class RELOCO_OWNER cow {
public:
  using value_type = T;
  using traits_type = cow_traits<T>;

  /**
   * @brief Borrows @p value using the process-wide default allocator for
   * any later clone (`to_mut()`/`into_owned()`). The referenced object
   * must outlive this `cow`.
   */
  constexpr cow(const T &value RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ptr_(std::addressof(value)), alloc_(default_allocator()) {}

  /**
   * @brief Borrows @p value, using @p alloc explicitly for any later
   * clone. The referenced object must outlive this `cow`.
   */
  constexpr cow(const T &value RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS, allocator_ref alloc) noexcept
      : ptr_(std::addressof(value)), alloc_(alloc) {}

  /**
   * @brief Takes ownership of @p value directly (no clone needed later).
   */
  explicit cow(T &&value) noexcept(std::is_nothrow_move_constructible_v<T>) : alloc_(default_allocator()) {
    construct_owned(std::move(value));
  }

  cow(const cow &) = delete;
  cow &operator=(const cow &) = delete;

  cow(cow &&other) noexcept(std::is_nothrow_move_constructible_v<T>) : alloc_(other.alloc_) {
    if (other.owned_) {
      construct_owned(std::move(*other.owned_ptr()));
      other.destroy_owned();
    } else {
      ptr_ = other.ptr_;
    }
  }

  cow &operator=(cow &&other) noexcept(std::is_nothrow_move_constructible_v<T>) {
    if (this != &other) {
      reset();
      alloc_ = other.alloc_;
      if (other.owned_) {
        construct_owned(std::move(*other.owned_ptr()));
        other.destroy_owned();
      } else {
        ptr_ = other.ptr_;
      }
    }
    return *this;
  }

  ~cow() { reset(); }

  /** @brief `true` if this `cow` currently borrows rather than owns. */
  [[nodiscard]] constexpr bool is_borrowed() const noexcept { return !owned_; }

  /** @brief `true` if this `cow` currently owns its value. */
  [[nodiscard]] constexpr bool is_owned() const noexcept { return owned_; }

  /**
   * @brief Read-only access to the current value, borrowed or owned.
   */
  [[nodiscard]] const T &get() const & noexcept RELOCO_LIFETIMEBOUND { return owned_ ? *owned_ptr() : *ptr_; }

  [[nodiscard]] const T &operator*() const & noexcept RELOCO_LIFETIMEBOUND { return get(); }
  [[nodiscard]] const T *operator->() const & noexcept RELOCO_LIFETIMEBOUND { return &get(); }

  /**
   * @brief Rust `Cow::to_mut` equivalent: clones the borrowed value into
   * owned storage (via `cow_traits<T>::try_clone`) if not already owned,
   * then returns a mutable reference to the now-owned value. A no-op
   * (beyond the reference) if already owned.
   */
  [[nodiscard]] result<std::reference_wrapper<T>> to_mut() & noexcept RELOCO_LIFETIMEBOUND {
    if (!owned_) {
      auto cloned = traits_type::try_clone(alloc_, *ptr_);
      if (!cloned)
        return unexpected(cloned.error());
      construct_owned(std::move(*cloned));
    }
    return std::ref(*owned_ptr());
  }

  /**
   * @brief Rust `Cow::into_owned` equivalent: consumes this `cow`,
   * returning its value as an owned `T` -- moved out directly if already
   * owned, otherwise cloned via `cow_traits<T>::try_clone`.
   */
  [[nodiscard]] result<T> into_owned() && noexcept {
    if (owned_) {
      T value(std::move(*owned_ptr()));
      destroy_owned();
      return value;
    }
    return traits_type::try_clone(alloc_, *ptr_);
  }
  result<T> into_owned() const & = delete;

  /**
   * @brief Deep-copies this `cow` (via `cow_traits<T>::try_clone` of the
   * current value, borrowed or owned), producing an independent owned
   * `cow<T>`.
   */
  [[nodiscard]] result<cow> try_clone() const noexcept {
    auto cloned = traits_type::try_clone(alloc_, get());
    if (!cloned)
      return unexpected(cloned.error());
    return cow(std::move(*cloned));
  }

private:
  T *owned_ptr() noexcept { return reinterpret_cast<T *>(&storage_); }
  const T *owned_ptr() const noexcept { return reinterpret_cast<const T *>(&storage_); }

  void construct_owned(T &&value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    new (static_cast<void *>(&storage_)) T(std::move(value));
    owned_ = true;
    ptr_ = nullptr;
  }

  void destroy_owned() noexcept {
    owned_ptr()->~T();
    owned_ = false;
  }

  void reset() noexcept {
    if (owned_)
      destroy_owned();
    ptr_ = nullptr;
  }

  alignas(T) std::byte storage_[sizeof(T)];
  const T *ptr_{nullptr};
  bool owned_{false};
  allocator_ref alloc_{};
};

} // namespace reloco
