<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Shared-library deployments and code size

`jplcz_reloco` is header-only: every translation unit (and, critically,
every shared object) that `#include`s a reloco header and names a given
class-template instantiation (`reloco::vector<MyType>`,
`reloco::optional<int>`, `reloco::flat_map<K, V>`, reloco's own
`basic_string<char>`, ...) gets its own copy of every member function of
that instantiation it actually uses. For a single executable or a single
`.so`, the linker already deduplicates that down to one copy of each. In a
**multi-`.so` deployment** -- one main application plus several
independently-built plugin/feature `.so`'s that each `#include` reloco and
use the same concrete types -- that deduplication only happens *within*
one `.so`'s link step, never *across* separate `-shared` invocations, so
every shared object pays for its own copy of the same code.

This guide covers `RELOCO_SHARED`/`RELOCO_SHARED_BUILD` and
`RELOCO_TYPE_INSTANCE(Type)`, the mechanism reloco provides to fix that. It
is entirely opt-in: a plain header-only build (the default) is completely
unaffected.

## How reloco's situation differs from `jplcz_microfmt`'s

`jplcz_microfmt` splits its own mechanism into two parts: one
(`MICROFMT_SHARED`/`MICROFMT_SHARED_BUILD` alone) for microfmt's own
*concrete* built-in formatters (`formatter<std::error_code>`,
`format_hexdump`, ...), and a second (`MICROFMT_FORMATTER_INSTANCE`) for
formatters that are themselves templated over an application-supplied
type, since microfmt cannot pre-migrate those ahead of time.

reloco mirrors both parts, in the same order below:

- **Part 1 -- reloco's own non-template concrete backends**
  (`RELOCO_API`/`RELOCO_SHARED_PROVIDE_DEFINITIONS`, see below): the mutex
  backends (`mutex.hpp`), the heap/stack allocator backends
  (`heap_allocator.hpp`/`stack_allocator.hpp`), the default allocator hook
  (`default_allocator.hpp`), and the `std::error_code` bridge
  (`error_std.hpp`, opt-in -- see below) are ordinary, non-template
  classes/functions, migrated the same way microfmt migrates its own
  built-in formatters.
- **Part 2 -- `RELOCO_TYPE_INSTANCE`**: everything else in reloco is a
  template over an application-supplied type (or over `CharT` for
  `reloco::string`/`reloco::sso_string`/`reloco::string_view`), which
  reloco cannot pre-migrate ahead of time -- generalized from
  microfmt's "formatters" to *any* reloco class template your application
  instantiates. `RELOCO_TYPE_INSTANCE` is deliberately close to
  `MICROFMT_FORMATTER_INSTANCE` in shape, because the same duplication
  problem, and the same opt-in fix, applies.

## Part 1: `RELOCO_API` and reloco's own concrete backends

`<reloco/detail/compat.hpp>`'s `RELOCO_API` macro mirrors microfmt's
`MICROFMT_API` exactly, and gates every non-trivial member function
definition of reloco's built-in, non-template concrete backends:

| Translation unit defines...          | `RELOCO_API` expands to                               | `RELOCO_SHARED_PROVIDE_DEFINITIONS` |
|----------------------------------------|--------------------------------------------------------|---------------------------------------|
| neither (plain header-only build)      | `inline`                                                | `1` (bodies compiled inline, as usual) |
| `RELOCO_SHARED_BUILD` (the build TU)    | export attribute (`__declspec(dllexport)`/visibility)  | `1` (bodies compiled once, exported)  |
| `RELOCO_SHARED` only (a consumer)       | import attribute (`__declspec(dllimport)`) or nothing  | `0` (bodies not compiled -- declaration only) |

Each migrated header keeps its class/function *declarations* unconditional
(so the public API and its documentation are identical in every mode), and
guards its out-of-line bodies (kept in a sibling `.ipp` file, `#include`d
from inside the header itself) behind
`#if RELOCO_SHARED_PROVIDE_DEFINITIONS`. Only genuinely non-trivial bodies
(real syscalls, loops, branches) are migrated this way; trivial one-line
getters and `constexpr` trivial constructors are left `inline` in every
mode, exactly like microfmt's own `bitfield.hpp`/`bitfield.ipp` precedent.

The migrated headers are: `mutex.hpp` (`mutex`/`recursive_mutex`/
`shared_mutex`/`condition_variable`/`error_checking_mutex`, for whichever
backend -- `RELOCO_MUTEX_BACKEND_PTHREAD` or `RELOCO_MUTEX_BACKEND_STD` --
is active), `heap_allocator.hpp`/`stack_allocator.hpp`
(`allocator_traits<heap_allocator_tag>`/`allocator_traits<stack_allocator_tag>`),
`default_allocator.hpp` (`reloco_global_alloc::default_allocator()`), and
`error_std.hpp` (`detail::error_category_impl` and the
`std::error_code`/`std::error_condition` bridge functions).

As with Part 2, this is entirely transparent to ordinary header-only
consumers: none of it does anything unless `RELOCO_SHARED` is defined.

### `reloco_compile.hpp`'s Part 1 coverage, and why `error_std.hpp` is excluded

`<reloco/reloco_compile.hpp>` (see the next section for its Part 2
coverage) also `#include`s `mutex.hpp`, `heap_allocator.hpp`,
`stack_allocator.hpp`, and `default_allocator.hpp`, so simply including
the umbrella from your `RELOCO_SHARED_BUILD` translation unit is enough to
get all four migrated for free.

**`error_std.hpp` is deliberately not included**, even though it is
`RELOCO_API`-guarded the same way: its `message()` returns a `std::string`
(a heap allocation), and this umbrella must never pull in std-allocating
or throwing machinery by default. If your application uses the
`std::error_code` bridge, `#include <reloco/error_std.hpp>` explicitly
alongside `reloco_compile.hpp` in your `RELOCO_SHARED_BUILD` translation
unit:

```cpp
// reloco_shared_lib.cpp
#define RELOCO_SHARED
#define RELOCO_SHARED_BUILD
#include <reloco/reloco_compile.hpp>
#include <reloco/error_std.hpp>   // opt-in: std::error_code bridge
#include "myapp_type_instances.hpp"
```

## Part 2: `RELOCO_TYPE_INSTANCE(Type)`

`<reloco/reloco_extern.hpp>`'s `RELOCO_TYPE_INSTANCE(Type)` macro is a
thin, opt-in wrapper around ordinary C++ explicit template instantiation.
It expands differently depending on which of
`RELOCO_SHARED`/`RELOCO_SHARED_BUILD` are defined in the translation unit
that uses it:

| Translation unit defines...            | Expands to                                              |
|-----------------------------------------|----------------------------------------------------------|
| neither (plain header-only build)       | nothing (a no-op)                                        |
| `RELOCO_SHARED` only (a consumer)       | `extern template class` **declaration** for that `Type`  |
| `RELOCO_SHARED_BUILD` (the build TU)    | explicit instantiation **definition** for that `Type`     |

This is exactly the same macro invocation in both places -- what changes
is only which macros are defined in that particular `.cpp` file, so you
write the list of types **once**, in one shared header, and include it
from both sides.

### Step-by-step

1. **Pick your shared "instance list" header.** This is application code,
   not reloco code -- create e.g. `myapp_type_instances.hpp`:

   ```cpp
   // myapp_type_instances.hpp
   #pragma once
   #include <reloco/reloco_extern.hpp>
   #include <reloco/vector.hpp>
   #include <reloco/optional.hpp>
   #include <reloco/flat_map.hpp>
   #include "my_types.hpp"

   // List every concrete class-template instantiation your application
   // actually uses and wants deduplicated across shared objects. Types
   // with commas (e.g. reloco::flat_map<int, int>) work fine -- the macro
   // is variadic.
   RELOCO_TYPE_INSTANCE(reloco::vector<MyType>);
   RELOCO_TYPE_INSTANCE(reloco::optional<int>);
   RELOCO_TYPE_INSTANCE(reloco::flat_map<int, MyType>);
   ```

2. **Include it from your `RELOCO_SHARED_BUILD` translation unit**
   (typically alongside `reloco_compile.hpp`, see below):

   ```cpp
   // reloco_shared_lib.cpp
   #define RELOCO_SHARED
   #define RELOCO_SHARED_BUILD
   #include <reloco/reloco_compile.hpp>
   #include "myapp_type_instances.hpp"
   ```

   This is what actually generates every listed `Type`'s member function
   bodies, exported once from `libreloco_shared.so`.

3. **Include the same header from every consumer translation unit** that
   uses one of those types:

   ```cpp
   // my_plugin.cpp
   #define RELOCO_SHARED
   #include <reloco/vector.hpp>
   #include "myapp_type_instances.hpp"

   void process(reloco::vector<MyType> &items) { /* ... */ }
   ```

   The `extern template class` declaration suppresses this translation
   unit's own local instantiation of `reloco::vector<MyType>` and binds
   the call to `libreloco_shared.so`'s copy instead.

4. **Link every consumer against the shared library** built in step 2.

CMake sketch:

```cmake
add_library(reloco_shared SHARED reloco_shared_lib.cpp)
target_compile_definitions(reloco_shared PUBLIC RELOCO_SHARED)
target_compile_definitions(reloco_shared PRIVATE RELOCO_SHARED_BUILD)
target_link_libraries(reloco_shared PUBLIC jplcz_reloco::reloco)

add_library(my_plugin SHARED my_plugin.cpp)
target_link_libraries(my_plugin PRIVATE reloco_shared)
```

### `reloco_compile.hpp`'s Part 2 coverage: reloco's own char-based string aliases

`<reloco/reloco_compile.hpp>` is a small umbrella for the
`RELOCO_SHARED_BUILD` translation unit that pre-declares
`RELOCO_TYPE_INSTANCE` for the three reloco-owned types nearly every
consumer ends up using regardless of its own application types:
`basic_string<char>`, `basic_sso_string<char>`, and
`basic_string_view<char>` (i.e. the underlying templates behind the
`reloco::string`/`reloco::sso_string`/`reloco::string_view` aliases).
Include it instead of hand-writing those three yourself:

```cpp
// reloco_shared_lib.cpp
#define RELOCO_SHARED
#define RELOCO_SHARED_BUILD
#include <reloco/reloco_compile.hpp>       // string / sso_string / string_view (char)
#include "myapp_type_instances.hpp"        // your own types
```

Ordinary consumer translation units do not need to include it (it errors
out if included without `RELOCO_SHARED_BUILD` defined, precisely to avoid
an ordinary consumer needlessly pulling in `string.hpp`/`sso_string.hpp`/
`string_view.hpp`); they only need `RELOCO_SHARED` and whichever
`reloco/*.hpp` headers they actually use, exactly as in a normal
header-only build.

**`wchar_t` is deliberately excluded.** `reloco::wstring`/
`reloco::wsso_string`/`reloco::wstring_view` (the `basic_string<wchar_t>`/
`basic_sso_string<wchar_t>`/`basic_string_view<wchar_t>` aliases) are far
less commonly used than their `char` counterparts, and unconditionally
instantiating them in every `RELOCO_SHARED` build would grow the shared
library for applications that never touch wide strings at all. If your
application does use them, add your own instance:

```cpp
RELOCO_TYPE_INSTANCE(reloco::basic_string<wchar_t>);
```

to your own instance-list header, exactly as for any other type
`reloco_compile.hpp` does not already cover.

### A typedef/alias caveat

`RELOCO_TYPE_INSTANCE(Type)` requires `Type` to name the class template
specialization itself, not a typedef-name over it: `template class`/
`extern template class` are rejected by the standard when applied to a
typedef-name. This is why `reloco_compile.hpp` instantiates
`reloco::basic_string<char>`, not `reloco::string` -- if you name one of
reloco's own aliased types yourself, use the underlying template the same
way. Application-defined class templates you name directly
(`reloco::vector<MyType>`, `reloco::optional<MyType>`, ...) are unaffected;
this only matters when the *outermost* name is itself a typedef/alias
rather than the template-id.

### Member function templates are not covered

Explicit instantiation of a class template does not instantiate member
function *templates* -- e.g. a `vector<T>::try_emplace_back<Args...>()`
call with a distinct `Args...` pack still gets its own per-translation-unit
copy regardless of `RELOCO_TYPE_INSTANCE`. This is an ordinary,
unavoidable consequence of C++'s explicit instantiation rules, not a
reloco limitation, and matches what `extern template` can and cannot do
for any C++ class template.

### Keeping the two sides in sync

Because the instance list is application-authored (not generated), a
mismatch is possible if you edit it carelessly -- e.g. a type used
somewhere but never listed, or listed in the build TU but a consumer
including a different, out-of-date copy of the header. Both failure modes
surface as an ordinary linker "undefined reference" error at link time,
not a silent miscompile or runtime bug -- exactly the same failure mode as
forgetting to link against the library at all. Using one single, shared
header for both sides (as shown above) is the simplest way to avoid this:
there is only one list to keep correct.

## See also

- [`reloco/detail/compat.hpp`](../include/reloco/detail/compat.hpp) --
  `RELOCO_API`/`RELOCO_API_CONSTEXPR`/`RELOCO_SHARED_PROVIDE_DEFINITIONS`'s
  definitions (Part 1).
- [`reloco/reloco_extern.hpp`](../include/reloco/reloco_extern.hpp) --
  `RELOCO_TYPE_INSTANCE`'s full doc comment and implementation (Part 2).
- [`reloco/reloco_compile.hpp`](../include/reloco/reloco_compile.hpp) --
  the umbrella header covering both reloco's own concrete backends (Part
  1) and its char-based string aliases (Part 2).
- [`reloco/reloco_config.hpp`](../include/reloco/reloco_config.hpp) --
  `RELOCO_SHARED`/`RELOCO_SHARED_BUILD`'s customization-point summary.
- [`jplcz_microfmt`'s shared-library guide](https://github.com/jplcz/microfmt/blob/main/docs/shared-library.md)
  -- the sibling mechanism this one mirrors, including
  `tools/codesize/testbed/` measurements of the underlying cross-`.so`
  duplication problem.
