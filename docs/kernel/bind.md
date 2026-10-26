# Binding spans to a loop

`#include <reloco/kernel/kernel.hpp>` (C++20 and newer). Everything lives in `reloco::kernel`.

## Why this exists

A typical kernel loop reads `in` and writes `out` for `i` in `0..n`. It is only safe if both buffers hold at least `n`
elements, and (when `out` is written) if `out` does not overlap `in`. Normally that is a comment, or a check on every
access.

*Binding* does the check once, before the loop:

1. every span has at least `n` elements (else `out_of_bounds`);
2. no mutable span overlaps another span (else `invalid_argument`), because writing one buffer would silently change
   what the other one reads.

The result is a `bound` token. Its domains produce `proven_index` values below `n`, and its views accept only those
indexes. So inside the loop the compiler can drop the per-element check, and the language (not a comment) stops you
from using an index on a buffer that was never checked.

## The pieces

| Name | What it is |
| --- | --- |
| `try_bind<Brand>(n, spans...)` | Checked factory; returns `result<bound<Brand, Ts...>>`. |
| `RELOCO_KERNEL_BIND(n, spans...)` | `try_bind` with a brand unique to the call site. |
| `with_bound(n, f, spans...)` | Binds, then calls `f(token)`; the lambda's type is the brand, so indexes cannot leave it. |
| `bound` | The token: `size()`, `view<I>()`, `domain()`, `blocks<B>()`, `tail<B>()`, `pairs()`, `windows(w)`, `strided(step)`. |
| `bound_span<T, Brand>` | A view with `operator()(proven_index)`, `load`/`store` for blocks, and `subspan(window)`. |
| `allow_alias` | Opt-in for in-place kernels (same buffer in and out). |

The **brand** is just a type used as a name. An index of brand A cannot be passed to a view of brand B, so mixing
indexes from two different bindings is a compile error.

## Example: scale a buffer

```cpp
#include <reloco/kernel/kernel.hpp>
#include <reloco/span.hpp>

namespace k = reloco::kernel;

// out[i] = in[i] * 2 for i in [0, n).
reloco::result<void> scale(reloco::span<int> out, reloco::span<const int> in, std::size_t n) {
  // The one-time check. `out` is mutable, so it must not overlap `in`; both need >= n elements.
  // RELOCO_KERNEL_BIND picks a brand unique to this line, so the indexes below belong to this binding only.
  auto bound = RELOCO_KERNEL_BIND(n, out, in);
  if (!bound)
    return reloco::unexpected(bound.error()); // out_of_bounds or invalid_argument

  auto dst = bound->view<0>(); // first span passed (out)
  auto src = bound->view<1>(); // second span passed (in)

  for (auto i : bound->domain()) // i is a proven_index: always < n
    dst(i) = src(i) * 2;         // no bounds check here
  return {};
}
```

## Example: vector blocks plus a tail

```cpp
// Copy in blocks of 4 elements, then the leftovers one by one.
auto bound = RELOCO_KERNEL_BIND(n, out, in);
if (!bound) return reloco::unexpected(bound.error());
auto dst = bound->view<0>();
auto src = bound->view<1>();

for (auto blk : bound->blocks<4>())  // each block covers 4 in-range elements
  dst.store(blk, src.load(blk));     // load returns a lane (lane_traits<T, 4>::type); store writes it back
for (auto i : bound->tail<4>())      // the n % 4 elements blocks<4>() did not cover
  dst(i) = src(i);
```

## Example: scoped style

```cpp
// The callable is the brand: nothing proven inside it can be used outside.
auto sum = k::with_bound(
    n,
    [](auto &bound) { // generic lambda taking the token
      auto src = bound.template view<0>();
      int total = 0;
      for (auto i : bound.domain())
        total += src(i);
      return total;
    },
    in); // spans come last, after the callable
// `sum` is result<int>: binding errors come back here and the lambda is not run.
```

## In-place kernels

```cpp
// out and in are the same buffer: allowed only when they are exactly the same range of the same element type.
auto bound = RELOCO_KERNEL_BIND_ALIASED(n, buf, reloco::span<const int>(buf));
```

Partial overlap, or the same bytes viewed as another type, is still `invalid_argument`. Two read-only spans may overlap
freely.

## Per-block tables

### Why

Some data has one value per *group* of elements rather than one per element: a gain for every 32 samples, a
rotation angle for every (x, y) pair, a scale for every block of values. That table is shorter than the data
(`n / B` entries), so it cannot share the main binding, whose rule is "every span has at least `n` elements".

`try_blockwise<B>` adds such a table to an existing binding. It checks once that the table has at least `n / B`
entries and does not overlap anything illegally, and returns a `block_span` that is read with a block of the same
binding. The block starting at `q * B` reads entry `q`, and `q < n / B` is already proven.

### How

```cpp
// data: n elements, written in place. gains: one value per block of 4 elements (n / 4 entries), read-only.
auto bound = RELOCO_KERNEL_BIND(data.size(), data);
if (!bound) return reloco::unexpected(bound.error());

// One-time check: gains.size() >= data.size() / 4 and gains does not overlap data.
auto table = bound->try_blockwise<4>(gains);
if (!table) return reloco::unexpected(table.error()); // out_of_bounds or invalid_argument

auto view = bound->view<0>();
for (auto blk : bound->blocks<4>()) {   // blk covers 4 elements; blk's table entry exists
  const std::int32_t g = table.value()(blk);     // entry blk.base() / 4, no check
  blk.for_each_lane([&](auto i) { view(i) = view(i) * g; });
}
// The leftover (size % 4) elements have no table entry, so they are not touched by blocks();
// handle them with bound->tail<4>() and a rule of your own.
```

Unlike `try_bind`, `try_blockwise` never allows exact aliasing: a table that is the same buffer as a writable span is
rejected with `invalid_argument`. Two read-only spans may overlap freely. A table made with `B = 1` is just another
full-length span; prefer plain `try_bind` for that.

## Limits to know about

- A call site has one brand. If the *same* `RELOCO_KERNEL_BIND` line runs twice (for example in a loop) with different
  extents, an index from the first binding type-checks against views of the second. Prefer `with_bound`, or build with
  `RELOCO_KERNEL_CHECKS >= 1` in tests: every `bound_span` access then re-verifies the index (see `checks.md`).
- The alias test compares addresses, so `try_bind` is not `constexpr`.
- Bind takes spans. For 2-D data use `tensor_view` (see `views.md`).
- Fault injection: `fault::bind_extent` and `fault::bind_disjoint` (both `bool &`) let tests force each error path.
