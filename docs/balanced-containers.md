<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Balanced ordered containers: `set` and `map`

`include/reloco/set.hpp`, `include/reloco/map.hpp`,
`include/reloco/detail/rbtree_base.hpp`

## Why they exist

reloco already had two ordered containers, each with a catch:

| Container | Catch |
|---|---|
| `flat_set`/`flat_map` | sorted `vector`: `O(n)` insert/erase, inserts invalidate references |
| `tree_set`/`tree_map` | node-based, but a plain *unbalanced* BST: sorted input (very common!) degrades it to a linked list, `O(n)` per operation |

`reloco::set` and `reloco::map` are the general-purpose choice: a
**red-black** tree, so insert, erase and lookup are `O(log n)` in the
*worst case*, whatever order the data arrives in, and a reference to an
element stays valid while other elements are inserted or erased. If you
do not know which ordered container to use, use these.

## How they work (short version)

Every element lives in its own allocator-owned node (`[header][payload]`,
see `detail/node_base.hpp`). The header's reserved `aux` byte stores the
node colour (0 = red, 1 = black). `rbtree_base` is simply `tree_base`
parameterised with `rb_policy`: after linking a leaf it recolours/rotates to
restore the red-black invariants, and erase relinks the in-order successor
*node* instead of copying values, so `T` needs no assignment operators.
The header packs the colour into the low bit of the parent pointer, so a node
is 24 bytes of bookkeeping instead of 32. `try_clone` copies the node structure
and colours directly in `O(n)` (no comparisons, no rebalancing), which is a
valid red-black tree by construction. The tree height is at most `2 * log2(n + 1)`. The cheap cases (parent is
black on insert, erasing a red leaf) and the rotation helpers are inline; only
the multi-step rebalancing is out of line. The tree caches its leftmost and
rightmost nodes, so `begin()`, `try_first`, `try_last`, `try_pop_first` and
`try_pop_last` are `O(1)` (draining a queue-like set costs no descent). Lookups
and inserts prefetch both children of each visited node, which cut 100k-element
lookup/insert/churn times by roughly 12-18%.

## API

Same shape as `tree_set`/`tree_map`. All mutators are fallible and
return `reloco::result<...>`; there is deliberately no `operator[]`.

`set<T, Compare = std::less<T>>`: `try_create`, `try_allocate(alloc)`,
`try_insert(value)` (`error::already_exists` on duplicate), `contains`,
`try_find`, `try_remove`, `try_first`/`try_last`,
`try_pop_first`/`try_pop_last`, `retain`, `append`,
`is_subset`/`is_superset`/`is_disjoint`, `try_clone`, `clear`, `size`,
`empty`, `height()` (diagnostic, `O(n)`), read-only iteration.

`map<Key, Mapped, Compare = std::less<Key>>`: the above (keyed by `Key`) plus
`try_insert(key, mapped)`, `try_at(key)`, `try_entry_or_insert`,
`try_entry_or_insert_with`, `try_entry_and_modify`,
`try_first_key_value`/`try_last_key_value`; `retain` receives
`(const Key &, Mapped &)`.

Both are trivially relocatable exactly when `Compare` is, and have
`container_ref_traits` adapters.

## Example

```cpp
#include <reloco/map.hpp>
#include <reloco/set.hpp>

// A set that is cheap to build from already-sorted ids (a plain BST would
// degenerate here); `try_create` uses the default allocator.
auto ids = reloco::set<int>::try_create();
if (!ids)
  return; // allocation failure
for (int i = 0; i < 100000; ++i) {
  // Fails with error::already_exists on duplicates; discarding is explicit.
  auto added = ids->try_insert(i);
  if (!added)
    return;
}

// A map from name to score. try_insert takes (key, mapped).
auto scores = reloco::map<std::string, int>::try_create();
if (!scores)
  return;
auto first = scores->try_insert("alice", 10);

// Look up and update in place: try_at returns a reference to the mapped
// value; it stays valid across inserts/erases of *other* keys.
if (auto score = scores->try_at("alice"))
  score->get() += 5;

// Walk in ascending key order (read-only iteration keeps the order intact).
for (const auto &[name, value] : *scores) {
  // use name/value
}
```

## Integration

* Everything allocates through an `allocator_ref` (`try_allocate(alloc)`),
  so it works with `heap_allocator`, `stack_allocator`, arenas, ...
* Choose `tree_set`/`tree_map` only if you want the smaller, simpler
  unbalanced tree and know your input is random.
* Choose `flat_set`/`flat_map` for read-mostly data where cache locality
  and `try_reserve` matter more than insert cost.

## Benchmarks

`benchmarks/containers/bench_ordered_sets.cpp` times `tree_set`, `set` and
`flat_set` on the same workloads, so you can see the trade-off on your own
machine (integer nanoseconds per element, best of 5):

```sh
# Build with optimization; benchmarks are off by default.
cmake -S . -B build-bench -DJPLCZ_RELOCO_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench --target jplcz_reloco_container_bench
./build-bench/jplcz_reloco_container_bench
```

Typical shape of the results (one run, 10 000 `uint32_t`; yours will differ):

| Workload | `tree_set` | `set` | `flat_set` |
|---|---|---|---|
| insert, sorted input | ~7000 | ~70 | ~20 |
| insert, random input | ~95 | ~105 | ~150 |
| lookup after sorted build | ~6000 | ~60 | ~60 |
| insert + erase, random | ~145 | ~170 | ~290 |

Takeaways: `tree_set` is competitive only on random data and collapses on
sorted input (the case `set` exists for); `flat_set` wins on lookup and
iteration but its insert/erase cost grows with size (at 100 000 random
elements it is ~6x slower than `set` to build). `tree_set` sorted cases are
skipped above 20 000 elements because they take quadratic time.

The same workloads also run on `tree_map`/`map`/`flat_map` with
`reloco::string` keys (16 chars) and values (24 chars). Strings are move-only,
so each insert clones a prebuilt pair inside the timed region; that cost is
the same for all three maps. One run, 10 000 elements (ns per element):

| Workload | `tree_map` | `map` | `flat_map` |
|---|---|---|---|
| insert, sorted input | ~24000 | ~280 | ~170 |
| insert, random input | ~260 | ~265 | ~2260 |
| lookup after sorted build | ~28000 | ~160 | ~160 |
| insert + erase, random | ~375 | ~395 | ~4440 |

With string keys the picture is the same, but comparisons are costlier, so
the balanced tree's lookup gap to `flat_set`-style binary search shrinks to
nothing, while `flat_map`'s insert cost (moving whole strings) grows
quickly with size.

### Optimizations that did not pay off

A bounded per-tree freelist of erased nodes (to skip allocator round trips on
insert/erase churn) was prototyped and benchmarked with the `churn
(erase+insert)` row: the difference was within run-to-run noise, because the
system allocator's thread cache is already cheap, so it was not kept. Packing
the colour bit also made no measurable speed difference for small payloads
(it reduces memory, not time).
