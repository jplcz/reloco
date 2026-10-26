# Example kernels

## Why

The kernel framework removes per-element bounds checks by proving ranges once. Reading API docs only goes so far;
these four small, general-purpose kernels show the recurring patterns end to end. Each has a plain, fully checked
scalar twin (`kernel_examples::scalar`) so you can see what the fast version replaces, and tests compare the two.

Source: `demos/kernel/example_kernels.hpp`. All arithmetic is integer (no floating point).

## The pattern

1. Validate arguments (cheap, once).
2. Bind the spans: `RELOCO_KERNEL_BIND(extent, a, b, ...)` checks sizes and aliasing once.
3. Loop over a *proven* domain; indexes from it need no checks.

## Kernels

| Kernel | Shows | Domain |
|---|---|---|
| `rescale` | two equal-length vectors, fixed-point multiply/shift/clamp | `bound->domain()` |
| `rotate_pairs` | interleaved (x, y) data with per-pair cos and sin tables, Q15 | `bound->pairs()`, lane `load`/`store`, `try_blockwise<2>` |
| `row_dots` | matrix rows with padded stride times a vector; 4 rows per pass, then a tail | `blocks<4>`, `tail<4>`, `try_row` |
| `moving_sum` | sliding window sums | `bound->windows(w)` |

```cpp
// out holds in.size() int8 values, in is read-only; shift 1..62 keeps the rounding term valid.
auto r = kernel_examples::rescale(out, in, /*multiplier*/ 3, /*shift*/ 4, /*offset*/ 0);
if (!r) { /* r.error(): invalid_argument, out_of_bounds or aliasing */ }
```

## Auditing

Every kernel has a scalar twin with identical results and error codes. Tests run both on random inputs, including
invalid arguments, via `audit::expect_same` (see [algorithms.md](algorithms.md)).
