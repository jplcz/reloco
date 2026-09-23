// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file reloco_extern.hpp
 * @brief Opt-in, developer-controlled explicit instantiation of
 * application-supplied reloco template instances (e.g. `reloco::vector<
 * MyType>`, `reloco::optional<MyType>`, `reloco::flat_map<K, V>`) for
 * multi-shared-object deployments.
 *
 * reloco is header-only: every translation unit (and, critically, every
 * shared object) that `#include`s a reloco header and names a given
 * `reloco::vector<MyType>`/`reloco::optional<MyType>`/... gets its own
 * copy of every member function of that class template instantiation it
 * actually uses. The linker already deduplicates that down to one copy
 * within a single link step (one executable, or one `.so`), but never
 * *across* separate `-shared` invocations -- a main application plus
 * several independently-built plugin/feature `.so`'s that each
 * `#include` reloco and name the same `reloco::vector<MyType>` each pay
 * for their own copy of the same code.
 *
 * Unlike `jplcz_microfmt` (see its `microfmt_extern.hpp` and
 * `MICROFMT_FORMATTER_INSTANCE`, which this header's design directly
 * mirrors), reloco has no built-in *concrete* entities of its own to
 * pre-migrate ahead of time: essentially everything reloco defines is a
 * template over an application-supplied type (or, for `reloco::string`/
 * `reloco::string_view`/`reloco::sso_string`, over `CharT`), so there is
 * no reloco-owned equivalent of `MICROFMT_SHARED`/`MICROFMT_SHARED_BUILD`
 * covering built-ins automatically. `RELOCO_TYPE_INSTANCE(Type)` is the
 * single mechanism this header provides, and it is entirely the calling
 * application's responsibility to list, in one shared header, every
 * concrete instantiation (of reloco's own templates, like
 * `reloco::vector<MyType>`, or of application-defined templates built on
 * top of reloco, like `MyApp::widget_list` if that is itself a template)
 * it wants deduplicated this way -- see `reloco_compile.hpp` for the one
 * partial exception (reloco's own char-based `string`/`string_view`/
 * `sso_string` aliases), which is still opt-in and still requires the
 * application to include that header explicitly.
 *
 * `RELOCO_TYPE_INSTANCE(...)` expands differently depending on which of
 * `RELOCO_SHARED`/`RELOCO_SHARED_BUILD` are defined in the translation
 * unit that uses it:
 *
 *   - neither defined (plain header-only build): expands to nothing (a
 *     no-op) -- ordinary per-translation-unit implicit instantiation,
 *     already deduplicated by the linker's normal vague-linkage handling
 *     within a single binary, is exactly what you want, and there is no
 *     separate shared object to deduplicate *against*.
 *   - `RELOCO_SHARED` only defined (an ordinary consumer .cpp/.so):
 *     expands to an `extern template class` *declaration*, suppressing
 *     this translation unit's own local instantiation and binding the
 *     call to the shared library's definition below at link time.
 *   - `RELOCO_SHARED_BUILD` defined (the one translation unit building
 *     the actual shared library): expands to an explicit instantiation
 *     *definition*, generating every non-template member of that class
 *     template instantiation's body, exported from the shared library.
 *
 * This is exactly the same macro invocation in both places -- what
 * changes is only which macros are defined in that particular `.cpp`
 * file, so an application writes the list of types **once**, in one
 * shared header, and includes it from both sides:
 *
 *   // myapp_type_instances.hpp -- application code, not reloco code
 *   #pragma once
 *   #include <reloco/reloco_extern.hpp>
 *   #include <reloco/vector.hpp>
 *   #include <reloco/optional.hpp>
 *   #include "my_types.hpp"
 *
 *   RELOCO_TYPE_INSTANCE(reloco::vector<MyType>);
 *   RELOCO_TYPE_INSTANCE(reloco::optional<int>);
 *   RELOCO_TYPE_INSTANCE(reloco::flat_map<int, MyType>);
 *
 *   // myapp_shared_lib.cpp -- the one .cpp, built `-shared`
 *   #define RELOCO_SHARED
 *   #define RELOCO_SHARED_BUILD
 *   #include "myapp_type_instances.hpp"
 *
 *   // any consumer .cpp, in any shard .so or the main executable
 *   #define RELOCO_SHARED
 *   #include "myapp_type_instances.hpp"
 *   // ... use reloco::vector<MyType>, reloco::optional<int>, ... as usual
 *
 * `RELOCO_SHARED` must be defined identically in every translation unit
 * in the deployment (it changes whether `RELOCO_TYPE_INSTANCE` expands to
 * a no-op, a declaration, or a definition); `RELOCO_SHARED_BUILD` must be
 * defined in exactly one of them, and every other translation unit must
 * be linked against the shared library that one produces.
 *
 * Only explicit instantiation-*eligible* templates qualify: an ordinary
 * class template (`reloco::vector<T>`, `reloco::optional<T>`,
 * `reloco::flat_map<K, V>`, `reloco::unique_ptr<T>`, `reloco::shared_ptr<
 * T>`, `reloco::function<R(Args...)>`, `reloco::basic_string<CharT>`,
 * ...) works as-is: `template class`/`extern template class` instantiate
 * every ordinary (non-template) member, exactly what a consumer normally
 * pays for per-translation-unit. Explicit instantiation of a class
 * template does **not** instantiate member function *templates* (e.g. a
 * `vector<T>::try_emplace_back<Args...>()` call with a distinct `Args...`
 * pack still gets its own per-translation-unit copy regardless) -- that
 * is an ordinary, unavoidable consequence of C++'s explicit instantiation
 * rules, not a reloco limitation.
 *
 * `Type` must name the class template specialization itself, not a
 * typedef-name/alias-template instantiation over it: `template class`/
 * `extern template class` are rejected by the standard (and diagnosed by
 * every major compiler) when applied to a typedef-name. This matters for
 * reloco's own `string`/`sso_string`/`string_view` aliases (`using string
 * = basic_string<char>;` and friends, see string.hpp/sso_string.hpp/
 * string_view.hpp) -- use `RELOCO_TYPE_INSTANCE(reloco::basic_string<
 * char>)`, not `RELOCO_TYPE_INSTANCE(reloco::string)` (see
 * reloco_compile.hpp, which does exactly this for reloco's own char-based
 * aliases). Application-defined class templates you name directly (
 * `reloco::vector<MyType>`, `reloco::optional<MyType>`, ...) are
 * unaffected -- this only matters when the *outermost* name is itself a
 * typedef/alias rather than the template-id.
 *
 * Keeping both call sites in sync is the caller's responsibility, exactly
 * as for `MICROFMT_FORMATTER_INSTANCE`: naming a `Type` with
 * `RELOCO_TYPE_INSTANCE` in a `RELOCO_SHARED` consumer without a matching
 * `RELOCO_SHARED_BUILD` definition linked in (or omitting a `Type` here
 * that some consumer still relies on the shared library for) surfaces as
 * an ordinary "undefined reference" link error, not a silent miscompile.
 */

#include "detail/compat.hpp"

#if defined(RELOCO_SHARED_BUILD)
#if defined(_MSC_VER)
#define RELOCO_TYPE_INSTANCE_DLLSPEC __declspec(dllexport)
#else
#define RELOCO_TYPE_INSTANCE_DLLSPEC
#endif
#define RELOCO_TYPE_INSTANCE(...) template class RELOCO_TYPE_INSTANCE_DLLSPEC __VA_ARGS__
#elif defined(RELOCO_SHARED)
#if defined(_MSC_VER)
#define RELOCO_TYPE_INSTANCE_DLLSPEC __declspec(dllimport)
#else
#define RELOCO_TYPE_INSTANCE_DLLSPEC
#endif
#define RELOCO_TYPE_INSTANCE(...) extern template class RELOCO_TYPE_INSTANCE_DLLSPEC __VA_ARGS__
#else
#define RELOCO_TYPE_INSTANCE(...)
#endif
