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

## Example 1: parsing a binary packet (split, strip, chunks)

```cpp
#include <reloco/span.hpp>

// Packet layout: [magic 0xCA 0xFE][length][payload ...][checksum]
reloco::result<void> parse(reloco::span<const std::uint8_t> packet) noexcept {
  constexpr std::uint8_t magic[] = {0xCA, 0xFE};

  // strip_prefix() answers "does it start with this, and what is left?" in one step.
  // It returns optional<span>: no match -> empty optional, nothing is copied.
  auto body = packet.strip_prefix(reloco::span<const std::uint8_t>(magic));
  if (!body)
    return reloco::unexpected(reloco::error::invalid_argument);

  // try_split_last() peels the checksum off the end. It returns a result so an empty body is an
  // error value (container_empty) instead of an assertion failure.
  auto parts = body->try_split_last();
  if (!parts)
    return reloco::unexpected(parts.error());
  auto [checksum, rest] = *parts; // checksum is a reference_wrapper<const uint8_t>, rest the remaining span

  // split_first() (checked variant) takes the length byte; the packet is known non-empty here.
  auto [length, payload] = rest.split_first();
  if (length != payload.size())
    return reloco::unexpected(reloco::error::invalid_argument);

  // chunks_exact(4) walks 4-byte records and skips an incomplete tail, which is exactly what a
  // fixed-size record format wants. chunks_exact_remainder(4) is that skipped tail.
  for (reloco::span<const std::uint8_t> record : payload.chunks_exact(4)) {
    (void)record; // decode one 4-byte record
  }
  (void)checksum;
  return {};
}
```

## Example 2: tokenizing a buffer (split, splitn, windows)

```cpp
int values[] = {3, 7, 0, 4, 0, 0, 9};
reloco::span<int> all(values);

// split(pred) yields the pieces between elements matching pred (here: zeros), lazily, as sub-spans.
// Empty pieces are kept: {3,7} {4} {} {9}.
for (reloco::span<int> run : all.split([](int v) { return v == 0; })) { /* process run */ }

// splitn(2, pred) stops after two pieces; the last piece is the untouched remainder: {3,7} {4,0,0,9}.
for (auto piece : all.splitn(2, [](int v) { return v == 0; })) {}

// rchunks(3) cuts 3-element chunks starting at the END: {0,0,9}, {7,0,4}, then the short {3} last.
for (auto chunk : all.rchunks(3)) {}
```

## Example 3: sorted data (binary search, partition_point)

```cpp
int sorted[] = {1, 3, 3, 5, 8, 13};
reloco::span<int> s(sorted);

// lower_bound/upper_bound return INDICES (like std::lower_bound minus begin()) and need sorted input.
std::size_t first_three = s.lower_bound(3); // 1: first element >= 3
std::size_t after_three = s.upper_bound(3); // 3: first element > 3
std::size_t how_many = after_three - first_three; // 2 copies of 3

// partition_point(pred) is the general form: binary search for where pred stops being true.
// Elements below 5 are the prefix, so the answer is the index of the first element >= 5.
std::size_t idx = s.partition_point([](int v) { return v < 5; }); // 3

// is_sorted() / is_sorted_by_key(key) verify a precondition before relying on it.
bool ok = s.is_sorted();
```

## Example 4: finding things (try_find, min/max, any/all)

```cpp
struct job { int priority; const char *name; };
job jobs[] = {{2, "build"}, {9, "deploy"}, {5, "test"}};
reloco::span<job> js(jobs);

// try_find_if() returns result<reference_wrapper<job>>: a reference INTO the array (not a copy), or
// error::not_found. Name the result first; hardened results cannot be dereferenced as temporaries.
auto urgent = js.try_find_if([](const job &j) { return j.priority > 8; });
if (urgent)
  urgent->get().priority = 0; // mutates jobs[1]

// try_max_by_key() picks the element with the largest key (the LAST one on ties); try_min_by_key()
// returns the FIRST minimum. Empty span -> error::container_empty.
auto best = js.try_max_by_key([](const job &j) { return j.priority; });

bool any_low = js.any([](const job &j) { return j.priority < 3; });
bool all_named = js.all([](const job &j) { return j.name != nullptr; });
std::size_t high = js.count_if([](const job &j) { return j.priority >= 5; });
int total = js.fold(0, [](int acc, const job &j) { return acc + j.priority; }); // 16
```

## Example 5: in-place rearranging (rotate, partition, copy_within, select_nth)

```cpp
int data[] = {1, 2, 3, 4, 5, 6};
reloco::span<int> s(data);

s.rotate_left(2);   // {3,4,5,6,1,2}: a ring buffer "unwrap" without a temporary
s.reverse();        // {2,1,6,5,4,3}

// stable_partition(pred) moves matching elements first and keeps relative order in both groups.
// It returns how many matched, so the two groups are s.subspan(0, n) and s.subspan(n).
std::size_t evens = s.stable_partition([](int v) { return v % 2 == 0; }); // {2,6,4,1,5,3}, evens == 3

// copy_within(offset, count, dest): memmove-like, overlapping ranges are safe. Shift {2,6,4} right by one.
s.copy_within(0, 3, 1);

// select_nth_unstable(n) puts the n-th smallest element in place in O(n) without a full sort.
// It returns {elements before, the element, elements after}: a cheap "median" lookup.
auto [lower, median, upper] = s.select_nth_unstable(s.size() / 2);

s.fill(0);                              // set every element
int next = 0;
s.fill_with([&next] { return next++; }); // generate: 0,1,2,...
```

Checked variants assert on bad arguments (`rotate_left(7)` on 6 elements); `try_rotate_left(7)`,
`try_swap_at(a, b)`, `try_copy_from(src)`, `try_copy_within(...)` return `result<void>` with `out_of_bounds` /
`invalid_argument` instead.

## Example 6: copying between spans

```cpp
int src[] = {1, 2, 3};
int dst[] = {0, 0, 0, 0};
reloco::span<int> to(dst);

// copy_from() needs equal lengths (a silent size mismatch is a classic bug). Use a sub-span for a part.
to.subspan(0, 3).copy_from(reloco::span<const int>(src));

// try_copy_from() reports the mismatch as an error instead of asserting.
auto r = to.try_copy_from(reloco::span<const int>(src)); // fails: 4 != 3

// swap_with_span() exchanges the contents of two equally sized spans; overlaps() tells whether two
// spans share memory (useful before a copy that would be undefined for overlapping ranges).
```

## ASCII helpers (`span<char>` / `span<const char>`)

`is_ascii()`, `eq_ignore_ascii_case(other)`, `make_ascii_uppercase()`, `make_ascii_lowercase()`
work byte-wise and are locale independent.

## Integration

`span` is already what containers hand out (`as_span()` style accessors), so these operations apply to any
contiguous container. Tests: `tests/test_span_slice_ops.cpp`.
