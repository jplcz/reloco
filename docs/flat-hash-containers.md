<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Flat hash containers: `flat_hash_set`/`flat_hash_map` and the `detail::flat_hash_base` engine

`flat_hash_set<T, Hash, KeyEqual>` and `flat_hash_map<Key, Mapped, Hash,
KeyEqual>` (`flat_hash_set.hpp`, `flat_hash_map.hpp`) are thin,
`Key`/`Mapped`-aware wrappers around `detail::flat_hash_base<T, Hash,
KeyEqual, KeyOf>` (`detail/flat_hash_base.hpp`): an open-addressing,
linear-probing hash table backed by a single `vector<optional<T>>` —
the "flat" (contiguous, vector-backed) counterpart to `tree_base`'s
node-based storage, exactly like `flat_set`/`flat_map` are the contiguous
counterpart to `tree_set`/`tree_map`, only unordered, matching Rust's
`HashSet`/`HashMap`.

## Why build on `vector<optional<T>>` instead of new low-level plumbing

Unlike `tree_base` (which needed its own `node_base` allocation/layout
engine, see [Tree containers](tree-containers.md)), `flat_hash_base` is
built entirely on two already-existing, already-tested reloco types:

- `vector<optional<T>>` is the one contiguous backing array — growable,
  heap-allocated, and **unconditionally trivially relocatable regardless
  of `T`** (`template <typename T> struct is_trivially_relocatable<
  vector<T>> : std::true_type {};`, `vector.hpp`).
- `optional<T>` gives each slot a built-in "empty" state without
  requiring `T` to be default-constructible — solving the classic
  open-addressing "how do I mark a slot as unoccupied without a sentinel
  value or a separate bitmap" problem for free.

Because `try_insert` always hands the engine an already-fully-constructed
`value_type` (a by-value sink parameter, the same convention as every
other reloco container's `try_insert`), placing it into a slot is an
ordinary `optional<T>::emplace(std::move(value))` — a plain,
non-fallible move-construction — rather than routing through
`construction_helpers::try_construct` (which exists to let a single
function support several different *construction-argument* protocols;
here there is only ever one already-built `T` to move in).

## Why there is no `flat_hash_base.ipp`

`tree_base` splits its non-template, `T`-independent logic into a `.ipp`
file compiled once for the whole program (see [Tree containers](
tree-containers.md#structural-helpers-small-inline-larger-out-of-line)):
`bst_unlink`'s CLRS deletion and `bst_clear`'s iterative teardown never
call `Compare` — they only relink/destroy `node_header` pointers, which is
exactly as non-template as `node_base`'s own layout math.

`flat_hash_base`'s two heaviest routines, `grow_to` (rehashing every live
element into a larger array) and `erase_slot` (the backward-shift walk
above), have no equivalent `Hash`-free structural core to peel off: both
call `Hash{}(key_of(value))` on every single element they touch, since a
slot's rehash target (`grow_to`) and a probe sequence's reachability test
(`erase_slot`) are only knowable by actually hashing that element's key.
Moving either routine to a single, `T`-independent `.ipp` definition would
mean routing every one of those hash calls through a `void*`-erased
function pointer — on the same amortized-`O(1)` hot path this whole
design exists to keep inlined — which would cost more than the (typically
small) template-instantiation duplication it would save, especially since
`Hash`/`KeyEqual` are almost always small, stateless types with only a
handful of distinct instantiations in any one program.

## Deletion: backward-shift, no tombstones

Classic open-addressing implementations mark a removed slot with a
tombstone so later insertions/deletions can still tell "empty, never
used" apart from "used, now vacated." Tombstones accumulate over many
insert/remove cycles, though: an all-tombstone probe sequence still has to
be walked in full on every lookup, so a table that has seen heavy
insert/remove churn degrades toward `O(n)` lookups even at low live-element
counts.

`flat_hash_base` avoids this with **backward-shift deletion**
(`erase_slot`, the canonical "deletion in open addressing without
tombstones" algorithm): removing a slot clears it, then walks its probe
sequence forward. Each subsequent occupied slot is relocated back into the
growing hole whenever leaving it in place would make it *unreachable from
its own ideal (hash-derived) slot* now that the hole is gone — i.e.
whenever the hole lies strictly between the element's ideal slot and its
current position, in probe order. The hole then "moves" to that
element's old position, and the walk continues until an already-empty
slot is reached.

The result: after any sequence of insertions and removals, every live
element remains reachable by its own plain linear probe from its ideal
slot — `contains`/`try_find`/`try_remove` never need to special-case
"deleted" slots, and the average probe length never grows purely from
removal history the way tombstone-based deletion's does.

One subtlety this creates for callers walking slots by index while
removing (see `retain` below): backward-shift deletion can move a live
element **into** the slot index that was just vacated, so a naive
forward-only index walk that always advances after a removal would skip
that relocated element.

## Growth: power-of-two capacity, integer-exact load factor

- `capacity()` is always `0` or a power of two.
- `max_load_factor = 0.875` (`7/8`): kept comfortably below `1.0` to keep
  average probe length short without wasting more than 1/8th of the
  backing array.
- Growth decisions use **integer arithmetic exclusively**, never
  floating point: since every non-zero capacity is a multiple of
  `min_capacity == 8`, the "how many occupied slots trigger growth"
  threshold is computed as `cap / 8 * 7`, which is exact at every scale —
  unlike `cap * 0.875` computed in `float`, which can lose precision for
  very large `cap`. (`load_factor()`, the public diagnostic accessor, does
  return a `float` — but nothing on the growth-decision path depends on
  it.)
- Growing rehashes every live element into a freshly allocated, larger
  `vector<optional<T>>` in one pass, then swaps it in; the only fallible
  step is the initial allocation. `try_reserve(additional)` performs this
  proactively (a no-op if the current capacity already suffices, and a
  true no-op — no allocation at all — for `additional == 0`, even on an
  empty table).

## Layer 2: `flat_hash_set`/`flat_hash_map`

`flat_hash_set<T, Hash, KeyEqual>` derives from `detail::flat_hash_base<T,
Hash, KeyEqual, detail::identity_key_of>`; `flat_hash_map<Key, Mapped,
Hash, KeyEqual>` derives from `detail::flat_hash_base<std::pair<Key,
Mapped>, Hash, KeyEqual, detail::pair_key_of>` — the same `KeyOf` functors
`tree_set`/`tree_map` reuse from `flat_container_base.hpp`. Each only adds
what its base cannot express generically:

- `try_allocate`/`try_create` factory wrappers and `try_clone` re-wrapping
  (identical pattern to `flat_set`/`flat_map`/`tree_set`/`tree_map`).
- `flat_hash_map` additionally adds the `Key`/`Mapped`-split convenience
  surface `tree_map`/`flat_map` also have: `try_insert(key, mapped)`,
  `try_at(key)` (mutable and `const` overloads), and the
  Rust-`entry`-flavored `try_entry_or_insert`/`try_entry_or_insert_with`/
  `try_entry_and_modify`. Like `tree_map`/`flat_map`, `flat_hash_map` has
  no `operator[]`: it would need to silently default-construct and insert
  a missing key, which reloco's fallible-everywhere design does not
  permit.

Both get a `container_ref_traits` specialization (associative, same shape
as `flat_set`/`flat_map`/`tree_set`/`tree_map`'s) and an
`is_trivially_relocatable` specialization that delegates to **both**
`Hash` and `KeyEqual` being trivially relocatable: a `flat_hash_set`/
`flat_hash_map`'s own members (a `vector<optional<T>>`, a size, a mask,
and `Hash`/`KeyEqual` themselves) are trivially relocatable regardless of
`T`, since `vector<U>` is unconditionally trivially relocatable for any
`U` — unlike `tree_set`/`tree_map`, which only need to delegate to
`Compare`, `flat_hash_set`/`flat_hash_map` delegate to two template
parameters because a hash table needs both a hasher and an equality
comparator, whereas an ordered tree only needs one three-way `Compare`.

Neither gets a `collection_view_traits` specialization: like `tree_set`/
`tree_map`, a hash table has no `O(1)` `at(index)`/`data()` to offer —
elements are not laid out in insertion order, so an index-based lookup is
not meaningful the way it is for `flat_set`/`flat_map`'s contiguous,
insertion-ordered `vector<T>`.

### Rust `HashSet`/`HashMap`-flavored API surface

| Method (on `flat_hash_set<T>` / `flat_hash_map<K, V>`) | Rust equivalent | Behavior |
|---|---|---|
| `try_take(key)` | `HashSet::take` / `HashMap::remove_entry` (engine level) | Removes and returns the matching element/entry, rather than just discarding it like `try_remove` |
| `try_remove_entry(key)` (map only) | `HashMap::remove_entry` | Removes and returns the `(Key, Mapped)` pair for `key` |
| `retain(pred)` | `HashSet::retain` / `HashMap::retain` | Keeps only elements/entries for which `pred` returns `true` |
| `is_subset(other)` / `is_superset(other)` / `is_disjoint(other)` (set only) | `HashSet::is_subset` / `is_superset` / `is_disjoint` | `O(n)` hash-lookup-based set comparisons |
| `try_entry_or_insert` / `try_entry_or_insert_with` / `try_entry_and_modify` (map only) | `HashMap::entry(key).or_insert(...)` / `.or_insert_with(...)` / `.and_modify(...)` | Same entry-API surface `tree_map` exposes |

All fallible operations return `error::not_found` for a missing key,
`error::already_exists` for `try_insert` on a present key, matching every
other reloco associative container.

`retain` is worth calling out specifically because of backward-shift
deletion's index-reuse subtlety mentioned above: the implementation walks
slot indices but **does not unconditionally advance the index after a
removal** — it re-examines the same index, since a live element may have
just been shifted into it. This is the one place in `flat_hash_base` where
the walk pattern must differ from `tree_base::retain`'s simple
forward-only in-order walk (safe there because `bst_unlink` never
disturbs not-yet-visited nodes).

## `flat_hash_set`/`flat_hash_map` vs. `tree_set`/`tree_map`/`flat_set`/`flat_map`

Four associative container families now exist, each making a different
trade-off:

| | `flat_set`/`flat_map` | `tree_set`/`tree_map` | `flat_hash_set`/`flat_hash_map` |
|---|---|---|---|
| Storage | One contiguous, reallocatable `vector<T>` | One allocation per element (`node_base`) | One contiguous, reallocatable `vector<optional<T>>` |
| Ordering | Sorted by `Compare` | Sorted by `Compare` | Unspecified (hash-derived) |
| Lookup | Binary search, `O(log n)` | Pointer-chasing tree walk, `O(log n)` average | Linear probing, `O(1)` average |
| Insert/erase | `O(n)` shift of the tail | `O(log n)` average, `O(n)` worst case (unbalanced) | `O(1)` average (amortized `O(1)` for growth) |
| Reference/pointer stability | Invalidated by any reallocating insert | Stable across insertions; invalidated only by erasing that specific element | Invalidated by any reallocating insert or by backward-shift deletion moving a *different* element |
| `capacity()`/`reserve()` | Yes | No such concept | Yes |
| Random access (`operator[]`, `data()`) | Yes (`collection_view_traits`) | No | No |

Prefer `flat_hash_set`/`flat_hash_map` when you do not need sorted
iteration and want the fastest average-case lookup/insert/remove; prefer
`flat_set`/`flat_map` when you need sorted iteration with a contiguous,
`data()`-indexable backing store; prefer `tree_set`/`tree_map` when you
need reference/pointer stability across insertions, or the `BTreeSet`/
`BTreeMap`-flavored `try_first`/`try_last`/`try_pop_first`/`try_pop_last`/
`append` API (see [Tree containers](
tree-containers.md#rust-btreesetbtreemap-flavored-api-surface)).
