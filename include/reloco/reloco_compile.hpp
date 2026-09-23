// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file reloco_compile.hpp
 * @brief Umbrella header for the single translation unit that builds a
 * RELOCO_SHARED library (see reloco/reloco_extern.hpp for
 * RELOCO_SHARED/RELOCO_SHARED_BUILD/RELOCO_TYPE_INSTANCE, and reloco/
 * detail/compat.hpp for RELOCO_API/RELOCO_SHARED_PROVIDE_DEFINITIONS).
 *
 * Two independent things happen here:
 *
 * - Reloco's own char-based string aliases (`reloco::string`, `reloco::
 *   sso_string`, `reloco::string_view` -- see string.hpp/sso_string.hpp/
 *   string_view.hpp), which nearly every consumer of reloco ends up using
 *   regardless of which application types it also stores, get a
 *   `RELOCO_TYPE_INSTANCE` here as a convenience -- still entirely
 *   opt-in, since it only has any effect when RELOCO_SHARED/
 *   RELOCO_SHARED_BUILD are defined.
 * - Reloco's non-template concrete backends (`mutex`/`recursive_mutex`/
 *   `shared_mutex`/`condition_variable`/`error_checking_mutex` in
 *   mutex.hpp; `allocator_traits<heap_allocator_tag>` in
 *   heap_allocator.hpp; `allocator_traits<stack_allocator_tag>` in
 *   stack_allocator.hpp; `reloco_global_alloc::default_allocator()` in
 *   default_allocator.hpp) are `RELOCO_API`-guarded (see each header):
 *   simply including them here, with RELOCO_SHARED_BUILD defined, is what
 *   causes their (non-template, so RELOCO_TYPE_INSTANCE does not apply)
 *   definitions to be emitted into this translation unit/the resulting
 *   shared library, instead of duplicated inline into every consumer TU.
 *
 * The `wchar_t`-based string counterparts (`reloco::wstring`, `reloco::
 * wsso_string`, `reloco::wstring_view`) are deliberately **not** included
 * here: they are far less commonly used than the `char` aliases, and
 * always paying for their instantiation in every RELOCO_SHARED build would
 * needlessly grow the shared library for applications that never touch
 * them. An application that does use the wide aliases must add its own
 * `RELOCO_TYPE_INSTANCE(reloco::wstring)` (and/or `wsso_string`/
 * `wstring_view`) to its own instance-list header (see
 * reloco/reloco_extern.hpp), exactly as for any other application-supplied
 * type this header does not already cover.
 *
 * `error_std.hpp` (the `std::error_code`/`std::error_condition` bridge --
 * see error_std.ipp) is also deliberately **not** included here, even
 * though it is otherwise `RELOCO_API`-guarded like the headers above: its
 * `message()` allocates a `std::string`, and this umbrella must not pull
 * in std-allocating (or throwing) machinery by default. An application
 * that uses the `std::error_code` bridge must explicitly
 * `#include <reloco/error_std.hpp>` alongside this header in its
 * RELOCO_SHARED_BUILD translation unit.
 *
 * Everything else an application stores/passes through a reloco
 * container/wrapper (`reloco::vector<MyType>`, `reloco::optional<MyType>`,
 * `reloco::flat_map<K, V>`, ...) is the application's own responsibility to
 * list via RELOCO_TYPE_INSTANCE in its own header, included alongside this
 * one from both the RELOCO_SHARED_BUILD translation unit and every
 * ordinary RELOCO_SHARED consumer.
 *
 * Usage:
 *
 *   // reloco_shared_lib.cpp -- the one .cpp file, built `-shared`
 *   #define RELOCO_SHARED
 *   #define RELOCO_SHARED_BUILD
 *   #include <reloco/reloco_compile.hpp>
 *   #include "myapp_type_instances.hpp"  // your own RELOCO_TYPE_INSTANCE(...) list
 *
 * Requires RELOCO_SHARED_BUILD (and, transitively, RELOCO_SHARED) to
 * already be defined before this header is included -- including it in an
 * ordinary header-only or RELOCO_SHARED-consumer build is almost certainly
 * a mistake (it would needlessly drag in string.hpp/sso_string.hpp/
 * string_view.hpp and their `extern template` declarations even for a
 * consumer that never uses them), so it is rejected at compile time
 * instead.
 */

#if !defined(RELOCO_SHARED_BUILD)
#error                                                                                                                \
    "reloco_compile.hpp is only meant for the translation unit that builds a RELOCO_SHARED library -- define RELOCO_SHARED and RELOCO_SHARED_BUILD before including it (see reloco/reloco_extern.hpp)"
#endif

#include "reloco_extern.hpp"

#include "sso_string.hpp"
#include "string.hpp"
#include "string_view.hpp"

// char-based only -- see this file's doc comment for why the wchar_t
// aliases (wstring/wsso_string/wstring_view) are deliberately excluded.
//
// The underlying class template (basic_string<char>, not the string
// alias) must be named here: `template class`/`extern template class`
// (see RELOCO_TYPE_INSTANCE) is only valid for a genuine class template
// specialization, not a typedef-name -- see reloco/reloco_extern.hpp's
// doc comment for the same caveat applied to your own instance lists.
RELOCO_TYPE_INSTANCE(reloco::basic_string<char>);
RELOCO_TYPE_INSTANCE(reloco::basic_sso_string<char>);
RELOCO_TYPE_INSTANCE(reloco::basic_string_view<char>);

// Non-template, RELOCO_API-guarded concrete backends -- merely including
// each header here (with RELOCO_SHARED_BUILD defined) causes its
// definitions to be emitted into this translation unit rather than
// duplicated inline into every consumer TU. error_std.hpp is deliberately
// NOT included here -- see this file's doc comment.
#include "default_allocator.hpp"
#include "heap_allocator.hpp"
#include "mutex.hpp"
#include "stack_allocator.hpp"
