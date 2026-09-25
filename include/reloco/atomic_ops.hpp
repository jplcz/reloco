// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file atomic_ops.hpp
 * @brief Free functions filling the handful of gaps between C++17
 * `std::atomic<T>` and Rust's `std::sync::atomic::Atomic*` API surface.
 *
 * Rust's atomic types (`AtomicUsize`, `AtomicIsize`, `AtomicPtr<T>`, ...)
 * are otherwise a near-exact match for `std::atomic<T>` --
 * `load`/`store`/`swap`/`compare_exchange(_weak)`/`fetch_add`/`fetch_sub`/
 * `fetch_and`/`fetch_or`/`fetch_xor` all already exist on `std::atomic<T>`
 * with equivalent semantics, so none of those are re-wrapped here (per the
 * project's own rule: don't port a Rust API that doesn't extend anything
 * over what the standard library already provides). Only the operations
 * genuinely missing from a C++17 `std::atomic<T>` are ported, in the
 * `reloco::atomic` namespace, as free functions taking a `std::atomic<T> &`
 * -- there is no `reloco::atomic<T>` wrapper type:
 *
 * - `fetch_max(a, val, order)`/`fetch_min(a, val, order)` atomically
 *   replace `*a` with `std::max(*a, val)`/`std::min(*a, val)`, returning
 *   the *previous* value, matching Rust's `AtomicT::fetch_max`/
 *   `fetch_min`. `std::atomic<T>::fetch_max`/`fetch_min` are only standard
 *   as of C++26 -- on a C++26 (or later) standard library that already
 *   provides them natively on `T`, these simply forward to the member
 *   function; otherwise they fall back to a portable
 *   `load`+`compare_exchange_weak` retry loop. Defined for integral and
 *   pointer `T`, exactly like `std::atomic<T>::fetch_add`/`fetch_sub`.
 * - `fetch_update(a, success_order, failure_order, f)` repeatedly reads
 *   `*a`, calls `f(current)` (a callable returning `optional<T>`, see
 *   `optional.hpp`), and attempts to `compare_exchange_weak` the result
 *   in; it keeps retrying with the freshly-observed value on a spurious
 *   CAS failure, stops immediately if `f` returns an empty `optional<T>`
 *   (`f` "gives up"), and returns `expected<T, T>` -- the previous value
 *   on success, or the last-observed value `f` gave up on, exactly
 *   matching Rust's `AtomicT::fetch_update(set_order, fetch_order, f) ->
 *   Result<T, T>` (`f: FnMut(T) -> Option<T>`). `std::atomic<T>` has no
 *   equivalent CAS-loop convenience at any C++ version -- this one is a
 *   genuine capability gap, not merely a naming difference.
 *
 * Everything else in Rust's atomic API (the `Ordering` enum, `swap`,
 * `compare_exchange`, the boolean/pointer flavors, ...) is already
 * `std::memory_order`/`std::atomic<T>` verbatim and is deliberately left
 * unported.
 */

#include "expected.hpp"
#include "optional.hpp"

#include <atomic>
#include <type_traits>
#include <utility>

namespace reloco {
namespace atomic {

namespace detail {

// The C++ standard only allows a `compare_exchange_weak`/`_strong` failure
// memory order that is "no stronger" than the success order (no
// `release`/`acq_rel` on the failure path). Downgrade `order` to the
// strongest legal failure order instead of asking every caller to work
// this out themselves.
constexpr std::memory_order to_cas_failure_order(std::memory_order order) noexcept {
  switch (order) {
  case std::memory_order_release:
    return std::memory_order_relaxed;
  case std::memory_order_acq_rel:
    return std::memory_order_acquire;
  default:
    return order;
  }
}

template <typename T, typename = void> struct has_native_fetch_max : std::false_type {};
template <typename T>
struct has_native_fetch_max<T, std::void_t<decltype(std::declval<std::atomic<T> &>().fetch_max(std::declval<T>()))>>
    : std::true_type {};

template <typename T, typename = void> struct has_native_fetch_min : std::false_type {};
template <typename T>
struct has_native_fetch_min<T, std::void_t<decltype(std::declval<std::atomic<T> &>().fetch_min(std::declval<T>()))>>
    : std::true_type {};

} // namespace detail

/**
 * @brief Atomically replaces `a`'s value with `std::max(a, val)`, returning
 * the value `a` held immediately before the operation. Matches Rust's
 * `AtomicT::fetch_max`.
 *
 * @tparam T An integral or pointer type (same constraint as
 * `std::atomic<T>::fetch_add`).
 */
template <typename T>
T fetch_max(std::atomic<T> &a, T val, std::memory_order order = std::memory_order_seq_cst) noexcept {
  static_assert(std::is_integral_v<T> || std::is_pointer_v<T>,
                "reloco::atomic::fetch_max requires an integral or pointer T");
  if constexpr (detail::has_native_fetch_max<T>::value) {
    return a.fetch_max(val, order);
  } else {
    const std::memory_order failure_order = detail::to_cas_failure_order(order);
    T current = a.load(failure_order);
    while (current < val) {
      if (a.compare_exchange_weak(current, val, order, failure_order))
        break;
    }
    return current;
  }
}

/**
 * @brief Atomically replaces `a`'s value with `std::min(a, val)`, returning
 * the value `a` held immediately before the operation. Matches Rust's
 * `AtomicT::fetch_min`.
 *
 * @tparam T An integral or pointer type (same constraint as
 * `std::atomic<T>::fetch_add`).
 */
template <typename T>
T fetch_min(std::atomic<T> &a, T val, std::memory_order order = std::memory_order_seq_cst) noexcept {
  static_assert(std::is_integral_v<T> || std::is_pointer_v<T>,
                "reloco::atomic::fetch_min requires an integral or pointer T");
  if constexpr (detail::has_native_fetch_min<T>::value) {
    return a.fetch_min(val, order);
  } else {
    const std::memory_order failure_order = detail::to_cas_failure_order(order);
    T current = a.load(failure_order);
    while (current > val) {
      if (a.compare_exchange_weak(current, val, order, failure_order))
        break;
    }
    return current;
  }
}

/**
 * @brief Fetches `a`'s current value, repeatedly applies `f` to it in a
 * `compare_exchange_weak` retry loop, and stores the result back. Matches
 * Rust's `AtomicT::fetch_update(set_order, fetch_order, f) -> Result<T,
 * T>`.
 *
 * `f` is called with the most recently observed value and must return
 * `optional<T>`: an empty result aborts the loop immediately (no store
 * happens) and `f`'s input is returned as the `expected<T, T>`'s error;
 * a present result is the candidate new value to attempt to install. `f`
 * may be called more than once (every time the CAS spuriously/genuinely
 * fails against another thread's concurrent update) and must be free of
 * side effects it isn't prepared to have re-run.
 *
 * @return `expected<T, T>` holding the value immediately before the
 * successful update, or (as the "error") the last value `f` was given
 * when it gave up.
 */
template <typename T, typename F>
expected<T, T> fetch_update(std::atomic<T> &a, std::memory_order success_order, std::memory_order failure_order,
                             F &&f) noexcept(noexcept(f(std::declval<T>()))) {
  T current = a.load(failure_order);
  for (;;) {
    optional<T> next = f(current);
    if (!next.has_value())
      return unexpected(current);
    if (a.compare_exchange_weak(current, *next, success_order, failure_order))
      return current;
  }
}

/**
 * @brief Same as the four-argument `fetch_update`, using `order` for both
 * the success and failure memory orders.
 */
template <typename T, typename F>
expected<T, T> fetch_update(std::atomic<T> &a, std::memory_order order, F &&f) noexcept(
    noexcept(fetch_update(a, order, detail::to_cas_failure_order(order), std::forward<F>(f)))) {
  return fetch_update(a, order, detail::to_cas_failure_order(order), std::forward<F>(f));
}

/**
 * @brief Same as the four-argument `fetch_update`, using
 * `std::memory_order_seq_cst` for both the success and failure memory
 * orders.
 */
template <typename T, typename F>
expected<T, T> fetch_update(std::atomic<T> &a,
                             F &&f) noexcept(noexcept(fetch_update(a, std::memory_order_seq_cst,
                                                                    std::forward<F>(f)))) {
  return fetch_update(a, std::memory_order_seq_cst, std::forward<F>(f));
}

} // namespace atomic
} // namespace reloco
