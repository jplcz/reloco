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

## Splicing nodes between intrusive `c_*` lists

**Why**: moving a run of nodes from one list to another (work stealing, handing a batch to a worker, merging queues)
with a pop/push loop touches every node. Splicing only rewires the nodes at the boundaries, so it costs O(1) wherever
the list can reach the ends of the run in constant time. Nothing is allocated and no node is constructed or destroyed;
the moved nodes simply belong to the destination afterwards.

**Which operations exist** (`slow_` marks anything that is not O(1)):

| List | Range splice (O(1)) | Whole-list splice |
|------|---------------------|-------------------|
| `c_tailq`, `c_list_head` | `splice_after/before(pos, other, first, last)`, `splice_front/back(other, first, last)` — `[first, last]` inclusive | `splice_after/before(pos, other)`, `splice_front/back(other)` — all O(1) |
| `c_list` | `splice_after/before(pos, other, first, last)`, `splice_front(other, first, last)` | `slow_splice_after/before/front/back(...)` — O(n): there is no tail pointer, so the last node is found by walking |
| `c_stailq` | `splice_after(pos, other, before_first, last)`, `splice_after(pos, other, last)`, `splice_front/back(other, [before_first,] last)` | `splice_after(pos, other)`, `splice_front/back(other)` — O(1) |
| `c_slist` | `splice_after(pos, other, before_first, last)`, `splice_after(pos, other, last)`, `splice_front(other, [before_first,] last)` | `slow_splice_after/front/back(...)` — O(n), same reason as `c_list` |

Singly linked nodes cannot find their predecessor, so their ranges are named by the node *before* the first moved node
(like `std::forward_list::splice_after`); the overloads without `before_first` start at the head of `other`.
`splice_before` does not exist for singly linked lists for the same reason.

```cpp
struct job {
  int id;
  struct { // layout-compatible with sys/queue.h TAILQ_ENTRY(job)
    job *next = nullptr;
    job **prev = nullptr;
  } link;
};
reloco::c_tailq<job, &job::link> ready;   // jobs waiting to run
reloco::c_tailq<job, &job::link> stolen;  // another worker's queue we are taking work from

// Take the jobs `first` .. `last` (inclusive) out of `stolen` and put them after `pos` in `ready`: O(1).
ready.splice_after(pos, stolen, first, last);

// Take everything `stolen` still has and append it to `ready`: O(1) because a tailq knows its tail.
ready.splice_back(stolen);
```

**Rules**:
* `first..last` must be linked in `other` (walking `next` from `first` reaches `last`); `pos` must be linked in the
  destination and must not be inside the moved range. These are caller preconditions and are not checked (checking
  would cost O(n)).
* The range overloads take `other` mainly to make call sites read symmetrically and, for `c_tailq`/`c_stailq`, to fix
  its tail. For `c_list`, `c_list_head` and `c_slist` the parameter is unused, because every node already knows what
  surrounds it; consequently `other` may be the destination itself, which moves a range within one list.
* The whole-list overloads require `other` to be a different list (checked with `RELOCO_ASSERT`); an empty `other` is a
  no-op, and afterwards `other` is empty and reusable.
