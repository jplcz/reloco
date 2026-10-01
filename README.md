<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# jplcz_reloco

_"Existence, in all its form and splendor, functions solely on one principle: Move is infallible."_

`reloco` ("**Rel**iable **Co**mponents") is a header-only C++17 library of
**Safety by Default** alternatives to Standard Library containers and
value-lifetime primitives. Every operation that can fail — an out-of-bounds
index, a use of a moved-from value, a dereference through a stale pointer —
is either rejected at compile time or surfaced through an explicit `result<T>`
(`reloco::expected<T, E>`) instead of undefined behavior, an exception, or a
silent trap that only fires in debug builds.

It targets the same class of code as `jplcz_microfmt`: embedded firmware,
RTOS tasks, interrupt paths, crash handlers, kernel-adjacent code, and any
call site where allocation-free, exception-free, and predictably-sized
containers matter — but as a general-purpose safety layer, not tied to
formatting.

## Guides

Start with the guide that matches what you are building:

| Guide | Covers |
|---|---|
| [Hardened containers](docs/hardened-containers.md) | Checked views and results, non-trapping access, assertion handling, and explicit security opt-out |
| [Fallible construction](docs/fallible-construction.md) | The `try_create`/`try_allocate`/`try_construct`/`try_clone`/`try_clone_at` protocol and its `has_try_*` detection traits/concepts |
| [Trivial relocation](docs/relocatable.md) | `is_trivially_relocatable<T>`: which types may be moved by copying bytes and abandoning the source, and why |
| [Thread-transfer/-sharing safety](docs/send-sync.md) | `is_send<T>`/`is_sync<T>`: which types are sound to hand to another thread or share concurrently, matching Rust's `Send`/`Sync` |
| [Reflection support](docs/reflection.md) | Experimental P2996/`-freflection` (GCC 16+ trunk): automatic `is_send`/`is_sync`/`is_trivially_relocatable` composition for types without an explicit specialization |
| [Futex backend](docs/futex.md) | `futex_word`/`futex_wait`/`futex_wake_one`/`futex_wake_all`: the low-level word-wait/wake primitive behind `barrier.hpp`, its Linux/FreeBSD/custom/portable backends, and the `RELOCO_FUTEX_BACKEND_CUSTOM` FreeBSD-kernel example |
| [Fault injection](docs/fault-injection.md) | `RELOCO_FAULT_POINT`/`RELOCO_FAULT_POINT_ARGS`/`fault_injector<Tag, Args...>`: deterministically reproducing concurrency races and other "impossible timing" bugs in single-threaded tests, opt-in via `RELOCO_ENABLE_FAULT_INJECTION` |
| [Over-alignment](docs/alignment.md) | `alignment_of<T>`: requesting SIMD-friendly over-aligned container storage without redeclaring `T` |
| [Lifetime safety](docs/lifetime-safety.md) | Borrowed values and pointers, lifetime annotations, consumed-value tracking, and compiler diagnostics |
| [Container contract](docs/container-contract.md) | Copy-paste template and checklist for lifetime annotations, rvalue protection, and the tri-tier accessor convention on a new container |
| [Type-erased base containers](docs/type-erased-base-containers.md) | How `vector`/`inline_vector`/`sso_vector`/`outline_vector` share one type-erased storage engine (`detail::vector_base.hpp`), and how to add a new vector flavor |
| [Deque containers](docs/deque-containers.md) | `vec_deque`'s ring-buffer engine (`detail::vector_base.hpp`'s `*_deque_base` policies), wrap-around handling, and how `try_insert_at`/`rotate_left`/`rotate_right` compose |
| [Ring buffers](docs/ring-buffer.md) | `ring_buffer`'s byte/POD-oriented streaming engine, zero-copy scatter-gather I/O, and the `try_consume_frame`/`try_write_frame_evicting` framing protocol |
| [Lock-free SPSC ring buffers](docs/atomic-ring-buffer.md) | `spsc_ring_buffer`'s cache-line-split, allocation-free, single-producer/single-consumer queue and its demand-driven cache-refresh strategy |
| [Tree containers](docs/tree-containers.md) | `tree_set`/`tree_map`'s node layout (`detail::node_base.hpp`), the unbalanced BST engine (`detail::tree_base.hpp`), and how they compare to `flat_set`/`flat_map` |
| [Flat hash containers](docs/flat-hash-containers.md) | `flat_hash_set`/`flat_hash_map`'s open-addressing engine (`detail::flat_hash_base.hpp`), backward-shift deletion, and how they compare to `tree_set`/`tree_map`/`flat_set`/`flat_map` |
| [Extending reloco](docs/extending.md) | The tag + `*_traits<Tag>` + `context_type` provider pattern used by `allocator_ref` and future pluggable backends |
| [Allocator capacity absorption](docs/allocator-capacity-absorption.md) | Binding `mem_block::size` to your allocator's real granularity: rationale, the `old_size`/`bytes` round-trip contract, arena/slab vs. system-allocator use, and the sanitizer caveat |
| [API reference](docs/reference.md) | Per-type quick reference for every public header |
| [Demos](demos/README.md) | Standalone example programs showing `scope()`/`guarded_mutex`/`barrier`, `channel`, `once_lock`, and `park`/`unpark` used together |
| [GDB pretty printers](docs/gdb-pretty-printers.md) | Formatting reloco containers/views/smart pointers in GDB: source, auto-load, or embed |
| [Package-manager integration](docs/package-managers.md) | Conan 2, vcpkg overlays, CPM.cmake, CPack packaging, and CMake-based dependency managers |
| [Shared-library deployments](docs/shared-library.md) | `RELOCO_SHARED`/`RELOCO_SHARED_BUILD` and `RELOCO_TYPE_INSTANCE(Type)`: deduplicating template instantiations across a multi-`.so` deployment |

## Design rationale: explicit failure, not undefined behavior

Standard containers push callers toward one of two bad defaults: undefined
behavior on out-of-bounds access (`operator[]`, `.front()` on an empty
container), or an exception-based escape hatch (`.at()`) that is often
unusable on platforms without exception support. `reloco` containers instead
expose a consistent three-tier access convention:

- **Checked, hard-failing accessors** (`front()`, `back()`, `operator[]`)
  still assert and trap on misuse in debug builds, and remain defined but
  intentionally strict — they are for cases the caller has already proven
  safe.
- **Fallible accessors** (`try_at()`, `try_front()`, `try_data()`, ...) return
  `reloco::expected<T, E>` and never trap; they are the default choice
  whenever an index, size, or pointer cannot be proven valid ahead of time.
- **Explicit escape-hatch accessors** (`unsafe_at()`, `unsafe_front()`, ...)
  skip all runtime checking. They are individually opt-in, are marked with
  `RELOCO_UNSAFE_BUFFER_USAGE` so Clang's `-Wunsafe-buffer-usage` flags every
  call site that has not been reviewed, and must be wrapped in
  `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE` to
  suppress that diagnostic — turning "this call is unchecked" into something
  visible in a diff and greppable across a codebase, rather than an implicit
  property of using `[]` or a raw pointer.

`reloco::value_ref<T>`/`reloco::value_ptr<T>` extend the same idea to
borrowed references and pointers: they reject binding to prvalue temporaries
at compile time (so a borrow can never quietly outlive its source), and
`reloco::checked_value<T>` brings Rust-like move semantics to C++ — a moved-
from `checked_value<T>` becomes an explicitly empty state instead of a value
in an unspecified condition, with `-Wconsumed`-backed compile-time enforcement
where the compiler supports it. See
[Lifetime safety](docs/lifetime-safety.md) for the full model and diagrams.

```cpp
#include <reloco/span.hpp>

void handle_packet(reloco::span<const uint8_t> bytes) {
  // Never traps or reads out of bounds, even with attacker-controlled input.
  const auto header = bytes.try_first(4);
  if (!header.has_value()) {
    // header.error() is a reloco::error — handle it explicitly.
    return;
  }

  // ...
}
```

## Add jplcz_reloco

### FetchContent or `add_subdirectory`

Use the CMake interface target:

```cmake
include(FetchContent)
FetchContent_Declare(
    jplcz_reloco
    GIT_REPOSITORY https://github.com/jplcz/reloco.git
)
FetchContent_MakeAvailable(jplcz_reloco)

target_link_libraries(my_target PRIVATE jplcz_reloco::reloco)
```

Direct `add_subdirectory` usage exposes the same target:

```cmake
add_subdirectory(third_party/jplcz_reloco)
target_link_libraries(my_target PRIVATE jplcz_reloco::reloco)
```

When embedded as a subdirectory, jplcz_reloco does not enable its tests,
header checks, strict warnings, or install rules by default. It also does not
change the parent project's global C++ standard. The interface target
requires C++17.

### Copy-paste: the full 4-step resolution block

The snippet below is exactly the pattern `jplcz_microfmt`/`jplcz_structo`/
`microvisor` use internally to consume their own dependencies (see "How the
jplcz_reloco dependency is resolved" in their own READMEs). Drop it into your
own `CMakeLists.txt`, rename every `MYPROJECT_` prefix to your own project's
name, and it resolves `jplcz_reloco` in order: an already-provided target, a
local checkout, your own Git remote/tag, then the official repository --
giving downstream users of *your* project the same override knobs this
project gives its own consumers.

```cmake
# Resolve jplcz_reloco: existing target > local checkout > caller's git
# remote/tag > official repository. Skip entirely if a parent build already
# provided the jplcz_reloco::reloco target.
if(NOT TARGET jplcz_reloco::reloco)
    set(MYPROJECT_RELOCO_SOURCE_DIR "" CACHE PATH
        "Path to a local jplcz_reloco checkout to use instead of fetching it")
    # Optional: allow the local checkout path via an environment variable too.
    if(NOT MYPROJECT_RELOCO_SOURCE_DIR AND DEFINED ENV{MYPROJECT_RELOCO_SOURCE_DIR})
        set(MYPROJECT_RELOCO_SOURCE_DIR "$ENV{MYPROJECT_RELOCO_SOURCE_DIR}"
            CACHE PATH
            "Path to a local jplcz_reloco checkout to use instead of fetching it"
            FORCE)
    endif()
    set(MYPROJECT_RELOCO_GIT_REPOSITORY "https://github.com/jplcz/reloco.git"
        CACHE STRING
        "Git repository to fetch jplcz_reloco from when MYPROJECT_RELOCO_SOURCE_DIR is unset")
    set(MYPROJECT_RELOCO_GIT_TAG "master" CACHE STRING
        "Git tag or commit to fetch jplcz_reloco from when MYPROJECT_RELOCO_SOURCE_DIR is unset")

    include(FetchContent)
    if(MYPROJECT_RELOCO_SOURCE_DIR)
        FetchContent_Declare(jplcz_reloco
            SOURCE_DIR "${MYPROJECT_RELOCO_SOURCE_DIR}")
    else()
        FetchContent_Declare(jplcz_reloco
            GIT_REPOSITORY "${MYPROJECT_RELOCO_GIT_REPOSITORY}"
            GIT_TAG "${MYPROJECT_RELOCO_GIT_TAG}")
    endif()

    # Keep reloco's own development targets out of the combined build.
    # CACHE ... FORCE is required: reloco's own CMakeLists.txt declares these
    # same variables via an unforced `set(... CACHE BOOL ...)`, which would
    # silently clear a plain `set()` of the same name (see "Local checkout
    # and overriding cache variables" above).
    set(JPLCZ_RELOCO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(JPLCZ_RELOCO_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(JPLCZ_RELOCO_BUILD_HEADER_CHECKS OFF CACHE BOOL "" FORCE)
    set(JPLCZ_RELOCO_ENABLE_STRICT_WARNINGS OFF CACHE BOOL "" FORCE)
    set(JPLCZ_RELOCO_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(jplcz_reloco)
endif()

target_link_libraries(my_target PRIVATE jplcz_reloco::reloco)
```

### Local checkout and overriding cache variables

Point `FetchContent_Declare` at a local working copy instead of fetching from
GitHub with `SOURCE_DIR`, and preset any `JPLCZ_RELOCO_*` cache variable (for
example `JPLCZ_RELOCO_PORTING_HEADERS`, see
[`include/reloco/detail/porting/README.md`](include/reloco/detail/porting/README.md))
before the `FetchContent_Declare`/`add_subdirectory` call that brings reloco
in:

```cmake
set(JPLCZ_RELOCO_PORTING_HEADERS "/path/to/porting" CACHE PATH "" FORCE)
set(JPLCZ_RELOCO_BUILD_TESTS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    jplcz_reloco
    SOURCE_DIR /path/to/local/jplcz_reloco
)
FetchContent_MakeAvailable(jplcz_reloco)
```

**Always preset a dependency-owned cache variable with
`CACHE <type> "" FORCE`, never a plain `set(VAR value)`.** CMake's
`set(<var> <value> CACHE <type> <docstring>)` (without `FORCE`) silently
discards any plain/normal variable of the same name already in scope the
first time it runs -- even though the cache entry did not previously exist --
so a parent project's unforced `set(JPLCZ_RELOCO_PORTING_HEADERS ...)`
executed *before* `include/reloco/CMakeLists.txt` declares that same variable
(via `set(JPLCZ_RELOCO_PORTING_HEADERS "" CACHE PATH ...)`, with no `FORCE`)
gets silently overwritten with the empty default as soon as reloco's own
`CMakeLists.txt` runs, with no warning or error -- the built-in default just
wins silently. Use `CACHE <type> "" FORCE` for every `JPLCZ_RELOCO_*` variable
you preset from a parent project, exactly like reloco's own forwarded options
already do internally (e.g. `JPLCZ_RELOCO_BUILD_TESTS OFF CACHE BOOL ""
FORCE` in a consumer like `jplcz_microfmt`/`jplcz_structo`'s own
`CMakeLists.txt`).

### Installed package and `ExternalProject`

Standalone builds enable `JPLCZ_RELOCO_INSTALL` by default:

```sh
cmake -S jplcz_reloco -B jplcz_reloco-build \
  -DJPLCZ_RELOCO_BUILD_TESTS=OFF \
  -DJPLCZ_RELOCO_BUILD_HEADER_CHECKS=OFF
cmake --build jplcz_reloco-build
cmake --install jplcz_reloco-build --prefix /opt/jplcz_reloco
```

The installation contains the public headers and a CMake config package:

```cmake
find_package(jplcz_reloco CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE jplcz_reloco::reloco)
```

An `ExternalProject_Add` dependency should pass its install prefix and disable
development-only targets:

```cmake
include(ExternalProject)
ExternalProject_Add(
    reloco_external
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/jplcz_reloco"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DJPLCZ_RELOCO_INSTALL=ON
        -DJPLCZ_RELOCO_BUILD_TESTS=OFF
        -DJPLCZ_RELOCO_BUILD_HEADER_CHECKS=OFF
)
```

Set `CMAKE_PREFIX_PATH` or `jplcz_reloco_DIR` to the installed package
directory when configuring a separate consuming project.

Custom build trees (e.g. an RTOS DDK/SDK with its own directory layout) can
override where each piece is installed, independent of
`CMAKE_INSTALL_PREFIX`, via:

- `JPLCZ_RELOCO_INSTALL_INCLUDEDIR` (default: `CMAKE_INSTALL_INCLUDEDIR`)
- `JPLCZ_RELOCO_INSTALL_DOCDIR` (default: `CMAKE_INSTALL_DOCDIR`)
- `JPLCZ_RELOCO_INSTALL_CMAKEDIR` (default:
  `${CMAKE_INSTALL_DATADIR}/cmake/jplcz_reloco`)

Each accepts a path relative to `CMAKE_INSTALL_PREFIX`, and must be set at
configure time (not `cmake --install`), for example:

```sh
cmake -S jplcz_reloco -B jplcz_reloco-build \
  -DJPLCZ_RELOCO_INSTALL_INCLUDEDIR=sdk/include \
  -DJPLCZ_RELOCO_INSTALL_CMAKEDIR=sdk/cmake/reloco
cmake --build jplcz_reloco-build
cmake --install jplcz_reloco-build --prefix /opt/sdk
```

Alternatively, add `include/` to the compiler include path:

```cpp
#include <reloco/array.hpp>
```

The library has no required third-party dependencies.

## Build the repository

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Strict GCC and Clang warnings are enabled by default for project targets.
Public headers are compiled under C++17, C++20, and C++23 when supported by
the active compiler. Clang's opt-in `-Wunsafe-buffer-usage` analysis uses a
ratcheted diagnostic baseline:

```bash
./scripts/check-unsafe-buffer-usage.sh
```

## License

See [LICENSE](LICENSE).
