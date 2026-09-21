<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Trivial relocation: `is_trivially_relocatable`

## What relocation means

Moving a C++ object today always means running code at the destination
address: a move constructor runs there, then a destructor runs at the
source address. For a type that is just "some bytes with no self-reference
or external registration" — a `struct { int; double; }`, a `unique_ptr<T>`
that only stores a pointer and a handle, a `basic_string` that only stores a
pointer, a size, and a capacity — that is pure overhead: the same effect can
be achieved by copying the object's bytes to the new address with
`memcpy`/`realloc` and simply treating the old address as no longer holding
an object, with no constructor or destructor call at either address at all.

A type has this property, *trivial relocation*, when:

- its bytes can be copied verbatim to a new address (`memcpy`/`realloc`-style,
  not `memmove`-only — the object does not need to be read again afterward),
  and
- the source address can then be treated as if the object had never been
  there (no destructor call is needed, and nothing else in the program holds
  a pointer to that now-stale address expecting the object to still live
  there).

This is *not* the same question `std::is_trivially_copyable` answers.
Trivial copyability requires that ordinary *copying* (not just relocating)
leaves two independent, fully live objects — which additionally requires a
trivial destructor, since both the original and the copy must be
independently destructible afterward. Relocation only ever needs one object
to survive; the source is abandoned, not kept alive, so its destructor never
needs to run there. That is exactly why `reloco::unique_ptr<T>` and
`reloco::string` qualify for relocation despite being move-only and
non-trivially-copyable (both declare a destructor): relocating either type's
bytes to a new address and abandoning the old one never leaves two
independent objects to destroy, and neither type points back into itself or
registers its own address anywhere external.

## `reloco::is_trivially_relocatable<T>`

`include/reloco/relocatable.hpp` defines the customization point:

```cpp
template <typename T> struct is_trivially_relocatable : std::is_trivially_copyable<T> {};

template <typename T>
inline constexpr bool is_trivially_relocatable_v = is_trivially_relocatable<T>::value;

#if RELOCO_CXX20
template <typename T> concept trivially_relocatable = is_trivially_relocatable_v<T>;
#endif
```

Every `std::is_trivially_copyable` type is trivially relocatable by
definition (the default), so plain `struct`s, enums, pointers, and other POD
types need no opt-in. reloco itself opts in the move-only types it ships
that qualify despite not being trivially copyable:

| Type | Relocatable? | Why |
|---|---|---|
| `reloco::unique_ptr<T>` | Always, regardless of `T` | Holds only a `T *` and an `allocator_ref`; the pointee is re-pointed-to, never itself relocated |
| `reloco::shared_ptr<T>` / `reloco::weak_ptr<T>` | Always, regardless of `T` | Holds only a `T *` and a control-block pointer, neither self-referential |
| `reloco::basic_string<CharT, TraitsT>` | Always, regardless of `CharT`/`TraitsT` | Holds only an `allocator_ref`, a `CharT *`, and two sizes; its heap buffer never points back at the object |
| `reloco::vector<T>` | Always, regardless of `T` | Holds only an `allocator_ref`, a `T *`, and two sizes; its heap buffer never points back at the object |
| `reloco::inline_vector<T, Capacity>` | Same as `T` | Storage is an inline `T[Capacity]`-shaped byte buffer embedded in the object, not a heap pointer, so relocating via `memcpy` is only safe when every contained `T` also is |
| `reloco::flat_set<T>` | Always, regardless of `T` | Wraps a `vector<T>`, which is itself always relocatable |
| `reloco::basic_inline_string<Capacity, CharT, TraitsT>` | Always, regardless of `Capacity`/`CharT`/`TraitsT` | Holds only an inline `CharT` array and a size; never self-referential |
| `reloco::basic_sso_string<CharT, TraitsT>` | Never, regardless of `CharT`/`TraitsT` | Holds an inline SSO buffer that `data()` can point into while small; relocating via `memcpy` while small would leave the copy's `data()` dangling into the old object |
| `reloco::optional<T>` | Same as `T` | Holds a `T` plus a `bool` flag inline, with no pointer back into itself |
| `reloco::checked_value<T>` | Same as `T` | Holds a `T` plus a `bool` flag, with no pointer back into itself |
| `reloco::checked_value<T *>` | Always, regardless of `T` | Holds only a `T *` and a `bool` flag |

> **Note:** this only covers `shared_ptr<T>`/`weak_ptr<T>` themselves, not a
> `T` that derives from `reloco::enable_shared_from_this<T>`. That base
> stores a `weak_ptr<T>` pointing back at `T`'s own address, so *relocating
> such a `T`* (not the `shared_ptr` wrapping it) would leave that
> self-pointer stale; do not opt such a `T` in to `is_trivially_relocatable`.

Not every move-only reloco type qualifies, though: `reloco::function<R(Args...)>`
(see `function.hpp`) is explicitly specialized to `false`, since a small
enough captured callable is stored inline in its small-object-optimization
buffer -- relocating the wrapper by copying bytes would then only be as
safe as the erased, captured type itself, which `function` has no way to
inspect.

`reloco::collection_view<T>`/`reloco::mutable_collection_view<T>` (see
`collection_view.hpp`) need no such opt-in at all: like `allocator_ref`,
each is just an untyped context pointer plus a `const vtable *`, already
trivially copyable, so the default definition already applies.
`reloco::mutable_container_ref<T, Key>` (see `container_ref.hpp`) is the
same story: both its sequence and associative implementations are just a
context pointer plus a `const vtable *`.

### Standard-library wrapper types: `relocatable_std.hpp`

`include/reloco/relocatable_std.hpp` is an opt-in header (not pulled in by
anything else in reloco, exactly like `container_ref_std.hpp`) that
specializes `is_trivially_relocatable` for `std::pair`, `std::tuple`,
`std::optional`, and `std::variant`, forwarding to the trait of each
contained type:

| Type | Relocatable? | Why |
|---|---|---|
| `std::pair<T1, T2>` | Same as `T1` and `T2` | Flat storage of `first`/`second`, no pointer back into itself |
| `std::tuple<Ts...>` | Same as every `Ts` | Flat storage of its elements, no pointer back into itself |
| `std::optional<T>` | Same as `T` | Holds `T` inline plus an engaged flag, no pointer back into itself |
| `std::variant<Ts...>` | Same as every `Ts` | Holds the active alternative inline plus an index, no pointer back into itself |

These four are already trivially relocatable without this header whenever
every contained type is trivially *copyable* -- the default
`is_trivially_relocatable<T> : std::is_trivially_copyable<T>` already gets
that case right, since each of these standard types is conditionally
trivially copyable based on its own contained type(s). This header only
changes the answer for the case the default gets wrong: a contained type
that is move-only or otherwise non-trivially copyable but still trivially
relocatable, such as `reloco::unique_ptr<T>`, `reloco::basic_string`, or
`reloco::vector<T>`. For example, `std::optional<reloco::string>` is not
trivially copyable (`reloco::basic_string` has a user-provided destructor),
so without this header it would not be considered relocatable either, even
though it safely is.

This relies on the standard library implementation not adding hidden
self-referential state to these class templates beyond what the standard
requires -- true of libstdc++, libc++, and MSVC STL in practice, and the
same assumption the P1144 relocation proposal itself makes when discussing
`std::pair`/`std::tuple`/`std::optional` as conditionally trivially
relocatable, but not something the standard formally guarantees today.

## Opting a type in

Specialize `is_trivially_relocatable` for your own type once you have
verified it holds no pointer back into itself (a data member's address
depending on `this`, an intrusive list/tree link, a `std::function` storing
a small-object pointer into its own storage, etc.) and is not registered
anywhere by its own address (e.g. in a static registry keyed by `this`):

```cpp
class arena_handle {
  arena *owner_;   // Points elsewhere, not back at *this.
  std::size_t id_;

public:
  ~arena_handle() noexcept { /* ... */ }
  // A user-declared destructor makes this non-trivially-copyable, so it
  // does not qualify by default even though it holds no self-reference.
};

template <> struct reloco::is_trivially_relocatable<arena_handle> : std::true_type {};
```

Do **not** specialize this for a type with an internal self-pointer (e.g. a
small-buffer-optimized string that may point into its own inline storage,
or an intrusive list node linked to its neighbors) — relocating its bytes to
a new address without fixing up that internal pointer produces a dangling
reference into the old, now-abandoned memory.

## Why this matters

A generic container that tracks `is_trivially_relocatable_v<T>` for its
element type can grow, shrink, or shift its storage with a raw
`memcpy`/`memmove`/`realloc` instead of a loop of move-construct-then-destroy
for every element — the same optimization `reloco::basic_string::try_reserve`
already applies to its own backing `char` buffer, generalized to any element
type a future container might hold. `reloco` does not yet ship a container
built on this trait; `is_trivially_relocatable` is provided now so both
generic algorithms and reloco's own future containers have a single,
already-verified customization point to build on, rather than every author
inventing (and getting slightly wrong) their own relocatability trait.
