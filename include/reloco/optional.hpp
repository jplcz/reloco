// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file optional.hpp
 * @brief Zero-allocation, conditionally-present value wrapper mirroring
 * the strict safety and tri-tier access patterns of reloco containers.
 *
 * `reloco::optional<T>` replaces `std::optional<T>` by eliminating
 * undefined behavior on empty access. Instead, it follows the `reloco` way:
 *
 * 1. **Checked (Default):** `value()`, `operator*`, and `operator->` use
 *    `RELOCO_ASSERT` to trap safely if empty.
 * 2. **Fallible:** `try_value()` returns `result<reference_wrapper<T>>`, and
 *    `ok_or(error)` bridges the optional directly into a `result<T>` pipeline.
 * 3. **Unsafe:** `unsafe_value()` and `unsafe_ptr()` are explicitly gated
 *    behind `RELOCO_UNSAFE_BUFFER_USAGE` and only checked via `RELOCO_DEBUG_ASSERT`.
 *
 * It manages memory inline without dynamic allocation and natively integrates
 * with `is_trivially_relocatable<T>` to ensure zero-overhead swaps and moves
 * where the underlying type permits.
 *
 * When AddressSanitizer/Valgrind support is active (see
 * reloco/detail/sanitizer.hpp), the inline `T` storage is poisoned
 * whenever the optional is empty (from construction, and again after
 * every `reset()`/`destroy()`) and unpoisoned right before it is
 * (re)constructed, so reading/writing through `unsafe_ptr()`/
 * `unsafe_value()` while empty is caught even in a release build with
 * `RELOCO_DEBUG_ASSERT` compiled out.
 */

#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "detail/sanitizer.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"

#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace reloco {

struct RELOCO_EXPORT nullopt_t {
  struct init {};
  constexpr explicit nullopt_t(init) noexcept {}
};

inline constexpr nullopt_t nullopt{nullopt_t::init{}};

template <typename T> class RELOCO_CONSUMABLE(unconsumed) optional {
public:
  using value_type = T;

  constexpr optional() noexcept RELOCO_RETURN_TYPESTATE(consumed) : dummy_('\0'), has_value_(false) {
    detail::poison_memory_region(std::addressof(value_), sizeof(T));
  }
  constexpr optional(nullopt_t) noexcept : dummy_('\0'), has_value_(false) {
    detail::poison_memory_region(std::addressof(value_), sizeof(T));
  }

  constexpr optional(const T &value) noexcept(std::is_nothrow_copy_constructible_v<T>)
      RELOCO_RETURN_TYPESTATE(unconsumed)
      : has_value_(false) {
    construct(value);
  }

  constexpr optional(T &&value) noexcept(std::is_nothrow_move_constructible_v<T>) RELOCO_RETURN_TYPESTATE(unconsumed)
      : has_value_(false) {
    construct(std::move(value));
  }

  optional(const optional &other) noexcept(std::is_nothrow_copy_constructible_v<T>) RELOCO_RETURN_TYPESTATE(unconsumed)
      : has_value_(false) {
    if (other.has_value_) {
      construct(other.value_);
    }
  }

  optional(optional &&other) noexcept(std::is_nothrow_move_constructible_v<T>) : has_value_(false) {
    if (other.has_value_) {
      construct(std::move(other.value_));
    }
  }

  template <typename... Args>
  constexpr explicit optional(std::in_place_t, Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
      RELOCO_RETURN_TYPESTATE(unconsumed)
      : has_value_(false) {
    construct(std::forward<Args>(args)...);
  }

  ~optional() { destroy(); }

  optional &operator=(nullopt_t) noexcept RELOCO_SET_TYPESTATE(consumed) {
    reset();
    return *this;
  }

  optional &operator=(const optional &other) noexcept(std::is_nothrow_copy_assignable_v<T> &&
                                                      std::is_nothrow_copy_constructible_v<T>) {
    if (this != &other) {
      if (other.has_value_) {
        if (has_value_)
          value_ = other.value_;
        else
          construct(other.value_);
      } else {
        destroy();
      }
    }
    return *this;
  }

  optional &operator=(optional &&other) noexcept(std::is_nothrow_move_assignable_v<T> &&
                                                 std::is_nothrow_move_constructible_v<T>) {
    if (this != &other) {
      if (other.has_value_) {
        if (has_value_)
          value_ = std::move(other.value_);
        else
          construct(std::move(other.value_));
      } else {
        destroy();
      }
    }
    return *this;
  }

  template <typename U = T>
  std::enable_if_t<std::is_constructible_v<T, U &&> && std::is_assignable_v<T &, U &&>, optional &>
  operator=(U &&value) noexcept(std::is_nothrow_assignable_v<T &, U &&> && std::is_nothrow_constructible_v<T, U &&>)
      RELOCO_SET_TYPESTATE(unconsumed) {
    if (has_value_) {
      value_ = std::forward<U>(value);
    } else {
      construct(std::forward<U>(value));
    }
    return *this;
  }

  [[nodiscard]] constexpr bool has_value() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) { return has_value_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) {
    return has_value_;
  }

  /** @brief Rust `Option::is_some()` alias for `has_value()`. */
  [[nodiscard]] constexpr bool is_some() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) { return has_value_; }

  /** @brief Rust `Option::is_none()` alias for `!has_value()`. */
  [[nodiscard]] constexpr bool is_none() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) { return !has_value_; }

  /**
   * @brief Rust `Option::is_some_and` equivalent: `true` if a value is
   * present and @p f returns `true` for it; `false` otherwise (@p f is
   * not invoked when empty).
   */
  template <typename F> [[nodiscard]] bool is_some_and(F &&f) const noexcept { return has_value_ && f(value_); }

  [[nodiscard]] T &value() & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(has_value_, "optional has no value");
    return value_;
  }

  [[nodiscard]] const T &value() const & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(has_value_, "optional has no value");
    return value_;
  }

  [[nodiscard]] T &&value() && noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(has_value_, "optional has no value");
    return std::move(value_);
  }

  /** @brief Rust `Option::unwrap()` alias for `value()`. */
  [[nodiscard]] T &unwrap() & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") { return value(); }
  /** @brief Rust `Option::unwrap()` alias for `value()`. */
  [[nodiscard]] const T &unwrap() const & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    return value();
  }
  /** @brief Rust `Option::unwrap()` alias for `value()`. */
  [[nodiscard]] T &&unwrap() && noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    return std::move(*this).value();
  }

  /**
   * @brief Rust `Option::expect(msg)` equivalent: like `value()`, but
   * @p msg is used as the `RELOCO_ASSERT_MSG` failure message instead of a
   * generic one, for a more actionable trap site.
   */
  [[nodiscard]] T &expect(const char *msg) & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT_MSG(has_value_, msg);
    return value_;
  }
  /** @copydoc expect(const char *) & */
  [[nodiscard]] const T &expect(const char *msg) const & noexcept RELOCO_LIFETIMEBOUND
      RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT_MSG(has_value_, msg);
    return value_;
  }
  /** @copydoc expect(const char *) & */
  [[nodiscard]] T &&expect(const char *msg) && noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT_MSG(has_value_, msg);
    return std::move(value_);
  }

  [[nodiscard]] T &operator*() & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") { return value(); }
  [[nodiscard]] const T &operator*() const & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    return value();
  }

  [[nodiscard]] T *operator->() noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(has_value_, "optional has no value");
    return std::addressof(value_);
  }

  [[nodiscard]] const T *operator->() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(has_value_, "optional has no value");
    return std::addressof(value_);
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_value() & noexcept RELOCO_LIFETIMEBOUND {
    if (!has_value_)
      return unexpected(error::not_found);
    return std::ref(value_);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_value() const & noexcept RELOCO_LIFETIMEBOUND {
    if (!has_value_)
      return unexpected(error::not_found);
    return std::cref(value_);
  }

  /**
   * @brief Bridges the optional directly into a `reloco::result` pipeline.
   * Allows specifying the exact error to return if the optional is empty.
   */
  [[nodiscard]] result<T> ok_or(error e) const & noexcept(std::is_nothrow_copy_constructible_v<T>) {
    if (has_value_)
      return value_;
    return unexpected(e);
  }

  [[nodiscard]] result<T> ok_or(error e) && noexcept(std::is_nothrow_move_constructible_v<T>) {
    if (has_value_)
      return std::move(value_);
    return unexpected(e);
  }

  template <typename U>
  [[nodiscard]] T value_or(U &&default_value) const & noexcept(std::is_nothrow_copy_constructible_v<T>) {
    return has_value_ ? value_ : static_cast<T>(std::forward<U>(default_value));
  }

  template <typename U>
  [[nodiscard]] T value_or(U &&default_value) && noexcept(std::is_nothrow_move_constructible_v<T>) {
    return has_value_ ? std::move(value_) : static_cast<T>(std::forward<U>(default_value));
  }

  /** @brief Rust `Option::unwrap_or` alias for `value_or`. */
  template <typename U>
  [[nodiscard]] T unwrap_or(U &&default_value) const & noexcept(std::is_nothrow_copy_constructible_v<T>) {
    return value_or(std::forward<U>(default_value));
  }
  /** @copydoc unwrap_or(U &&) const & */
  template <typename U>
  [[nodiscard]] T unwrap_or(U &&default_value) && noexcept(std::is_nothrow_move_constructible_v<T>) {
    return std::move(*this).value_or(std::forward<U>(default_value));
  }

  /**
   * @brief Rust `Option::unwrap_or_default` equivalent: returns the
   * contained value, or a default-constructed `T` if empty.
   */
  template <typename U = T, std::enable_if_t<std::is_nothrow_default_constructible_v<U>, int> = 0>
  [[nodiscard]] T unwrap_or_default() const & noexcept(std::is_nothrow_copy_constructible_v<T>) {
    return has_value_ ? value_ : T();
  }
  /** @copydoc unwrap_or_default() const & */
  template <typename U = T, std::enable_if_t<std::is_nothrow_default_constructible_v<U>, int> = 0>
  [[nodiscard]] T unwrap_or_default() && noexcept(std::is_nothrow_move_constructible_v<T>) {
    return has_value_ ? std::move(value_) : T();
  }

  /**
   * @brief Rust `Option::unwrap_or_else` equivalent: returns the
   * contained value, or invokes @p f (no arguments) and returns its
   * result if empty.
   */
  template <typename F>
  [[nodiscard]] T unwrap_or_else(F &&f) const & noexcept(std::is_nothrow_copy_constructible_v<T>) {
    return has_value_ ? value_ : f();
  }
  /** @copydoc unwrap_or_else(F &&) const & */
  template <typename F> [[nodiscard]] T unwrap_or_else(F &&f) && noexcept(std::is_nothrow_move_constructible_v<T>) {
    return has_value_ ? std::move(value_) : f();
  }

  /**
   * @brief Rust `Option::map` equivalent: if a value is present, applies
   * @p f to it and returns the result wrapped in a new `optional`;
   * otherwise returns an empty `optional` of the mapped type.
   */
  template <typename F> [[nodiscard]] auto map(F &&f) const & noexcept {
    using U = decltype(f(std::declval<const T &>()));
    if (has_value_)
      return optional<U>(f(value_));
    return optional<U>(nullopt);
  }

  template <typename F> [[nodiscard]] auto map(F &&f) && noexcept {
    using U = decltype(f(std::declval<T &&>()));
    if (has_value_)
      return optional<U>(f(std::move(value_)));
    return optional<U>(nullopt);
  }

  /**
   * @brief Rust `Option::and_then` equivalent: if a value is present,
   * invokes @p f with it and returns its `optional` result directly
   * (allowing flattening); otherwise returns an empty `optional`.
   */
  template <typename F> [[nodiscard]] auto and_then(F &&f) const & noexcept {
    using Ret = decltype(f(std::declval<const T &>()));
    if (has_value_)
      return f(value_);
    return Ret(nullopt);
  }

  template <typename F> [[nodiscard]] auto and_then(F &&f) && noexcept {
    using Ret = decltype(f(std::declval<T &&>()));
    if (has_value_)
      return f(std::move(value_));
    return Ret(nullopt);
  }

  /**
   * @brief Rust `Option::or_else` equivalent: returns `*this` if a value
   * is present, otherwise invokes @p f (which takes no arguments) and
   * returns its `optional<T>` result.
   */
  template <typename F> [[nodiscard]] optional or_else(F &&f) const & noexcept {
    if (has_value_)
      return *this;
    return f();
  }

  template <typename F> [[nodiscard]] optional or_else(F &&f) && noexcept {
    if (has_value_)
      return std::move(*this);
    return f();
  }

  /**
   * @brief Rust `Option::filter` equivalent: keeps the current value only
   * if it is present and @p pred(value) is `true`; otherwise returns an
   * empty `optional<T>`.
   */
  template <typename Pred> [[nodiscard]] optional filter(Pred &&pred) const & noexcept {
    if (has_value_ && pred(std::as_const(value_)))
      return *this;
    return nullopt;
  }

  /**
   * @brief Rust `Option::map_or` equivalent: if a value is present,
   * applies @p f to it and returns the result; otherwise returns
   * @p default_value.
   */
  template <typename U, typename F> [[nodiscard]] U map_or(U default_value, F &&f) const & noexcept {
    if (has_value_)
      return f(value_);
    return default_value;
  }
  /** @copydoc map_or(U, F &&) const & */
  template <typename U, typename F> [[nodiscard]] U map_or(U default_value, F &&f) && noexcept {
    if (has_value_)
      return f(std::move(value_));
    return default_value;
  }

  /**
   * @brief Rust `Option::map_or_else` equivalent: if a value is present,
   * applies @p f to it and returns the result; otherwise invokes
   * @p default_fn (no arguments) and returns its result.
   */
  template <typename D, typename F> [[nodiscard]] auto map_or_else(D &&default_fn, F &&f) const & noexcept {
    if (has_value_)
      return f(value_);
    return default_fn();
  }
  /** @copydoc map_or_else(D &&, F &&) const & */
  template <typename D, typename F> [[nodiscard]] auto map_or_else(D &&default_fn, F &&f) && noexcept {
    if (has_value_)
      return f(std::move(value_));
    return default_fn();
  }

  /**
   * @brief Rust `Option::zip` equivalent: if both `*this` and @p other
   * hold a value, returns an `optional<std::pair<T, U>>` containing both;
   * otherwise returns an empty `optional`.
   */
  template <typename U>
  [[nodiscard]] auto zip(const optional<U> &other) const & noexcept(std::is_nothrow_copy_constructible_v<T> &&
                                                                    std::is_nothrow_copy_constructible_v<U>) {
    using Zipped = optional<std::pair<T, U>>;
    if (has_value_ && other.has_value())
      return Zipped(std::in_place, value_, other.value());
    return Zipped(nullopt);
  }

  /**
   * @brief Rust `Option::xor` equivalent (renamed since `xor` is a
   * reserved alternative operator token in C++): returns the operand that
   * holds a value if exactly one of `*this`/@p other does; otherwise
   * returns an empty `optional<T>`.
   */
  [[nodiscard]] auto logical_xor(const optional &other) const noexcept(std::is_nothrow_copy_constructible_v<T>) {
    if (has_value_ && !other.has_value_)
      return optional(*this);
    if (!has_value_ && other.has_value_)
      return optional(other);
    return optional(nullopt);
  }

  /**
   * @brief Rust `Option::take` equivalent: moves the value out into a
   * freshly-returned `optional<T>`, leaving `*this` empty. Always
   * callable, regardless of typestate (an empty `optional` simply
   * returns another empty one), matching `reset()`.
   */
  [[nodiscard]] optional take() & noexcept(std::is_nothrow_move_constructible_v<T>) RELOCO_SET_TYPESTATE(consumed) {
    if (!has_value_)
      return optional(nullopt);
    optional result(std::move(value_));
    destroy();
    return result;
  }

  /**
   * @brief Rust `Option::replace` equivalent: moves @p value in, returning
   * whatever `*this` held beforehand (empty or not) as a fresh
   * `optional<T>`.
   */
  template <typename U = T>
  optional replace(U &&value) & noexcept(std::is_nothrow_constructible_v<T, U &&> &&
                                         std::is_nothrow_move_constructible_v<T>) RELOCO_SET_TYPESTATE(unconsumed) {
    optional old = take();
    construct(std::forward<U>(value));
    return old;
  }

  /**
   * @brief Rust `Option::get_or_insert` equivalent: if empty, moves
   * @p value in; either way, returns a reference to the now-present
   * value.
   */
  T &get_or_insert(T value) & noexcept(std::is_nothrow_move_constructible_v<T>) RELOCO_LIFETIMEBOUND
      RELOCO_SET_TYPESTATE(unconsumed) {
    if (!has_value_)
      construct(std::move(value));
    return value_;
  }

  /**
   * @brief Rust `Option::get_or_insert_with` equivalent: if empty, invokes
   * @p factory (which takes no arguments) and moves its result in --
   * @p factory is never invoked when a value is already present; either
   * way, returns a reference to the now-present value.
   */
  template <typename F>
  T &get_or_insert_with(F &&factory) & noexcept RELOCO_LIFETIMEBOUND RELOCO_SET_TYPESTATE(unconsumed) {
    if (!has_value_)
      construct(factory());
    return value_;
  }

  /**
   * @brief Rust `Option::insert` equivalent: unconditionally (re)constructs
   * the contained value from @p value, discarding any previous one, and
   * returns a reference to it. Unlike `get_or_insert`, this always
   * overwrites; this is an alias of `emplace(value)`.
   */
  template <typename U = T>
  T &insert(U &&value) & noexcept(std::is_nothrow_constructible_v<T, U &&>) RELOCO_LIFETIMEBOUND
      RELOCO_SET_TYPESTATE(unconsumed) {
    return emplace(std::forward<U>(value));
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_value() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(has_value_, "optional has no value");
    return value_;
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_value() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(has_value_, "optional has no value");
    return value_;
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T *unsafe_ptr() noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(has_value_, "optional has no value");
    return std::addressof(value_);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T *unsafe_ptr() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(has_value_, "optional has no value");
    return std::addressof(value_);
  }

  template <typename... Args>
  T &emplace(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>) RELOCO_LIFETIMEBOUND
      RELOCO_SET_TYPESTATE(unconsumed) {
    destroy();
    construct(std::forward<Args>(args)...);
    return value_;
  }

  RELOCO_REINITIALIZES void reset() noexcept RELOCO_SET_TYPESTATE(consumed) { destroy(); }

  void swap(optional &other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>) {
    if (has_value_ == other.has_value_) {
      if (has_value_) {
        using std::swap;
        swap(value_, other.value_);
      }
    } else {
      if (has_value_) {
        other.construct(std::move(value_));
        destroy();
      } else {
        construct(std::move(other.value_));
        other.destroy();
      }
    }
  }

  /**
   * @brief Recovers from Clang's "unknown" typestate at a reference/pointer
   * boundary; see the primary template's @ref checked_value::as_known for
   * the full explanation.
   */
  [[nodiscard]] optional &as_known() & noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(has_value_, "optional: as_known() called on empty object");
    return *this;
  }

  [[nodiscard]] const optional &as_known() const & noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(has_value_, "optional: as_known() called on empty object");
    return *this;
  }

private:
  void destroy() noexcept {
    if (has_value_) {
      if constexpr (!std::is_trivially_destructible_v<T>) {
        value_.~T();
      }
      has_value_ = false;
      detail::poison_memory_region(std::addressof(value_), sizeof(T));
    }
  }

  template <typename... Args> void construct(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    detail::unpoison_memory_region(std::addressof(value_), sizeof(T));
    new (std::addressof(value_)) T(std::forward<Args>(args)...);
    has_value_ = true;
  }

  union {
    char dummy_;
    T value_;
  };
  bool has_value_;
};

/**
 * @brief Optional natively propagates the relocatability of its underlying type.
 */
template <typename T> struct is_trivially_relocatable<optional<T>> : is_trivially_relocatable<T> {};

template <typename T> [[nodiscard]] constexpr bool operator==(const optional<T> &lhs, const optional<T> &rhs) noexcept {
  if (lhs.has_value() != rhs.has_value())
    return false;
  if (!lhs.has_value())
    return true;
  // Safely bypass bounds checks internally since we already checked has_value()
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  return lhs.unsafe_value() == rhs.unsafe_value();
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

template <typename T> [[nodiscard]] constexpr bool operator!=(const optional<T> &lhs, const optional<T> &rhs) noexcept {
  return !(lhs == rhs);
}

template <typename T> [[nodiscard]] constexpr bool operator==(const optional<T> &opt, nullopt_t) noexcept {
  return !opt.has_value();
}

template <typename T> [[nodiscard]] constexpr bool operator==(nullopt_t, const optional<T> &opt) noexcept {
  return !opt.has_value();
}

template <typename T> [[nodiscard]] constexpr bool operator!=(const optional<T> &opt, nullopt_t) noexcept {
  return opt.has_value();
}

template <typename T> [[nodiscard]] constexpr bool operator!=(nullopt_t, const optional<T> &opt) noexcept {
  return opt.has_value();
}

template <typename T, typename U>
[[nodiscard]] constexpr bool operator==(const optional<T> &opt, const U &value) noexcept {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  return opt.has_value() ? opt.unsafe_value() == value : false;
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

template <typename T, typename U>
[[nodiscard]] constexpr bool operator==(const U &value, const optional<T> &opt) noexcept {
  return opt == value;
}

template <typename T, typename U>
[[nodiscard]] constexpr bool operator!=(const optional<T> &opt, const U &value) noexcept {
  return !(opt == value);
}

template <typename T, typename U>
[[nodiscard]] constexpr bool operator!=(const U &value, const optional<T> &opt) noexcept {
  return !(opt == value);
}

/**
 * @brief Rust `Result::ok()` equivalent: converts a present value into
 * `optional<T>`, discarding the error on failure. Named `Ok` (capitalized)
 * to match Rust's `Result::Ok` variant casing rather than the lowercase
 * accessor method.
 */
template <typename T, typename E> [[nodiscard]] auto Ok(const expected<T, E> &e) noexcept {
  if (e.has_value())
    return optional<T>(e.value());
  return optional<T>(nullopt);
}

/**
 * @brief Rust `Result::err()` equivalent: converts a failure into
 * `optional<E>`, discarding the value on success. Named `Err`
 * (capitalized) to match Rust's `Result::Err` variant casing.
 */
template <typename T, typename E> [[nodiscard]] auto Err(const expected<T, E> &e) noexcept {
  if (!e.has_value())
    return optional<E>(e.error());
  return optional<E>(nullopt);
}

/**
 * @brief Rust `bool::then` equivalent: if @p condition is `true`, invokes
 * @p f (which takes no arguments) and returns its result wrapped in a new
 * `optional`; otherwise returns an empty `optional` of the same type,
 * without invoking @p f.
 */
template <typename F> [[nodiscard]] auto then(bool condition, F &&f) noexcept {
  using U = decltype(f());
  if (condition)
    return optional<U>(f());
  return optional<U>(nullopt);
}

/**
 * @brief Rust `bool::then_some` equivalent: if @p condition is `true`,
 * moves @p value into a new `optional<T>`; otherwise returns an empty
 * `optional<T>`, without evaluating @p value further. Unlike `then()`,
 * @p value is always constructed regardless of @p condition (it is an
 * ordinary function argument, not a lazily-invoked callable); prefer
 * `then()` when constructing the value has a cost worth skipping.
 */
template <typename T>
[[nodiscard]] optional<T> then_some(bool condition, T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
  if (condition)
    return optional<T>(std::move(value));
  return optional<T>(nullopt);
}

/**
 * @brief Rust `Option::flatten` equivalent: collapses a nested
 * `optional<optional<T>>` into an `optional<T>` (empty if either layer is
 * empty). Implemented as a free function since C++ cannot partially
 * specialize a member function on `T` itself being an `optional`.
 */
template <typename T> [[nodiscard]] auto flatten(const optional<optional<T>> &opt) noexcept {
  if (opt.has_value())
    return optional<T>(opt.value());
  return optional<T>(nullopt);
}

/** @copydoc flatten(const optional<optional<T>> &) */
template <typename T> [[nodiscard]] auto flatten(optional<optional<T>> &&opt) noexcept {
  if (opt.has_value())
    return optional<T>(std::move(opt).value());
  return optional<T>(nullopt);
}

} // namespace reloco