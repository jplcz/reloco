<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Intrusive pointers: `intrusive_ptr`, `intrusive_ref_counted`

`include/reloco/intrusive_ptr.hpp`

A reference-counted smart pointer whose refcount lives **inside the pointee**
(Boost `intrusive_ptr`, Linux `kref`, COM `AddRef`/`Release`).

| | `shared_ptr` / `rc` | `intrusive_ptr` |
|---|---|---|
| Handle size | two words | one word |
| Control block | separate or combined | none (count is a member) |
| Weak pointers | yes | no |
| Pointer from `this` | `enable_*_from_this` back-pointer | `intrusive_from_this()`, no extra state |
| Works with legacy retain/release objects | no | yes, via `intrusive_ptr_traits` |

Pick it when handles must be one word, when objects are handed through C APIs
as raw pointers, or when the type already carries a count.

## Building blocks

* `intrusive_ptr<T>`: the pointer. Only calls `intrusive_ptr_traits<T>`.
* `intrusive_ref_counted<Derived, Counter>`: CRTP base implementing the default
  protocol. `Counter` is `atomic_refcount` (default, thread-safe) or
  `local_refcount` (plain integer, single-threaded like `rc`).
* `intrusive_ptr_traits<T>`: customization point (`add_ref`, `release`, optional
  `use_count`, optional `thread_safe`).
* `try_allocate_intrusive<T>(alloc, args...)` / `try_create_intrusive<T>(args...)`:
  allocate, construct (through `construction_helpers::try_construct`, so fallible
  constructors are honoured) and return the first owner as `result<intrusive_ptr<T>>`.

## Basic use

```cpp
#include <reloco/intrusive_ptr.hpp>

// Derive from the counted base. `Derived` (first argument) is the class itself;
// the second argument picks the counter (omitted: atomic, safe across threads).
struct session : reloco::intrusive_ref_counted<session> {
  explicit session(int id) noexcept : id(id) {}
  int id;
};

void use() {
  // Allocates with the default allocator and constructs session(7). Fallible:
  // returns an error (allocation_failed, ...) instead of throwing.
  auto created = reloco::try_create_intrusive<session>(7);
  if (!created)
    return;
  reloco::intrusive_ptr<session> a = *std::move(created); // moves the first owner out (count 1)

  reloco::intrusive_ptr<session> b = a; // copy retains (count 2)
  b.reset();                            // release (count 1)

  // a's destructor drops the last reference: ~session() runs and the storage
  // goes back to the allocator the object came from.
}
```

Use `try_allocate_intrusive<T>(alloc, ...)` with an explicit `allocator_ref` to
choose the allocator. The object remembers it and frees itself there.

## Access tiers

Same tiers as the other smart pointers:

```cpp
reloco::intrusive_ptr<session> p = /* ... */;

p->id;        // checked: RELOCO_ASSERT on null
*p;           // checked: RELOCO_ASSERT on null
p.get();      // raw pointer, may be null, no ownership change
p.try_get();  // result<session *>: error::empty_pointer instead of asserting
p.unsafe_get(); // RELOCO_UNSAFE_BUFFER_USAGE, debug-only null check
p.use_count();  // number of owners, 0 for an empty pointer
```

Accessors are `const &`-qualified and lifetime-bound; calling them on a
temporary `intrusive_ptr` is rejected so the result cannot dangle.

## Getting a pointer back from inside the object

```cpp
struct node : reloco::intrusive_ref_counted<node> {
  void register_with(registry &r) {
    // Returns an owning pointer to *this, or error::pointer_expired when no
    // intrusive_ptr owns the object (called from the constructor, or the
    // object lives on the stack). Never resurrects a dying object.
    if (auto self = intrusive_from_this())
      r.add(*std::move(self));
  }
};
```

## Polymorphism and casts

`intrusive_ptr<Derived>` converts implicitly to `intrusive_ptr<Base>`.
`static_pointer_cast`, `dynamic_pointer_cast` and `const_pointer_cast` retain
the object and return a new pointer. Objects are always destroyed and freed as
their most-derived type (the factory records it), even through a `Base` pointer
and even if `Base` has no virtual destructor.

```cpp
struct shape : reloco::intrusive_ref_counted<shape> { virtual ~shape() = default; };
struct square final : shape { long side[4]; };

auto sq = reloco::try_create_intrusive<square>().value();
reloco::intrusive_ptr<shape> base = sq;                // implicit upcast, retains
auto back = reloco::dynamic_pointer_cast<square>(base); // retains, null on mismatch
```

## Integrating legacy retain/release objects

Specialize `intrusive_ptr_traits` for a type you cannot change:

```cpp
// A C object with its own reference count.
struct c_buffer;
void c_buffer_retain(const c_buffer *);
void c_buffer_release(const c_buffer *);

template <> struct reloco::intrusive_ptr_traits<c_buffer> {
  // Called on every copy of an intrusive_ptr<c_buffer>.
  static void add_ref(const c_buffer &b) noexcept { c_buffer_retain(&b); }
  // Called when an intrusive_ptr<c_buffer> is destroyed or reset.
  static void release(const c_buffer &b) noexcept { c_buffer_release(&b); }
  // Optional: only needed if use_count() is called.
  // static std::size_t use_count(const c_buffer &b) noexcept;
  // Optional: true only if add_ref/release are thread-safe. Absent means false,
  // which makes intrusive_ptr<c_buffer> neither Send nor Sync (see send-sync.md).
  static constexpr bool thread_safe = true;
};
```

Such types are not created by `try_allocate_intrusive` (that requires a
`intrusive_ref_counted` base); whoever owns the storage keeps doing so.

## Interop with raw pointers (unsafe tier)

These are `RELOCO_UNSAFE_BUFFER_USAGE` and must be called inside an
unsafe-buffer block (see [Lifetime safety](lifetime-safety.md)):

```cpp
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
// Take over a reference you already own (no count change), e.g. one returned
// by a C API.
auto owned = reloco::intrusive_ptr<session>::unsafe_adopt(raw);
// Create a new owner of a live object (count + 1).
auto shared = reloco::intrusive_ptr<session>::unsafe_retain(raw);
// Give the reference away without releasing it, e.g. to hand it to C code.
// Pair with unsafe_adopt later or the object leaks.
session *handed_off = owned.unsafe_release();
RELOCO_END_UNSAFE_BUFFER_USAGE
```

## Objects not created by the factory

An `intrusive_ref_counted` object created any other way (stack, member, static)
has no recorded allocator: reaching zero does nothing. It can be shared via
`unsafe_retain` as long as it outlives every `intrusive_ptr` to it. In debug
builds its destructor asserts that no reference is left. Copying, moving or
assigning a counted object never copies the count or allocator: the new object
starts unowned.

## Thread safety

`intrusive_ptr<T>` is `Send`/`Sync` only when the count is atomic
(`atomic_refcount`, or `traits::thread_safe == true`) **and** `T` is itself
`Send` and `Sync`, like `shared_ptr<T>`. With `local_refcount` it is neither,
like `rc<T>`. See [Send and Sync](send-sync.md).

## Other properties

* `is_trivially_relocatable<intrusive_ptr<T>>` is true; `sizeof` is one pointer.
* `std::hash`, comparison operators (including with `nullptr`) and `swap` are provided.
* Reference-count overflow asserts (`RELOCO_ASSERT`).
