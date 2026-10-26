# Projections: rows, columns, tiles, transposes, reshapes and broadcasts

`#include <reloco/kernel/kernel.hpp>` (C++20 and newer). Everything lives in `reloco::kernel`.

## Why this exists

Numeric code constantly needs "part of" a tensor: one row of a matrix, one column, a block, or the matrix seen
sideways. The naive way is to copy the elements into a new buffer, which allocates and costs time. The other
naive way is to compute offsets by hand (`base + r * stride + c`), which is exactly the arithmetic that goes wrong and
reads outside the buffer.

A *projection* gives you a new `tensor_view` over the **same storage**. It only changes three things: the shape, the
strides and where the first element is. Nothing is copied or allocated.

Because the parent view was already proven to lie inside its storage, each projection only needs a few integer
comparisons to prove the same for the child. A projection of a mutable view stays mutable: it reaches a subset of
the parent's elements, so writes through it cannot collide with each other.

## The projections

| Function | Result | Fails with |
| --- | --- | --- |
| `try_slice<D>(v, first, count)` | Same rank; dimension `D` restricted to `[first, first + count)`. | `out_of_bounds` |
| `try_index<D>(v, i)` | Rank - 1; dimension `D` fixed at `i`. | `out_of_bounds` |
| `try_row(v, r)` / `try_column(v, c)` | `try_index<0>` / `try_index<R-1>`. | `out_of_bounds` |
| `try_tile(v, origin, extents)` | Same rank; a rectangular block. | `out_of_bounds` |
| `transpose(v)` / `transpose<A, B>(v)` | Same rank; two dimensions swapped (default: the last two). | never |
| `permute<P...>(v)` | Same rank; dimension `d` is the parent's dimension `P[d]`. `P...` must be a permutation (compile-time check). | never |
| `diagonal<A, B>(v)` | Rank - 1; dimensions `A` and `B` merged into one that walks both together, placed last. | never |
| `try_reshape<N>(v, extents)` | Rank `N`; same elements in the same order. Needs a dense row-major view and an equal element count. | `invalid_argument` |
| `try_broadcast<N>(v, extents)` | Rank `N`, **read-only**; repeats size-1 and new leading dimensions (NumPy rules, stride 0). | `invalid_argument` |

Results always have runtime strides (`dynamic_strides`). A zero-size result is legal and empty.

## Example

```cpp
#include <reloco/kernel/kernel.hpp>
#include <reloco/span.hpp>
#include <vector>

namespace k = reloco::kernel;

// A 3x4 matrix of ints stored with a row stride of 5 (one padding element per row).
std::vector<int> data(15);
auto m = k::try_make_tensor<int, 2>(reloco::span<int>(data),
                                    {3, 4},   // shape: 3 rows, 4 columns
                                    {5, 1})  // strides in elements: next row is +5, next column is +1
             .value();

// Row 2 as a rank-1 view of 4 elements. try_* because row 2 might not exist: out_of_bounds if r >= 3.
auto row = k::try_row(m, 2);
if (row) row->at(0) = 1;   // writes data[10]; same storage as m, nothing copied

// Column 1 as a rank-1 view of 3 elements with stride 5.
auto col = k::try_column(m, 1);

// The 2x2 block whose top-left corner is (1, 1): origin first, then the block's extents.
auto block = k::try_tile(m, k::shape<2>{1, 1}, k::shape<2>{2, 2});

// m seen as 4x3: transposed.at(c, r) is the very same element as m.at(r, c). Cannot fail.
auto t = k::transpose(m);

// Projections chain: column 0 of the 2x4 band starting at row 1.
auto band = k::try_slice<0>(m, 1, 2);
if (band) auto c0 = k::try_column(*band, 0);
```

## Notes

- A projection is a pointer-like handle: the element references it gives stay valid as long as the *storage* lives.
- `storage()` of a projection is the parent's storage starting at the projection's first element, so do not compare
  it with the parent's `storage()`.
- `try_broadcast` returns a `const` view on purpose: every repeated element is the same memory, so a write through the
  view would change several "different" elements at once. A mutable view with overlapping strides is rejected by
  `try_make_tensor` for the same reason.
- `try_reshape` refuses padded or transposed views, because their elements are not one contiguous run; it would have
  to copy. Make the data dense first, or work on rows.
- Not provided: `reverse`. Strides are unsigned (a view walks forward through memory), so a backwards walk cannot be
  expressed; iterate with `n - 1 - i` over a proven domain instead.

## Why these four

### permute and diagonal
Reordering dimensions or reading a diagonal only changes how indexes turn into offsets. They reach a subset of the
parent's elements, so they cannot fail and keep write access.

```cpp
// A 2x3x4 block of data, dense. permute<2, 0, 1> makes dimension 0 of the result the parent's dimension 2.
auto cube = k::try_make_contiguous_tensor<int, 3>(storage, {2, 3, 4}).value();
auto moved = k::permute<2, 0, 1>(cube);   // shape {4, 2, 3}; moved.at(i, j, k) is cube.at(j, k, i)

// The main diagonal of a matrix: diag.at(i) is m.at(i, i); extent is min(rows, cols).
auto diag = k::diagonal<0, 1>(m);
```

### reshape
Treat the same run of elements as another shape, for example flattening a matrix into a vector to reuse a 1-D
algorithm. It is checked once: the view must be dense and the element counts must match.

```cpp
// 12 elements seen as a 3x4 matrix, then as a flat vector of 12.
auto mat = k::try_make_contiguous_tensor<int, 2>(storage, {3, 4}).value();
auto flat = k::try_reshape<1>(mat, {12});   // out: invalid_argument if the count differs or mat is padded
```

### broadcast
Use one small tensor where a bigger one is expected, for example the same row for every row of a matrix, without
copying it. Shapes are aligned from the last dimension; each source extent must equal the target or be 1.

```cpp
// One row of 3 values repeated as 4 rows: rows.at(r, c) is row.at(c) for every r. The result is read-only.
auto row = k::try_make_contiguous_tensor<int, 1>(storage, {3}).value();
auto rows = k::try_broadcast<2>(row, {4, 3});
```
