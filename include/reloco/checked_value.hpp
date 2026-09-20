// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file checked_value.hpp
 * @brief Move-only value wrapper approximating Rust's use-after-move checks.
 */

#include "detail/assert.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief Move-only wrapper giving best-effort, Rust-like move semantics to a
 * value of type @c T.
 *
 * `checked_value<T>` is never copied implicitly: a moved-from instance is
 * poisoned and any further access aborts via @c RELOCO_ASSERT, on every
 * compiler. When compiled with Clang, the wrapper additionally opts into
 * `-Wconsumed`: `std::move(v)` marks `v` `"consumed"`, so reading a
 * moved-from value is *also* a compile-time diagnostic wherever Clang can
 * see it, matching (approximately) Rust's compile-time use-after-move error.
 *
 * This is a best-effort, opt-in approximation, not a real borrow checker:
 * - The static diagnostic is Clang-only and warns rather than hard-errors
 *   unless the build enables `-Werror` (see
 *   `JPLCZ_RELOCO_ENABLE_STRICT_WARNINGS`/`JPLCZ_RELOCO_ENABLE_WERROR`).
 * - Clang does not reliably flag "move from an already-moved-from value" at
 *   the second `std::move(...)` call site, so double-move is caught only by
 *   the runtime assert below, on every compiler.
 * - There is no aliasing/exclusivity analysis: this only prevents *using a
 *   value after it has been moved from*, not general reference aliasing.
 */
template <typename T> class RELOCO_CONSUMABLE(unconsumed) checked_value {
  static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow move constructible");
  // CTAD deduces checked_value<std::nullptr_t> from a bare `nullptr` literal
  // (`checked_value v(nullptr);`), silently bypassing the null-checking
  // checked_value<T *> specialization below. Reject it here with a clear
  // message instead of instantiating a useless wrapper around nullptr_t.
  static_assert(!std::is_same_v<T, std::nullptr_t>,
                "checked_value<std::nullptr_t> is not useful and provides no null-checking; write "
                "checked_value<YourType *>(nullptr) explicitly to get the pointer specialization instead");

public:
  /** @brief Wraps @p value in a fresh, unconsumed `checked_value`. */
  constexpr explicit checked_value(T value) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : value_(std::move(value)) {}

  // Move-only: copies are never implicit. Use clone() to opt into an
  // explicit, Rust-`Clone`-style copy when T supports it.
  checked_value(const checked_value &) = delete;
  checked_value &operator=(const checked_value &) = delete;

  constexpr checked_value(checked_value &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
      : value_(take_from(other)) {}

  constexpr checked_value &operator=(checked_value &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    if (this != &other) {
      value_ = take_from(other);
      moved_from_ = false;
    }
    return *this;
  }

  ~checked_value() noexcept = default;

  /**
   * @brief Returns whether this value has already been moved from.
   *
   * Always callable, regardless of typestate, so callers can query state
   * without themselves triggering a diagnostic or an assert.
   */
  [[nodiscard]] constexpr bool is_moved_from() const noexcept { return moved_from_; }

  /** @brief Mutable borrow of the held value. Traps if moved from. */
  [[nodiscard]] constexpr T &get() & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(!moved_from_, "checked_value: access after move");
    return value_;
  }

  /** @brief Read-only borrow of the held value. Traps if moved from. */
  [[nodiscard]] constexpr const T &get() const & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(!moved_from_, "checked_value: access after move");
    return value_;
  }

  // Borrowing from a temporary would dangle at the end of the full
  // expression; use take() to move the value out of a temporary instead.
  T &get() && = delete;
  const T &get() const && = delete;

  /**
   * @brief Mutable borrow without the always-on moved-from check.
   *
   * Explicitly-unsafe tier: only a `RELOCO_DEBUG_ASSERT`, so it is compiled
   * out under `NDEBUG` (unless `RELOCO_DEBUG` is also defined). Use only
   * once the caller has already established `unconsumed` state (for example
   * via `-Wconsumed`, or by construction just above) and repeated `get()`
   * overhead is unacceptable on a hot path.
   *
   * Marked `RELOCO_UNSAFE_BUFFER_USAGE`: under Clang's
   * `-Wunsafe-buffer-usage`, every call site must be wrapped in
   * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE`,
   * making the opt-out to the unsafe tier explicit and greppable at each use.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &unsafe_get() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!moved_from_, "checked_value: access after move");
    return value_;
  }

  /** @brief Read-only counterpart of @ref unsafe_get. */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const T &unsafe_get() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!moved_from_, "checked_value: access after move");
    return value_;
  }

  T &unsafe_get() && = delete;
  const T &unsafe_get() const && = delete;

  /**
   * @brief Moves the held value out and poisons this wrapper.
   *
   * Rust-equivalent of moving out of an owned binding: after `take()`, this
   * object is in the `consumed` state and any further access traps.
   */
  [[nodiscard]] constexpr T take() && noexcept RELOCO_CALLABLE_WHEN("unconsumed") RELOCO_SET_TYPESTATE(consumed) {
    RELOCO_ASSERT(!moved_from_, "checked_value: take() after move");
    moved_from_ = true;
    return std::move(value_);
  }

  constexpr T *operator->() noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") { return &get(); }

  constexpr const T *operator->() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    return &get();
  }

  constexpr T &operator*() & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") { return get(); }

  constexpr const T &operator*() const & noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    return get();
  }

  T &operator*() && = delete;
  const T &operator*() const && = delete;

  /**
   * @brief Explicit, Rust-`Clone`-style copy; only available when @c T is
   * copy-constructible. Traps if this value has already been moved from.
   */
  template <typename U = T, std::enable_if_t<std::is_copy_constructible_v<U>, int> = 0>
  [[nodiscard]] constexpr checked_value clone() const & noexcept(std::is_nothrow_copy_constructible_v<T>)
      RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(!moved_from_, "checked_value: clone() after move");
    return checked_value(value_);
  }

  /**
   * @brief Recovers from Clang's "unknown" typestate at a reference/pointer
   * boundary (for example, a `checked_value &` callback parameter) that the
   * static analysis cannot otherwise resolve to `unconsumed`.
   *
   * Asserts the real runtime invariant before returning to `unconsumed`, so
   * it is safe to use even where the static state is genuinely unknown.
   * Prefer plain access when the state is statically known; reach for this
   * only at such boundaries, never to silence a real reuse-after-move
   * warning.
   */
  checked_value &as_known() & noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(!moved_from_, "checked_value: as_known() after move");
    return *this;
  }

private:
  static constexpr T take_from(checked_value &other) noexcept {
    RELOCO_ASSERT(!other.moved_from_, "checked_value: move from an already moved-from value");
    other.moved_from_ = true;
    return std::move(other.value_);
  }

  T value_;
  bool moved_from_{false};
};

template <typename T> checked_value(T) -> checked_value<T>;

/**
 * @brief `checked_value<T>` adds only a `bool` flag alongside `T`, with no
 * pointer back into itself; relocating it is safe whenever relocating `T`
 * on its own would be.
 */
template <typename T> struct is_trivially_relocatable<checked_value<T>> : is_trivially_relocatable<T> {};

/**
 * @brief Partial specialization for raw pointers.
 *
 * In addition to the move-once semantics of the primary template,
 * `checked_value<T *>` guards its dereferencing operators
 * (`operator*`/`operator->`) with a null check: a moved-from *or* null
 * pointer both trap via `RELOCO_ASSERT` before any dereference happens.
 * `get()` still returns the raw, possibly-null pointer for callers that want
 * to inspect or null-check it explicitly without dereferencing.
 *
 * This does not take ownership of the pointee; only the pointer *slot* is
 * move-once. Taking or moving out of a `checked_value<T *>` leaves the
 * source holding `nullptr`, matching `std::unique_ptr`'s moved-from state,
 * so a bypassed (disabled) assert still fails safely with a null dereference
 * rather than reading stale/dangling memory.
 */
template <typename T> class RELOCO_CONSUMABLE(unconsumed) checked_value<T *> {
public:
  /** @brief Wraps @p value (possibly null) in a fresh, unconsumed wrapper. */
  constexpr explicit checked_value(T *value) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : value_(value) {}

  checked_value(const checked_value &) = delete;
  checked_value &operator=(const checked_value &) = delete;

  constexpr checked_value(checked_value &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
      : value_(take_from(other)) {}

  constexpr checked_value &operator=(checked_value &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    if (this != &other) {
      value_ = take_from(other);
      moved_from_ = false;
    }
    return *this;
  }

  ~checked_value() noexcept = default;

  /** @brief Returns whether this value has already been moved from. */
  [[nodiscard]] constexpr bool is_moved_from() const noexcept { return moved_from_; }

  /**
   * @brief Returns the raw, possibly-null pointer. Traps if moved from.
   *
   * Does not dereference the pointer, so a null value is returned as-is;
   * use @ref is_null or `operator bool` to check before dereferencing
   * manually, or prefer `operator*`/`operator->`, which check for you.
   */
  [[nodiscard]] constexpr T *get() const noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(!moved_from_, "checked_value: access after move");
    return value_;
  }

  /** @brief Whether the held pointer is null. Traps if moved from. */
  [[nodiscard]] constexpr bool is_null() const noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(!moved_from_, "checked_value: access after move");
    return value_ == nullptr;
  }

  constexpr explicit operator bool() const noexcept RELOCO_CALLABLE_WHEN("unconsumed") { return !is_null(); }

  /**
   * @brief Moves the held pointer out, nulls the source, and poisons this
   * wrapper's typestate.
   */
  [[nodiscard]] constexpr T *take() && noexcept RELOCO_CALLABLE_WHEN("unconsumed") RELOCO_SET_TYPESTATE(consumed) {
    RELOCO_ASSERT(!moved_from_, "checked_value: take() after move");
    moved_from_ = true;
    T *taken = value_;
    value_ = nullptr;
    return taken;
  }

  /** @brief Dereferences the pointee. Traps if moved from or null. */
  constexpr T &operator*() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(!moved_from_, "checked_value: access after move");
    RELOCO_ASSERT(value_ != nullptr, "checked_value: dereferencing a null pointer");
    return *value_;
  }

  /** @brief Member access on the pointee. Traps if moved from or null. */
  constexpr T *operator->() const noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(!moved_from_, "checked_value: access after move");
    RELOCO_ASSERT(value_ != nullptr, "checked_value: dereferencing a null pointer");
    return value_;
  }

  /**
   * @brief Returns the raw, possibly-null pointer without the always-on
   * moved-from check.
   *
   * Explicitly-unsafe tier: only a `RELOCO_DEBUG_ASSERT`, so it is compiled
   * out under `NDEBUG` (unless `RELOCO_DEBUG` is also defined). Still does
   * not dereference; combine with @ref unsafe_deref, or check `is_null()`
   * manually, once moved-from state has already been ruled out.
   *
   * Marked `RELOCO_UNSAFE_BUFFER_USAGE`: under Clang's
   * `-Wunsafe-buffer-usage`, every call site must be wrapped in
   * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE`,
   * making the opt-out to the unsafe tier explicit and greppable at each use.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T *unsafe_get() const noexcept {
    RELOCO_DEBUG_ASSERT(!moved_from_, "checked_value: access after move");
    return value_;
  }

  /**
   * @brief Dereferences the pointee without the always-on moved-from/null
   * checks.
   *
   * Explicitly-unsafe tier: only `RELOCO_DEBUG_ASSERT`s, so both checks
   * compile out under `NDEBUG` (unless `RELOCO_DEBUG` is also defined).
   * Use only once the caller has already established `unconsumed` state and
   * non-null (for example via `is_null()`/`operator bool()`) and the
   * checked `operator*`/`operator->` overhead is unacceptable on a hot path.
   *
   * Marked `RELOCO_UNSAFE_BUFFER_USAGE`, same call-site requirement as
   * @ref unsafe_get.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &unsafe_deref() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!moved_from_, "checked_value: access after move");
    RELOCO_DEBUG_ASSERT(value_ != nullptr, "checked_value: dereferencing a null pointer");
    return *value_;
  }

  /** @brief Explicit, Rust-`Clone`-style copy of the pointer value itself. */
  [[nodiscard]] constexpr checked_value clone() const noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(!moved_from_, "checked_value: clone() after move");
    return checked_value(value_);
  }

  /**
   * @brief Recovers from Clang's "unknown" typestate at a reference/pointer
   * boundary; see the primary template's @ref checked_value::as_known for
   * the full explanation.
   */
  checked_value &as_known() & noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(!moved_from_, "checked_value: as_known() after move");
    return *this;
  }

private:
  static constexpr T *take_from(checked_value &other) noexcept {
    RELOCO_ASSERT(!other.moved_from_, "checked_value: move from an already moved-from value");
    other.moved_from_ = true;
    T *taken = other.value_;
    other.value_ = nullptr;
    return taken;
  }

  T *value_;
  bool moved_from_{false};
};

/**
 * @brief `checked_value<T *>` holds only a `T *` and a `bool`, with no
 * pointer back into itself. Always relocatable, regardless of `T`.
 */
template <typename T> struct is_trivially_relocatable<checked_value<T *>> : std::true_type {};

} // namespace reloco
