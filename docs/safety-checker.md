<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# reloco-tidy: the safety checker

`reloco-tidy` is a clang-tidy plugin (`tools/tidy`) that enforces reloco's safe-coding
rules on the AST, so derived projects (`structo`, `microfmt`, your own firmware) get the
same guarantees reloco's own headers are written to. It complements, and does not replace,
Clang's `-Wlifetime-safety*`, `-Wdangling-gsl` and `-Wunsafe-buffer-usage`.

## Motivation

The author got fed up with how trivially easy it is to write undefined behavior or otherwise
unsafe code in C and C++: dangling views, uninitialized locals, raw pointers and C arrays that
lose their bounds, silent copies, aliasing mutation. None of it needs a warning to compile. The
checker turns those habits into diagnostics, and every opt-out into a visible, greppable marker
(`NOLINT`, `RELOCO_BEGIN_UNSAFE_CODE`, `RELOCO_BORROW_EXEMPT`) that a reviewer can question.

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

## Using the checker in your IDE

Every IDE below ends up running the same `clang-tidy` with the same plugin, so first make
that work from a terminal, then point the IDE at it. Two prerequisites for all of them:

1. A `compile_commands.json` (`cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, or Meson/Bear).
2. A project-root `.clang-tidy` that enables the checks. IDEs read it automatically, so the
   per-IDE settings stay minimal. Plugins are not loadable from `.clang-tidy`; the `--load`
   flag has to come from the IDE or wrapper (shown below).

```yaml
# .clang-tidy (project root)
# Enable only reloco's checks; add your own groups after them.
Checks: '-*,reloco-*'
# Make findings errors in CI; leave empty to keep them warnings in the editor.
WarningsAsErrors: ''
# Only report diagnostics from your own headers, not system/3rd-party ones.
HeaderFilterRegex: '^(?!.*(/_deps/|/third_party/)).*'
CheckOptions:
  # Opt-in mode for derived projects: check only RELOCO_BEGIN/END_SAFE_CODE regions.
  # SafeCodeMode is a per-check option; repeat it for every reloco check you enable
  # (or use 'all', the default, to check everything outside the exempt paths).
  - key: reloco-no-raw-pointer.SafeCodeMode
    value: opt-in
  - key: reloco-borrow-check.SafeCodeMode
    value: opt-in
```

Because the plugin cannot be loaded from `.clang-tidy`, the simplest portable approach is a
wrapper script that every IDE can call in place of `clang-tidy`.
This repository ships one as `tools/clang-tidy-reloco`. It loads `build-tidy/libreloco-tidy.so`;
override with the `RELOCO_TIDY_PLUGIN` and `CLANG_TIDY` environment variables (the plugin and the
executable must have the same LLVM major version). Copy it into your project if you build the
plugin elsewhere. Use its path in the settings below.

### VS Code

Use the *clangd* extension (preferred, live as you type) or the *C/C++* extension.

- **clangd**: clangd links its own clang-tidy and cannot load external plugins, so use it for
  the editor's compiler diagnostics and run reloco-tidy as a task (below) with problem
  matching. Set `"clangd.arguments": ["--clang-tidy=false"]` to avoid duplicate noise.
- **C/C++ extension**: set `"C_Cpp.codeAnalysis.clangTidy.enabled": true`,
  `"C_Cpp.codeAnalysis.clangTidy.path": "${workspaceFolder}/tools/clang-tidy-reloco"` and
  `"C_Cpp.codeAnalysis.runAutomatically": true`. Findings appear in the Problems panel,
  and fix-its show up as quick fixes (lightbulb).

Task with problem matcher, so findings are clickable (`.vscode/tasks.json`):

```jsonc
{
  "version": "2.0.0",
  "tasks": [
    {
      // Runs reloco-tidy on the file open in the editor.
      "label": "reloco-tidy: current file",
      "type": "shell",
      "command": "${workspaceFolder}/tools/clang-tidy-reloco",
      // -p: directory containing compile_commands.json.
      "args": ["-p", "${workspaceFolder}/build", "${file}"],
      "problemMatcher": {
        "owner": "reloco-tidy",
        "fileLocation": "absolute",
        "pattern": {
          "regexp": "^(.*):(\\d+):(\\d+):\\s+(warning|error|note):\\s+(.*)$",
          "file": 1, "line": 2, "column": 3, "severity": 4, "message": 5
        }
      }
    },
    {
      // Applies the automatic fix-its (see "Fix-its" above); review the diff afterwards.
      "label": "reloco-tidy: fix current file",
      "type": "shell",
      "command": "${workspaceFolder}/tools/clang-tidy-reloco",
      "args": ["-fix", "-p", "${workspaceFolder}/build", "${file}"]
    }
  ]
}
```

### CLion

CLion runs clang-tidy itself, so use its external-tool path for the plugin:

1. *Settings | Editor | Inspections | C/C++ | General | Clang-Tidy*: enable it and choose
   *Use .clang-tidy files*. CLion shows the built-in checks live.
2. *Settings | Tools | External Tools | +*: Program `$ProjectFileDir$/tools/clang-tidy-reloco`,
   Arguments `-p $ProjectFileDir$/build $FilePath$`, Working directory `$ProjectFileDir$`.
   Output filters (*Advanced Options | Output Filters*):
   `$FILE_PATH$:$LINE$:$COLUMN$: .*`. Assign a keyboard shortcut under *Keymap*.
3. Optional: *Settings | Build, Execution, Deployment | CMake*, profile option
   `-DCMAKE_CXX_CLANG_TIDY=/path/tools/clang-tidy-reloco` makes every build run the checker,
   with findings shown in the Build tool window.

### Other editors

| Editor | How |
|---|---|
| Visual Studio | Use the clang-tidy wrapper through *Project | Properties | Code Analysis | Clang-Tidy* (`ClangTidyToolPath`), or the CMake `CMAKE_CXX_CLANG_TIDY` route. |
| Qt Creator | *Analyze | Clang-Tidy and Clazy | Diagnostics Configuration*; set the executable to the wrapper. |
| Neovim / Emacs | Use `nvim-lint` / `flycheck` with the wrapper as the command and `-p build %file`; `clang-tidy` output is already in the standard `file:line:col:` format. |
| Any CI | `run-clang-tidy -clang-tidy-binary tools/clang-tidy-reloco -p build` (parallel across files). |
| Any build system | `-DCMAKE_CXX_CLANG_TIDY=/path/tools/clang-tidy-reloco` runs it on every compiled file. |

### Troubleshooting

- *"unable to load plugin" / undefined symbols*: plugin and `clang-tidy` major versions differ.
- *Nothing is reported*: the file lives under an exempt path (`tests/`, `build/`, ...), or
  `SafeCodeMode: opt-in` is set and the code is outside `RELOCO_BEGIN/END_SAFE_CODE`.
- *Missing includes in the IDE*: regenerate `compile_commands.json`; headers need `-I` flags.
- *Slow*: use `run-clang-tidy -j$(nproc)` and restrict to changed files in CI.

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
| `reloco-implicit-conversion` | Implicit narrowing, sign-changing and lossy float conversions are errors; use `static_cast` or a checked helper. Constants that fit are fine. | Rust has no implicit `as`. |
| `reloco-explicit-conversion` | Single-argument constructors and conversion operators must be `explicit` (`initializer_list` constructors excepted). | No silent conversions. |
| `reloco-must-use-result` | Functions returning `expected`/`result`/`error`/`status`/`optional` types must be `[[nodiscard]]` (or the type must be). | Rust's `#[must_use]`. |
| `reloco-unused-result` | A discarded call result of those types is an error; cast to `void` with a reason. | Dropped errors. |
| `reloco-const-local` | A local that is never mutated must be `const` (pointers, references and arrays excluded). | Immutable by default, like `let`. |
| `reloco-no-shadow` | A local may not shadow another local or parameter of an enclosing scope. | |
| `reloco-enum-switch` | A `switch` over an enum lists every enumerator, **even when it has a `default`**: a `default` that stands in for enumerators (usually written to silence GCC/Clang or static analyzers) is reported. `default` stays allowed as an extra arm for out-of-range values once all enumerators are listed; suppress with `NOLINT` where a catch-all is intended. | A new enumerator breaks the build. Pair with `-Wimplicit-fallthrough` for fallthrough. |
| `reloco-banned-low-level` | `goto`, `setjmp`/`longjmp`, `union` and `volatile` (suppress MMIO with `NOLINT` and a reason). | Each bypasses a static guarantee. |
| `reloco-mutable-global` | Mutable namespace-scope, static-member and function-static variables are banned, except atomic/mutex/lock types. | Rust's `static mut`. |
| `reloco-unsafe-call-scope` | Calls to `unsafe_*` / `[[unsafe_buffer_usage]]` functions are only allowed from the unsafe tier or inside unsafe regions (also Clang's `unsafe_buffer_usage` blocks). | Rust's `unsafe` blocks. |
| `reloco-checked-operator` | `operator[]`, unary `operator*` and `operator->` must be `RELOCO_CHECKED` (they bounds/null check), on the operator or its class; using such an operator on a type that is not `RELOCO_CHECKED` is a violation. Callers inside the unsafe tier are exempt. | These are where out-of-bounds and null dereferences hide. |
| `reloco-safe-math` | **Opt-in.** Inside a function or class marked `RELOCO_SAFE_MATH`, raw integer `+ - * / % << >>`, `++`, `--`, unary `-` on non-constant operands are rejected; use `checked.hpp`, `saturating.hpp` or `wrapping.hpp`. Unmarked code is never checked. | Overflow is explicit where it matters. |
| `reloco-member-init` | Every field has a default member initializer (self-initialising class types excepted). | No indeterminate members. |
| `reloco-rule-of-five` | Declaring any of destructor, copy/move constructor, copy/move assignment means declaring all five (`= default`/`= delete`). A lone defaulted destructor is allowed. | Explicit ownership semantics. |
| `reloco-use-after-move` | A local or parameter passed to `std::move` may not be read again on any path (loops included) until it is reassigned or reinitialised (`x = ...`, `x.clear()`, `reset`, `assign`, `emplace`). Trivially copyable types are exempt (a move is a copy). Emits a note at the move. | Rust's "borrow of moved value". |
| `reloco-iterator-invalidation` | Inside a range-for, the iterated container may not be modified (non-const member other than `at`/`front`/`back`/`begin`/`end`/`find`/`size`..., assignment, or passing it to a mutable-reference parameter), unless the change is immediately followed by `break`/`return`. Iterators bound to variables are covered by `reloco-borrow-check`. | Hidden iterators dangle after reallocation. |
| `reloco-leak` | **Opt-in by type.** A local or by-value parameter of a `RELOCO_MUST_CONSUME` class must be consumed on every path that leaves the function: `std::move(x)`, `return x`, `&x` (escape) or a `RELOCO_CONSUMES` member. Reported at the exit, with a note at the declaration. | Linear resources (transactions, tokens) cannot be dropped silently. |
| `reloco-lock-guard` | In a class that owns a mutex (a `RELOCO_CAPABILITY` member), every other mutable member must be `RELOCO_GUARDED_BY(mutex)`, `RELOCO_UNGUARDED`, const, a reference, atomic or a synchronisation primitive. Fix-it appends `RELOCO_GUARDED_BY(mu)` when there is exactly one mutex. Enable `-Wthread-safety` so access to guarded members is then enforced by Clang. | Every shared field names its lock. |
| `reloco-narrowing-flow` | Explicit integer casts (`static_cast`, functional, C-style) must be provably lossless. A value-range analysis understands constants, `&` masks, `%`, `>>`, `/`, `+ - *` on known ranges, `?:` and small types; casts that may truncate or change sign are reported with the computed range. Same-width `char` to byte reinterpretation is allowed. Complements `reloco-implicit-conversion`, which stops at explicit casts. | Explicit casts are where truncation hides. |
| `reloco-send-sync` | Arguments passed to a `RELOCO_REQUIRE_SEND` parameter (thread/task spawn) must be Send; arguments to a `RELOCO_REQUIRE_SYNC` parameter must be Sync. Send is structural: scalars and aggregates of Send members; owners (`RELOCO_OWNER`) iff their template arguments are; raw pointers, references, views (`RELOCO_POINTER`), `RELOCO_NOT_SEND` types and lambdas capturing by reference or `this` are not. `RELOCO_SEND` overrides after an audit. Sync: `RELOCO_SYNC`, atomics, mutexes, all-const types, or a mutex owner. | Rust's `Send`/`Sync`. |
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

Slicing is deliberately not a separate rule: polymorphic bases are already a red flag.

### Annotations used by the flow checks

```cpp
#include <reloco/lifetime.hpp>

// reloco-leak: a transaction must be committed or handed over on every path.
// RELOCO_MUST_CONSUME marks the class; RELOCO_CONSUMES (first in the declaration) marks the
// member that releases it.
struct RELOCO_MUST_CONSUME txn { RELOCO_CONSUMES void commit() noexcept; };

// reloco-lock-guard: every mutable member next to a mutex names its lock (-Wthread-safety
// then rejects unlocked access). RELOCO_UNGUARDED documents a deliberate exception.
class counter {
  reloco::mutex mu_;
  int hits_ RELOCO_GUARDED_BY(mu_) = 0;
  int generation_ RELOCO_UNGUARDED = 0;  // written once before threads start
};

// reloco-send-sync: the parameter that crosses a thread boundary says so. The checker then
// verifies each argument at the call site; the lambda below captures by value, so it is Send.
template <typename F> void spawn(F fn RELOCO_REQUIRE_SEND) noexcept;
void start(int n) noexcept { spawn([n]() noexcept { (void)n; }); }
```

## Fix-its and "how to fix" notes

Every diagnostic that has a well-known remedy carries a `note: how to fix: ...`
line showing the concrete replacement. Mechanical remedies are also offered as
automatic fix-its, applied with `clang-tidy -fix`:

```sh
# Apply all automatic fixes, then review the diff before building.
clang-tidy-24 --load=libreloco-tidy.so --checks='-*,reloco-*' -fix src/foo.cpp -- -std=c++20 -Iinclude
```

| Check | Automatic fix |
|-------|---------------|
| `reloco-explicit-conversion` | inserts `explicit ` |
| `reloco-must-use-result` | inserts `[[nodiscard]] ` |
| `reloco-ref-qualified-member` | inserts ` &` after the cv-qualifiers |
| `reloco-noexcept` | inserts ` noexcept` after cv/ref qualifiers |
| `reloco-const-local` | inserts `const ` (single-declarator statements only) |
| `reloco-uninit-local` | appends ` = {}` |
| `reloco-implicit-conversion` | wraps the expression in `static_cast<T>(...)` |
| `reloco-member-init` | appends `{}` to the field |

Fix-its are skipped inside macro expansions. Fixes such as ` = {}`, `const`
and `static_cast` change semantics, so review the diff rather than applying it
blindly. All other checks (borrow, copy, out-param, C arrays, banned
constructs, ...) only emit the note, because the right fix depends on the
design.

## Exemptions

Every check honours these; each use should say why it is sound.

| Mechanism | Scope |
|---|---|
| `// NOLINT(reloco-xxx): reason`, `NOLINTNEXTLINE`, `NOLINTBEGIN/END` | line / block (standard clang-tidy) |
| `RELOCO_BEGIN_UNSAFE_CODE` / `RELOCO_END_UNSAFE_CODE` | region, all reloco checks |
| `RELOCO_UNSAFE_BUFFER_USAGE` function or a function named `unsafe_*` | the unsafe tier (raw pointers, casts, arrays tolerated) |
| `#pragma clang unsafe_buffer_usage begin/end`, `RELOCO_BEGIN/END_UNSAFE_BUFFER_USAGE` | region, **borrow check only** |
| `RELOCO_BORROW_EXEMPT` on a function or class | borrow check, e.g. `swap`/`exchange` primitives |
| `RELOCO_CHECKED` / `RELOCO_SAFE_MATH` | not exemptions: they declare a type is checked / opt code into strict math |
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
`RELOCO_BORROW_EXEMPT`, `RELOCO_CHECKED`, `RELOCO_SAFE_MATH`,
`RELOCO_MUST_CONSUME`, `RELOCO_CONSUMES`, `RELOCO_UNGUARDED`, `RELOCO_SEND`, `RELOCO_NOT_SEND`, `RELOCO_SYNC`,
`RELOCO_REQUIRE_SEND`, `RELOCO_REQUIRE_SYNC`, `RELOCO_UNSAFE_BUFFER_USAGE`, `RELOCO_BEGIN/END_SAFE_CODE`,
`RELOCO_BEGIN/END_UNSAFE_CODE`, `RELOCO_BEGIN/END_UNSAFE_BUFFER_USAGE`. All compile to nothing
where the compiler lacks the attribute. See also [lifetime safety](lifetime-safety.md).
