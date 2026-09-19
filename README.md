<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# jplcz_reloco

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
| [Lifetime safety](docs/lifetime-safety.md) | Borrowed values and pointers, lifetime annotations, consumed-value tracking, and compiler diagnostics |
| [Extending reloco](docs/extending.md) | The tag + `*_traits<Tag>` + `context_type` provider pattern used by `allocator_ref` and future pluggable backends |
| [Package-manager integration](docs/package-managers.md) | Conan 2, vcpkg overlays, CPM.cmake, CPack packaging, and CMake-based dependency managers |

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
    // header.error() is a reloco::span_error — handle it explicitly.
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
