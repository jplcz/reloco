<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Type-erased base containers: sharing storage logic across `vector`/`inline_vector`/`sso_vector`

`vector<T>`, `inline_vector<T, Capacity>`, and `sso_vector<T,
InlineCapacity>` (`vector.hpp`, `inline_vector.hpp`, `sso_vector.hpp`) all
need the exact same growth/resize/insert/erase/retain/dedup/move logic --
only *where the bytes live* differs (heap-only, inline-only, or
inline-with-heap-fallback). Rather than hand-rolling that logic three times
over, all three are thin, strongly-typed wrappers around a single
type-erased engine defined in `include/reloco/detail/vector_base.hpp`
(out-of-line bodies in `vector_base.ipp`). This page explains that engine's
two-layer design, how the three public containers plug into it, and what to
do if you add a fourth vector flavor.

If you haven't already, read [Container contract](container-contract.md)
first: it covers the lifetime/rvalue-safety/tri-tier-accessor rules every
reloco container follows, including the three described here.
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

```cpp
struct type_metadata {
  std::size_t element_size;
  std::size_t element_alignment;
  bool is_trivially_destructible;
  bool is_trivially_relocatable;
  bool is_trivially_copyable;
  bool is_default_constructible;
};

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
- A compiler-generated closure that loops element-by-element and calls
  through `construction_helpers` (see `construction_helpers.hpp`) for
  fallible per-element construction/cloning -- the exact same tiered
  dispatch `construction_helpers` uses for a single element, just looped.

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

Four classes hold the actual `void* data_ / size_t size_ / size_t cap_`
triple and the storage-*shape*-specific growth logic; none of them are
templated on `T`:

| Class | Backs | Storage shape |
|---|---|---|
| `unowned_vector_base` | (base of all three below) | Just the `data_`/`size_`/`cap_`/`operations_` fields and every operation expressible without knowing whether storage is inline, heap, or mixed (`try_resize_base`, `try_insert_at_base`, `try_erase_at_base`, `retain_base`, `dedup_by_base`, ...) |
| `heap_vector_base` | `vector<T>` | `allocator_ref`-owned heap allocation only; no inline buffer, unbounded growth (`try_reserve_base` may always call `allocator_ref::expand_in_place`/`reallocate`) |
| `inline_vector_base` | `inline_vector<T, Capacity>` | Fixed-capacity buffer embedded in the object; no allocator; growth past `Capacity` fails with `error::capacity_exceeded` |
| `mixed_vector_base` | `sso_vector<T, InlineCapacity>` | Starts in an embedded inline buffer like `inline_vector_base`, promotes to an `allocator_ref`-owned heap allocation once `InlineCapacity` is exceeded, exactly like `basic_string`'s small-string optimization |

Every `try_*_base` primitive on `unowned_vector_base` takes the caller's
`inline_storage`/`max_inline`/`max_cap` explicitly as parameters rather than
reading them through a virtual call, so `heap_vector_base` (which has no
inline storage) simply passes `get_inline_storage() == nullptr` /
`inline_capacity() == 0`, and the shared logic branches on those values
instead of needing three separate reimplementations of, say,
"reserve" -- `try_reserve_base` handles going inline-to-heap (mixed),
heap-only growth, and rejecting growth past a fixed `Capacity` (inline) all
in one function body.

Each derived policy additionally implements the handful of operations that
genuinely differ by storage shape and can't be expressed generically:
`destroy_elements`, `move_construct_from_base`, `move_assign_from_base`,
`get_inline_storage`, `is_inline`, `get_allocator`, `inline_capacity`,
`max_capacity`. `heap_vector_base`'s `move_construct_from_base` is `constexpr`
and trivial (steal the pointer + reset the source), while
`inline_vector_base`/`mixed_vector_base`'s must actually relocate elements
byte range by byte range since the destination is a *different* object's
embedded storage, not a pointer that can simply be reassigned -- hence
those two are `RELOCO_API`-declared and defined in `vector_base.ipp`
instead of inlined here.

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

This is the layer `vector<T>`, `inline_vector<T, Capacity>`, and
`sso_vector<T, InlineCapacity>` actually derive from
(`typed_vector_base<T, heap_vector_base>`, `typed_vector_base<T,
inline_vector_base>`, `typed_vector_base<T, mixed_vector_base>`
respectively). It reintroduces every `T`-aware surface a caller sees --
member-type aliases, the checked/`try_*`/`unsafe_*` tri-tier accessors
(see [Container contract](container-contract.md)), `try_emplace_back`,
`try_insert_at`, `retain`/`dedup_by` with strongly-typed predicates -- and
every non-trivial member function does nothing but forward straight to the
matching `Base::*_base` primitive, passing `metadata_for<T>` and the
policy's own `get_inline_storage()`/`inline_capacity()`/`max_capacity()` so
`Base` stays completely ignorant of `T`.

`typed_vector_base` also owns the `RELOCO_BLOCK_RVALUE_ACCESS(T)` and
`RELOCO_LIFETIMEBOUND` annotations for the whole tri-tier surface, so
`vector<T>`/`inline_vector<T, Capacity>`/`sso_vector<T, InlineCapacity>`
inherit them for free instead of repeating them three times.

## How the three public containers plug in

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
```

`inline_vector_storage<T, Capacity>` is just the `alignas(...) std::byte
storage_bytes_[sizeof(T) * Capacity]` buffer (see `alignment.hpp`); it is
inherited *before* `typed_vector_base` so its address is stable and known
by the time the base class constructor runs, letting `inline_vector`/
`sso_vector` pass `this->storage_bytes_` straight into
`typed_vector_base`'s constructor (which forwards it down to
`inline_vector_base`/`mixed_vector_base`).

Each concrete container is responsible only for:

- Constructors/destructor (wiring up the allocator or inline buffer,
  calling `destroy_elements`/`move_construct_from_base`/
  `move_assign_from_base` at the right lifecycle points).
- The `try_create`/`try_allocate`/`try_clone`/`try_clone_at` fallible
  construction protocol (see [Fallible construction](fallible-construction.md)),
  since that protocol is inherently container-specific (e.g. `vector<T>`
  takes an allocator up front, `inline_vector<T, Capacity>` never does).
- Anything genuinely unique to that flavor: `inline_vector`/`sso_vector`'s
  `try_to_vector()` upgrade path, `vector<T>`'s unconditional
  `is_trivially_relocatable` specialization vs. `inline_vector`/
  `sso_vector`'s conditional ones (see [Trivial relocation](relocatable.md)).

Everything else -- growth, resize, insert/erase, retain/dedup, the full
tri-tier access surface -- is inherited unchanged from `typed_vector_base`.

## Shared-library considerations

`unowned_vector_base`'s heavier primitives (`try_reserve_base`,
`try_resize_base`, `try_insert_at_base`, `try_erase_at_base`,
`retain_base`, `dedup_by_base`, `shrink_to_fit_base`) and each storage
policy's `destroy_elements`/`move_construct_from_base`/
`move_assign_from_base` (where non-trivial) are declared `RELOCO_API` in
`vector_base.hpp` and defined out-of-line in `vector_base.ipp`, included
from `vector_base.hpp` itself only when `RELOCO_SHARED_PROVIDE_DEFINITIONS`
is set -- exactly like `heap_allocator.ipp`/`error_std.ipp` guard their own
bodies. See [Shared-library deployments](shared-library.md) for what that
macro means and when it's set for you automatically vs. requires
`RELOCO_TYPE_INSTANCE`.

Because these bodies are untyped (`void*`/`type_metadata` instead of
`T`/`T*`), they compile and link exactly once regardless of how many
different `vector<T>`/`inline_vector<T, Capacity>`/`sso_vector<T,
InlineCapacity>` instantiations a program uses -- the main reason this
split exists, beyond code reuse: it keeps per-`T` template bloat in a
multi-`.so` deployment down to just `typed_vector_base<T, Base>`'s thin
forwarding wrappers.

## Adding a fourth vector flavor

1. Add a new untyped storage policy deriving from `unowned_vector_base`,
   implementing `destroy_elements`, `move_construct_from_base`,
   `move_assign_from_base`, `get_inline_storage`, `is_inline`,
   `get_allocator`, `inline_capacity`, `max_capacity` for the new storage
   shape.
2. If any of those bodies are non-trivial, declare them `RELOCO_API` and
   define them in `vector_base.ipp`, following the existing
   `inline_vector_base`/`mixed_vector_base` bodies as a template.
3. Declare the public container as `typed_vector_base<T, YourNewBase>`,
   plus whatever constructors/fallible-construction entry points and
   flavor-specific extras it needs (following the "How the three public
   containers plug in" section above).
4. Do **not** duplicate any `try_*` mutation/access method on the new
   container -- if you find yourself doing so, the missing primitive
   belongs on `unowned_vector_base` (parameterized over
   `inline_storage`/`max_inline`/`max_cap` like its siblings), not on the
   new type.
5. Add the new container to [API reference](reference.md) and follow the
   [Container contract](container-contract.md) checklist for its
   lifetime/rvalue-safety/tri-tier annotations.
