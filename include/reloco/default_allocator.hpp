// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file default_allocator.hpp
 * @brief Process-wide default `allocator_ref`, similar in spirit to Rust's
 * `#[global_allocator]`.
 *
 * `reloco::default_allocator()` is the allocator containers and other
 * library-internal call sites reach for when the caller does not hand them
 * an explicit `allocator_ref`. It forwards to the `reloco_global_alloc::
 * default_allocator()` hook: out of the box that hook is implemented here to
 * return `allocator<heap_allocator_tag>::ref()` (the process heap), but
 * applications can replace it wholesale, similar to implementing Rust's
 * `GlobalAlloc` trait for `#[global_allocator]`.
 *
 * To override, define `RELOCO_DEFAULT_ALLOCATOR_CUSTOM` (any value) before
 * this header is first included -- typically via `reloco_user_config.hpp`
 * (see `reloco_config.hpp`) -- which suppresses the built-in definition of
 * `reloco_global_alloc::default_allocator()` so exactly one definition
 * (yours) exists. `reloco_user_config.hpp` cannot `#include
 * <reloco/allocator.hpp>` itself (see `reloco_config.hpp` for why), so
 * define the Tag, `allocator_traits<Tag>` specialization, and the hook's
 * out-of-line definition in their own ordinary header, included by the
 * application through the normal path -- not from `reloco_user_config.hpp`:
 *
 * @code
 * // reloco_user_config.hpp
 * #define RELOCO_DEFAULT_ALLOCATOR_CUSTOM
 *
 * // my_arena_allocator.hpp -- included normally elsewhere by the app,
 * // e.g. from a source file, before any use of reloco::default_allocator().
 * #include <reloco/allocator.hpp>
 * #include <reloco/default_allocator.hpp>
 *
 * struct my_arena_tag {};
 *
 * template <> struct reloco::allocator_traits<my_arena_tag> {
 *   using context_type = my_arena;
 *   // ... allocate/deallocate, see docs/extending.md ...
 * };
 *
 * inline reloco::allocator_ref reloco::reloco_global_alloc::default_allocator() noexcept {
 *   static my_arena arena;
 *   return reloco::allocator<my_arena_tag>(arena).ref();
 * }
 * @endcode
 *
 * For a stateless backend, the hook can just return `allocator<Tag>::ref()`
 * directly, no static context needed.
 *
 * The declaration above is `RELOCO_API`-decorated (see `detail/compat.hpp`),
 * so a custom hook that also wants `RELOCO_SHARED`/`RELOCO_SHARED_BUILD`
 * support (see `reloco_extern.hpp`/`docs/shared-library.md`) should define
 * it as `RELOCO_API` instead of plain `inline`, guarded on
 * `RELOCO_SHARED_PROVIDE_DEFINITIONS` so only the one `RELOCO_SHARED_BUILD`
 * translation unit actually provides a body:
 *
 * @code
 * #if RELOCO_SHARED_PROVIDE_DEFINITIONS
 * RELOCO_API reloco::allocator_ref reloco::reloco_global_alloc::default_allocator() noexcept {
 *   static my_arena arena;
 *   return reloco::allocator<my_arena_tag>(arena).ref();
 * }
 * #endif
 * @endcode
 *
 * A custom hook that never needs `RELOCO_SHARED` support at all can keep
 * using plain `inline` as shown in the first example -- `RELOCO_API` is
 * only needed to participate in the split.
 *
 * `reloco_global_alloc` is `RELOCO_EXPORT`-annotated so a stateful custom
 * hook's function-local `static` (as in the example above) stays one
 * shared, process-wide instance even across a `-fvisibility=hidden`
 * shared-library boundary -- see `detail/compat.hpp` and
 * `RELOCO_ENABLE_EXPORT` in `reloco_config.hpp` to opt in; the built-in,
 * stateless heap-backed default below has no such state to share, so this
 * only matters for a custom hook.
 */

#include "reloco_config.hpp"

#include "allocator.hpp"
#include "detail/compat.hpp"

namespace reloco {

/**
 * @brief Customization-point hook for `reloco::default_allocator()`.
 *
 * A plain struct (rather than a free function) so it can be forward
 * declared from headers that must not pull in `allocator.hpp`, and so its
 * one static member can be redefined out-of-line exactly once, either here
 * (the built-in heap-backed default) or by the application when
 * `RELOCO_DEFAULT_ALLOCATOR_CUSTOM` is defined.
 *
 * `RELOCO_EXPORT`-annotated (see `detail/compat.hpp`, `type_id.hpp`, and
 * `fallible_singleton.hpp`, which have the same concern): a custom hook is
 * expected to hold process-wide state via a function-local `static` (see
 * the file-level doc comment's example), exactly like
 * `fallible_singleton`'s storage -- an inline function's local statics are
 * only guaranteed to be one merged instance across shared objects if the
 * function itself keeps default visibility, so under `-fvisibility=hidden`
 * without this, two shared objects would each silently get their own
 * separate `default_allocator()` state instead of sharing one. Opt in with
 * `RELOCO_ENABLE_EXPORT` (see `reloco_config.hpp`); as with the other
 * `RELOCO_EXPORT` use sites, this is a no-op on a backend without an
 * equivalent attribute (e.g. MSVC).
 */
struct RELOCO_EXPORT reloco_global_alloc {
  [[nodiscard]] static RELOCO_API allocator_ref default_allocator() noexcept;
};

/**
 * @brief Returns the process-wide default allocator.
 *
 * The returned `allocator_ref` is a cheap, non-owning two-word handle; it
 * may be copied and stored freely, but its validity is tied to whatever
 * backs `reloco_global_alloc::default_allocator()` (the built-in default,
 * `heap_allocator_tag`, is stateless and always valid).
 */
[[nodiscard]] inline allocator_ref default_allocator() noexcept { return reloco_global_alloc::default_allocator(); }

} // namespace reloco

#if !defined(RELOCO_DEFAULT_ALLOCATOR_CUSTOM)
#include "heap_allocator.hpp"

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
namespace reloco {

RELOCO_API allocator_ref reloco_global_alloc::default_allocator() noexcept {
  return allocator<heap_allocator_tag>::ref();
}

} // namespace reloco
#endif
#endif
