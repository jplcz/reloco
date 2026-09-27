<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Experimental reflection support (P2996 / `-freflection`)

reloco has optional, fully opt-in support for
[P2996 "Reflection for C++26"](https://wg21.link/p2996), currently
implemented experimentally by GCC trunk (16+) behind the `-freflection`
flag. It is used to close a gap two of reloco's own customization-point
traits share -- **composition is not automatic** -- for exactly the cases
where composing them structurally is actually sound.

This document explains:

- what "composition is not automatic" means and why it matters,
- how reloco detects reflection support,
- exactly what gets derived for `is_send`/`is_sync` and
  `is_trivially_relocatable`, and why the two traits need *different* rules
  to stay sound,
- the `detail::requires_explicit_send_sync` marker mechanism, and
- how to build and test with reflection enabled yourself.

## The problem: composition is not automatic

Rust's compiler derives `Send`, `Sync`, and (for `Copy`) triviality
automatically, per field, for every type: a `struct` is `Send` exactly
when every one of its fields is `Send`. C++ has no such reflection built
into the language (until now), so reloco's equivalent traits --
[`is_send`/`is_sync`](send-sync.md) and
[`is_trivially_relocatable`](relocatable.md) -- fall back to a *manually
curated* customization point instead: default to a value, and expect the
author of a genuinely unusual type to specialize the trait themselves.

That default is never walked through a struct's own fields. A plain
```cpp
struct handle { reloco::rc<int> owner; int id; };
```
is `is_send_v<handle> == true` today (the unconditional default), even
though it embeds an `rc<int>` -- whose own specialization says `is_send ==
false` -- because nothing ever looks *inside* `handle`. Nobody wrote
`handle`'s `is_send` specialization, so it silently defaults to the
unsound answer.

P2996 reflection is precisely the tool needed to fix this: given a
complete class type, it can enumerate every base class and non-static
data member (including private ones) as a compile-time value you can loop
over with `template for`, and query each member's type. That is enough to
implement "walk `T`'s fields the way Rust's compiler does" as an ordinary,
if experimental, C++ function.

## Detecting reflection support

`include/reloco/detail/compat.hpp` defines:

```cpp
#if defined(__cpp_impl_reflection) && RELOCO_HAS_INCLUDE(<meta>)
#define RELOCO_HAS_REFLECTION 1
#else
#define RELOCO_HAS_REFLECTION 0
#endif
```

`__cpp_impl_reflection` is the SD-6 feature-test macro P2996 itself
defines, and (at least on GCC) it is **only ever defined when the compiler
was actually invoked with `-freflection`** -- it is not defined merely by
selecting `-std=c++26`. That makes `RELOCO_HAS_REFLECTION` a strict,
zero-cost opt-in: every build that does not pass `-freflection` (the
overwhelming majority of real-world builds, and all of reloco's own CI at
the time of writing) sees `RELOCO_HAS_REFLECTION` as `0` and is completely
unaffected -- same headers, same generated code, same diagnostics.

To try it yourself, you need a P2996-capable compiler (GCC 16+ trunk as of
this writing) and both `-std=c++26` (or `-std=gnu++26`) and `-freflection`:

```sh
g++-16 -std=c++26 -freflection -I include -c your_file.cpp
```

or, configuring the whole project with CMake:

```sh
cmake -G Ninja -DCMAKE_CXX_COMPILER=g++-16 -DCMAKE_CXX_STANDARD=26 \
      -DCMAKE_CXX_FLAGS=-freflection -B build-reflect
cmake --build build-reflect --target jplcz_reloco_tests
```

The reflection-specific code lives entirely inside `#if RELOCO_HAS_REFLECTION`
blocks in `send_sync.hpp` and `relocatable.hpp`; there is no separate
"reflection edition" of the library, and no reloco container or public API
changes shape based on it.

## `is_send`/`is_sync`: composing through explicit specializations only

`detail::compose_send_sync<Trait, T>()` (`send_sync.hpp`) is a `consteval`
function that, for a class type `T`:

1. enumerates `T`'s base classes via `std::meta::bases_of`,
2. enumerates `T`'s non-static data members via
   `std::meta::nonstatic_data_members_of`, using
   `std::meta::access_context::unchecked()` for both so *private* bases
   and members are visible too (matching how Rust's compiler sees every
   field regardless of visibility -- an all-`public` walk would silently
   under-count fields for the overwhelming majority of C++ classes, which
   keep their state private), and
3. requires `Trait<Member>::value` for every one of them, recursing
   through the same `Trait` (so `is_send`'s own composition of a nested
   `handle::owner` field calls back into `is_send<rc<int>>`, which is an
   explicit specialization, not the composed fallback).

Non-class types (fundamentals, pointers, references, unions, arrays, ...)
are treated as an opaque `true` leaf -- identical to today's non-reflection
default for those types.

```cpp
#if RELOCO_HAS_REFLECTION
template <typename T> struct is_send : std::bool_constant<detail::compose_send_sync<is_send, T>()> {
  static_assert(!std::is_base_of_v<detail::requires_explicit_send_sync, T>, /* ... */);
};
#else
template <typename T> struct is_send : std::true_type {
  static_assert(!std::is_base_of_v<detail::requires_explicit_send_sync, T>, /* ... */);
};
#endif
```

Crucially, this **only ever changes behavior for a `T` that has no
explicit `is_send<T>`/`is_sync<T>` specialization of its own**: ordinary
C++ overload resolution for class template specializations always prefers
a real specialization over the primary template, so `rc<T>`'s own
`is_send<rc<T>> : std::false_type` is chosen directly and the composed
primary template body is never even instantiated for `rc<T>` itself. The
composed fallback only ever runs for the "arbitrary user/composite type"
case that used to be an unconditional `true`.

```cpp
struct handle { reloco::rc<int> owner; int id; };

static_assert(reloco::is_send_v<reloco::rc<int>> == false); // explicit specialization, unaffected
static_assert(reloco::is_send_v<handle> == false);          // composed transitively, -freflection only
```

### The `requires_explicit_send_sync` marker

Composing a type's `Send`/`Sync`-ness from its field *types* is only sound
when the hazard is itself visible in those types. It is not, for several
of reloco's own types: `rc<T>`'s hazard is that its refcount increment is
non-atomic -- a property of *behavior*, not of the fact that it happens to
store a `T *` and a control-block pointer (both of which look perfectly
`Send` in isolation). Composing over `rc<T>`'s fields would therefore
derive the *wrong* answer (`true`) even with perfect reflection.

`detail::requires_explicit_send_sync` (`send_sync.hpp`) is an empty marker
base class that exists to prevent exactly that mistake, on **every**
build, reflection or not:

```cpp
namespace detail { struct requires_explicit_send_sync {}; }

template <typename T> class RELOCO_OWNER rc : private detail::requires_explicit_send_sync { /* ... */ };
template <typename T> struct is_send<rc<T>> : std::false_type {};
```

Any reloco type built on non-atomic shared state or unsynchronized
interior mutability privately inherits this marker instead of relying on
the default. `std::is_base_of_v` ignores accessibility, so a *private*
base is enough -- it does not affect the derived type's public interface
at all. The primary templates for both `is_send<T>` and `is_sync<T>`
`static_assert(!std::is_base_of_v<detail::requires_explicit_send_sync, T>,
...)`, so:

- a marked type **with** its own specialization never instantiates the
  primary template at all (the specialization wins), and the assertion
  never fires;
- a marked type **without** one hits the primary template, and the
  assertion fails at compile time with a clear message pointing at
  `send_sync.hpp`, instead of silently defaulting to (or composing) an
  unsound `true`.

This holds regardless of `RELOCO_HAS_REFLECTION`: even on a
reflection-enabled build, a marked-but-unspecialized type's structural
composition would run and might derive a plausible-looking `true` from
its field types alone, so the `static_assert` fires unconditionally rather
than trusting composition for these specific types. Today's marked types:
`rc<T>`/`weak_rc<T>`, `cell<T>`/`ref_cell<T>`, `shared_ptr<T>`/
`weak_ptr<T>`, `function<Sig>`, `function_ref<Sig>`, `bytes`,
`once_lock<T>`, `lazy_lock<T, F>`, `join_handle<R>`/
`scoped_join_handle<R>`, and channel's `sender<T>`/`sync_sender<T>`/
`receiver<T>`.

## `is_trivially_relocatable`: composing via P1144's own conditional rule

Unlike `is_send`/`is_sync`, `is_trivially_relocatable<T>`'s existing
default (`std::is_trivially_copyable_v<T>`) is already *safe* -- it can
never be wrong, only pessimistic (it says `false` for some types that
actually are safe to relocate, like a plain struct wrapping a
`unique_ptr<T>`). That means a naive field-type-only composition rule
(`true` iff every field is itself relocatable) would be **unsound** here,
the opposite problem from `is_send`/`is_sync`: a class could have every
field individually relocatable while its own user-provided destructor
does something address-dependent (registers `this` in a static table, for
example) that a raw `memcpy` relocation would silently skip.

`detail::compose_relocatable<T>()` (`relocatable.hpp`) therefore applies
the same conditional rule the P1144 standard proposal itself defines for
`std::is_trivially_relocatable`, not a naive field walk:

1. `std::is_trivially_copyable_v<T>` is checked first (today's existing,
   always-correct default).
2. Failing that, `T` is derived `true` only if:
   - `T` has **no user-provided special member function** -- default
     constructor, copy/move constructor, copy/move assignment, or
     destructor. An implicitly-defined or explicitly `= default`ed one is
     fine (it cannot hide any address-dependent side effect a raw
     relocation would skip); a user-provided one might, so its mere
     presence blocks auto-derivation regardless of what it actually does.
     `detail::has_no_user_provided_special_members<T>()` checks this via
     `std::meta::is_special_member_function`/`std::meta::is_user_provided`.
   - *and* every one of `T`'s bases and non-static data members is itself
     `is_trivially_relocatable`, recursing through the same trait (so a
     nested `unique_ptr<T>` field is picked up via *its own* explicit
     specialization in `unique_ptr.hpp`, not re-derived).
3. A non-class type that already failed step 1 (a reference, a function
   type, ...) conservatively stays `false`.

```cpp
struct wraps_unique_ptr { reloco::unique_ptr<int> p; int extra; };

static_assert(reloco::is_trivially_relocatable_v<reloco::unique_ptr<int>> == true);  // explicit specialization
static_assert(reloco::is_trivially_relocatable_v<wraps_unique_ptr> == true);         // composed, -freflection only

struct custom_dtor_wrapper { reloco::unique_ptr<int> p; ~custom_dtor_wrapper() {} };
static_assert(reloco::is_trivially_relocatable_v<custom_dtor_wrapper> == false); // user-provided dtor blocks it
```

As with `is_send`/`is_sync`, this only ever changes behavior for a `T`
with no explicit `is_trivially_relocatable<T>` specialization of its own:
`reloco::unique_ptr<T>` itself has a user-provided destructor, so step 2
would say `false` for it if it were ever reached -- but it never is, since
`unique_ptr<T>`'s own manual, already-verified specialization
(`is_trivially_relocatable<unique_ptr<T>> : std::true_type`) always wins
first.

## Why this isn't wired into CI (yet)

P2996 is not shipping in any stable GCC/Clang/MSVC release as of this
writing -- only experimental compiler trunks (GCC 16+ behind
`-freflection`). reloco's own CI matrix targets stable, released
toolchains (see `.github/workflows/ci.yml`/`cross-compile.yml`), so this
feature is currently developer/experimentation-only: build it yourself
with a P2996-capable compiler using the commands above. It is written to
be immediately foldable into CI as an additional matrix leg once a
mainstream compiler ships stable reflection support, with zero risk to
every other configuration in the meantime.
