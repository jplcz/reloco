// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file send_sync.hpp
 * @brief `is_send<T>`/`is_sync<T>`, customization-point traits matching
 * Rust's `Send`/`Sync` auto traits:
 *
 * - `is_send<T>`: is it sound to *transfer ownership* of a `T` to another
 *   thread (move it there and keep using it only from that thread from
 *   then on)?
 * - `is_sync<T>`: is it sound to *share* a `T` across threads through a
 *   `const T &` (i.e. is `T` sound to put behind something like
 *   `guarded_mutex<T>`/`shared_ptr<T>` and touch concurrently)?
 *
 * Rust derives both automatically per-field at compile time; C++ has no
 * equivalent reflection, so both traits follow the same manually-curated
 * customization-point shape `relocatable.hpp`'s `is_trivially_relocatable`
 * already established: default to the safe-looking case (here, `true` --
 * an ordinary value type with no hidden non-atomic shared state is fine to
 * move to another thread or place behind external synchronization), and
 * have each type that is deliberately *not* thread-transferable/-shareable
 * specialize itself to `false` in its own header, right next to its
 * `is_trivially_relocatable` specialization (if it has one).
 *
 * Composition is **not** automatic: `is_send<vector<rc<T>>>` is `true`
 * even though it should not be, because nothing walks `vector<T>`'s
 * element type. Only specialize/consult these traits for the specific
 * types reloco itself marks (`rc<T>`/`weak_rc<T>`, `cell<T>`/`ref_cell<T>`,
 * `shared_ptr<T>`/`weak_ptr<T>`) or types you have manually verified
 * yourself -- exactly the same caveat `is_trivially_relocatable` carries.
 *
 * reloco specializes both traits as follows:
 *
 * - `rc<T>`/`weak_rc<T>` (`rc.hpp`): `is_send`/`is_sync` are unconditionally
 *   `false`, regardless of `T` -- matching Rust's `Rc<T>`/`Weak<T>`. Their
 *   refcounts are plain, non-atomic increments/decrements; even sending a
 *   single handle to another thread is unsound while any clone of it
 *   might still be dropped concurrently from the original thread.
 * - `cell<T>`/`ref_cell<T>` (`cell.hpp`): `is_send<cell<T>>`/
 *   `is_send<ref_cell<T>>` forward to `is_send<T>` (moving the whole cell
 *   to another thread is fine exactly when moving a bare `T` would be),
 *   but `is_sync` is unconditionally `false` for both -- their interior
 *   mutability (`cell<T>::set`, `ref_cell<T>`'s runtime borrow flag) has no
 *   synchronization at all, matching Rust's `Cell<T>`/`RefCell<T>` (never
 *   `Sync`, regardless of `T`).
 * - `shared_ptr<T>`/`weak_ptr<T>` (`shared_ptr.hpp`): both traits require
 *   `T` to be both `is_send`*and* `is_sync`, matching Rust's `Arc<T>`/
 *   `Weak<T>` (`Send`/`Sync` only when `T: Send + Sync`) -- their refcounts
 *   are atomic, so the smart pointer itself is always safe to transfer or
 *   share, but the shared `T` it protects is reachable concurrently
 *   through any live clone, so `T` itself must tolerate that.
 *
 * Neither trait is checked automatically by any reloco container; use
 * `static_assert(is_send_v<T>, ...)`/`static_assert(is_sync_v<T>, ...)` at
 * whatever API boundary actually crosses a thread (reloco does this in
 * `guarded_mutex<T>`, matching Rust's `unsafe impl<T: Send> Sync for
 * Mutex<T>`: the mutex synchronizes every access, so only `T: Send` is
 * required -- not `T: Sync` -- for the guarded value itself).
 */

#include "detail/compat.hpp"

#include <type_traits>

namespace reloco {

/**
 * @brief Customization point: is it sound to move a `T` to another thread
 * and continue using it only from there? Defaults to `true`. Specialize
 * to `std::false_type` (or forward to another trait) for a type with
 * non-atomic shared state that makes cross-thread ownership transfer
 * unsound (see the file-level documentation above).
 */
template <typename T> struct is_send : std::true_type {};

/**
 * @brief Convenience variable template for `is_send<T>::value`.
 */
template <typename T> inline constexpr bool is_send_v = is_send<T>::value;

/**
 * @brief Customization point: is it sound to share a `T` across threads
 * through a `const T &` (i.e. concurrent read access, or access
 * externally synchronized by the caller)? Defaults to `true`. Specialize
 * to `std::false_type` for a type whose `const`-qualified operations still
 * mutate unsynchronized shared state (see the file-level documentation
 * above).
 */
template <typename T> struct is_sync : std::true_type {};

/**
 * @brief Convenience variable template for `is_sync<T>::value`.
 */
template <typename T> inline constexpr bool is_sync_v = is_sync<T>::value;

#if RELOCO_CXX20

template <typename T>
concept sendable = is_send_v<T>;

template <typename T>
concept syncable = is_sync_v<T>;

#endif // RELOCO_CXX20

} // namespace reloco
