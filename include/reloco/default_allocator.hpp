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
 */

#include "reloco_config.hpp"

#include "allocator.hpp"

namespace reloco {

/**
 * @brief Customization-point hook for `reloco::default_allocator()`.
 *
 * A plain struct (rather than a free function) so it can be forward
 * declared from headers that must not pull in `allocator.hpp`, and so its
 * one static member can be redefined out-of-line exactly once, either here
 * (the built-in heap-backed default) or by the application when
 * `RELOCO_DEFAULT_ALLOCATOR_CUSTOM` is defined.
 */
struct reloco_global_alloc {
  [[nodiscard]] static allocator_ref default_allocator() noexcept;
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

namespace reloco {

inline allocator_ref reloco_global_alloc::default_allocator() noexcept {
  return allocator<heap_allocator_tag>::ref();
}

} // namespace reloco
#endif
