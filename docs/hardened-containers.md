<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Hardened containers and views

`reloco` provides small C++17-compatible containers and views for code that
cannot rely on exceptions, allocation, or build-mode-dependent safety. The
core types include:

| Type | Purpose |
|---|---|
| `reloco::array<T, N>` | Fixed-size owning array with hardened element access |
| `reloco::string_view` | Non-owning character view with checked and non-trapping access |
| `reloco::span<T>` | Non-owning contiguous range over contiguous storage |
| `reloco::string` | Allocator-backed, growable character buffer with fallible construction |
| `reloco::vector<T>` | Allocator-backed, growable dynamic array with fallible construction |
| `reloco::unique_ptr<T>` | Move-only, allocator-backed smart pointer with fallible construction |
| `reloco::shared_ptr<T>` / `reloco::weak_ptr<T>` | Reference-counted, allocator-backed smart pointer with fallible construction |
| `reloco::expected<T, E>` | Allocation-free value-or-error result |

These types provide familiar standard-library-style APIs while keeping the
reloco surface available in C++17. The non-owning view types interoperate
with their standard-library equivalents when the toolchain provides them.

## Prefer hardened types in low-level code

Use the hardened reloco type by default when writing firmware, kernel-mode
components, interrupt handlers, crash diagnostics, protocol parsers, and
other code where an unchecked access or dangling borrow is a security issue.

| Instead of | Prefer | When |
|---|---|---|
| `std::array<T, N>` | `reloco::array<T, N>` | Fixed-size owned storage |
| `std::span<T>` | `reloco::span<T>` | Borrowed contiguous storage |
| `std::string_view` | `reloco::string_view` | Borrowed character data |
| `std::string` | `reloco::string` | Allocator-backed, growable, fallible character storage |
| `std::vector<T>` | `reloco::vector<T>` | Allocator-backed, growable, fallible dynamic array |
| `std::unique_ptr<T>` | `reloco::unique_ptr<T>` | Allocator-backed, fallible single-object ownership |
| `std::shared_ptr<T>` / `std::weak_ptr<T>` | `reloco::shared_ptr<T>` / `reloco::weak_ptr<T>` | Allocator-backed, fallible shared object ownership |
| `std::expected<T, E>` | `reloco::expected<T, E>` | Allocation-free fallible results |

This is a project default, not a ban on the standard library. Keep standard
types when required by a platform API, third-party library, ABI, or generic
ecosystem interface. Convert to a hardened view at the boundary and keep the
security-sensitive implementation on reloco types.

`reloco::string` and `reloco::vector<T>` are the dynamic owning containers
reloco provides: an allocator-backed, growable character buffer and dynamic
array, respectively, whose construction and every mutation that can fail
(`try_reserve`, `try_append`/`try_push_back`, `try_insert`/`try_insert_at`,
...) returns `reloco::result<T>` instead of throwing. Both compose with the
fallible-construction protocol (see `docs/fallible-construction.md`), so
`reloco::unique_ptr<reloco::string>::try_create(...)`,
`reloco::unique_ptr<reloco::vector<T>>::try_create(...)`, and similar work
out of the box. `std::map` and other associative containers still have no
direct reloco replacement (see `reloco::mutable_container_ref`/
`container_ref_std.hpp` for opt-in, type-erased mutation of `std::map`
itself); use them only where allocation, failure behavior, and execution
context are explicitly acceptable, and prefer caller-owned fixed storage
plus `reloco::span` in bounded or kernel-mode paths.

```cpp
void parse_packet(std::span<const std::byte> platform_input) {
  reloco::span<const std::byte> input = platform_input;
  // Keep checked parsing on the hardened view from this point onward.
}
```

## Security is enabled by default

Unlike conventional standard-library containers, where unchecked element
access is commonly the default, reloco makes checked behavior the default
and requires an explicit opt-out. Checked reloco operations use
`RELOCO_ASSERT`. These checks remain active in release builds and are not
removed merely because `NDEBUG` is defined. Invalid checked access calls the
configured assertion handler and then traps.

This makes hardening opt-out rather than opt-in: applications get checked
behavior by default and must explicitly define `RELOCO_DISABLE_ASSERT` to
remove it. Do this only after proving that every checked precondition is
satisfied. With assertions disabled, violating a precondition is undefined
behavior; the macro is a performance and code-size tradeoff, not an error
recovery mode.

```cmake
target_compile_definitions(firmware PRIVATE RELOCO_DISABLE_ASSERT=1)
```

The opt-out applies globally to checked reloco operations in that target.
Prefer selecting an `unsafe_*` operation at a measured hot call site instead
of disabling hardening for the entire program.

## Choose an access tier

`reloco::string_view` exposes three access styles:

| Tier | Examples | Behavior on invalid input |
|---|---|---|
| Checked | `operator[]`, `front()`, `back()`, `substr()` | Assertion handler, then trap |
| Non-trapping | `try_at()`, `try_front()`, `try_back()`, `try_substr()` | Returns `reloco::result` (`reloco::expected<T, reloco::error>`) |
| Explicitly unsafe | `unsafe_front()`, `unsafe_back()`, `unsafe_substr()` | Debug assertion only; caller owns the precondition |

Use checked operations when invalid input indicates a programming defect. Use
`try_*` operations for data-dependent bounds or empty-input cases that the
application expects to handle.

```cpp
reloco::string_view input = receive_field();

if (auto first = input.try_front()) {
  consume(first.value().get());
} else {
  report_empty_field();
}

if (auto payload = input.try_substr(header_size)) {
  decode(payload.value());
} else {
  report_truncated_field();
}
```

`reloco::expected<T, E>::value()` and `error()` are also checked. Test the
result with `has_value()` or its boolean conversion before accessing the
active alternative.

`reloco::span<T>` follows the same model:

| Tier | Examples |
|---|---|
| Checked | `operator[]`, `front()`, `back()`, `subspan()`, `first()`, `last()` |
| Non-trapping | `try_at()`, `try_front()`, `try_back()`, `try_subspan()`, `try_first()`, `try_last()` |
| Explicitly unsafe | `unsafe_at()`, `unsafe_front()`, `unsafe_back()`, `unsafe_subspan()`, `unsafe_first()`, `unsafe_last()` |

Fallible span operations return `reloco::result` (`reloco::expected<T, reloco::error>`).
`as_bytes()` creates a read-only byte view without copying the represented
storage.

`reloco::array<T, N>` provides checked `operator[]`, fallible `try_at()`,
explicit `unsafe_at()`, hardened iterators and data access, `as_span()`,
compile-time `static_subspan<Offset, Count>()`, `fill()`, `swap()`, and
allocation-free `map()`. It supports aggregate initialization, structured
bindings, and zero-length arrays. Borrowing accessors, including `get<I>()`,
are rejected on temporary arrays. Tuple traits are provided for structured
bindings; use ADL `get` rather than expecting `std::get` or `std::apply`
interoperability.

## Debug checks and unsafe operations

`RELOCO_DEBUG_ASSERT` protects lower-level operations whose contracts are
intended to be established by nearby code. It is active in debug builds and
when `RELOCO_DEBUG` is defined, but it is disabled by `NDEBUG` otherwise.

Only operations explicitly named `unsafe_*` use this debug-check tier.
Checked `reloco::span<T>` indexing and subviews remain hardened in release
builds. Both `reloco::span<T>` and `reloco::string_view` name their fast
paths with an `unsafe_*` prefix so security-sensitive call sites remain
visible in review.

Every `unsafe_*` method across the library (`array`, `span`, `string_view`,
`value_ptr`, and `checked_value`) is additionally marked
`RELOCO_UNSAFE_BUFFER_USAGE` (see `reloco/lifetime.hpp`). Under Clang's
`-Wunsafe-buffer-usage`, any unwrapped call site is a compiler diagnostic —
callers must wrap the call in
`RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE` to
make the opt-out explicit and greppable, not just documented in a comment:

```cpp
if (!field.empty()) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  const char first = field.unsafe_front();
  RELOCO_END_UNSAFE_BUFFER_USAGE
  consume(first);
}
```

Do not use an unsafe operation solely to avoid handling invalid input. Keep
the precondition adjacent to the call and prefer a checked or `try_*` API at
trust boundaries.

## Dangling-reference prevention

`reloco::expected` deletes dereference and pointer-style accessors on
temporary result objects where the returned value could dangle after the full
expression. `reloco::span<T>` similarly rejects indexing, pointer access,
and iterator access on temporary span objects. `reloco::string_view` also
rejects construction from a temporary owning string.

```cpp
std::string storage = load_name();
reloco::string_view safe = storage;

// Rejected: the owning string would be destroyed immediately.
// reloco::string_view dangling = load_name();
```

The owner must still outlive every non-owning `reloco::string_view` or
`reloco::span<T>`. Hardening detects API contract violations; it cannot
extend the lifetime of referenced storage.

For non-owning references to individual objects, and for guidance on compiler
annotations, see [Lifetime safety and `value_ref`](lifetime-safety.md).

## Configure assertion reporting

The default assertion handler writes the failed expression, source location,
and message to standard error before trapping. Replace it when diagnostics
must go to a device log, crash record, or platform-specific transport:

```cpp
void assertion_log(const char *expression, const char *file, int line,
                   const char *message) {
  write_crash_record(expression, file, line, message);
}

reloco::set_assert_handler(assertion_log);
```

Define `RELOCO_DISABLE_ASSERT_STDIO` to suppress the default standard-error
dependency while preserving checks and traps:

```cmake
target_compile_definitions(firmware PRIVATE RELOCO_DISABLE_ASSERT_STDIO=1)
```

Install a custom handler before a failure can occur if the default handler is
disabled and the platform requires a persistent diagnostic record.
