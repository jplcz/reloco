# span: Rust-style slice operations

`include/reloco/span.hpp`

## Why

A `span<T>` is a non-owning view of contiguous memory, the same idea as a Rust slice (`&[T]` / `&mut [T]`).
Rust's slice API covers the things people otherwise hand-write with index loops: splitting, searching,
rotating, partitioning. Writing those loops by hand is where off-by-one and out-of-range bugs come from.
These members give you the same vocabulary with reloco's safety tiers.

## Conventions

- **Checked** (`split_first`, `copy_from`, ...): `RELOCO_ASSERT` on misuse.
- **`try_*`**: returns `result<T>` with `error::out_of_bounds`, `container_empty`, `invalid_argument` or `not_found`.
- **Mutating members** are `const &` (a span is a view; it does not own the elements) and require a non-const `T`.
- **Reference results** are `result<std::reference_wrapper<T>>` (`try_find`, `try_max`, ...). Results are
  lifetime-bound; name the result before dereferencing it (rvalue access is blocked).
- Rust's `min`/`max`/`find` are spelled `try_min`/`try_max`/`try_find` because they can fail.
- Rust's `split_at_mut` and `get(range)` already exist as `split_at` and `try_subspan`.

## Examples

```cpp
int data[] = {1, 2, 3, 4, 5, 6, 7};
reloco::span<int> s(data);  // view over the array; nothing is copied

// --- splitting ---
auto head_tail = s.try_split_first();   // result<pair<ref, span>>: first element + the rest
auto front = s.split_off_first(2);      // returns {1,2} and shrinks s itself to {3..7}

// Lazy ranges: no allocation, pieces are spans into the original data.
for (auto chunk : s.chunks_exact(3)) {} // full chunks of 3; the leftover is s.chunks_exact_remainder(3)
for (auto piece : s.split([](int v) { return v % 3 == 0; })) {} // pieces between matches
for (auto c : s.rchunks(2)) {}          // chunks counted from the end

// --- searching ---
bool pre = s.starts_with(reloco::span<const int>(data, 2)); // prefix test
auto rest = s.strip_prefix(reloco::span<const int>(data, 2)); // result<span>: span without the prefix
auto first_big = s.try_find_if([](int v) { return v > 4; }); // result<reference_wrapper<int>>
std::size_t idx = s.partition_point([](int v) { return v < 4; }); // binary search on sorted/partitioned data
std::size_t lo = s.lower_bound(4);      // first index with value >= 4 (sorted input required)

// --- mutation ---
s.reverse();                // in place
s.rotate_left(2);           // element 2 becomes the first; try_rotate_left reports a bad count
s.fill(0);                  // or fill_with(generator)
s.copy_within(0, 3, 2);     // (offset, count, dest): overlapping ranges are handled
s.stable_partition([](int v) { return v % 2 == 0; }); // evens first, order kept; returns the even count

// --- queries ---
bool any_big = s.any([](int v) { return v > 5; });
std::size_t n = s.count(3);
int sum = s.fold(0, [](int acc, int v) { return acc + v; });
bool sorted = s.is_sorted();
```

## ASCII helpers (`span<char>` / `span<const char>`)

`is_ascii()`, `eq_ignore_ascii_case(other)`, `make_ascii_uppercase()`, `make_ascii_lowercase()`
work byte-wise and are locale independent.

## Integration

`span` is already what containers hand out (`as_span()` style accessors), so these operations apply to any
contiguous container. Tests: `tests/test_span_slice_ops.cpp`.
