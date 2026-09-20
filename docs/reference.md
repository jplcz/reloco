<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# API reference

Quick, per-type reference for every public reloco header. Start with a
[guide](../README.md#guides) for the concepts behind these types (fallible
construction, the checked/`try_*`/`unsafe_*` tri-tier convention, lifetime
annotations, trivial relocation); this page is a map of what exists and
where, not a tutorial.

| Header | Type(s) | One-line summary |
|---|---|---|
| `expected.hpp` | `expected<T, E>`, `unexpected<E>` | Allocation-free value-or-error result |
| `error.hpp` | `error`, `result<T>` | The one error enum every fallible reloco operation returns, and its `expected<T, error>` alias |
| `span.hpp` | `span<T>` | Non-owning, checked view over a contiguous range |
| `array.hpp` | `array<T, N>` | Fixed-size owning array with hardened element access |
| `string_view.hpp` | `basic_string_view<CharT, TraitsT>` (`string_view`, `wstring_view`) | Non-owning, checked view over character data |
| `string.hpp` | `basic_string<CharT, TraitsT>` (`string`, `wstring`) | Move-only, allocator-backed, growable character buffer |
| `vector.hpp` | `vector<T>` | Move-only, allocator-backed, growable dynamic array |
| `unique_ptr.hpp` | `unique_ptr<T>` | Move-only, allocator-backed smart pointer with fallible construction |
| `shared_ptr.hpp` | `shared_ptr<T>`, `weak_ptr<T>`, `enable_shared_from_this<T>` | Reference-counted, allocator-backed smart pointer with fallible construction |
| `function.hpp` | `function<R(Args...)>` | Type-erased, allocator-backed callable wrapper with fallible construction |
| `collection_view.hpp` | `collection_view<T>`, `mutable_collection_view<T>`, `collection_view_traits<Container>` | Type-erased, non-owning views over an adapted sequence container |
| `container_ref.hpp` | `mutable_container_ref<T, Key = void>`, `container_ref_traits<Container>` | Type-erased handle for structurally mutating (growing/inserting/erasing) an adapted sequence or associative container |
| `container_ref_std.hpp` | `container_ref_traits<std::vector<T>>`, `container_ref_traits<std::map<Key, Value>>` | Opt-in `container_ref_traits` adapters for `std::vector`/`std::map` |
| `value_ptr.hpp` | `value_ptr<T>` | Nullable, non-owning pointer that rejects binding to prvalue temporaries |
| `value_ref.hpp` | `value_ref<T>` | Non-null, non-owning reference wrapper that rejects binding to prvalue temporaries |
| `checked_value.hpp` | `checked_value<T>` | Move-only wrapper with Rust-like use-after-move checks |
| `allocator.hpp` | `allocator_ref`, `allocator<Tag>`, `allocator_traits<Tag>`, `mem_block`, `usage_hint` | Type-erased allocator handle and the tag-based provider pattern backing it |
| `heap_allocator.hpp` | `heap_allocator_tag` | Stateless `allocator_traits` backend over the process heap (`new`/`delete`) |
| `default_allocator.hpp` | `default_allocator()`, `reloco_global_alloc` | Process-wide default allocator, overridable like Rust's `#[global_allocator]` |
| `concepts.hpp` | `has_try_create_v`, `has_try_allocate_v`, `has_try_construct_v`, `has_try_clone_v`, `has_try_clone_at_v` (+ C++20 concepts) | Detection traits for the fallible-construction protocol |
| `construction_helpers.hpp` | `construction_helpers` | Compile-time dispatcher picking the best construction/clone strategy for a type |
| `relocatable.hpp` | `is_trivially_relocatable<T>` (+ C++20 `trivially_relocatable`) | Marks types safely movable by copying bytes and abandoning the source |
| `lifetime.hpp` | `RELOCO_LIFETIMEBOUND`, `RELOCO_OWNER`, `RELOCO_POINTER`, `RELOCO_UNSAFE_BUFFER_USAGE`, ... | Compiler-specific lifetime/ownership/safe-buffers annotation macros |
| `rvalue_safety.hpp` | `RELOCO_BLOCK_RVALUE_ACCESS` | Deletes rvalue accessors that would otherwise dangle past a temporary |
| `reloco_config.hpp` | (user override header hook) | How to override library-wide defaults from `reloco_user_config.hpp` |

## `expected<T, E>` / `result<T>`

`include/reloco/expected.hpp`, `include/reloco/error.hpp`

A `std::expected`-like, allocation-free value-or-error type, usable in
C++17 (no dependency on the standard library's own `<expected>`).
`reloco::result<T>` is `reloco::expected<T, reloco::error>` — the one error
type every fallible reloco operation returns (see
[Fallible construction](fallible-construction.md)).

```cpp
reloco::result<int> parse(std::string_view s) noexcept {
  if (s.empty())
    return reloco::unexpected(reloco::error::invalid_argument);
  return 42;
}
```

## `span<T>`

`include/reloco/span.hpp`

Non-owning view over a contiguous range of `T`, with the checked/`try_*`/
`unsafe_*` tri-tier convention on every element/subrange accessor
(`operator[]`/`at()`, `try_at()`/`try_first()`/`try_last()`/`try_subspan()`,
`unsafe_at()`/...). See [Hardened containers](hardened-containers.md).

## `array<T, N>`

`include/reloco/array.hpp`

Fixed-size, stack- or member-embeddable owning array. Same tri-tier element
access as `span`, plus `as_span()` to hand out a borrowed, checked view
without exposing the underlying storage directly.

## `basic_string_view<CharT, TraitsT>` (`string_view`, `wstring_view`)

`include/reloco/string_view.hpp`

Non-owning, checked view over character data — `std::string_view`, plus the
tri-tier convention (`front()`/`try_front()`/`unsafe_front()`, etc.),
rejection of dangling prvalue `std::basic_string` temporaries at the
constructor, and interop with both `std::basic_string_view` and
`std::basic_string`.

## `basic_string<CharT, TraitsT>` (`string`, `wstring`)

`include/reloco/string.hpp`

Move-only, allocator-backed, growable character buffer — the fallible,
allocator-explicit analogue of `std::string`. No small-string optimization:
every non-empty instance holds exactly one heap allocation, obtained and
released through a bound `reloco::allocator_ref`.

```cpp
auto s = reloco::string::try_create(reloco::string_view("hello"));
if (!s)
  return; // s.error() is a reloco::error.
auto ok = s->try_append(reloco::string_view(" world"));
assert(s->view() == "hello world");
```

Construction and cloning:

| Function | Behavior |
|---|---|
| `try_create(view_type sv = {})` | Builds from `sv` using `default_allocator()` |
| `try_allocate(allocator_ref alloc, view_type sv = {})` | Builds from `sv` using an explicit allocator |
| `try_clone(allocator_ref alloc)` / `try_clone()` | Fallible deep copy, explicit or own allocator |
| `try_clone_at(allocator_ref alloc, basic_string *storage, const basic_string &source)` | Fallible deep copy directly into uninitialized storage |

Because it implements this protocol itself (see
[Fallible construction](fallible-construction.md)), `basic_string` composes
with anything built on `has_try_create_v`/`construction_helpers`:
`reloco::unique_ptr<reloco::string>::try_create(...)` just works.

Mutation: `try_reserve`, `shrink_to_fit`, `try_assign`, `try_append`,
`try_push_back`, `pop_back`/`try_pop_back`, `try_insert`,
`erase`/`try_erase`, `try_resize`, `clear`. Every fallible one returns
`reloco::result<void>`.

Element access follows the same checked/`try_*`/`unsafe_*` tri-tier
convention as `string_view`/`span`/`array`: `operator[]`/`at()`/`front()`/
`back()` assert; `try_at()`/`try_front()`/`try_back()` return
`reloco::result<...>`; `unsafe_at()`/`unsafe_front()`/`unsafe_back()`/
`unsafe_c_str()` are `RELOCO_UNSAFE_BUFFER_USAGE`-gated escape hatches.
`data()` is always safe (points at a shared static null character when
empty, never `nullptr`). `view()` and implicit conversions to
`reloco::string_view`/`std::string_view` provide borrowed access; explicit
`operator std::basic_string<CharT, TraitsT>()` converts to an owning
standard-library string when one is actually needed at a boundary.

`reloco::is_trivially_relocatable<basic_string<CharT, TraitsT>>` is always
`true` (see [Trivial relocation](relocatable.md)).

## `vector<T>`

`include/reloco/vector.hpp`

Move-only, allocator-backed, growable dynamic array — the fallible,
allocator-explicit analogue of `std::vector`. Every instance holds at most
one heap allocation, obtained and released through a bound
`reloco::allocator_ref`.

```cpp
auto v = reloco::vector<int>::try_create();
if (!v)
  return; // v.error() is a reloco::error.
auto ok = v->try_push_back(1);
ok = v->try_insert_at(0, 0);
assert((*v)[0] == 0 && (*v)[1] == 1);
```

Construction and cloning:

| Function | Behavior |
|---|---|
| `try_create(size_type initial_cap = 0)` | Builds, optionally reserving capacity, using `default_allocator()` |
| `try_allocate(allocator_ref alloc, size_type initial_cap = 0)` | Builds, optionally reserving capacity, using an explicit allocator |
| `try_clone(allocator_ref alloc)` / `try_clone()` | Fallible deep copy, explicit or own allocator |
| `try_clone_at(allocator_ref alloc, vector *storage, const vector &source)` | Fallible deep copy directly into uninitialized storage |

Because it implements this protocol itself (see
[Fallible construction](fallible-construction.md)), `vector<T>` composes
with anything built on `has_try_create_v`/`construction_helpers`:
`reloco::unique_ptr<reloco::vector<int>>::try_create(...)` just works.
Element construction (`try_emplace_back`/`try_insert_at`) and cloning both
delegate to `construction_helpers`, so element types that implement their
own fallible-construction protocol compose transparently; trivially
copyable element types with no custom `try_clone` take a single-`memcpy`
fast path when cloning.

Mutation: `try_reserve`, `shrink_to_fit`, `try_emplace_back`, `try_push_back`,
`try_pop_back`, `try_insert_at`, `try_erase_at`, `clear`. Every fallible one
returns `reloco::result<...>`. Growth prefers
`allocator_ref::expand_in_place` first; when that fails it either
byte-relocates the whole buffer in one `reallocate` call (when
`is_trivially_relocatable_v<T>`) or falls back to move-constructing each
element into a freshly allocated block.

Element access follows the same checked/`try_*`/`unsafe_*` tri-tier
convention as `span`/`array`/`string`: `operator[]`/`front()`/`back()`
assert (there is no separately named `at()` — `operator[]` itself is the
checked tier); `try_at()`/`try_front()`/`try_back()` return
`reloco::result<...>`; `unsafe_at()`/`unsafe_data()` are
`RELOCO_UNSAFE_BUFFER_USAGE`-gated escape hatches. `data()`/`front()`/
`back()` assert the vector is non-empty; `try_data()`/`try_front()`/
`try_back()` fail with `error::container_empty` instead.

`reloco::is_trivially_relocatable<vector<T>>` is always `true` regardless of
`T` (see [Trivial relocation](relocatable.md)): the vector's own handle is
just an `allocator_ref` plus a pointer and two sizes, with no
self-reference into its own storage.

## `unique_ptr<T>`

`include/reloco/unique_ptr.hpp`

Move-only, allocator-backed smart pointer. `try_allocate(allocator_ref,
Args...)`/`try_create(Args...)` allocate the box and then resolve the best
available construction strategy for `T` via
`construction_helpers::try_construct` (see
[Fallible construction](fallible-construction.md)) — so any `T` that
implements `try_construct`/`try_allocate`/`try_create`, or is simply
nothrow-constructible from `Args...`, works with no extra glue code.

```cpp
auto ptr = reloco::unique_ptr<widget>::try_create(arg1, arg2);
if (ptr)
  (*ptr)->do_something();
```

`operator*`/`operator->`/`get()` are checked (`RELOCO_ASSERT` on null);
`unsafe_get()` skips that check and is `RELOCO_UNSAFE_BUFFER_USAGE`-gated.
No copy constructor: use `T`'s own `try_clone`/`try_clone_at` explicitly for
a deep copy. `reloco::is_trivially_relocatable<unique_ptr<T>>` is always
`true`, regardless of `T` (see [Trivial relocation](relocatable.md)).

## `shared_ptr<T>` / `weak_ptr<T>` / `enable_shared_from_this<T>`

`include/reloco/shared_ptr.hpp`

Reference-counted, allocator-backed smart pointer. `shared_ptr<T>` is
copyable (sharing ownership, incrementing a refcount) and movable;
`weak_ptr<T>` observes without extending the object's lifetime, and
`lock()`s back into a `shared_ptr<T>` (or `error::pointer_expired` if the
object is already gone). Object construction goes through
`construction_helpers::try_construct`, exactly like `unique_ptr`, so the
same `try_construct`/`try_allocate`/`try_create`/nothrow-constructible tiers
apply.

Two allocation layouts are available, mirroring `std::allocate_shared` vs.
`std::make_shared`:

```cpp
// Single allocation (object + control block together); recommended default.
auto ptr = reloco::try_create_combined_shared<widget>(arg1, arg2);

// Object and control block allocated separately: the object's storage is
// freed as soon as the last shared_ptr releases it, independently of any
// surviving weak_ptr (which only keeps the small control block alive).
auto ptr2 = reloco::try_create_shared<widget>(arg1, arg2);
```

`try_allocate_shared`/`try_allocate_combined_shared` take an explicit
`allocator_ref`; `try_create_shared`/`try_create_combined_shared` use
`default_allocator()`. `static_pointer_cast`/`dynamic_pointer_cast`/
`const_pointer_cast`/`reinterpret_pointer_cast` mirror the `std::shared_ptr`
casts. `operator*`/`operator->`/`get()` are checked; `unsafe_get()` skips
the null check and is `RELOCO_UNSAFE_BUFFER_USAGE`-gated; `try_get()`
returns `result<T *>`. `reloco::is_trivially_relocatable<shared_ptr<T>>`
and `<weak_ptr<T>>` are always `true`, regardless of `T` (see
[Trivial relocation](relocatable.md)).

## `function<R(Args...)>`

`include/reloco/function.hpp`

Type-erased, allocator-backed callable wrapper (reloco's `std::function`
counterpart). `try_allocate(allocator_ref, F)`/`try_create(F)` wrap any
callable convertible to `R(Args...)`, choosing the cheapest storage tier at
construction time: a bare function pointer (or captureless lambda) stored
directly with no allocation, a small-object-optimization inline buffer
(`function<R(Args...)>::soo_capacity` bytes, alignment up to
`alignof(std::max_align_t)`), or a single heap allocation for anything
larger.

```cpp
auto fn = reloco::function<int(int)>::try_create([captured](int v) noexcept {
  return captured + v;
});
if (fn)
  int result = (*fn)(41);
```

Move-only: `try_clone()` performs an explicit fallible deep copy, failing
with `error::unsupported_operation` if the captured callable is not
`std::is_nothrow_copy_constructible_v` (or if the function is empty).
`operator()` asserts non-empty; `try_call(Args...)` is the checked
alternative, failing with `error::container_empty` instead -- and if `R` is
itself a `result<U>`, `try_call`'s own result is flattened rather than
double-wrapped. `reloco::is_trivially_relocatable<function<R(Args...)>>` is
always `false`: the captured callable may live inline in the SOO buffer, so
relocating the wrapper by copying bytes is only as safe as the (erased)
captured type itself (see [Trivial relocation](relocatable.md)).

## `collection_view<T>` / `mutable_collection_view<T>` / `collection_view_traits<Container>`

`include/reloco/collection_view.hpp`

Type-erased, non-owning views over an *adapted* sequence container
(`reloco::span<T>`, `reloco::array<T, N>`, `std::array<T, N>`,
`std::span<T>`, ...), matching `allocator_ref`'s customization-point shape:
a two-word handle (an untyped context pointer plus a `const vtable *`), no
virtual base class, no RTTI, no allocation of its own. There is
deliberately no structural detection ("any type that happens to have
`size()`/`begin()`/`end()`"): a container type is only usable through these
views once someone specializes `collection_view_traits<Container>` for it,
exactly like `allocator_ref` requires an explicit `allocator_traits<Tag>`
specialization. Every required accessor must be supplied explicitly by the
adapter author; optional capabilities (contiguous `data()` access, mutable
access) are switched on by required `static constexpr bool` flags
(`has_data`, `is_mutable`) on the traits specialization, mirroring how
`allocator_ref::can_expand_in_place()`/`can_reallocate()`/`can_advise()`
report an optional backend operation.

`collection_view<T>` is *unconditionally read-only*: every accessor
(`size`/`empty`/`at`/`try_at`/`unsafe_at`/`data`/`try_data`/`unsafe_data`/
`for_each`) returns or visits `const T &`/`const T *`, regardless of
whether the bound container or `T` itself is `const`-qualified.
`mutable_collection_view<T>`, derived from `collection_view<T>`, is the
*only* way to obtain write access: its converting constructor additionally
requires the adapter's `is_mutable` flag and a non-`const` lvalue container,
and it adds `T &`/`T *`-returning overloads of `at`/`try_at`/`unsafe_at`/
`data`/`try_data`/`unsafe_data`/`for_each` alongside the read-only ones it
inherits.

```cpp
reloco::array<int, 3> a{1, 2, 3};

reloco::collection_view<int> view(a);       // read-only
int total = 0;
view.for_each([&](const int &v) { total += v; });

reloco::mutable_collection_view<int> mview(a); // requires a non-const array
mview.at(0) = 100;                             // a[0] == 100
```

Both converting constructors are `explicit`: binding a container is always
a deliberate, visible step at the call site, never an implicit conversion.
Neither view allocates, copies, or owns the underlying container: like
`span`/`allocator_ref`, the referenced container must outlive every view
built from it.

Built-in `collection_view_traits` adapters ship for `reloco::span<T>`,
`reloco::array<T, N>`, `std::array<T, N>`, and `std::span<T>` (the last
gated behind `RELOCO_HAS_STD_SPAN`). No adapters for owning/node-based
containers (`std::vector`, `std::list`, `std::map`, ...) are provided yet;
see the `collection_view_traits` primary template's documentation for the
full contract required to add one.

## `mutable_container_ref<T, Key = void>` / `container_ref_traits<Container>`

`include/reloco/container_ref.hpp` (core, no built-in adapters),
`include/reloco/container_ref_std.hpp` (opt-in `std::vector`/`std::map`
adapters)

Type-erased, non-owning handle allowing *structural* mutation of an adapted
container (growing, inserting, erasing, clearing) as well as
indexed/keyed access to its existing elements -- unlike
`mutable_collection_view`, which only mutates elements already present and
never resizes the container. Same customization-point shape as
`allocator_ref`/`collection_view`: a container is only usable here once
someone specializes `container_ref_traits<Container>` for it; there is no
structural detection.

`mutable_container_ref<T, Key = void>` is an alias picking between two
implementations based on whether `Key` is `void`:

- `mutable_container_ref<T>` (`Key = void`): a *sequence* container handle
  (`container_ref_traits<Container>::is_associative == false`). Offers
  `try_push_back`/`try_push_front`/`try_insert_at(index,
  value)`/`try_erase_at(index)`/`clear()`, `for_each(Fn)` (implemented
  generically over `at`/`size` -- no separate trait hook needed, since this
  contract targets index-addressable "vector-like" containers), and
  `at`/`try_at`/`unsafe_at(index)` for existing elements.
- `mutable_container_ref<T, Key>` (`Key` other than `void`): an
  *associative* container handle (`is_associative == true`). Offers
  `try_insert_at(key, value)`/`try_erase(key)`/`clear()`, `for_each(Fn)`
  (via a required trait-level iteration hook, since there is no index
  concept to fall back on), and `at`/`try_at`/`unsafe_at(key)` for existing
  entries.

```cpp
std::vector<int> v{1, 2, 3};
reloco::mutable_container_ref<int> ref(v);
ref.try_push_back(4);          // v == {1, 2, 3, 4}
ref.try_insert_at(0, 0);       // v == {0, 1, 2, 3, 4}
ref.at(0) = 100;                // v == {100, 1, 2, 3, 4}
```

Every fallible trait function the adapted container cannot support (e.g. a
container with no efficient front-insertion, or inserting a key that
already exists) must still be implemented by the traits specialization --
it simply reports that through its own `result<void>` (e.g.
`unexpected(error::unsupported_operation)`, `error::already_exists`,
`error::not_found`) rather than being gated by a separate capability flag.

`container_ref_std.hpp` is deliberately a separate header from
`container_ref.hpp`: its `std::vector<T>`/`std::map<Key, Value>` adapters
wrap standard-library operations that allocate and may throw, which the
rest of reloco avoids; only a caller who explicitly `#include`s this header
opts into that behavior. Every adapter function in it wraps the
underlying call in `try`/`catch (...)`, converting any thrown exception
into `unexpected(error::allocation_failed)`.

## `value_ptr<T>` / `value_ref<T>`

`include/reloco/value_ptr.hpp`, `include/reloco/value_ref.hpp`

Non-owning pointer/reference wrappers that document a borrowed relationship
without taking ownership, and reject binding to prvalue temporaries at
compile time so a borrow can never quietly outlive its source. `value_ptr`
is nullable (`operator bool`, defaults to null); `value_ref` is always
non-null and convertible to/from `value_ptr`. See
[Lifetime safety](lifetime-safety.md).

## `checked_value<T>`

`include/reloco/checked_value.hpp`

Move-only wrapper giving Rust-like use-after-move checking to any nothrow
move-constructible `T` (plus a `T *` partial specialization with an
additional null check on dereference). A moved-from instance is poisoned:
further access asserts and traps, on every compiler; under Clang, `
-Wconsumed` additionally flags use-after-move at compile time. See
[Lifetime safety](lifetime-safety.md). `reloco::is_trivially_relocatable<
checked_value<T>>` mirrors `T`'s own relocatability; `checked_value<T *>` is
always relocatable (see [Trivial relocation](relocatable.md)).

## `allocator_ref` / `allocator<Tag>` / `allocator_traits<Tag>`

`include/reloco/allocator.hpp`

Type-erased, two-word handle (`allocator_ref`) to an allocator backend
described by a `Tag` + `allocator_traits<Tag>` specialization — no virtual
base class, no vtable-carrying inheritance. Every operation
(`allocate`/`deallocate`/`expand_in_place`/`reallocate`/`advise`) returns
`reloco::result<T>` and is `RELOCO_UNSAFE_BUFFER_USAGE`-gated (raw sized
pointers, no bounds-tracked alternative). See
[Extending reloco](extending.md) for how to add a new backend.

## `heap_allocator_tag` / `default_allocator()`

`include/reloco/heap_allocator.hpp`, `include/reloco/default_allocator.hpp`

`heap_allocator_tag` is the stateless, built-in backend over the process
heap. `default_allocator()` is the process-wide default `allocator_ref`
(similar to Rust's `#[global_allocator]`): out of the box it returns the
heap backend, but an application can replace it wholesale by defining
`RELOCO_DEFAULT_ALLOCATOR_CUSTOM` — see the header's own documentation for
the exact override recipe (a customization-header include cycle makes it
slightly more involved than a plain `reloco_user_config.hpp` define).

## Fallible construction: `concepts.hpp` / `construction_helpers.hpp`

`include/reloco/concepts.hpp`, `include/reloco/construction_helpers.hpp`

`has_try_create_v<T, Args...>`, `has_try_allocate_v<T, Args...>`,
`has_try_construct_v<T, Args...>`, `has_try_clone_v<T>` (and its
`_allocator_aware`/`_self_contained` halves), and `has_try_clone_at_v<T>`
detect which of the five fallible-construction functions a type implements
(plus matching C++20 `concept`s). `construction_helpers::try_construct`/
`try_allocate`/`try_clone`/`try_clone_at` pick the most efficient available
strategy at compile time so generic code never hand-writes the `if
constexpr` dispatch itself. See
[Fallible construction](fallible-construction.md) for the full protocol,
and `unique_ptr.hpp`/`string.hpp` for two complete, real-world examples.

## `is_trivially_relocatable<T>`

`include/reloco/relocatable.hpp`

Customization-point trait marking a type whose object representation can be
relocated by copying its bytes to a new address and abandoning the old one,
without running a move constructor or destructor at either address. See
[Trivial relocation](relocatable.md) for the full explanation and the
built-in specializations (`unique_ptr<T>`, `basic_string<CharT, TraitsT>`,
`checked_value<T>`/`checked_value<T *>`).

## Lifetime and safety annotation macros

`include/reloco/lifetime.hpp`, `include/reloco/rvalue_safety.hpp`

Compiler-feature-detected macros used throughout the library:
`RELOCO_LIFETIMEBOUND` (Clang `[[clang::lifetimebound]]`),
`RELOCO_OWNER`/`RELOCO_POINTER` (GSL owner/pointer annotations for static
analyzers), `RELOCO_UNSAFE_BUFFER_USAGE`/`RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/
`RELOCO_END_UNSAFE_BUFFER_USAGE` (Clang `-Wunsafe-buffer-usage` opt-in
gating), and `RELOCO_BLOCK_RVALUE_ACCESS(Type)` (deletes a container's
rvalue accessors so a borrow can never outlive a temporary). See
[Lifetime safety](lifetime-safety.md).

## `reloco_config.hpp`

`include/reloco/reloco_config.hpp`

The library-wide user-override mechanism: define
`RELOCO_HAS_USER_CONFIG`/create `reloco_user_config.hpp` on the include path
to override compile-time defaults (e.g. `RELOCO_DEFAULT_ALLOCATOR_CUSTOM`,
assertion-handling behavior) before any other reloco header is processed.
See the header's own documentation for the exact mechanism and its
constraints (why it cannot itself include headers like `allocator.hpp`).
