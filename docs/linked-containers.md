<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Owning linked containers: `tailq`, `stailq`, `slist`, `list`, `circular_list`

reloco already ships *intrusive* linked containers (`c_tailq`, `c_stailq`, `c_slist`, `c_list`, `c_list_head`): you
embed a hook in your own struct and the container only links nodes you allocated. That is perfect for kernels and
hot paths, but if you simply want "a list of `T`" you would have to design a node type, allocate it, link it and free
it by hand.

The containers on this page are the **owning** counterparts. Each allocates one node per element through an
`allocator_ref`, constructs your `T` inside it (fallibly, no exceptions) and lets the matching intrusive container do
all the linking. They are worth reaching for when you need

* element addresses that stay valid while other elements are inserted or erased,
* O(1) insert/erase at a position you already hold an iterator to,
* non-movable element types.

For everything else prefer `vector`/`vec_deque` (better cache locality).

## Which one do I pick?

| Container | Header | Built on | Links per node | Tail ops (`back`, `push_back`) | O(1) erase at iterator | Iteration |
|-----------|--------|----------|----------------|-------------------------------|------------------------|-----------|
| `tailq<T>` | `tailq.hpp` | `c_tailq` | 2 pointers | yes | yes (`erase`) | forward |
| `stailq<T>` | `stailq.hpp` | `c_stailq` | 1 pointer | yes (no `pop_back`) | after a position (`erase_after`) | forward |
| `slist<T>` | `slist.hpp` | `c_slist` | 1 pointer | no | after a position (`erase_after`) | forward |
| `list<T>` | `list.hpp` | `c_list` | 2 pointers | no | yes (`erase`) | forward |
| `circular_list<T>` | `circular_list.hpp` | `c_list_head` | 2 pointers | yes | yes (`erase`) | bidirectional |

Rule of thumb: `tailq` is the general-purpose choice, `stailq` a cheap FIFO, `slist` a cheap stack, `list` a stack
that also needs O(1) erase, `circular_list` when you need to walk backwards.

## Example

```cpp
#include <reloco/tailq.hpp>

struct job { int id; };

// Allocation can fail, so every inserting operation returns reloco::result<std::reference_wrapper<T>>
// instead of throwing. The container uses reloco::default_allocator() unless you pass an allocator_ref.
reloco::tailq<job> queue;

auto first = queue.try_emplace_back(job{1}); // constructs the element in a freshly allocated node
if (!first) { /* first.error() is e.g. error::allocation_failed */ }
(void)queue.try_push_front(job{0});

queue.front().id;            // checked tier: asserts the container is not empty
queue.try_back();            // fallible tier: error::container_empty instead of asserting
// unsafe_front()/unsafe_back() are the explicitly-unsafe tier (debug assert only).

// erase(it) is O(1) and returns the iterator following the erased element.
auto it = queue.erase(queue.cbegin());

// Insert before a position; before end() means "append" on containers that have a tail.
(void)queue.try_insert(queue.cend(), job{2});

job last = queue.pop_back(); // removes the element and returns it by value (T must be nothrow move-constructible)
```

Using a specific allocator, and cloning into another one:

```cpp
// allocator_ref is a cheap two-word handle; the allocator it points at must outlive the container.
reloco::tailq<job> q(my_allocator_ref);
auto copy = q.try_clone(other_allocator_ref); // deep copy, result<tailq<job>>; also try_clone() with q's allocator
```

Singly linked containers erase *after* a position because they cannot find a node's predecessor:

```cpp
reloco::slist<job> stack;
(void)stack.try_push_front(job{1});
(void)stack.try_push_front(job{2});
// Removes the element following begin(); try_erase_after reports invalid_argument (end()) or out_of_bounds
// (nothing follows) instead of asserting.
stack.erase_after(stack.cbegin());
```

## API summary

Everything below is available on every container unless a capability is named.

| Group | Members |
|-------|---------|
| Construction | `explicit C(allocator_ref = default_allocator())` (does not allocate), `try_create()`, `try_allocate(alloc)`, `try_clone()`, `try_clone(alloc)`; move-only |
| Capacity | `size()` (O(1)), `empty()`, `get_allocator()` |
| Access | `front()`, `try_front()`, `unsafe_front()`; `back()`, `try_back()`, `unsafe_back()` *(tail)* |
| Insert | `try_emplace_front`/`try_push_front`; `try_emplace_back`/`try_push_back` *(tail)*; `try_emplace_after`/`try_insert_after` (position must not be `end()`); `try_emplace`/`try_insert` before a position *(doubly linked)* |
| Remove | `pop_front`/`try_pop_front`; `pop_back`/`try_pop_back` *(tail and doubly linked)*; `erase(it)` *(doubly linked)*; `erase_after(it)`/`try_erase_after(it)` *(singly linked)*; `clear()` |
| Relinking (no allocation) | `try_append(C &&)`, `try_prepend(C &)`, `try_splice(pos, other)` / `(pos, other, it)` / `(pos, other, first, last)` *(doubly linked)*, `try_splice_after(pos, other)` / `(pos, other, prev)` / `(pos, other, before_first, last)` *(singly linked)*, `reverse()`, `retain(pred)` |
| Iteration | `begin`/`end`/`cbegin`/`cend`; forward iterators (bidirectional for `circular_list`) |

Notes you should know about:

* **Safety tiers and rvalues** follow [container-contract.md](container-contract.md): accessors are `&`/`const &`
  qualified and deleted on temporaries, so `make_list().front()` does not compile.
* **Move assignment** destroys the target's elements and then adopts the source's nodes *and* its allocator, so the
  nodes are always freed by the allocator that made them.
* **`try_append`/splicing** require both containers to use the same allocator (`error::invalid_argument`
  otherwise), unless the destination is empty, in which case it adopts the source's allocator. They only
  relink nodes: no allocation, no element moves, moved elements keep their addresses. Containers without a tail first
  walk to the last node. Splicing a container into itself is `invalid_argument`. Iterator arguments must belong to the
  source. Splicing is O(moved nodes); `try_splice` before `end()` is rejected on `list` (no tail).
* **`try_insert(cend(), ...)`** appends on `tailq`/`circular_list` but returns `invalid_argument` on `list`, which
  has no tail pointer.
* **Iterator validity**: inserting never invalidates iterators; erasing invalidates only the erased element's.
  Dereferencing `end()` is a precondition violation.
* Elements must be nothrow move-constructible only for the `pop_*` functions that return `T` by value.
* The containers are not trivially relocatable (the head points into the container itself).

## Integrating with other subsystems

* **Allocators**: pass any `allocator_ref` (stack, bucket, pool, ...); see [allocator-capacity-absorption.md](
  allocator-capacity-absorption.md). The node size is `sizeof(T)` plus the hook, so a `bucket_allocator` sized for the
  node works well.
* **Fallible construction**: elements are built through `construction_helpers::try_construct`, so element types that
  follow the [fallible-construction](fallible-construction.md) protocol (`try_create`, `try_allocate`, ...) work and
  their errors are returned from the `try_emplace*` call. The containers themselves provide `try_create`/
  `try_allocate`, so they can be nested inside other reloco containers.
* **Intrusive layer**: the owning containers are thin; if you need nodes you allocate yourself, use the `c_*`
  containers directly. This work also added `back()` to `c_tailq`/`c_stailq`, `remove_after()` to `c_stailq`,
  `insert_after`/`insert_before` to `c_list_head`, and `operator==` to `allocator_ref`.
