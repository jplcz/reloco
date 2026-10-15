// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/**
 * @file tls_slot.hpp
 * @brief `tls_slot<T>`: zero-valid, lazily initialised storage for one complex object inside a thread-local block.
 *
 * A thread-local block (see `tls_provider.hpp`) must be usable from zero-initialised memory. A `T` that owns
 * memory (a `vector`, a `string`, a connection) is not. `tls_slot<T>` closes that gap: it is a byte buffer plus a
 * state byte, **all-zero means empty**, and the object is built on the thread's first access by a fallible
 * initialiser that may allocate and may sleep. The thread's block destroys the slot (`destroy()`) when the thread
 * exits. docs/tls-slot.md has the guide.
 *
 * Access is scoped: there is no accessor returning a reference, only closures that receive one, so the reference
 * cannot outlive the access.
 *
 * | Call | Initialises | May sleep | Result |
 * |---|---|---|---|
 * | `try_with(init, f)` / `try_with_mut(init, f)` | yes, on first use | yes (`RELOCO_MIGHT_SLEEP`) | `result<R>`: the init error, or `f`'s result |
 * | `with_ready(f)` / `with_ready_mut(f)` | never | no (usable in atomic context) | `optional<R>`: `none` while not ready |
 *
 * `R` is the closure's return type; for `void` the results are `result<void>` and `bool`. The closure's result must not
 * borrow from the object (`R` is checked not to be a reference or pointer).
 *
 * @code
 * struct conn { vector<int> frames; };
 * static result<conn> make_conn() noexcept;                 // may allocate; the error is surfaced by try_with
 *
 * tls_slot<conn> slot;                                      // normally a member of the module's thread-local block
 * auto r = slot.try_with_mut(&make_conn, [](conn &c) { return c.frames.size(); });  // result<size_t>
 * auto n = slot.with_ready([](const conn &c) { return c.frames.size(); });          // optional<size_t>
 * @endcode
 */

#include "atomic_context.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "lifetime.hpp"
#include "optional.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace reloco {

namespace detail {

template <typename R> inline constexpr bool tls_slot_result_is_owned_v = !std::is_reference_v<R> && !std::is_pointer_v<R>;

} // namespace detail

/**
 * @brief Zero-valid storage for one `T` in a thread-local block; the object is built lazily and destroyed with the thread.
 *
 * The all-zero byte pattern is the empty state, so a block of `tls_slot`s can be created by zero-filling (or copying a
 * zero image). Not copyable or movable: its address is the address of the thread's storage. Not thread-safe by design:
 * a slot belongs to one thread.
 *
 * @tparam T Constructed from a `result<T>` produced by the initialiser; must be nothrow move constructible and nothrow
 *           destructible.
 */
template <typename T> class RELOCO_OWNER tls_slot {
  static_assert(std::is_nothrow_move_constructible_v<T>, "tls_slot<T>: T must be nothrow move constructible");
  static_assert(std::is_nothrow_destructible_v<T>, "tls_slot<T>: T must be nothrow destructible");

public:
  /** @brief Initialiser signature: builds the value or reports why it could not (typically `error::allocation_failed`). */
  using init_fn = result<T> (*)() noexcept;

  tls_slot() noexcept = default;
  tls_slot(const tls_slot &) = delete;
  tls_slot &operator=(const tls_slot &) = delete;
  tls_slot(tls_slot &&) = delete;
  tls_slot &operator=(tls_slot &&) = delete;

  // Not a destructor: the owning block calls destroy() at thread exit; a slot that is part of a zero image must stay trivial.
  ~tls_slot() = default;

  /** @brief True once the object has been built and not yet destroyed. */
  [[nodiscard]] bool is_ready() const noexcept { return state_ == state_ready; }

  /**
   * @brief Runs `f(const T &)`, building the object with `init` first when the slot is empty.
   * @return `f`'s result, or the initialiser's error; `error::busy` when called from inside `init` (re-entrancy).
   */
  template <typename F> [[nodiscard]] RELOCO_MIGHT_SLEEP auto try_with(init_fn init, F &&f) & noexcept {
    return try_with_impl<const T>(init, std::forward<F>(f));
  }

  /** @brief As `try_with`, but `f` receives `T &`. */
  template <typename F> [[nodiscard]] RELOCO_MIGHT_SLEEP auto try_with_mut(init_fn init, F &&f) & noexcept {
    return try_with_impl<T>(init, std::forward<F>(f));
  }

  /** @brief Runs `f(const T &)` only when the object is ready; never initialises, never sleeps. */
  template <typename F> [[nodiscard]] auto with_ready(F &&f) & noexcept {
    return with_ready_impl<const T>(std::forward<F>(f));
  }

  /** @brief As `with_ready`, but `f` receives `T &`. */
  template <typename F> [[nodiscard]] auto with_ready_mut(F &&f) & noexcept {
    return with_ready_impl<T>(std::forward<F>(f));
  }

  /**
   * @brief Destroys the object if it is ready; the slot is empty again and can be re-initialised.
   *
   * Called by the owning block at thread exit. It must not run while a closure of this slot is executing.
   */
  void destroy() & noexcept {
    if (state_ == state_ready) {
      state_ = state_empty; // first: an access from T's destructor sees an empty slot, not a half-destroyed object
      object().~T();
    }
  }

private:
  static constexpr std::uint8_t state_empty = 0;
  static constexpr std::uint8_t state_initializing = 1;
  static constexpr std::uint8_t state_ready = 2;

  // NOLINTNEXTLINE(reloco-banned-construct): storage_ holds a T whenever state_ is ready.
  [[nodiscard]] T &object() & noexcept RELOCO_LIFETIMEBOUND { return *std::launder(reinterpret_cast<T *>(storage_)); }

  template <typename Access, typename F> [[nodiscard]] auto try_with_impl(init_fn init, F &&f) & noexcept {
    using R = std::invoke_result_t<F, Access &>;
    static_assert(detail::tls_slot_result_is_owned_v<R>, "tls_slot: the closure must not return a reference or pointer");
    using out_t = result<R>;
    if (state_ == state_initializing) {
      return out_t(unexpected(error::busy));
    }
    if (state_ == state_empty) {
      state_ = state_initializing;
      auto built = init();
      if (!built.has_value()) {
        state_ = state_empty;
        return out_t(unexpected(built.error()));
      }
      ::new (static_cast<void *>(storage_)) T(std::move(built.value()));
      state_ = state_ready;
    }
    if constexpr (std::is_void_v<R>) {
      std::forward<F>(f)(static_cast<Access &>(object()));
      return out_t();
    } else {
      return out_t(std::forward<F>(f)(static_cast<Access &>(object())));
    }
  }

  template <typename Access, typename F> [[nodiscard]] auto with_ready_impl(F &&f) & noexcept {
    using R = std::invoke_result_t<F, Access &>;
    static_assert(detail::tls_slot_result_is_owned_v<R>, "tls_slot: the closure must not return a reference or pointer");
    if constexpr (std::is_void_v<R>) {
      if (state_ != state_ready) {
        return false;
      }
      std::forward<F>(f)(static_cast<Access &>(object()));
      return true;
    } else {
      if (state_ != state_ready) {
        return optional<R>(nullopt);
      }
      return optional<R>(std::forward<F>(f)(static_cast<Access &>(object())));
    }
  }

  // NOLINTNEXTLINE(reloco-no-c-array): the slot must be zero-valid and constructible in raw zeroed memory.
  alignas(T) unsigned char storage_[sizeof(T)] = {};
  std::uint8_t state_ = state_empty;
};

} // namespace reloco
