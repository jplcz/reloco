<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Tree containers: `tree_set`/`tree_map` and the `detail::tree_base` engine

`tree_set<T, Compare>` and `tree_map<Key, Mapped, Compare>` (`tree_set.hpp`,
`tree_map.hpp`) are thin, `Key`/`Mapped`-aware wrappers around
`detail::tree_base<T, Compare, KeyOf>` (`detail/tree_base.hpp`): a basic,
unbalanced, node-based binary search tree, allocator-managed only -- unlike
`vector`'s four storage flavors, there is exactly one storage policy here,
so there is no combinatorial "share one algorithm across several storage
backends" problem to solve with a fully type-erased engine (see "Why
`tree_base` is *not* fully type-erased" below).

If you haven't already, read [Type-erased base containers](
type-erased-base-containers.md) first: `tree_base` reuses two of its lower
layers as-is (`detail/type_metadata.hpp` and `detail/type_operations.hpp`),
and this page assumes familiarity with `metadata_for<T>`/`type_operations`.

## Layer 1: `node_base`, a single-allocation `[header][payload]` node layout

Every tree node is one `allocator_ref`-owned allocation, laid out as
`[node_header][T payload]`, header first (`detail/node_base.hpp`):

```cpp
struct node_header {
  node_header *parent = nullptr;
  node_header *left = nullptr;
  node_header *right = nullptr;
  unsigned char aux = 0; // reserved for a future balancing policy
};

class node_base {
  static constexpr std::size_t header_size = sizeof(node_header);
  static constexpr std::size_t header_alignment = alignof(node_header);

  static constexpr std::size_t payload_offset(const type_metadata &type) noexcept; // header_size, rounded up to type.element_alignment
  static constexpr std::size_t node_alignment(const type_metadata &type) noexcept; // max(header_alignment, type.element_alignment)
  static constexpr std::size_t node_size(const type_metadata &type) noexcept;      // payload_offset(type) + type.element_size

  static void *payload_of(node_header *node, const type_metadata &type) noexcept;
  static node_header *header_of(void *payload, const type_metadata &type) noexcept;

  static result<node_header *> try_allocate_node(allocator_ref alloc, const type_metadata &type) noexcept;
  static void deallocate_node(allocator_ref alloc, node_header *node, const type_metadata &type) noexcept;
};
```

`node_base` is deliberately **not** templated on `T` or on an alignment
value: `payload_offset()`/`node_alignment()`/`node_size()` take a runtime
`const type_metadata &` (exactly like `vector_operations`/`type_operations`
do) and compute the header-to-payload offset with one branch-free
`(header_size + mask) & ~mask` round-up, rather than instantiating a fresh
`node_base<Alignment>` per distinct alignment value used anywhere in the
program. `payload_of()`/`header_of()` are each other's inverse --
`header_of(payload_of(node, type), type) == node` -- and both go through
`std::launder()`, since the byte range they reinterpret was placement-new'd
as one type (`node_header` or the payload's `T`) and is being viewed
through a pointer of the other.

This header only provides layout and raw allocate/deallocate of one node's
storage; it does not construct or destroy the payload itself.

## Layer 2: `tree_base<T, Compare, KeyOf>`

`tree_base` implements the actual BST algorithm -- comparison-driven
insert/find/erase, in-order traversal -- as a template instantiated per
`(T, Compare, KeyOf)`, exactly like `flat_container_base<Storage, Compare,
KeyOf>`. `KeyOf` is the same `identity_key_of`/`pair_key_of` pair of
functors `flat_container_base.hpp` already defines (`tree_set` uses the
former, `tree_map` the latter), reused as-is rather than duplicated.

### Why `tree_base` is *not* fully type-erased

Unlike `vector_base.hpp`, `tree_base` does not erase `Compare`/`KeyOf`
behind function pointers. Two things point the same way here, more firmly
than they did for `flat_container_base` (see that analysis in
[Type-erased base containers](type-erased-base-containers.md)):

- **The remaining templated code is exactly the hot path.** Once node
  layout (`node_base`) and payload destruction (`type_operations`) are
  factored out, all that is left templated on `(T, Compare, KeyOf)` is the
  comparison-driven walk in `try_insert`/`find_node` -- the `O(log n)`
  (worst case `O(n)`, see below) loop where every visited node calls
  `Compare`. Erasing `Compare` into a function pointer would force an
  indirect call on that exact loop, undermining the reason to reach for a
  tree in the first place.
- **There is no combinatorial storage-policy problem to solve.** `vector_base`
  erases `T` because *five* storage policies (unowned/inline/sso/outline/
  mixed) all need the *same* grow/shrink/insert algorithm -- erasing lets
  that logic exist once instead of five times. `tree_base` deliberately has
  only one storage policy (allocator-only), so there is no duplication
  across policies for erasure to cancel out; the only duplication that
  exists is across different `(T, Compare, KeyOf)` triples, and `Compare`/
  `KeyOf` are almost always small, stateless types (`std::less<T>`,
  `identity_key_of`, `pair_key_of`), so that duplication is small too.

What *is* erased -- node layout and payload destruction -- lives on the
`O(1)`-per-node side of every operation, not the `O(log n)`-per-comparison
side, which is exactly where `vector_operations`/`type_operations`
themselves draw the same line (range/element operations, never the
caller's own indexing arithmetic).

### Structural helpers: small inline, larger out-of-line

`tree_base.hpp` splits its non-template, `T`-independent logic by size,
the same way `vector_base.hpp` does:

- **Tiny navigation helpers stay `inline` in the header**:
  `bst_leftmost`/`bst_rightmost`/`bst_successor`/`bst_predecessor` (in-order
  traversal) and `bst_transplant` (CLRS's single-parent-relink primitive)
  are each a handful of lines; out-of-lining them would only add an extra
  call/return per traversal step for no real code-size benefit.
- **Larger, branchier logic is declared `RELOCO_API` and defined once in
  `tree_base.ipp`**, compiled once for the whole program instead of once
  per `(T, Compare, KeyOf)`:
  - `bst_unlink` -- the full three-case CLRS deletion (no children, one
    child, two children via in-order-successor transplant).
  - `bst_destroy_node`/`bst_clear` -- the only place `tree_base` touches
    `type_operations` at all, routing payload destruction through the
    erased `destroy_one` (a no-op for trivially destructible `T`) so these
    two functions themselves stay non-template.

Deletion never moves or reassigns a `T` value: `bst_unlink` relinks
`node_header` pointers only (CLRS's "transplant" technique), so `T` needs
no move- or copy-assignment operator -- only construct/destroy/(for
`try_clone`) copy, the same requirements every other reloco container
already imposes.

`bst_clear` (backing both `clear()` and the destructor) tears the whole
tree down iteratively, with no recursion and no extra storage: repeatedly
right-rotate away the left child (parent links are not kept consistent --
the whole tree is being discarded) until the root has none, then destroy
that left-child-free node and descend into its former right child. This
avoids unbounded call-stack depth on a degenerate (linked-list-shaped)
tree, which a naive recursive post-order destroy would not.

### It is a *plain, unbalanced* BST

Worst case (e.g. inserting already-sorted input) degenerates to a linked
list: `O(n)` `try_insert`/`try_find`/`try_remove` instead of `O(log n)`.
`node_header::aux` exists precisely so a balancing policy (a red-black
colour bit, an AVL balance factor, ...) can be layered onto this same node
layout later without changing it again -- this engine does not implement
one yet.

## Layer 3: `tree_set`/`tree_map`

`tree_set<T, Compare>` derives from `detail::tree_base<T, Compare,
detail::identity_key_of>`; `tree_map<Key, Mapped, Compare>` derives from
`detail::tree_base<std::pair<Key, Mapped>, Compare, detail::pair_key_of>`.
Each only adds what its base cannot express generically:

- `try_allocate`/`try_create` factory wrappers and `try_clone` re-wrapping
  (identical pattern to `flat_set`/`flat_map`).
- `tree_map` additionally adds the `Key`/`Mapped`-split convenience surface
  `flat_map` also has: `try_insert(key, mapped)`, `try_at(key)` (mutable
  and `const` overloads), and the Rust-`entry`-flavored
  `try_entry_or_insert`/`try_entry_or_insert_with`/`try_entry_and_modify`.
  `tree_map` has no `operator[]`, for the same reason `flat_map` does not:
  it would need to silently default-construct and insert a missing key,
  which reloco's fallible-everywhere design does not permit.

Both get a `container_ref_traits` specialization (associative, same shape
as `flat_set`/`flat_map`'s) and an `is_trivially_relocatable` specialization
that delegates to `is_trivially_relocatable<Compare>`: a `tree_set`/
`tree_map`'s own members (a node pointer, a size, an `allocator_ref`, and
`Compare` itself) never point into `*this`, so relocating its bytes is
always safe regardless of `T`/`Key`/`Mapped` -- those values live in
separately allocated nodes a byte-copy of the container itself never
touches.

Neither gets a `collection_view_traits` specialization: that trait models
`O(1)` `at(index)`/`data()` random access, which a node-based tree
fundamentally cannot offer (an index-based lookup would have to walk the
tree), unlike `flat_set`/`flat_map`'s contiguous `vector<T>`-backed
storage.

### Rust `BTreeSet`/`BTreeMap`-flavored API surface

Both containers keep every element in ascending `Compare` order at all
times (that is the whole point of a BST), so `tree_base` exposes several
Rust `BTreeSet`/`BTreeMap` methods that have no `flat_set`/`flat_map`
equivalent today:

| Method (on `tree_set<T>` / `tree_map<K, V>`) | Rust equivalent | Behavior |
|---|---|---|
| `try_first()` / `try_first_key_value()` | `BTreeSet::first` / `BTreeMap::first_key_value` | Reference to the smallest element/entry, without removing it |
| `try_last()` / `try_last_key_value()` | `BTreeSet::last` / `BTreeMap::last_key_value` | Reference to the greatest element/entry, without removing it |
| `try_pop_first()` | `BTreeSet::pop_first` / `BTreeMap::pop_first` | Removes and returns the smallest element/entry |
| `try_pop_last()` | `BTreeSet::pop_last` / `BTreeMap::pop_last` | Removes and returns the greatest element/entry |
| `retain(pred)` | `BTreeSet::retain` / `BTreeMap::retain` | Keeps only elements/entries for which `pred` returns `true`, in one in-order walk |
| `append(other)` | `BTreeSet::append` / `BTreeMap::append` | Moves every element/entry out of `other` into `*this`, leaving `other` empty |
| `is_subset(other)` / `is_superset(other)` / `is_disjoint(other)` | `BTreeSet::is_subset` / `is_superset` / `is_disjoint` | Merge-walk set comparisons, `O(size() + other.size())` |

All of these fail with `error::container_empty` (`try_first`/`try_last`/
`try_pop_first`/`try_pop_last`) rather than returning an empty
`optional`-like value, consistent with every other fallible reloco API.

`append` is the one operation worth calling out specifically: it never
allocates or constructs a new `T`. Each element being moved already lives
in its own `node_base` allocation in `other`'s tree; `append` walks
`other` in order, structurally unlinks each node (`bst_unlink`, the same
primitive `try_remove` uses -- see Layer 2 above), and re-links it directly
into `*this`'s tree via a small `insert_node` helper, so only
`node_header` pointers move, never `T` itself. On a key collision (present
in both `*this` and `other`), the entry already in `*this` is unlinked and
destroyed first, matching Rust's "`self`'s value is overwritten by
`other`'s" semantics -- then the incoming node is inserted at that
now-vacant key with a plain retry, which is guaranteed collision-free
since the conflicting key was just removed.

### `tree_set`/`tree_map` vs. `flat_set`/`flat_map`

Both pairs are sorted, unique-key associative containers with the same
fallible `try_insert`/`try_find`/`contains`/`try_remove` surface, but they
make opposite trade-offs:

| | `flat_set`/`flat_map` | `tree_set`/`tree_map` |
|---|---|---|
| Storage | One contiguous, reallocatable `vector<T>` | One allocation per element (`node_base`) |
| Lookup | Binary search, cache-friendly | Pointer-chasing tree walk |
| Insert/erase | `O(n)` shift of the tail | `O(log n)` average, `O(n)` worst case (unbalanced) |
| Reference/pointer stability | Invalidated by any reallocating insert | Stable across insertions; invalidated only by erasing that specific element |
| `capacity()`/`reserve()` | Yes | No such concept |
| Random access (`operator[]`, `data()`) | Yes (`collection_view_traits`) | No |

Prefer `flat_set`/`flat_map` when you mostly build once and then look
things up (the common case) or need reference stability to not matter;
prefer `tree_set`/`tree_map` when you need reference/pointer stability
across insertions, or expect insert-heavy usage interleaved with lookups on
already-large collections where `flat_*`'s `O(n)` shift-on-insert would
dominate.

## Adding a balancing policy

`node_header::aux` and the `bst_*` free functions in `tree_base.hpp` are
the extension points: a red-black/AVL variant would reuse `node_base`
unchanged, keep the same `[header][payload]` layout and `bst_leftmost`/
`bst_successor`/`bst_predecessor`/`bst_transplant` navigation helpers
as-is, and only replace `try_insert`'s post-insertion fixup and
`bst_unlink`'s pre-removal fixup with balance-aware versions that read/
write `aux`. `tree_set`/`tree_map` would not need to change at all -- they
only depend on `tree_base`'s public surface, not its internal balance (or
lack thereof).
