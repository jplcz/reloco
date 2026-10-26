# Loop domains and proven indexes

Headers: `reloco/kernel/index/*.hpp` (all included by `reloco/kernel/kernel.hpp`). Requires C++20.

## The idea in one minute

An ordinary loop index is just a number. Nothing tells the compiler (or a reader) that `i` is smaller than the
buffer size, so every access `data[i]` is a leap of faith.

A **domain** is a loop range that only produces indexes it can *prove* are in range. Each index comes wrapped in a
`proven_index`: a tiny type with **no public constructor**, so the only way to get one is to iterate a domain.
Because the domain was created from a size `n`, every index it hands out is `< n`.

```cpp
#include <reloco/kernel/kernel.hpp>

namespace k = reloco::kernel;

std::size_t sum_of_indexes(std::size_t n) {
  std::size_t total = 0;
  // domain(n) is "all indexes 0 .. n-1".
  // Each `i` is a proven_index: the type itself says "this is < n".
  for (auto i : k::domain(n))
    total += i.value();   // value() gives the plain number when you need it
  return total;
}
```

Today the proof is about the *domain's own size*. Once the planned tensor views and bindings land,
a view will accept only indexes minted for **its** size, so using an index with the wrong
buffer becomes a compile error instead of a memory bug.

## `proven_index<Tag>`

```cpp
// The Tag says which extent the index was proven against.
// The plain factories (domain, blocks, ...) use `unbranded`.
using index_t = reloco::kernel::proven_index<reloco::kernel::unbranded>;
```

| Operation | Meaning |
| --- | --- |
| `i.value()` | The index as a `std::size_t`. |
| `static_cast<std::size_t>(i)` | Same, spelled as an explicit conversion. There is deliberately **no implicit conversion**, so an index cannot silently flow into unchecked code. |
| `==`, `<`, `<=>` | Compare two indexes of the same tag. |
| `index_t{5}`, `index_t(n)` | **Does not compile.** That is the point: you cannot forge a proof. |

Arithmetic on `value()` (for example `i.value() + 1`) gives a plain, **unproven** number again.

## The domains

All of them work with range-based `for`, are `constexpr`, never allocate, and are overflow-safe even when `n` is
near the largest `std::size_t`.

### `domain(n)`: every index

```cpp
// 0, 1, 2, 3, 4
for (auto i : k::domain(5)) { /* i < 5 */ }
```

### `blocks<B>(n)` and `.tail()`: vector-friendly groups

SIMD and unrolled loops process `B` elements per iteration, then finish the leftovers one by one. Hand-writing
`for (i = 0; i + B <= n; i += B)` is the classic off-by-one trap. `blocks<B>(n)` generates exactly `n / B` groups
and guarantees `base + B <= n` for every one of them.

```cpp
std::size_t sum = 0;

// 10 elements, 4 per block  ->  blocks starting at 0 and 4, tail is 8 and 9.
auto chunks = k::blocks<4>(10);

for (auto blk : chunks) {
  // blk.base() + 4 <= 10 is guaranteed by the domain, not by a comment.

  // for_each_lane calls the function once per lane, fully unrolled at compile time.
  // Each lane `i` is a proven index.
  blk.for_each_lane([&](auto i) { sum += i.value(); });

  // Specific lanes are available as compile-time constants: lane<K>() requires K < 4,
  // and a larger K is a compile error.
  auto first = blk.lane<0>();
  auto last = blk.lane<3>();
  (void)first;
  (void)last;
}

// The leftover `n % B` indexes (here 8 and 9). Together, blocks and tail cover [0, n) exactly once.
for (auto i : chunks.tail())
  sum += i.value();
```

If `n < B` there are no blocks and everything lands in the tail. `k::tail<B>(n)` gives only the tail when you do
not need the blocks.

### `pairs(n)`: adjacent pairs

Used for data stored as pairs (complex numbers as re/im, 2-D points, packed nibbles). It is simply `blocks<2>(n)`.

```cpp
for (auto p : k::pairs(7)) {          // pairs (0,1), (2,3), (4,5); index 6 is in pairs(7).tail()
  auto even = p.lane<0>();            // 2*i
  auto odd = p.lane<1>();             // 2*i + 1, guaranteed < 7
  (void)even;
  (void)odd;
}
```

### `windows(n, w)`: sliding windows

A window of width `w` starting at `s` fits when `s + w <= n`. `windows` produces exactly those starts (`n - w + 1`
of them; none if `w == 0` or `w > n`).

```cpp
// All windows of 3 elements inside 8 elements: starts 0 .. 5.
for (auto win : k::windows(8, 3)) {
  win.start();   // plain integer start position
  win.size();    // 3

  // indices() iterates the proven indexes inside this window. Every i < 8.
  for (auto i : win.indices())
    (void)i;

  // first() and last() are proven too (a window has at least one element).
  (void)win.first();
  (void)win.last();
}
```

### `strided(n, step)`: every `step`-th index

```cpp
// n = 10, step = 4  ->  0, 4, 8. A step of 0 yields nothing.
for (auto i : k::strided(10, 4)) { /* i < 10 */ }
```

The count is computed up front and each index is `counter * step`, so the loop never overflows.

### `index_range<Tag>`

The type behind `domain`, `tail`, `block::lanes()` and `window::indices()`: a half-open range `[first, last)` of
proven indexes with `size()`, `empty()`, `first()` and `last()`. You normally get one from the functions above and
do not construct it yourself.

## Using domains with real data (today)

Until views exist, convert at the edge and keep the loop body simple:

```cpp
// out[i] = a[i] + b[i], with the sizes validated once, outside the loop.
reloco::result<void> add(reloco::span<const int> a, reloco::span<const int> b, reloco::span<int> out) noexcept {
  // One cheap, always-on check at the boundary. This is the "outer" safety.
  if (a.size() != out.size() || b.size() != out.size())
    return reloco::unexpected(reloco::error::invalid_argument);

  // The domain is built from out.size(), and every size above was just checked to be equal.
  for (auto i : reloco::kernel::domain(out.size()))
    out[i.value()] = a[i.value()] + b[i.value()];   // still bounds-checked span access (safe, a bit slower)
  return {};
}
```

That version keeps `span`'s checks. The point of the upcoming view layer is to remove them from the hot path
**without** giving up safety, by letting a view accept only indexes proven for its own size.

## Rules to remember

* A domain's indexes are proven only against the size you built it from. Building `domain(n)` and then indexing a
  buffer of a different size is **not** protected until views exist; keep the sizes equal and validated.
* `.tail()` and the blocks together cover the extent exactly once. Handle both, or elements are skipped.
* Do not dereference `end()`. With `RELOCO_KERNEL_CHECKS >= 1` this asserts (see [checks.md](checks.md)).
* Everything is integer-only. The element type of your own data is yours to choose, including floating point; the
  library never computes on elements.
