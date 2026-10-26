# Tensor views

`#include <reloco/kernel/kernel.hpp>` (C++20 and newer). Everything lives in `reloco::kernel`.

## Why this exists

A matrix or vector in a numeric kernel is just a flat buffer plus a description: "this is 3 rows of 4 numbers". The
description is *shape* (how many elements along each dimension) and *strides* (how far apart neighbours are in the
buffer). Getting the description wrong reads outside the buffer.

The usual defence is a comment (`// Proof: (rows - 1) * stride + len <= buf.size()`) or a bounds check on every
access. The first is not checked by anything; the second is too slow inside a hot loop.

`tensor_view` takes a third route: **check once, when the view is created, that every element the shape and strides
can reach lies inside the buffer.** After that the view *is* the proof. The cost is a few integer operations at the
boundary, and nothing per element.

## The pieces

| Type | What it is |
| --- | --- |
| `shape<Rank>` | Extents, outermost first: `shape<2>{rows, cols}`. A zero extent is legal and makes the view empty. |
| `strides<...>` | One stride per dimension, in elements. Each is `dynamic_stride` (a runtime value) or `static_stride<N>` (a compile-time constant that costs no storage). |
| `tensor_view<T, Rank, Strides>` | Non-owning handle: a `span<T>` plus the shape and strides. `T = const U` makes it read-only. |
| `try_make_tensor` | The checked factory. The only way to get a view over real storage. |
| `try_make_contiguous_tensor` | Shortcut for plain row-major data with no padding. |
| `lane_traits<T, W>` | What "a vector of `W` elements" means. Defaults to `reloco::array<T, W>`; specialize it to return a SIMD register type. |

## Example: a padded matrix

```cpp
#include <reloco/kernel/kernel.hpp>
#include <reloco/span.hpp>

namespace kernel = reloco::kernel;

// A weight matrix stored as 3 rows, each holding 2 useful values, but rows start every 4 elements
// (padding keeps each row aligned). The buffer therefore needs (3 - 1) * 4 + 2 = 10 elements.
reloco::result<int> sum_weights(reloco::span<const int> buffer) {
  // The unit stride along a row is a compile-time constant, so it takes no storage and folds into the
  // generated code. Only the row stride (4) is a run-time value.
  using padded = kernel::strides<kernel::dynamic_stride, kernel::static_stride<1>>;

  auto view = kernel::try_make_tensor<const int, 2, padded>(
      buffer,        // the backing storage; the view is validated against its size
      {3, 2},        // shape: 3 rows, 2 columns
      padded(4));    // one run-time stride value: the distance between rows
  if (!view)         // out_of_bounds, integer_overflow or invalid_argument, all reported, never a crash
    return reloco::unexpected(view.error());

  int total = 0;
  for (std::size_t r = 0; r < view->extent(0); ++r)
    for (std::size_t c = 0; c < view->extent(1); ++c)
      total += view->at(r, c);   // checked access: fine outside hot loops
  return total;
}
```

For the common "plain row-major, no padding" case you do not have to spell the strides out:

```cpp
// 2 rows x 3 columns in 6 contiguous ints; strides become {3, 1} automatically.
auto m = kernel::try_make_contiguous_tensor<int, 2>(reloco::span<int>(data), {2, 3});
```

## What `try_make_tensor` checks

| Failure | Meaning |
| --- | --- |
| `error::out_of_bounds` | Some reachable element would lie past the end of the storage. The largest offset is `sum((extent - 1) * stride)`, and it must be below `storage.size()`. |
| `error::integer_overflow` | That sum (or an element count) does not fit `size_t`. It is reported, never wrapped. |
| `error::invalid_argument` | A **mutable** view would map two different indexes to the same element (for example stride 0, or rows closer together than their length). |

An empty shape (any extent zero) is always valid, even over empty storage. Read-only views may overlap (stride 0 is a
broadcast); mutable views may not, so two writes can never alias. The overlap test is conservative: it is exact for
nested layouts (row-major, column-major, padded, tiled) and may reject exotic interleavings that happen not to collide.

## Accessing elements

| Call | Check | Use for |
| --- | --- | --- |
| `at(i, j)` | always on; traps on a bad index | ordinary code |
| `try_at(i, j)` | always on; returns `result` | when a bad index is a recoverable error |
| `unsafe_at(i, j)` | debug builds only (`NDEBUG`) | you already know the index is valid |

The view is a pointer-like handle, like `span`: an element reference stays valid as long as the *storage* does, even if
the temporary view that produced it is gone. Under the hood, access does not call the checked `span`/`array`
operators: the offset math and the final element read use `unchecked_at` (see
[hardened-containers.md](../hardened-containers.md)), because the factory already proved the range. Where an extra
re-check is wanted (tests), it is controlled by `RELOCO_KERNEL_CHECKS` ([checks.md](checks.md)).

The next layer (binding views to loop domains so that even the per-access index check disappears) is planned, as are
projections such as slicing and transposing.

## Custom lane types

```cpp
// Teach the framework to use a real SIMD register for 4 floats. The framework only ever calls load/store with a
// span it has already proven to hold exactly 4 elements, so the intrinsic needs no extra check.
template <> struct reloco::kernel::lane_traits<float, 4> {
  using type = my_simd4f;                                         // your register type
  static type load(reloco::span<const float> from);               // from.size() == 4
  static void store(reloco::span<float> to, const type &lane);    // to.size() == 4
};
```

Without a specialization the lane is `reloco::array<T, W>`, which compilers vectorize well.

## Testing the failure paths

`try_make_tensor` exposes two [fault points](../fault-injection.md): `kernel::fault::tensor_max_offset` (receives the
computed largest offset) and `kernel::fault::tensor_disjoint` (receives the overlap verdict of a mutable view). A hook
can corrupt them to drive the `out_of_bounds` and `invalid_argument` branches deterministically. See
`tests/kernel/test_view_fault.cpp`.
