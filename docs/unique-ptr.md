# `unique_ptr` and `unique_polymorphic_ptr`: sole ownership of allocator-backed objects

Headers: `reloco/unique_ptr.hpp`, `reloco/unique_polymorphic_ptr.hpp`

## Why two types

A kernel or embedded component often needs exactly one owner for a heap
object, with allocation that can fail and a specific allocator (not the global
`new`). Both handles share that model: allocation goes through an
`allocator_ref`, every constructor-like entry point returns
`result<handle>` (out-of-memory is a value, not an exception), they are
move-only and trivially relocatable.

They differ in what they pay for:

| | `unique_ptr<T>` | `unique_polymorphic_ptr<Base>` |
|--|--|--|
| Owns | exactly `T` | any `Derived` reachable as `Base` |
| State | pointer + allocator | pointer, complete-object address, block size, destructor function, allocator |
| Needs virtual destructor | no | **no** (destructor is captured when `Derived` is known) |
| Virtual / multiple inheritance | n/a | works |
| Adopt an existing block | no | yes (`adopt`, unsafe) |

Use `unique_ptr<T>` by default: it is the smallest, fastest handle. Reach for
`unique_polymorphic_ptr` only when the owner must not know the concrete type
(a table of drivers, allocators, plugins...).

Conversion goes one way only: `unique_ptr<Derived>` can be moved into a
`unique_polymorphic_ptr<Base>`, but a `unique_polymorphic_ptr` can never become
a `unique_ptr` (the exact type is no longer known).

Why the extra state matters: the allocator contract (`allocator.hpp`) returns a
`mem_block` whose `size` may exceed the request and `deallocate` takes a size.
Freeing `sizeof(Base)` for a derived object would be wrong, and calling
`~Base()` without a virtual destructor would be undefined. The polymorphic
handle therefore remembers the block size and how to destroy the complete
object. `unique_ptr<T>` frees with `sizeof(T)`, which is correct because it only
ever holds exactly `T`. Because the complete-object address is stored, no
downcast is needed, so virtual inheritance works.

## `unique_ptr<T>` API

| Member | Purpose |
|--------|---------|
| `try_allocate(alloc, args...)` | Allocate from `alloc` and construct a `T` (see `fallible-construction.md`). |
| `try_create(args...)` | Same, using `default_allocator()`. |
| `operator*`, `operator->` | Checked access (assert on null). Deleted on rvalues. |
| `get()` / `unsafe_get()` | Raw pointer; `unsafe_get` skips the release-build null check. |
| `reset()` | Destroy the object and free its block. |

## `unique_polymorphic_ptr<Base>` API

| Member | Purpose |
|--------|---------|
| `try_allocate<Derived = Base>(alloc, args...)` | Construct a `Derived` in memory from `alloc`, owned as `Base`. |
| `try_create<Derived = Base>(args...)` | Same, using `default_allocator()`. |
| `adopt<Derived>(alloc, obj, bytes)` | **Unsafe.** Take ownership of an object built elsewhere. |
| converting move from `unique_ptr<Derived>&&` | Upcast an existing exact-type handle. |
| `operator*`, `operator->`, `get()`, `unsafe_get()`, `reset()` | As for `unique_ptr`. |

## Examples

```cpp
#include <reloco/allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/unique_polymorphic_ptr.hpp>

// An interface WITHOUT a virtual destructor: unique_polymorphic_ptr does not
// need one, it destroys the complete object itself.
struct shape {
  int sides() const noexcept { return sides_; }
  int sides_ = 0;
};

struct square : shape {
  explicit square(int e) noexcept : edge(e) { sides_ = 4; }
  int edge;
};

void exact_type(reloco::allocator_ref alloc) {
  // Which allocator owns the memory is explicit; here the caller supplies it.
  // Allocation can fail: the failure is a reloco::error inside the result.
  // 7 is forwarded to square's constructor.
  auto sq = reloco::unique_ptr<square>::try_allocate(alloc, 7);
  if (!sq)
    return;
  // Checked access: asserts if the handle is null.
  (*sq)->sides();
} // freed here
```

```cpp
void through_base(reloco::allocator_ref alloc) {
  // Build a `square`, own it as `shape`. `alloc` supplies the memory;
  // 7 is passed to square's constructor.
  auto s = reloco::unique_polymorphic_ptr<shape>::try_allocate<square>(alloc, 7);
  if (!s)
    return;

  // Upcast an existing exact-type handle (one-way: the reverse does not exist).
  auto exact = reloco::unique_ptr<square>::try_allocate(alloc, 3);
  if (!exact)
    return;
  reloco::unique_polymorphic_ptr<shape> base = std::move(*exact); // `exact` is now empty
} // each object destroyed as a `square`, its block freed with the size the allocator returned
```

```cpp
// Adopting an object you built yourself (unsafe tier: you vouch for it).
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
void adopt_use(reloco::allocator_ref alloc) {
  // The block (and its real size) must come from this same allocator.
  auto block = alloc.allocate(sizeof(square), alignof(square));
  if (!block)
    return;
  // Construct the object at the start of the block yourself.
  auto *obj = ::new (block->ptr) square(5);
  // obj = the complete object (must be at the block start);
  // block->size = the size the allocator returned (not sizeof(square)).
  auto owned = reloco::unique_polymorphic_ptr<shape>::adopt(alloc, obj, block->size);
}
RELOCO_END_UNSAFE_BUFFER_USAGE
```

## Rules to remember

- Prefer `unique_ptr<T>` unless the concrete type must be hidden.
- `adopt` requires the object at the very start of a block from the same
  allocator, with the size that allocator returned.
- A `unique_ptr<Derived>` converted to `unique_polymorphic_ptr` is still freed
  with `sizeof(Derived)`; use `unique_polymorphic_ptr::try_allocate` directly to
  free with the allocator-returned size.
