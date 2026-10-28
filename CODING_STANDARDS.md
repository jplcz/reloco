<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Safety coding standards

This is the single checklist of safety rules for code that uses **reloco**, **microfmt** or **structo**
(your own firmware, kernel modules, protocol parsers, libraries). It is compiled from:

* reloco: `docs/lifetime-safety.md`, `docs/container-contract.md`, `docs/hardened-containers.md`,
  `docs/extending.md`, `docs/safety-checker.md`, `docs/send-sync.md`, `docs/lock-discipline.md`,
  `docs/atomic-context.md`, `docs/secure-wipe.md`, `docs/fallible-construction.md`, `docs/relocatable.md`,
  `docs/kernel/checks.md`, `docs/per-cpu-slot.md`, `docs/tls-slot.md`
* microfmt: `docs/lifetime-safety.md`, `docs/hardened-containers.md`, `docs/extending.md`
* structo: `docs/coding-guide.md`

Each rule says **what** to do, **why** it exists (so you can judge edge cases) and **how** to follow it.
Rules marked *(enforced)* are checked by `reloco-tidy`, a Clang warning, or a script; the others are
review rules. microfmt and structo inherit these standards; they add only the items in
[section 14](#14-project-specific-additions). When a project document and this file disagree, the
project document wins, and this file should be fixed.

## Contents

1. [Core principles](#1-core-principles)
2. [Toolchain and build configuration](#2-toolchain-and-build-configuration)
3. [Access tiers: checked, `try_`, `unsafe_`](#3-access-tiers-checked-try_-unsafe_)
4. [Lifetime annotations and borrowing](#4-lifetime-annotations-and-borrowing)
5. [Rvalue protection](#5-rvalue-protection)
6. [Containers and the standard library](#6-containers-and-the-standard-library)
7. [Errors and construction](#7-errors-and-construction)
8. [Language-level rules](#8-language-level-rules-reloco-tidy)
9. [Ownership, moves and typestate](#9-ownership-moves-and-typestate)
10. [Concurrency and execution context](#10-concurrency-and-execution-context)
11. [Memory, secrets and hardware](#11-memory-secrets-and-hardware)
12. [Customisation points and type-erased handles](#12-customisation-points-and-type-erased-handles)
13. [Exemptions](#13-exemptions-and-how-to-use-them)
14. [Project-specific additions](#14-project-specific-additions)
15. [Pre-merge checklist](#15-pre-merge-checklist)

---

## 1. Core principles

1. **Safe is the default; unsafe is explicit and greppable.** Checked behaviour needs no opt-in. Every
   opt-out is a visible marker (`unsafe_*`, `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`, `NOLINT`,
   `RELOCO_BEGIN_UNSAFE_CODE`, `RELOCO_BORROW_EXEMPT`) that a reviewer can question.
2. **Failure is a value, not an exception and not undefined behaviour.** Fallible work returns
   `reloco::result<T>`; programming errors trap through `RELOCO_ASSERT`.
3. **Types carry the proof.** Prefer a type that cannot be misused (`value_ref`, `span`,
   `checked_value`) over a comment saying "must not be null".
4. **Annotations help; they never replace enforcement.** A lifetime or typestate macro expands to
   nothing on some compilers. Portable enforcement (deleted overloads, ref-qualifiers, runtime
   asserts) must exist underneath it.
5. **Never trade hardening for speed globally.** Use one `unsafe_*` call at a measured hot spot, not
   `RELOCO_DISABLE_ASSERT` for the whole program.

## 2. Toolchain and build configuration

| # | Rule | Why / how |
|---|---|---|
| 2.1 | **`reloco-tidy` must always be compiled with Clang, never GCC.** Use `-DCMAKE_CXX_COMPILER=clang++-N`, where `N` is the major version of the `clang-tidy-N` that will load it. `tools/tidy/CMakeLists.txt` refuses to configure with another compiler. | The plugin is loaded *into* `clang-tidy`, which is built from LLVM/Clang. It must share that binary's C++ ABI, RTTI and exception settings and link against the same LLVM symbols; a GCC build is at best unloadable and at worst crashes inside clang-tidy. |
| 2.2 | The plugin's LLVM major version must equal the `clang-tidy` executable's. Verified: 20, 21, 22, 24 (19 loses `annotate_type` checks). | A mismatch gives "unable to load plugin" or undefined symbols. |
| 2.3 | Build and test with **Clang** (the repo uses `clang++-24`) in addition to GCC. | Annotation correctness (`-Wdangling*`, `-Wconsumed`, `-Wunsafe-buffer-usage`, `-Wthread-safety`) only appears as Clang diagnostics, never as a GCC error. |
| 2.4 | Keep strict warnings on (`JPLCZ_*_ENABLE_STRICT_WARNINGS`). Strict Clang builds enable `-Wdangling`, `-Wdangling-gsl`, `-Wdangling-assignment-gsl`, `-Wdangling-field`, `-Wreturn-stack-address`, `-Wconsumed`, and `-Wlifetime-safety` where available, as errors. Enable `-Wthread-safety` once you use `RELOCO_GUARDED_BY`. *(enforced)* | These catch dangling borrows and lock misuse that the type system cannot. |
| 2.5 | Run `./scripts/check-unsafe-buffer-usage.sh` (Clang `-Wunsafe-buffer-usage`, C++17/20/23). It must report **0** diagnostics (`*_UNSAFE_BUFFER_MAX_WARNINGS` defaults to `0`; raise it only temporarily while migrating). *(enforced)* | Keeps every unwrapped `unsafe_*` call out of the tree. |
| 2.6 | Run `scripts/check-no-std-types.py --include-dir include` (and `reloco-banned-std-type`) on library headers. *(enforced)* | Keeps allocating/throwing `std::` facilities out of freestanding code. |
| 2.7 | Run the sanitizer build (`scripts/run-sanitizers.sh`) and the native CI script before merging. | Hardening detects contract violations; sanitizers find what it cannot see. |
| 2.8 | **Never fail the CMake configure because a compiler lacks a safety option.** Probe every safety/bounds/hardening flag (`-fbounds-safety`, `-Wunsafe-buffer-usage`, `-Wdangling*`, `-Wconsumed`, `-Wlifetime-safety`, `-fhardened`, `-ftrivial-auto-var-init`, ...) with `check_cxx_compiler_flag` and add it only when the probe succeeds. If the user *explicitly* requested the option and the compiler can't do it, emit `message(WARNING ...)` at most and continue without it. Never `message(FATAL_ERROR ...)` for an unsupported optional safety flag. | Older Clang, AppleClang, GCC and MSVC support different subsets (reloco's `CMakeLists.txt` already probes each flag this way). A missing diagnostic must not break an otherwise valid build. A hard error is reserved for something that cannot work at all, such as building the `reloco-tidy` plugin with a non-Clang compiler (rule 2.1). |
| 2.9 | Do not define `RELOCO_DISABLE_ASSERT` (or `RELOCO_DISABLE_ASSERT_STDIO` without a replacement handler) unless every checked precondition is proven. With asserts disabled a violated precondition is undefined behaviour. | It is a size/speed trade-off, not a recovery mode. |
| 2.10 | Install a custom assert handler (`reloco::set_assert_handler`) before a failure can occur when the target has no stderr (device log, crash record). | A trap with no record is undebuggable in the field. |
| 2.11 | Format C++ with `clang-format-24`. | Repository convention. |

```cmake
# Probe, add if supported, otherwise warn (only if the user asked for it) and go on. Never FATAL_ERROR.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wunsafe-buffer-usage" MYPROJ_HAS_UNSAFE_BUFFER_USAGE)
if(MYPROJ_HAS_UNSAFE_BUFFER_USAGE)
  target_compile_options(my_target PRIVATE -Wunsafe-buffer-usage)
elseif(MYPROJ_REQUIRE_BOUNDS_CHECKS)
  # The user asked for it explicitly: tell them it is unavailable, but keep configuring.
  message(WARNING "my_target: ${CMAKE_CXX_COMPILER_ID} lacks -Wunsafe-buffer-usage; skipping")
endif()
```

```bash
# Building the checker: Clang only, version matching clang-tidy-24.
cmake -S tools/tidy -B build-tidy -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=clang++-24
ninja -C build-tidy
clang-tidy-24 --load=build-tidy/libreloco-tidy.so --checks='-*,reloco-*' -p build src/foo.cpp
```

## 3. Access tiers: checked, `try_`, `unsafe_`

Any class that owns, indexes or guards data that can be invalid, absent or out of range must follow
the **tri-tier accessor convention** (see `container-contract.md` for the copy-paste template).

| Tier | Name | Behaviour on bad input | Use when |
|---|---|---|---|
| Checked (default) | `operator[]`, `front()`, `back()`, `value()`, `*`, `->`, `get()` | `RELOCO_ASSERT`: handler, then trap; **active in release** | Invalid input is a programming bug |
| Fallible | `try_at()`, `try_front()`, `try_substr()`, `try_push_back()` | Returns `reloco::result<T>` | Bounds depend on data you must handle |
| Unsafe | `unsafe_at()`, `unsafe_data()`, `unsafe_deref()` | `RELOCO_DEBUG_ASSERT` only (gone under `NDEBUG`) | Precondition already proven next to the call |

Rules:

* **3.1** The plain name is always the checked tier. Never make `operator[]` unchecked.
* **3.2** `unsafe_*` accessors use `RELOCO_DEBUG_ASSERT`, never `RELOCO_ASSERT`, and are marked
  `RELOCO_UNSAFE_BUFFER_USAGE`. *(enforced by `-Wunsafe-buffer-usage`)*
* **3.3** Every call to an `unsafe_*` function is wrapped in
  `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE` / `RELOCO_END_UNSAFE_BUFFER_USAGE` and keeps its precondition
  check adjacent. Never use an unsafe call just to avoid handling invalid input. At trust boundaries
  (network, files, other processes) use checked or `try_*`. *(`reloco-unsafe-call-scope`)*
* **3.4** Do not add a tier nobody uses yet. A class that guards access needs at least the checked tier;
  add `try_*`/`unsafe_*` when a real call site needs them. Storage-free resolvers with nothing to
  assert are exempt.
* **3.5** An unbounded, null-terminated pointer (`c_str()`-style) is **unsafe tier only**
  (`unsafe_c_str()`), with no checked or `try_*` twin.
* **3.6** `try_*` accessors that return a borrow use `result<std::reference_wrapper<T>>` (never
  `result<T &>`) or a raw pointer for `try_data()`.
* **3.7** `operator[]`, unary `*` and `->` must be `RELOCO_CHECKED` (they must actually check), and
  using such an operator on a non-`RELOCO_CHECKED` type is a violation. *(`reloco-checked-operator`)*
* **3.8** Signed indices are rejected unless proven non-negative: use `std::size_t`, a range-for, or
  check `i >= 0` first. *(`reloco-signed-index`)*
* **3.9** `unchecked_at()` / `unchecked_subspan()` (no check, no assumption, any build) are only for hot
  loops whose index range was proven once outside the loop. Otherwise use `operator[]`.
* **3.10** Raw pointer casts and address conversions (`reloco::unsafe::ptr_cast`,
  `reloco::unsafe::unchecked_address`) are allowed only after validating alignment, bounds,
  mutability and owner lifetime.
* **3.11** `expected::value()`/`error()` are checked: test `has_value()` first.
* **3.12** `reloco::optional<T>` is Rust-like: a moved-from optional is always empty. Never rely on
  `std::optional`'s "still engaged" behaviour.
* **3.13** Compute kernels: validate sizes once, outside the loop (always-on outer boundary), iterate
  proven domains (`domain`, `blocks`, `try_bind`), and never forge a `proven_index`. Run tests with
  `RELOCO_KERNEL_CHECKS=1`; choose one level per target; the result must not depend on the level.

```cpp
reloco::string_view input = receive_field();

// Fallible tier: truncated input is expected data, handle it.
if (auto payload = input.try_substr(header_size)) {
  decode(payload.value());
} else {
  report_truncated_field();
}

if (!input.empty()) {
  // Unsafe tier: emptiness was just checked on the line above, so the check would be redundant.
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  const char first = input.unsafe_front();
  RELOCO_END_UNSAFE_BUFFER_USAGE
  consume(first);
}
```

## 4. Lifetime annotations and borrowing

Macros live in `<reloco/lifetime.hpp>`. They expand to nothing when unsupported, so they must never be
the only protection (principle 4).

| # | Rule |
|---|---|
| 4.1 | Mark owning types `RELOCO_OWNER`; non-owning views, handles and reference wrappers `RELOCO_POINTER`. Classes holding borrowed storage must be `RELOCO_POINTER`. *(`reloco-pointer-annotation`)* |
| 4.2 | Every accessor returning a reference, pointer, iterator or view into `*this` (or an argument) carries `RELOCO_LIFETIMEBOUND`, in **all three tiers**. *(`reloco-lifetime-annotations`)* |
| 4.3 | A constructor or setter that **stores** a borrowed argument uses `RELOCO_LIFETIME_CAPTURE_BY_THIS` (or `RELOCO_LIFETIME_CAPTURE_BY(...)`). On constructors, `LIFETIMEBOUND` is equivalent; capture-by-this also fits void setters. |
| 4.4 | A parameter that is only read and copied is marked `RELOCO_COPIES`. Never put `RELOCO_LIFETIMEBOUND` on a copied parameter: callers passing temporaries then fail with `-Wdangling`. |
| 4.5 | Prefer `value_ref<T>` (non-null borrow of an lvalue, rejects temporaries) and `value_ptr<T>` (nullable borrow, checked `*`/`->`) over raw pointers. `value_ref<const T>` for read-only. The owner must outlive every copy; neither extends lifetime. |
| 4.6 | A `span`/`string_view`/`value_ptr`/`value_ref` member in a wrapper means the wrapper is a `RELOCO_POINTER` and its constructors/accessors propagate `RELOCO_LIFETIMEBOUND`. |
| 4.7 | `RELOCO_UNSAFE_BUFFER_USAGE` for low-level functions that cannot offer a safe alternative (raw memory management). Use the stronger macros (`RELOCO_ASSUME_ALIGNED`, `RELOCO_MUSTTAIL`, `RELOCO_MALLOC_PAIR`) only when their contract holds on every path. |
| 4.8 | Use named lifetimes (`RELOCO_LT(a)`, `RELOCO_LT_RETURN(a)`, `RELOCO_LT_THIS(a)`, `RELOCO_LT(static)`) when a result borrows only *some* arguments or a parameter must be static. Keep `RELOCO_LIFETIMEBOUND` too where Clang's `-Wdangling` should also fire. *(`reloco-lifetime-names`)* |
| 4.9 | Never return or store a pointer/view/array decay of a local through a pointer or non-const reference parameter. *(`reloco-escaping-local`)* |
| 4.10 | A call may not receive overlapping places when one is bound mutably (`f(x, x)`, `x.m(x)`). *(`reloco-aliased-arguments`)* |
| 4.11 | Do not mutate a place while a borrow of it is live (Rust's aliasing-XOR-mutability); inside a range-for do not modify the container. *(`reloco-borrow-check`, `reloco-iterator-invalidation`)* |
| 4.12 | Members are constructed in declaration order: never initialise a member from one declared later, and never let a pointer/view member into a sibling stay in a copyable/movable class (delete copy and move). *(`reloco-member-order`)* |
| 4.13 | `const char *` that only ever refers to literals/static storage is `RELOCO_STATIC_STR`; everything else is `reloco::string_view`. *(`reloco-static-string`)* |
| 4.14 | Foreign-address-space pointers (syscall arguments, guest memory) are `RELOCO_NS(tag)`: never dereference them, convert only in a `RELOCO_NS_TRANSLATOR` function after validating the range. *(`reloco-ns-pointer`)* |
| 4.15 | Code that destroys an object and then frees its storage is wrapped in `RELOCO_BEGIN_ALLOW_USE_AFTER_DESTROY` / `RELOCO_END_ALLOW_USE_AFTER_DESTROY` to silence `-Wlifetime-safety-invalidation` false positives. |

```cpp
class RELOCO_POINTER packet_view {  // non-owning handle: tagged so Clang tracks it
public:
  // 'source' is stored in the object: the view may not outlive it.
  explicit packet_view(reloco::span<const std::byte> source RELOCO_LIFETIMEBOUND
                           RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept;

  // The returned pointer borrows from *this's storage; the '&' blocks calls on temporaries.
  reloco::value_ptr<const std::byte> data() const & noexcept RELOCO_LIFETIMEBOUND;
  reloco::value_ptr<const std::byte> data() const && = delete;
};
```

## 5. Rvalue protection

Why: `make_container().data()` returns a pointer into an object that dies at the semicolon.

* **5.1** Put `RELOCO_BLOCK_RVALUE_ACCESS(ElementType);` as the first line after `public:` of every
  container or view. It deletes the `&&` overloads of `operator[]`, `front`/`back`,
  `at`/`try_at`/`unsafe_at`, `data`/`try_data`/`unsafe_data`, `begin`/`end`/`rbegin`/`rend`/`c*`,
  `operator*`/`operator->`/`get`/`unsafe_get`.
* **5.2** Every other aliasing accessor (`view()`, `as_span()`, `unsafe_c_str()`, `base()`, bespoke
  `try_substr()`) is ref-qualified `&`/`const &` and gets an explicit `... const && = delete;`.
  Non-static members that return an alias of the object must be `&`/`const &` qualified, or have an
  explicit `&&` overload. *(`reloco-ref-qualified-member`)*
* **5.3** Exempt: by-value handles/proxies (`RELOCO_POINTER`), `operator()`, unary `*`/`->` handled via
  the macro.
* **5.4** `.ref()`-style methods on owning wrappers are `&`-qualified and `RELOCO_LIFETIMEBOUND` so a
  handle to a temporary does not compile.
* **5.5** Constructors from owning types reject temporaries (`string_view` from a temporary `string`,
  `value_ref` from an rvalue).

## 6. Containers and the standard library

| # | Rule |
|---|---|
| 6.1 | Before using any STL facility, check what reloco/microfmt/structo already offer (for example `reloco::call_location` instead of `std::source_location`). |
| 6.2 | Use the hardened types by default in firmware, kernels, interrupt handlers, crash diagnostics and parsers: `reloco::array`, `span`, `string_view`, `string`, `vector`, `inline_vector`, `external_vector`, `sso_vector`, `flat_set`/`flat_map`, `flat_hash_map`, `tree_map`, `optional`, `unique_ptr`, `shared_ptr`, `expected`. |
| 6.3 | No `std::` containers, `string`, `optional`, `variant`, `function`, smart pointers, exceptions types or iostreams in main code, examples or tests. Plain value types, fixed-width integers, `std::pair`/`std::tuple` of trivial types, algorithms and type-traits headers are fine. *(`reloco-banned-std-type`, `check-no-std-types.py`)* |
| 6.4 | Deliberate interop with the standard library is allowed only at the boundary and must be marked: `// std-interop-ok[: reason]` (line), `// std-interop-begin` / `// std-interop-end` (block), `// std-interop-file` (file). Convert to a hardened view at the boundary and keep the sensitive logic on hardened types. |
| 6.5 | Why no `std::`: (a) allocation-free by default, since there is often no allocator at boot or in trap context; (b) no hidden libc dependency from `assert()`/`abort()` in `at()` or hardened modes. |
| 6.6 | Pick the container by capacity/ownership model: fixed capacity without allocation (`inline_vector`), caller-owned storage (`external_vector`, `outline_vector`), growable with fallible allocation (`vector`). Prefer caller-owned fixed storage plus `span` in bounded or kernel-mode paths. |
| 6.7 | Node-based `std::map` has no replacement; use only where allocation, failure behaviour and execution context are explicitly acceptable. |
| 6.8 | Avoid raw pointers in new code; use `span`, `value_ptr`, `value_ref`, integer addresses or reloco types. Raw pointers are allowed only inside `RELOCO_POINTER`/`RELOCO_OWNER` types, the unsafe tier, vtables, function pointers and `extern "C"`. *(`reloco-no-raw-pointer`)* Owning allocation links, static vtables and public C-ABI callback fields legitimately stay raw. |
| 6.9 | No raw C arrays; use `reloco::array`, `inline_vector` or a span. *(`reloco-no-c-array`)* |
| 6.10 | When adding a container, follow the `container-contract.md` checklist: `RELOCO_OWNER`/`RELOCO_POINTER`, `RELOCO_BLOCK_RVALUE_ACCESS` first, `RELOCO_LIFETIMEBOUND` on all tiers, explicit deletion of unnamed borrow accessors, unsafe-tier gating, consumed-state annotations for use-after-move hazards, and `RELOCO_BEGIN/END_UNSAFE_BUFFER_USAGE` around the raw-pointer implementation body. Validate with `check-unsafe-buffer-usage.sh` and a Clang build. |
| 6.11 | `is_trivially_relocatable`: opt a type in only if its bytes can be copied elsewhere with the source abandoned (no self-pointer, no external registration). `basic_sso_string` is *not* relocatable because `data()` points into itself. |
| 6.12 | Over-aligned element types: `vector`/`array`/`inline_vector` apply `alignment_of<T>`; pass `data()` to SIMD code only through those. |

## 7. Errors and construction

| # | Rule |
|---|---|
| 7.1 | Every function and lambda is `noexcept`. No `throw`/`try`/`catch`. *(`reloco-noexcept`, `reloco-banned-construct`)* |
| 7.2 | Failure is `reloco::result<T>` with `reloco::error`; no type defines its own scoped error enum. |
| 7.3 | Functions returning `expected`/`result`/`error`/`status`/`optional` are `[[nodiscard]]`; a discarded result is an error (cast to `void` with a reason). *(`reloco-must-use-result`, `reloco-unused-result`)* |
| 7.4 | **Never put fallible work in a constructor.** Expose `T::try_create(Args...)`, `T::try_allocate(allocator_ref, Args...)`, `storage->try_construct(Args...)`, `source.try_clone(...)` or `T::try_clone_at(...)`. Implement one of `try_create`/`try_allocate` shape, never both. |
| 7.5 | Every growing/mutating operation that can fail is `try_*` returning `result` (`try_reserve`, `try_push_back`, `try_insert_at`, ...). Views convert to owners via fallible copies (`try_to_string()`, `try_to_vector()`). |
| 7.6 | `vector<T>::try_create(cap)` only reserves; `data()` asserts on an empty vector. Use `try_create_sized(n)` for `n` value-initialised elements, and check `n == 0` first. |
| 7.7 | Optional trait operations are omitted entirely (not stubbed to fail), so "unsupported" (`can_x() == false`) differs from "failed this time". |
| 7.8 | An `out` parameter is marked `RELOCO_OUT` and written on every returning path before being read. *(`reloco-out-param`)* |
| 7.9 | Allocator implementations that report a block larger than requested must read `allocator-capacity-absorption.md` first: containers absorb and round-trip that size. |

## 8. Language-level rules (`reloco-tidy`)

Full rationale, examples and fix-its for every check are in `docs/safety-checker.md`. Summary:

| Area | Rules |
|---|---|
| Initialisation | Initialise locals at declaration (`reloco-uninit-local`); every field has a default member initialiser (`reloco-member-init`); never leave indeterminate values. Branch-dependent init is caught by `-Wsometimes-uninitialized` / `-Wconditional-uninitialized`. |
| Immutability | Locals never mutated are `const` (`reloco-const-local`); no local shadows an enclosing local/parameter (`reloco-no-shadow`); `mutable` members must be atomic, lock, `once_flag`, `cell` or `RELOCO_GUARDED_BY` (`reloco-mutable-member`). |
| Conversions | No implicit narrowing/sign change (`reloco-implicit-conversion`); explicit integer casts must be provably lossless (`reloco-narrowing-flow`); single-argument constructors and conversion operators are `explicit` (`reloco-explicit-conversion`); no pointer/integer/float used as a condition, compare explicitly (`reloco-implicit-bool`). |
| Banned constructs | `throw`/`try`/`catch`, `new`/`delete`, `const_cast`, `reinterpret_cast`, `dynamic_cast`, C-style casts, C string/memory/stdlib functions such as `memcpy`/`strlen`/`malloc` (`reloco-banned-construct`); `goto`, `setjmp`/`longjmp`, `union`, `volatile` (`reloco-banned-low-level`; MMIO needs `NOLINT` with a reason). |
| Copies and moves | Classes are move-only unless `RELOCO_COPYABLE`; implicit copies are flagged, explicit copies (`T(x)`) are fine (`reloco-implicit-copy`); no read after `std::move` until reassigned (`reloco-use-after-move`); no `std::move` of a const non-trivial object (`reloco-move-const`); polymorphic classes are not copy-constructed from derived objects (`reloco-slicing`). |
| Special members | Declare all five or none: destructor, copy/move constructor, copy/move assignment, as `= default`/`= delete` (`reloco-rule-of-five`). A lone defaulted destructor is allowed. |
| Switches | A `switch` over an enum lists every enumerator even if it has a `default` (`reloco-enum-switch`); a `switch` over a non-enum must have a `default`. |
| Globals | No mutable namespace-scope, static-member or function-static variables except atomic/mutex/lock types (`reloco-mutable-global`). |
| Virtuals | No unqualified virtual call in constructors/destructors (`reloco-virtual-call-in-ctor`); no default arguments on virtual functions (`reloco-virtual-default-argument`). |
| Lambdas | List captures explicitly; `[=]` and `[&]` are flagged (`reloco-lambda-default-capture`). |
| Arithmetic | Inside `RELOCO_SAFE_MATH` functions/classes raw integer `+ - * / % << >> ++ -- unary-` on non-constants is rejected; use `checked.hpp`, `saturating.hpp` or `wrapping.hpp` (`reloco-safe-math`, opt-in). Use it for sizes, offsets and lengths parsed from untrusted input. |
| Early start | `RELOCO_EARLY_STATIC` types used before the C++ runtime starts have `constexpr` constructors, trivial destructors and constant-initialised objects (`reloco-early-static`). |

Opt-in checks only run on code you mark: `RELOCO_SAFE_MATH`, `RELOCO_MUST_CONSUME`, `RELOCO_NS`,
`RELOCO_STATIC_STR`, `RELOCO_EARLY_STATIC`, `RELOCO_LT*`. Mark your own types where the hazard exists.

## 9. Ownership, moves and typestate

* **9.1** Use `reloco::checked_value<T>` whenever "a moved-from value must not be used again" is an
  invariant. Moved-from access traps at runtime on every compiler and warns statically on Clang
  (`-Wconsumed`). `checked_value<T *>` also null-checks `*`/`->`; spell the pointee type
  (`checked_value<widget *> p(nullptr)`), because a bare `nullptr` is rejected.
* **9.2** For your own types with a one-way terminal transition (writer with `end()`, transaction with
  `commit()`, single-use token), add consumed-state annotations:
  1. `RELOCO_CONSUMABLE(unconsumed)` on the class;
  2. `RELOCO_RETURN_TYPESTATE(unconsumed)` on **every** constructor that yields a usable object,
     including move constructors (the most common mistake);
  3. `RELOCO_CALLABLE_WHEN("unconsumed")` (quoted strings, unlike the other macros) on every method
     that must not run after the transition;
  4. `RELOCO_SET_TYPESTATE(consumed)` on the terminal method; list every legal state
     (`"unconsumed", "consumed"`) when it is idempotent or called from a destructor;
  5. keep a **runtime** guard (`committed_`) behind every state change: the attributes are
     diagnostics, not enforcement (GCC ignores them);
  6. add an `as_known()` escape hatch only for callback/reference boundaries where Clang sees
     `unknown`; it asserts the real invariant and is never a way to silence a genuine reuse bug;
  7. verify with `clang++ -Wconsumed -fsyntax-only` (one test that must warn, one that stays silent).
* **9.3** Do not use consumed-state for resettable/reusable objects, for transitions that depend on
  data the compiler cannot see, or for types only reached through type-erased dispatch; use a runtime
  check there. Annotate the element type, not a container of elements.
* **9.4** Linear resources (`RELOCO_MUST_CONSUME` types) must be consumed on every path: `std::move`,
  `return`, escape via `&x`, or a `RELOCO_CONSUMES` member. *(`reloco-leak`)*
* **9.5** Intrusive/shared ownership: `unsafe_adopt`/`unsafe_retain`/`unsafe_release` are unsafe tier
  and must be paired; a leaked `release` is a leaked object.

## 10. Concurrency and execution context

| # | Rule |
|---|---|
| 10.1 | **Send/Sync.** Specialise `reloco::is_send<T>` / `is_sync<T>` to `false` for types with non-atomic shared state or unsynchronised interior mutability. Never leave `is_sync` at the default `true` if a `const` operation mutates unsynchronised state. Types that must be explicitly classified inherit `detail::requires_explicit_send_sync`. |
| 10.2 | `is_send` / `is_sync` do not compose automatically: `vector<rc<T>>` still reads as Send. `static_assert` at every API boundary that crosses a thread (as `guarded_mutex<T>` does with `is_send_v<T>`). |
| 10.3 | Thread/task spawn parameters carry `RELOCO_REQUIRE_SEND` (or `RELOCO_REQUIRE_SYNC`); arguments must then be Send/Sync: no raw pointers, references, views, `RELOCO_NOT_SEND` types, or lambdas capturing by reference or `this`. `RELOCO_SEND` overrides only after an audit. Scoped threads that are joined before borrowed data dies are the deliberate exception. *(`reloco-send-sync`)* |
| 10.4 | **Locks.** Release every `lock`/`lock_shared`/`write_lock`/`read_lock` on every path; no double lock/unlock; prefer RAII guards (`scoped_lock()`), which are never tracked. A function may return holding a lock only if it is a lock implementation (`RELOCO_LOCK_IMPL`). *(`reloco-lock-balance`)* |
| 10.5 | No pointers to locks, no `&lock`; pass `Lock &`. Legacy C lock APIs use `RELOCO_LEGACY_LOCK` and `RELOCO_ACQUIRES(n)`/`RELOCO_RELEASES(n)` shims while migrating. *(`reloco-lock-alias`)* |
| 10.6 | In a class owning a mutex (`RELOCO_CAPABILITY` member) every other mutable member is `RELOCO_GUARDED_BY(mu)`, `RELOCO_UNGUARDED` (with a comment), const, a reference, atomic or a sync primitive. Enable `-Wthread-safety` and use `RELOCO_REQUIRES` for cross-function contracts. *(`reloco-lock-guard`)* |
| 10.7 | **Atomic context.** Never sleep inside atomic context (IRQ/preemption off, spinlock held). Annotate: `RELOCO_MIGHT_SLEEP` (individual members, not whole mutex classes), `RELOCO_ATOMIC_ONLY`, `RELOCO_ATOMIC_ENTRY` (interrupt handlers), `RELOCO_ATOMIC_SCOPE` (guards), `RELOCO_ATOMIC_LOCK`, `RELOCO_ATOMIC_TOKEN` (proof parameter), `RELOCO_ATOMIC_CALLBACK`. *(`reloco-atomic-context`)* |
| 10.8 | Condition waits and futex-style waits are always re-checked in a loop; never assume a wakeup means the condition holds. |
| 10.9 | Per-CPU data: no allocator, zero is a valid empty state, never destroyed, accessed only in atomic context so the CPU cannot change under the reference. |
| 10.10 | Thread-local cells (`tls_slot`): zero is valid/empty, built lazily and fallibly, destroyed by the owning block; closures must not return references or pointers to the cell. |
| 10.11 | Fault-injection hooks are test-only: keep them disabled in production builds and never let a `fault_injector` outlive its hook or be returned by value. |

```cpp
// One atomic level: while irq_off lives, sleeping calls are reported by reloco-tidy.
RELOCO_MIGHT_SLEEP void wait_for_io();            // may block
struct RELOCO_ATOMIC_SCOPE irq_off { irq_off(); ~irq_off(); };  // live object = atomic context

void f() {
  irq_off guard;     // atomic from here...
  // wait_for_io();  // ...error: sleeping in atomic context
}
```

## 11. Memory, secrets and hardware

* **11.1** Secrets (keys, passwords, plaintext) are wiped with `reloco::secure_wipe` or held in
  `reloco::secure_vector<T>`. A plain `memset` before free is a dead store the optimiser removes.
* **11.2** `secure_wipe` rules: live, writable, non-const storage only; trivially copyable objects only;
  the wiped object is dead afterwards (never read it back). It erases that storage only, not copies in
  registers, spill slots or by-value arguments: keep secrets in few, short-lived objects, and consider
  `-fzero-call-used-regs` and stack clearing.
* **11.3** `secure_vector<T>` elements are trivially copyable and destructible, never `std::string`.
  There is no implicit conversion to/from `vector<T>`; the only exit is the explicit, greppable
  `unwrap_unwiped()`, which gives up the guarantee.
* **11.4** Struct holding secrets: wipe plain fields in the destructor body; members like
  `secure_vector` wipe themselves.
* **11.5** Never implement crypto algorithms (hashes, signatures, ciphers) by hand; use a crypto
  library, preferring the platform's native one.
* **11.6** Replace the wipe primitive for ports that need it (`RELOCO_SECURE_WIPE_IMPL`, defined before
  the first reloco include and identically in every translation unit, otherwise ODR violation).
* **11.7** Raw memory management (`allocator_ref::allocate`, `deallocate`, `reallocate`, ...) is
  `RELOCO_UNSAFE_BUFFER_USAGE`: call it only inside an unsafe-buffer region. Bump allocators such as
  microfmt's `scratch_allocator` do not run destructors: only trivially destructible types, check every
  returned pointer, keep the backing buffer alive, never share one between concurrent operations.
* **11.8** Byte cursors (`byte_cursor`/endian helpers): the buffer outlives the cursor and every span it
  returns; use `try_*` reads for untrusted data, and `unsafe_read`/`unsafe_load` only after verifying
  `remaining()`.

## 12. Customisation points and type-erased handles

* **12.1** Plug-in backends use the **tag + `*_traits<Tag>` + `context_type`** pattern (`extending.md`):
  an empty tag, a traits specialisation with `context_type` and static operations, a context held by
  value, and a type-erased `*_ref` that borrows it. No virtual interfaces, no allocation, no runtime
  registration. Stateless backends declare `context_type = void` and drop the context parameter.
* **12.2** The context (or its owning wrapper) must outlive every `*_ref` derived from it: type erasure
  never transfers ownership.
* **12.3** Mutating operations take `value_ref<context_type>`, read-only ones
  `value_ref<const context_type>`; optional state in an erased handle uses `value_ptr`.
* **12.4** A `*_ref` is a two-word handle (`void *ctx` + `const vtable *`) with a `RELOCO_LIFETIMEBOUND`
  constructor, `noexcept` vtable entries, and every public method forwarding through the table,
  returning `error::unsupported_operation` when unbound. Generic conveniences are ordinary
  non-template code compiled once, not once per backend.
* **12.5** A trait/policy template parameter the caller implements must come with a complete, minimal,
  compilable example implementation in the header's doc comment (or a declarations-only archetype).
* **12.6** `reloco_user_config.hpp` contains only `#define`s, never `#include <reloco/...>`. Put
  overrides (for example a custom default allocator) in an ordinary header guarded by an opt-out macro.

## 13. Exemptions and how to use them

Every exemption must say **why it is sound**. A bare suppression is a review failure.

| Mechanism | Scope |
|---|---|
| `// NOLINT(reloco-xxx): reason`, `NOLINTNEXTLINE`, `NOLINTBEGIN/END` | Line / block |
| `RELOCO_BEGIN_UNSAFE_CODE` / `RELOCO_END_UNSAFE_CODE` | Region, all reloco checks |
| `RELOCO_UNSAFE_BUFFER_USAGE` on a function, or a function named `unsafe_*` | The unsafe tier |
| `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE` / `RELOCO_END_UNSAFE_BUFFER_USAGE` | Region; Clang `-Wunsafe-buffer-usage` and the borrow check only |
| `RELOCO_BORROW_EXEMPT` (comment the reason above it) | Borrow check, on a function, class or field |
| `RELOCO_ATOMIC_EXEMPT`, `RELOCO_LOCK_IMPL`, `RELOCO_LEGACY_LOCK` | Atomic-context / lock checks |
| `std-interop-*` comment markers | Banned `std::` types |
| `ExemptPaths` option | Default: tests, examples, demos, benchmarks, `_deps`, third-party and build trees |

Keep unsafe regions as narrow as possible and put a checked public API around them. For gradual
adoption in a code base with third-party sources, set `SafeCodeMode: opt-in` and bracket your own code
with `RELOCO_BEGIN_SAFE_CODE` / `RELOCO_END_SAFE_CODE`; the `strict` profile is the target.

## 14. Project-specific additions

* **reloco itself.** `tests/compile_all_headers.cpp` must report **0** findings for every `reloco-*`
  check; any new finding is a bug or needs a reasoned suppression. Add a `tools/tidy/test` fixture for
  each new check.
* **microfmt.** Re-exports reloco's `value_ref`, `value_ptr`, `checked_value`, hardened containers and
  the `RELOCO_*` macros under `microfmt::`; use the `RELOCO_*` macros directly, not a private alias.
  Writers (`json::object_writer`, `cbor::map_writer`, ...) use consumed-state annotations and
  `as_known()` at callback boundaries. Keep a sink adapter and its backing buffer alive while using the
  type-erased `sink`. Inspector views never own scratch spans or contexts; those must outlive the view
  and everything derived from it. Escalate with `MICROFMT_UNSAFE_BUFFER_MAX_WARNINGS=0`.
* **structo.** Follows reloco's standards rather than defining parallel ones (`docs/coding-guide.md`).
  `include/structo/**`, `examples/**` and tests use `reloco::` containers instead of `std::` ones. Any
  class that guards access to data that can be invalid or absent follows the tri-tier convention
  (storage-free resolvers like `per_cpu_ptr` are exempt). Runtime-polymorphic `*_ref` handles use one
  `vtable` of `noexcept` function pointers resolved once per backend (`template <typename Backend>
  static constexpr vtable s_vtbl`) and fall back to `error::unsupported_operation` when unbound; follow
  `hw/uart_ref.hpp`. New tests prefer `TEST_F`/`TEST_P`. Kernel-level guards (`irq_guard`,
  `preemption_guard`, spinlock guards) carry the atomic-context annotations.

## 15. Pre-merge checklist

- [ ] New classes carry `RELOCO_OWNER`/`RELOCO_POINTER`, and every borrow-returning accessor has
      `RELOCO_LIFETIMEBOUND` plus `&`/`const &` qualification or `RELOCO_BLOCK_RVALUE_ACCESS`.
- [ ] Constructors/setters that retain a borrow use `RELOCO_LIFETIME_CAPTURE_BY_THIS`.
- [ ] Access follows checked / `try_*` / `unsafe_*`; every unsafe call sits inside an unsafe-buffer
      region with its precondition adjacent.
- [ ] No raw pointers, C arrays, `std::` containers, exceptions, `new`/`delete` or banned C functions
      outside marked, justified regions.
- [ ] Fallible operations return `result<T>`; construction that can fail uses `try_create`-style
      factories; results are `[[nodiscard]]`.
- [ ] Use-after-move hazards have `checked_value` or consumed-state annotations **plus** a runtime guard.
- [ ] Shared state is classified (`is_send`/`is_sync`), locks are balanced and `RELOCO_GUARDED_BY`,
      sleeping is never reachable from atomic context.
- [ ] Secrets are wiped (`secure_wipe`/`secure_vector`); no hand-written crypto.
- [ ] `reloco-tidy` (built with **Clang**, matching `clang-tidy-N`) reports no findings; every
      `NOLINT`/exemption has a reason.
- [ ] Clang build with strict warnings is clean; `check-unsafe-buffer-usage.sh` reports 0;
      `check-no-std-types.py` passes; sanitizer run is clean.
- [ ] CMake safety/bounds options are probed and **ignored with at most a warning** when the compiler
      lacks them; nothing fails to configure for a missing optional safety flag.
- [ ] A guide document with API description and commented examples exists for every new header.
