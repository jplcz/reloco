// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file lazy_lock.hpp
 * @brief `lazy_lock<T, F>`, matching Rust's stable `std::sync::LazyLock<T,
 * F>` (née `once_cell::sync::Lazy<T, F>`): a value that is lazily
 * initialized, at most once, from a closure captured at construction
 * time, the first time it is dereferenced.
 *
 * Builds directly on `once_lock.hpp`'s `once_lock<T>` -- a `lazy_lock<T,
 * F>` is exactly a `once_lock<T>` paired with the closure `F` that knows
 * how to fill it, so the whole implementation is a thin `operator*`/
 * `operator->`/`get()` wrapper around `once_lock<T>::get_or_init`, with
 * every one of `once_lock<T>`'s own thread-safety/lock-freedom properties
 * (see `once_lock.hpp`) carried over unchanged.
 *
 * Unlike `once_lock<T>::get_or_init(F)`, which takes the initializer at
 * every call site (so different calls could, in principle, race with
 * different closures -- only the first one actually runs), `lazy_lock<T,
 * F>` captures `F` exactly once, at construction, matching Rust's
 * `LazyLock::new(f)` -- every `operator*`/`operator->`/`get()` call
 * afterward takes no arguments, exactly like dereferencing an ordinary
 * `T` once initialization has happened. This makes `lazy_lock<T, F>` the
 * right shape for a lazily-initialized `static`/global or struct field
 * (Rust's own primary use case for `LazyLock`), where `once_lock<T>`
 * alone would otherwise force every access site to repeat (or share, via
 * some other indirection) the same initializer closure.
 *
 * `F` must be invocable as `T()`. `lazy_lock` deliberately has no
 * class-template default for `F` (unlike, say, a hypothetical
 * `lazy_lock<T, F = function<T()>>`): a deduction guide below lets a
 * local `lazy_lock` be declared directly from a closure --
 *
 * @code
 * reloco::lazy_lock config([]() -> reloco::string { return load_config(); });
 * use(*config); // first dereference anywhere runs the closure exactly once
 * @endcode
 *
 * -- while a `static`/global or struct field, which must name a concrete
 * type, uses `function.hpp`'s `function<T()>` as `F` explicitly (its
 * small-object optimization means a captureless or small-capture closure
 * still allocates nothing):
 *
 * @code
 * reloco::lazy_lock<reloco::string, reloco::function<reloco::string()>>
 *     config([]() -> reloco::string { return load_config(); });
 * @endcode
 *
 * `F` itself is stored for the lifetime of the `lazy_lock` (unlike Rust's
 * `LazyLock`, which drops its closure in place once it has run, reusing
 * the same storage the value now occupies) -- reloco keeps `F` and `T` in
 * separate members instead, trading a few extra bytes for a
 * substantially simpler implementation built entirely on the existing,
 * already-reviewed `once_lock<T>` rather than a new hand-rolled tagged
 * union/state machine.
 *
 * `is_send<lazy_lock<T, F>>` requires both `is_send<T>` and `is_send<F>`
 * (moving the whole, possibly-not-yet-initialized `lazy_lock` to another
 * thread needs both the closure and the eventual value to tolerate that).
 * `is_sync<lazy_lock<T, F>>` additionally requires `is_sync<T>`, matching
 * Rust's `unsafe impl<T, F: Send> Sync for LazyLock<T, F> where
 * OnceLock<T>: Sync` -- concurrent readers reach a `const T &` once ready
 * without ever touching `F` again, so `F` need not itself be `Sync`.
 */

#include "once_lock.hpp"
#include "send_sync.hpp"

#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief A value lazily initialized, at most once, from a closure
 * captured at construction time, matching Rust's stable
 * `std::sync::LazyLock<T, F>`. See the file-level documentation above.
 */
template <typename T, typename F> class lazy_lock {
  static_assert(std::is_invocable_r_v<T, F &>, "lazy_lock<T, F>: F must be invocable as T()");

public:
  /**
   * @brief Captures `f`, to be run at most once, the first time this
   * `lazy_lock` is dereferenced.
   */
  constexpr explicit lazy_lock(F f) noexcept(std::is_nothrow_move_constructible_v<F>) : f_(std::move(f)) {}

  lazy_lock(const lazy_lock &) = delete;
  lazy_lock &operator=(const lazy_lock &) = delete;
  lazy_lock(lazy_lock &&) = delete;
  lazy_lock &operator=(lazy_lock &&) = delete;

  /**
   * @brief Returns a reference to the value, running the captured closure
   * first if this is the first call (from any thread; concurrent callers
   * block until it completes), matching Rust's `LazyLock`'s `Deref`.
   */
  [[nodiscard]] const T &operator*() const & noexcept RELOCO_LIFETIMEBOUND { return *get(); }

  /** @brief Same as `operator*`, for `->` member access. */
  [[nodiscard]] const T *operator->() const & noexcept RELOCO_LIFETIMEBOUND { return get(); }

  /**
   * @brief Returns a pointer to the value, running the captured closure
   * first if this is the first call. Never returns `nullptr`.
   */
  [[nodiscard]] const T *get() const & noexcept RELOCO_LIFETIMEBOUND {
    return &cell_.get_or_init([this]() noexcept(std::is_nothrow_invocable_v<F &>) -> T { return f_(); });
  }

private:
  mutable F f_;
  mutable once_lock<T> cell_;
};

/** @brief Deduction guide: `lazy_lock(f)` deduces `T` from `f`'s return
 * type and `F` from `f`'s own type, so a local `lazy_lock` can be
 * declared directly from a closure without spelling out either. */
template <typename F> lazy_lock(F) -> lazy_lock<std::invoke_result_t<F &>, F>;

/**
 * @brief `is_send<lazy_lock<T, F>>` requires both `is_send<T>` and
 * `is_send<F>` -- see the file-level documentation above.
 */
template <typename T, typename F>
struct is_send<lazy_lock<T, F>> : std::bool_constant<is_send_v<T> && is_send_v<F>> {};

/**
 * @brief `is_sync<lazy_lock<T, F>>` requires `is_send<T>`, `is_sync<T>`,
 * and `is_send<F>` -- see the file-level documentation above.
 */
template <typename T, typename F>
struct is_sync<lazy_lock<T, F>> : std::bool_constant<is_send_v<T> && is_sync_v<T> && is_send_v<F>> {};

} // namespace reloco
