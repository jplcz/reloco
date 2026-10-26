# Kernel framework: bounds-proven loops

> Requires **C++20 or newer**. The rest of reloco still builds as C++17.

"Kernel" in this directory means a **compute kernel**: a hot numeric loop such as a dot product, a normalization
or a matrix-vector product. It has nothing to do with an operating system kernel or the `RELOCO_KERNEL` macro.

## Why does this exist?

Fast loops read memory through raw pointers, because a bounds check on every element stops the compiler from
vectorizing the loop and costs a compare and a branch per access. The price is that an off-by-one error (`i + 4 <= n`
written as `i + 4 < n`, a forgotten tail, a stride that is one row too long) silently reads or writes outside the
buffer.

The usual defence is a comment that says "Proof: this loop only touches indexes below n". Comments are never
compiled, never tested, and go stale the first time somebody edits the loop.

This framework replaces the comment with **types and one-time checks**:

1. You validate sizes **once**, before the loop (cheap, always on).
2. The loop then iterates over **domains** that only ever produce indexes which are provably in range.
3. In release builds the loop compiles to the same machine code as hand-written pointer code.
4. In test builds you can switch on extra checks that re-verify every proof (see [checks.md](checks.md)).

## Where to read next

| Guide | What it covers |
| --- | --- |
| [index.md](index.md) | Loop domains (`domain`, `blocks`, `tail`, `pairs`, `windows`, `strided`) and `proven_index`. |
| [bind.md](bind.md) | `try_bind`, `with_bound`, `bound`, `bound_span`, `allow_alias`: prove once that several spans are long enough and do not alias, then index them without checks. |
| [benchmarks.md](benchmarks.md) | The codegen contract (`scripts/check-kernel-codegen.sh`) and the timing benchmark: the bound loop compiles like the hand-written unchecked one. |
| [algorithms.md](algorithms.md) | `fill`, `try_copy`, `try_transform`, `try_zip_transform`, `reduce`, `try_sum`, `try_dot`, `try_min`/`try_max`, `try_argmin`/`try_argmax`, scans: checked once, no per-element checks. |
| [projections.md](projections.md) | `try_slice`, `try_index`, `try_row`, `try_column`, `try_tile`, `transpose`: zero-copy sub-views of a `tensor_view`. |
| [views.md](views.md) | `shape`, `strides`, `tensor_view`, `try_make_tensor`, `lane_traits`: validate a buffer's layout once, then access it. |
| [examples.md](examples.md) | Four worked kernels (`rescale`, `rotate_pairs`, `row_dots`, `moving_sum`) with scalar twins. |
| [checks.md](checks.md) | `RELOCO_KERNEL_CHECKS`: when the inner loop is verified, and why mixing levels is safe. |

## Include

```cpp
// One include for everything in the framework.
#include <reloco/kernel/kernel.hpp>
```
