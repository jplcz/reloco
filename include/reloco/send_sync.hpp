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
 *
 * The default-`true` fallback above is unavoidable for arbitrary
 * user-supplied `T` (reloco has no way to walk `T`'s fields the way Rust's
 * compiler does), but it would be silently unsound if it ever applied to
 * one of reloco's own types that owns non-atomic shared state or
 * unsynchronized interior mutability -- exactly the bug class this file's
 * own `rc<T>`/`function<Sig>`/`bytes` specializations exist to close.
 * `detail::requires_explicit_send_sync` (below) turns "forgot to
 * specialize" into a hard compile error for exactly those types, instead
 * of a silent, wrong default: any reloco type built on top of such
 * non-atomic/unsynchronized state privately inherits that marker, which
 * makes the primary template's `static_assert` fire unless that specific
 * type has its own `is_send<T>`/`is_sync<T>` specialization (a
 * specialization is always preferred over the primary template, so a
 * type that *does* specialize never instantiates the primary template's
 * body at all, and never trips the assertion).
 *
 * **Experimental**: when built with `-freflection` on a P2996-capable
 * compiler (currently GCC trunk/16+; see `RELOCO_HAS_REFLECTION` in
 * `detail/compat.hpp`), the "arbitrary `T`" default above stops being a
 * blind `true` and instead structurally composes the trait from `T`'s
 * base classes and non-static data members (including private ones),
 * recursing through the same trait -- much closer to what Rust's compiler
 * actually does. This only ever changes behavior for a `T` that has *no*
 * explicit `is_send<T>`/`is_sync<T>` specialization of its own: a real
 * specialization is always preferred by ordinary C++ overload resolution
 * for class template specializations, so it is chosen instead and the
 * structural fallback is never even instantiated for that `T`. This closes
 * (for reflection-enabled builds only) exactly the "composition is not
 * automatic" caveat two paragraphs up: e.g. a plain user struct embedding
 * an `rc<T>` field now correctly comes out `is_send == false`, composed
 * transitively through `rc<T>`'s own specialization, with no manual
 * annotation required. `detail::requires_explicit_send_sync`-marked types
 * are unaffected either way -- their hazard (a non-atomic refcount, a
 * const-invoked-non-const callable, ...) is a property of *behavior*, not
 * of field types, so it can never be safely derived structurally; they
 * must keep specializing manually regardless of reflection availability.
 */

#include "detail/compat.hpp"

#include <type_traits>

#if RELOCO_HAS_REFLECTION
#include <meta>
#endif

namespace reloco {

namespace detail {

/**
 * @brief Private marker base class: any reloco type built on non-atomic
 * shared state or unsynchronized interior mutability (a raw `rc<T>`
 * member, a type-erased callable invoked non-`const`, etc.) should
 * privately inherit from this tag instead of relying on
 * `is_send<T>`/`is_sync<T>`'s default `true`. Doing so turns a missing
 * `is_send<T>`/`is_sync<T>` specialization for that type into a hard
 * compile error instead of a silent, unsound default -- see the
 * `static_assert`s in the primary templates below. `std::is_base_of_v`
 * ignores accessibility, so a *private* base is enough; it need not
 * affect the derived type's public interface at all.
 */
struct requires_explicit_send_sync {};

#if RELOCO_HAS_REFLECTION

/**
 * @brief Experimental, `-freflection`-only structural fallback for
 * `is_send<T>`/`is_sync<T>` (see the file-level "Experimental" section
 * above). Only ever instantiated for a `T` with no explicit
 * specialization of `Trait` (a specialization always wins over the
 * primary template that calls this, so this can never override anyone's
 * manually-verified judgment). Walks `T`'s base classes and non-static
 * data members -- via `access_context::unchecked()`, so private members
 * are seen too, matching how Rust's compiler sees every field regardless
 * of visibility -- and requires `Trait<Member>::value` for every one of
 * them, recursing through the same `Trait` so nested reloco-marked types
 * (`rc<T>`, `function<Sig>`, ...) are picked up transitively. Anything
 * that isn't `is_class_v` (fundamentals, pointers, references, unions,
 * arrays, ...) is treated as an opaque `true` leaf, identical to today's
 * non-reflection default.
 */
template <template <typename> class Trait, typename T> consteval bool compose_send_sync() {
  if constexpr (std::is_class_v<T>) {
    bool ok = true;
    template for (constexpr auto b :
                  define_static_array(std::meta::bases_of(^^T, std::meta::access_context::unchecked()))) ok =
        ok &&Trait<typename[:std::meta::type_of(b):]>::value;
    template for (constexpr auto m : define_static_array(
                      std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()))) ok =
        ok &&Trait<typename[:std::meta::type_of(m):]>::value;
    return ok;
  } else {
    return true;
  }
}

#endif // RELOCO_HAS_REFLECTION

} // namespace detail

/**
 * @brief Customization point: is it sound to move a `T` to another thread
 * and continue using it only from there? Defaults to `true` (or, on a
 * `RELOCO_HAS_REFLECTION` build, a structural composition over `T`'s
 * fields -- see the file-level documentation above). Specialize to
 * `std::false_type` (or forward to another trait) for a type with
 * non-atomic shared state that makes cross-thread ownership transfer
 * unsound.
 */
#if RELOCO_HAS_REFLECTION
template <typename T> struct is_send : std::bool_constant<detail::compose_send_sync<is_send, T>()> {
#else
template <typename T> struct is_send : std::true_type {
#endif
  static_assert(!std::is_base_of_v<detail::requires_explicit_send_sync, T>,
                "T privately inherits detail::requires_explicit_send_sync but has no "
                "is_send<T> specialization -- see send_sync.hpp");
};

/**
 * @brief Convenience variable template for `is_send<T>::value`.
 */
template <typename T> inline constexpr bool is_send_v = is_send<T>::value;

/**
 * @brief Customization point: is it sound to share a `T` across threads
 * through a `const T &` (i.e. concurrent read access, or access
 * externally synchronized by the caller)? Defaults to `true` (or, on a
 * `RELOCO_HAS_REFLECTION` build, a structural composition over `T`'s
 * fields -- see the file-level documentation above). Specialize to
 * `std::false_type` for a type whose `const`-qualified operations still
 * mutate unsynchronized shared state.
 */
#if RELOCO_HAS_REFLECTION
template <typename T> struct is_sync : std::bool_constant<detail::compose_send_sync<is_sync, T>()> {
#else
template <typename T> struct is_sync : std::true_type {
#endif
  static_assert(!std::is_base_of_v<detail::requires_explicit_send_sync, T>,
                "T privately inherits detail::requires_explicit_send_sync but has no "
                "is_sync<T> specialization -- see send_sync.hpp");
};

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
