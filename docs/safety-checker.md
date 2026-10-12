<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# reloco-tidy: the safety checker

`reloco-tidy` is a clang-tidy plugin (`tools/tidy`) that enforces reloco's safe-coding
rules on the AST, so derived projects (`structo`, `microfmt`, your own firmware) get the
same guarantees reloco's own headers are written to. It complements, and does not replace,
Clang's `-Wlifetime-safety*`, `-Wdangling-gsl` and `-Wunsafe-buffer-usage`.

Tests, examples, demos, benchmarks, build trees and third-party code are exempt by default;
library headers and implementations must pass.

## Building and running

```bash
# The newest /usr/lib/llvm-N with clang-tidy headers is chosen automatically (30 down to 18).
# Override with -DRELOCO_TIDY_LLVM_VERSION=24 or -DRELOCO_TIDY_LLVM_ROOT=/path/to/llvm.
cmake -S tools/tidy -B build-tidy -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build-tidy

# The plugin must match the major version of the clang-tidy executable that loads it.
clang-tidy-24 --load=build-tidy/libreloco-tidy.so --checks='-*,reloco-*' \
    -p build src/foo.cpp
```

Fixture tests: `ctest` target `reloco_tidy_fixtures`, or directly
`python3 tools/tidy/test/run_tests.py --clang-tidy <exe> --plugin <so> --include include`.
Fixtures use `// expect: <check>` on the offending line.

## Rules

| Check | Rule | Why |
|---|---|---|
| `reloco-ref-qualified-member` | Non-static members must be `&`/`const &` qualified, or have an explicit `&&` overload. | Calling them on temporaries is blocked, so views and references to a dying object cannot be created. |
| `reloco-no-raw-pointer` | Raw pointers only inside `RELOCO_POINTER`/`RELOCO_OWNER` types, the unsafe tier, vtables, function pointers and `extern "C"`. Use `value_ptr`, `value_ref` or `span` elsewhere. | Raw pointers carry no size, lifetime or ownership. |
| `reloco-no-c-array` | No raw C array variables or members; use `reloco::array`, `inline_vector` or a span. | Arrays decay to pointers and lose their bounds. |
| `reloco-noexcept` | Every function and lambda is `noexcept` (a conditional `noexcept(expr)` counts as explicit). | Failure is a `result<T>`, never an exception. |
| `reloco-lifetime-annotations` | Accessors returning aliases need `RELOCO_LIFETIMEBOUND`; handle constructors need lifetimebound or capture annotations on borrowed parameters. | Lets Clang diagnose dangling views at the call site. |
| `reloco-pointer-annotation` | Classes holding borrowed storage must be `RELOCO_POINTER`; owners `RELOCO_OWNER`. | Without them Clang's lifetime analysis does not track the type. |
| `reloco-banned-std-type` | `std::` facilities with a reloco/microfmt/structo equivalent (containers, `string`, `span`, `optional`, `variant`, `function`, smart pointers, `source_location`, exception types, iostreams) are banned. The message names the replacement. | Decided on the AST, so aliases are caught too. Complements `scripts/check-no-std-types.py`. |
| `reloco-banned-construct` | `throw`/`try`/`catch`, `new`/`delete`, `const_cast`, `reinterpret_cast`, `dynamic_cast`, C-style casts, and C string/memory/stdlib functions (`memcpy`, `strlen`, `malloc`, ...). | Each defeats a static guarantee. |
| `reloco-implicit-copy` | Classes are move-only unless marked `RELOCO_COPYABLE` (Rust's `Copy`). Implicit copies (`T y = x`, by-value arguments, by-value range-for, copy assignment) are flagged. Explicit copies (`T(x)`, `T y(x)`, `T y{x}`, `static_cast<T>(x)`, member initialisers) are the equivalent of `.clone()` and always allowed. `RELOCO_POINTER` views are always copyable. | Makes every duplicate of a resource visible in the source. |
| `reloco-uninit-local` | Locals are always initialised at declaration. Classes with a non-trivial default constructor, and static locals, are fine. | No indeterminate values. |
| `reloco-out-param` | A `RELOCO_OUT` parameter must be written on every returning path and not read before its first write. See below. | Callers can rely on the output being set. |
| `reloco-borrow-check` | Best-effort Rust-style borrow checking inside one function. See below. | Aliasing XOR mutability. |

### `RELOCO_OUT`

```cpp
// x is an out-parameter: the function must assign it on every path that returns.
bool parse(int *x RELOCO_OUT, bool ok) noexcept {
  if (!ok)
    return false;   // error: 'x' is not written on every path that returns
  *x = 42;          // for `int &x RELOCO_OUT` write `x = 42;`
  return true;
}
```

A write is an assignment to `*x`, `x[i]`, `x->m` (or the reference and its members), a non-const
member call on it, or passing it to a callee taking a non-const pointer/reference. One member
write counts as writing the whole object, and handing it to a callee counts as a write. Reading
before the first write, and `RELOCO_OUT` on a const or non-pointer parameter, are errors.
Paths ending in a `noreturn` call are ignored.

### `reloco-borrow-check`

Intra-procedural and flow-sensitive (NLL-style):

- A *borrow* is a local reference, pointer or `RELOCO_POINTER` view initialised from a place
  (a variable plus a field path): `auto &r = x.first;`, `span s = buf;`, `r = x.cfirst();`.
  A `RELOCO_LIFETIMEBOUND` call result borrows from the bound argument.
- It is live from its initialiser to its last use; inside a loop it lives to the loop end when
  declared outside the loop.
- A *shared* borrow (const target) excludes mutation of the place: assignment, `++`/`--`, a
  non-const method call, a non-const reference argument, or a move (`&&` argument).
- A *mutable* borrow excludes every use of an overlapping place.
- A range-for borrows its range for the whole body, mutably if the loop variable is a non-const reference.
- Disjoint fields do not overlap.

Limits: no inter-procedural reasoning, no aliasing through containers or heap, no use-after-move
tracking. Use Clang's `-Wlifetime-safety` for use-after-return/scope. It is a bug finder, not a proof.

## Exemptions

Every check honours these; each use should say why it is sound.

| Mechanism | Scope |
|---|---|
| `// NOLINT(reloco-xxx): reason`, `NOLINTNEXTLINE`, `NOLINTBEGIN/END` | line / block (standard clang-tidy) |
| `RELOCO_BEGIN_UNSAFE_CODE` / `RELOCO_END_UNSAFE_CODE` | region, all reloco checks |
| `RELOCO_UNSAFE_BUFFER_USAGE` function or a function named `unsafe_*` | the unsafe tier (raw pointers, casts, arrays tolerated) |
| `#pragma clang unsafe_buffer_usage begin/end`, `RELOCO_BEGIN/END_UNSAFE_BUFFER_USAGE` | region, **borrow check only** |
| `RELOCO_BORROW_EXEMPT` on a function or class | borrow check, e.g. `swap`/`exchange` primitives |
| `ExemptPaths` option (`;`-separated substrings, `none` to disable) | default `/tests/;/test/;/examples/;/example/;/demos/;/benchmarks/;/_deps/;/third_party/;/build/;/build-` |

System headers are never reported.

## Opt-in mode for derived projects

By default (`SafeCodeMode: all`) the rules apply to all non-exempt code. Projects that include
3rd-party sources can switch to `SafeCodeMode: opt-in`, where only code between the safe-region
macros is checked:

```cpp
#include <reloco/lifetime.hpp>

RELOCO_BEGIN_SAFE_CODE   // checked from here ...
void ours(int *x RELOCO_OUT) noexcept { *x = 1; }
RELOCO_END_SAFE_CODE     // ... to here

RELOCO_BEGIN_UNSAFE_CODE // always exempt, also inside a safe region
RELOCO_END_UNSAFE_CODE
```

The macros expand to `_Pragma("region ...")`, which GCC, Clang and MSVC accept silently
(`-Wunknown-pragmas` clean), so they are safe in any compiler. Set the mode in `.clang-tidy`:
`CheckOptions: [{key: reloco-no-raw-pointer.SafeCodeMode, value: opt-in}]` (the same option exists on every check).

## Macro reference (`reloco/lifetime.hpp`)

`RELOCO_POINTER`, `RELOCO_OWNER`, `RELOCO_LIFETIMEBOUND`, `RELOCO_COPYABLE`, `RELOCO_OUT`,
`RELOCO_BORROW_EXEMPT`, `RELOCO_UNSAFE_BUFFER_USAGE`, `RELOCO_BEGIN/END_SAFE_CODE`,
`RELOCO_BEGIN/END_UNSAFE_CODE`, `RELOCO_BEGIN/END_UNSAFE_BUFFER_USAGE`. All compile to nothing
where the compiler lacks the attribute. See also [lifetime safety](lifetime-safety.md).
