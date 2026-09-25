<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Type-erased base containers: sharing storage logic across `vector`/`inline_vector`/`sso_vector`/`outline_vector`

`vector<T>`, `inline_vector<T, Capacity>`, `sso_vector<T, InlineCapacity>`,
and `outline_vector<T>` (`vector.hpp`, `inline_vector.hpp`, `sso_vector.hpp`,
`outline_vector.hpp`) all need the exact same growth/resize/insert/erase/
retain/dedup logic -- only *where the bytes live*, and *who owns them*,
differs (heap-only, inline-only, inline-with-heap-fallback, or a
caller-owned span the container never allocates or frees). Rather than
hand-rolling that logic four times over, all four are thin, strongly-typed
wrappers around a single type-erased engine defined in
`include/reloco/detail/vector_base.hpp` (out-of-line bodies in
`vector_base.ipp`), itself built on two lower headers,
`detail/type_metadata.hpp` and `detail/type_operations.hpp`, that describe
`T` alone rather than anything array-shaped. This page explains that
engine's layered design, how the four public containers plug into it, and
what to do if you add a fifth vector flavor.

If you haven't already, read [Container contract](container-contract.md)
first: it covers the lifetime/rvalue-safety/tri-tier-accessor rules every
reloco container follows, including the four described here.
`flat_container_base.hpp` (backing `flat_set`/`flat_map`/
`inline_flat_set`/`inline_flat_map`) applies the same "shared base +
storage-defining `Storage` parameter" idea one level up, on top of
`vector<T>`/`inline_vector<T, Capacity>` themselves -- read this page first,
that one generalizes naturally from it.

## Why type erasure instead of a single templated base

A naive shared base would be `template <typename T, typename Storage>
vector_common`, instantiated once per `(T, Storage)` pair -- but that still
duplicates every non-trivial member function's *machine code* per `T`
(growth arithmetic, shifting, bounds checks are identical for `int` and
`std::string`, only the per-element constructor/destructor/move calls
differ). `vector_base.hpp` instead erases `T` out of the storage-management
layer entirely: the untyped `*_base` classes operate on `void*` + byte
offsets + a `vector_operations` function-pointer table, so their non-inline
bodies (declared `RELOCO_API`, defined once in `vector_base.ipp`) compile
and link *once*, not once per `T`. Only the thin `typed_vector_base<T,
Base>` layer -- reference-returning wrappers, iterator types, `noexcept`
signatures -- is templated on `T`, and its bodies are small enough that
per-`T` inlining/duplication there is cheap.

This mirrors the tag + `context_type` + `*_traits<Tag>` pattern from
[Extending reloco](extending.md): both patterns move type-specific behavior
out to a small, resolved-once table (`allocator_traits<Tag>` there,
`vector_operations` here) so most of the code operates generically against
it. The difference is *when* the table is chosen: `allocator_ref` resolves
its vtable at construction time from a runtime `context_type`; `vector_base`
resolves its table entirely at compile time, per `T`, via
`get_operations_for<T>()`.

## Layer 1: the `vector_operations` table

`type_metadata`/`metadata_for<T>` live in their own header,
`detail/type_metadata.hpp`, separate from `vector_base.hpp`:

```cpp
struct type_metadata {
  std::size_t element_size;
  std::size_t element_alignment;
  bool is_trivially_destructible;
  bool is_trivially_relocatable;
  bool is_trivially_copyable;
  bool is_default_constructible;
};
```

`type_metadata` describes only `T` itself -- size, alignment, and a handful
of triviality facts -- with nothing specific to *contiguous array* storage,
so it isn't part of `vector_operations`' own header. A future type-erased
engine for a node- or bucket-based container (a hash map, a list, ...)
can `#include "type_metadata.hpp"` and reuse `metadata_for<T>` as-is,
without depending on anything `vector_base.hpp` defines.

The *per-element* construct/clone/destroy/relocate dispatch itself lives
in its own header too, `detail/type_operations.hpp`, one layer below
`vector_operations`:

```cpp
struct type_operations {
  void (*destroy_one)(const type_metadata &, void *data) noexcept;
  result<void> (*clone_one)(const type_metadata &, allocator_ref alloc, void *dest, const void *src) noexcept;
  result<void> (*copy_construct_one)(const type_metadata &, allocator_ref alloc, void *dest,
                                      const void *value_ptr) noexcept;
  void (*relocate_one)(const type_metadata &, void *to, const void *from) noexcept;
};
```

Unlike `vector_operations`, `type_operations` says nothing about *ranges*
or contiguous shifting -- it only knows how to construct/clone/destroy/
relocate *one* `T` at a caller-given address, which is exactly the set of
facts a node-based container (each node holding one element) needs just as
much as an array-based one. `get_type_operations_for<T>()` resolves it the
same way `get_operations_for<T>()` resolves `vector_operations`: the
shared `operations_for_trivial_element` table for `T` trivial enough to
`memcpy` (`has_trivial_type_ops<T>`, also reused by `vector_base.hpp` as
`has_trivial_vector_ops<T>`, since a *range* of such `T` qualifies for the
whole-range fast path under the same condition), a `std::pair<T1, T2>`
partial specialization sharing that table when both members independently
qualify, or a per-`T` `type_operations_for<T>` table of compiler-generated
closures otherwise.

`vector_base.hpp`'s own non-trivial range resolvers (`clone_range`,
`copy_construct_range`, `move_range`, `move_range_up`, `destroy_range`)
loop over `get_type_operations_for<T>()`'s function pointers rather than
duplicating that per-element tiered dispatch inline -- so the logic
`construction_helpers` needs to construct/clone one `T` is written exactly
once, in `type_operations.hpp`, and any future map/list engine reuses it
unchanged. Only the *trivial* range fast path stays specific to
`vector_operations`: for trivial `T`, one whole-range `memcpy`/`memset` via
`trivial_operator_set` is strictly cheaper than looping a single-element
function pointer once per index, so that path is not rebuilt on top of
`type_operations`.

`vector_operations` itself, layered on top in `vector_base.hpp`, is where
the *array-shaped* per-`T` behavior lives:

```cpp
struct vector_operations {
  void (*destroy_range)(const type_metadata &, void *data, std::size_t from, std::size_t to) noexcept;
  result<void> (*clone_range)(const type_metadata &, const void *src, void *dest, std::size_t size,
                               allocator_ref alloc) noexcept;
  result<void> (*copy_construct_range)(const type_metadata &, allocator_ref alloc, void *data, std::size_t from,
                                        std::size_t to, const void *value_ptr) noexcept;
  void (*move_range)(const type_metadata &, void *to, const void *from, std::size_t count);
  void (*move_range_up)(const type_metadata &, void *to, const void *from, std::size_t count);
};
```

`metadata_for<T>` captures the compile-time facts about `T` the engine
needs (size/alignment/triviality), computed once as a `constexpr` value.
`get_operations_for<T>()` resolves, per function pointer and independently
of the others, to one of two implementations:

- **`trivial_operator_set`**'s `memcpy`/`memset`-based bodies, whenever `T`
  is trivially destructible/relocatable/copyable enough for that particular
  operation (e.g. `copy_construct_range` can use the trivial path only when
  `T` is *both* trivially copyable *and* trivially default-constructible,
  independent of whether `destroy_range` needs the trivial or generic path).
- A loop over `get_type_operations_for<T>()`'s matching single-element
  function pointer (`destroy_one`/`clone_one`/`copy_construct_one`/
  `relocate_one`), which itself dispatches, per-`T`, through
  `construction_helpers` (see `construction_helpers.hpp`) exactly the same
  tiered logic `construction_helpers` uses for a single element -- just
  resolved once in `type_operations.hpp` and shared, rather than
  reimplemented in every range resolver.

When every operation resolves to the trivial path (`has_trivial_vector_ops<T>`),
`get_operations_for<T>()` returns the single shared `operations_for_trivial`
table instead of instantiating a fresh `vector_operations_for<T>` --
avoiding a redundant per-`T` table for the common case of plain-old-data
element types. A `std::pair<T1, T2>` partial specialization extends this:
if both `T1` and `T2` independently qualify as trivial, the pair as a whole
still shares the one global trivial table, even though `std::pair` itself
is not detected as trivially-anything by the primary template.

`destroy_range` is deliberately allowed to be `nullptr` (skipping the call
entirely) when `T` is trivially destructible -- there is no trivial
"no-op" function pointer to fall back to for that one, unlike the other
four operations which always have a `trivial_operator_set` body to point
to.

## Layer 2: untyped storage policies

Five classes hold the actual `void* data_ / size_t size_ / size_t cap_`
triple and the storage-*shape*-specific growth logic; none of them are
templated on `T`:

| Class | Backs | Storage shape |
|---|---|---|
| `unowned_vector_base` | (base of all four below) | Just the `data_`/`size_`/`cap_`/`operations_` fields and every operation expressible without knowing whether storage is inline, heap, mixed, or caller-owned (`try_resize_base`, `try_insert_at_base`, `try_erase_at_base`, `retain_base`, `dedup_by_base`, ...) |
| `heap_vector_base` | `vector<T>` | `allocator_ref`-owned heap allocation only; no inline buffer, unbounded growth (`try_reserve_base` may always call `allocator_ref::expand_in_place`/`reallocate`) |
| `inline_vector_base` | `inline_vector<T, Capacity>` | Fixed-capacity buffer embedded in the object; no allocator; growth past `Capacity` fails with `error::capacity_exceeded` |
| `mixed_vector_base` | `sso_vector<T, InlineCapacity>` | Starts in an embedded inline buffer like `inline_vector_base`, promotes to an `allocator_ref`-owned heap allocation once `InlineCapacity` is exceeded, exactly like `basic_string`'s small-string optimization |
| `outline_vector_base` | `outline_vector<T>` | Fixed-capacity buffer the *caller* owns (a `span<std::byte>` bound once at construction); no allocator, no move support at all -- see below |

Every `try_*_base` primitive on `unowned_vector_base` takes the caller's
`inline_storage`/`max_inline`/`max_cap` explicitly as parameters rather than
reading them through a virtual call, so `heap_vector_base` (which has no
inline storage) simply passes `get_inline_storage() == nullptr` /
`inline_capacity() == 0`, and the shared logic branches on those values
instead of needing four separate reimplementations of, say,
"reserve" -- `try_reserve_base` handles going inline-to-heap (mixed),
heap-only growth, and rejecting growth past a fixed capacity (inline and
outline alike) all in one function body. `inline_vector_base` and
`outline_vector_base` are structurally near-identical from
`unowned_vector_base`'s point of view -- both pass a fixed, non-reallocatable
storage pointer as `inline_storage` with `max_inline == max_cap == cap_`, so
`try_reserve_base` treats "growth past this pointer's capacity" identically
for both; the only difference is *whose* buffer that pointer refers to.

Each derived policy additionally implements the handful of operations that
genuinely differ by storage shape and can't be expressed generically:
`destroy_elements`, `get_inline_storage`, `is_inline`, `get_allocator`,
`inline_capacity`, `max_capacity`, and -- for every policy except
`outline_vector_base` -- `move_construct_from_base`/`move_assign_from_base`.
`heap_vector_base`'s `move_construct_from_base` is `constexpr` and trivial
(steal the pointer + reset the source), while `inline_vector_base`/
`mixed_vector_base`'s must actually relocate elements byte range by byte
range since the destination is a *different* object's embedded storage, not
a pointer that can simply be reassigned -- hence those two are
`RELOCO_API`-declared and defined in `vector_base.ipp` instead of inlined
here. `outline_vector_base` implements neither: see "Why
`outline_vector_base` has no move support" below.

### Why `outline_vector_base` has no move support

Every other storage policy's "move" has a well-defined destination to
relocate *into*: `heap_vector_base` reassigns a pointer, `inline_vector_base`/
`mixed_vector_base` copy element bytes into the destination object's *own*
embedded buffer. `outline_vector_base` has no such destination -- its
`data_` points at a span some *other* piece of code owns, and there is no
second span for a hypothetical move target to relocate into; the only thing
a "move" could steal is the pointer *value* itself, which would leave two
live handles racing over how they each thought they'd bound that memory.
Rather than pick an unsound behavior, `outline_vector_base` simply omits
`move_construct_from_base`/`move_assign_from_base`, and `outline_vector<T>`
explicitly `= delete`s its own move constructor/assignment (see
"How the four public containers plug in" below) -- `unowned_vector_base`
already deletes copy *and* move at the C++ special-member level, so this is
enforced structurally, not just by convention.

## Layer 3: `typed_vector_base<T, Base>`

```cpp
template <typename T, typename Base> class typed_vector_base : public Base {
public:
  using value_type = T;
  using reference = T &;
  // ... the usual container member-type aliases ...

  [[nodiscard]] result<void> try_reserve(size_type new_cap) & noexcept {
    return Base::try_reserve_base(Base::get_allocator(), metadata_for<T>, new_cap,
                                   Base::get_inline_storage(), Base::inline_capacity(),
                                   Base::max_capacity(metadata_for<T>));
  }
  // ... try_resize, try_push_back, try_emplace_back, try_erase_at, retain,
  //     dedup_by, data(), begin()/end(), operator[], try_at, ...
};
```

This is the layer `vector<T>`, `inline_vector<T, Capacity>`,
`sso_vector<T, InlineCapacity>`, and `outline_vector<T>` actually derive from
(`typed_vector_base<T, heap_vector_base>`, `typed_vector_base<T,
inline_vector_base>`, `typed_vector_base<T, mixed_vector_base>`,
`typed_vector_base<T, outline_vector_base>` respectively). It reintroduces
every `T`-aware surface a caller sees -- member-type aliases, the
checked/`try_*`/`unsafe_*` tri-tier accessors (see
[Container contract](container-contract.md)), `try_emplace_back`,
`try_insert_at`, `retain`/`dedup_by` with strongly-typed predicates -- and
every non-trivial member function does nothing but forward straight to the
matching `Base::*_base` primitive, passing `metadata_for<T>` and the
policy's own `get_inline_storage()`/`inline_capacity()`/`max_capacity()` so
`Base` stays completely ignorant of `T`.

`typed_vector_base` also owns the `RELOCO_BLOCK_RVALUE_ACCESS(T)` and
`RELOCO_LIFETIMEBOUND` annotations for the whole tri-tier surface, so
`vector<T>`/`inline_vector<T, Capacity>`/`sso_vector<T, InlineCapacity>`/
`outline_vector<T>` inherit them for free instead of repeating them four
times. Because `typed_vector_base<T, Base>` never itself declares a move
constructor, a concrete container built on a `Base` whose own move ctor/
assignment is deleted (`outline_vector_base`, transitively via
`unowned_vector_base`) automatically ends up with an implicitly-deleted
move constructor too -- `outline_vector<T>` doesn't need to do anything
special to become immovable beyond not declaring one of its own.

## How the four public containers plug in

```cpp
// vector.hpp
template <typename T>
class RELOCO_OWNER vector : public detail::typed_vector_base<T, detail::heap_vector_base> { ... };

// inline_vector.hpp
template <typename T, std::size_t Capacity>
class RELOCO_OWNER inline_vector : detail::inline_vector_storage<T, Capacity>,
                                    public detail::typed_vector_base<T, detail::inline_vector_base> { ... };

// sso_vector.hpp
template <typename T, std::size_t InlineCapacity>
class RELOCO_OWNER sso_vector : detail::inline_vector_storage<T, InlineCapacity>,
                                 public detail::typed_vector_base<T, detail::mixed_vector_base> { ... };

// outline_vector.hpp -- RELOCO_POINTER, not RELOCO_OWNER: it never owns the bytes it manages elements in.
template <typename T>
class RELOCO_POINTER outline_vector : public detail::typed_vector_base<T, detail::outline_vector_base> {
  constexpr explicit outline_vector(span<std::byte> storage RELOCO_LIFETIMEBOUND
                                         RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : /* typed_vector_base<T, outline_vector_base> */ base_t(storage.data(), storage.size() / sizeof(T)) {}

  outline_vector(const outline_vector &) = delete;
  outline_vector &operator=(const outline_vector &) = delete;
  outline_vector(outline_vector &&) = delete;
  outline_vector &operator=(outline_vector &&) = delete;
};
```

`inline_vector_storage<T, Capacity>` is just the `alignas(...) std::byte
storage_bytes_[sizeof(T) * Capacity]` buffer (see `alignment.hpp`); it is
inherited *before* `typed_vector_base` so its address is stable and known
by the time the base class constructor runs, letting `inline_vector`/
`sso_vector` pass `this->storage_bytes_` straight into
`typed_vector_base`'s constructor (which forwards it down to
`inline_vector_base`/`mixed_vector_base`). `outline_vector<T>` needs no such
mixin -- there is no embedded buffer to give an address to, since the bytes
it uses live in the caller's `span<std::byte>` instead; it derives from
`typed_vector_base<T, outline_vector_base>` alone.

`outline_vector<T>` is tagged `RELOCO_POINTER`, unlike the other three
`RELOCO_OWNER` flavors (see [Container contract](container-contract.md#class-declaration-template)):
it manages *element* construction/destruction inside the bound span, but
does not own the span's *bytes* -- the same distinction that makes
`mutable_sequence_container_ref<T>` (see `container_ref.hpp`) `RELOCO_POINTER`
despite mutating the container it's bound to.

Each concrete container is responsible only for:

- Constructors/destructor (wiring up the allocator, inline buffer, or bound
  span; calling `destroy_elements`/`move_construct_from_base`/
  `move_assign_from_base` at the right lifecycle points, where the flavor
  supports moving at all).
- The `try_create`/`try_allocate`/`try_clone`/`try_clone_at` fallible
  construction protocol (see [Fallible construction](fallible-construction.md)),
  since that protocol is inherently container-specific (e.g. `vector<T>`
  takes an allocator up front, `inline_vector<T, Capacity>` never does).
  `outline_vector<T>` implements none of it: binding an already-allocated
  span can never itself fail, so its plain constructor is sufficient.
- Anything genuinely unique to that flavor: `inline_vector`/`sso_vector`'s
  `try_to_vector()` upgrade path, `vector<T>`'s unconditional
  `is_trivially_relocatable` specialization vs. `inline_vector`/
  `sso_vector`'s conditional ones (see [Trivial relocation](relocatable.md)),
  `outline_vector<T>`'s explicitly deleted move constructor/assignment (and
  its *lack* of an `is_trivially_relocatable` specialization at all -- the
  primary template's `std::is_trivially_copyable<T>` fallback already
  reports `false` once copy is deleted, correctly forbidding relocation
  along with the move it would otherwise stand in for).

Everything else -- growth, resize, insert/erase, retain/dedup, the full
tri-tier access surface -- is inherited unchanged from `typed_vector_base`.

## Shared-library considerations

`unowned_vector_base`'s heavier primitives (`try_reserve_base`,
`try_resize_base`, `try_insert_at_base`, `try_erase_at_base`,
`retain_base`, `dedup_by_base`, `shrink_to_fit_base`), each storage
policy's `destroy_elements`/`move_construct_from_base`/
`move_assign_from_base` (where non-trivial), and `trivial_operator_set`
are declared `RELOCO_API` in `vector_base.hpp` and defined out-of-line in
`vector_base.ipp`, included from `vector_base.hpp` itself only when
`RELOCO_SHARED_PROVIDE_DEFINITIONS` is set -- exactly like
`heap_allocator.ipp`/`error_std.ipp` guard their own bodies.
`trivial_type_operations`'s bodies follow the identical pattern one layer
down, declared in `type_operations.hpp` and defined in
`type_operations.ipp`, included from `type_operations.hpp` itself under
the same guard. See [Shared-library deployments](shared-library.md) for
what that macro means and when it's set for you automatically vs.
requires `RELOCO_TYPE_INSTANCE`.

Because these bodies are untyped (`void*`/`type_metadata` instead of
`T`/`T*`), they compile and link exactly once regardless of how many
different `vector<T>`/`inline_vector<T, Capacity>`/`sso_vector<T,
InlineCapacity>`/`outline_vector<T>` instantiations a program uses -- the
main reason this split exists, beyond code reuse: it keeps per-`T` template
bloat in a multi-`.so` deployment down to just `typed_vector_base<T,
Base>`'s thin forwarding wrappers.

## Adding a fifth vector flavor

`outline_vector_base`/`outline_vector<T>` is a fully worked, real example of
this process (not a hypothetical) -- read it alongside these steps:

1. Add a new untyped storage policy deriving from `unowned_vector_base`,
   implementing `destroy_elements`, `get_inline_storage`, `is_inline`,
   `get_allocator`, `inline_capacity`, `max_capacity` for the new storage
   shape, plus `move_construct_from_base`/`move_assign_from_base` *if* the
   new flavor supports moving -- it's fine to omit both, like
   `outline_vector_base` does, if there is no sound way to relocate the new
   storage shape (see "Why `outline_vector_base` has no move support"
   above); the concrete container then simply doesn't declare a move
   constructor/assignment of its own, and gets an implicitly-deleted one
   for free.
2. If any of those bodies are non-trivial, declare them `RELOCO_API` and
   define them in `vector_base.ipp`, following the existing
   `inline_vector_base`/`mixed_vector_base`/`outline_vector_base` bodies as
   a template.
3. Declare the public container as `typed_vector_base<T, YourNewBase>`,
   plus whatever constructors/fallible-construction entry points and
   flavor-specific extras it needs (following the "How the four public
   containers plug in" section above). Tag it `RELOCO_OWNER` if it owns the
   bytes its elements live in, or `RELOCO_POINTER` if -- like
   `outline_vector<T>` -- it only owns the *elements*, not the underlying
   storage (see [Container contract](container-contract.md#class-declaration-template)).
4. Do **not** duplicate any `try_*` mutation/access method on the new
   container -- if you find yourself doing so, the missing primitive
   belongs on `unowned_vector_base` (parameterized over
   `inline_storage`/`max_inline`/`max_cap` like its siblings), not on the
   new type.
5. Add the new container to [API reference](reference.md) and follow the
   [Container contract](container-contract.md) checklist for its
   lifetime/rvalue-safety/tri-tier annotations.
