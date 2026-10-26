# Core algorithms

`#include <reloco/kernel/kernel.hpp>` (C++20 and newer). Everything lives in `reloco::kernel`.

## Why this exists

Filling, copying, mapping, summing and scanning arrays are the loops every numeric program contains. Written by hand
each one repeats the same questions: is the output long enough? Do input and output overlap? Can the sum overflow?
Those questions are easy to forget and expensive to ask on every element.

These algorithms ask them **once**, before the loop, using the binding layer (`bind.md`). Inside the loop every access
uses a proven index, so there is no per-element check. They also show the pattern for writing your own kernels.

## Rules shared by all of them

- Functions starting with `try_` return a `reloco::result` and never trap on bad input.
- The output span must be at least as long as the input (`out_of_bounds`); extra output elements are not touched.
- Output must not overlap an input (`invalid_argument`). Update a buffer in place with `transform_in_place`.
- Sums and dot products use an accumulator type **you** choose (`try_sum<std::int64_t>(bytes)`), so adding 8-bit
  values does not silently wrap. Overflow is reported as `integer_overflow`. The accumulator and element types must
  be integers; for other types use `reduce` with your own operation.
- If an algorithm fails part-way (overflow), the contents of the output are unspecified.
- Nothing allocates. Nothing uses floating point inside reloco.

## The algorithms

| Function | What it does | Errors |
| --- | --- | --- |
| `fill(out, value)` | Sets every element. | none |
| `try_copy(out, in)` | Copies `in` to the start of `out`. | `out_of_bounds`, `invalid_argument` |
| `try_transform(out, in, f)` | `out[i] = f(in[i])`. | same |
| `transform_in_place(data, f)` | `x = f(x)` for each element. | none |
| `try_zip_transform(out, a, b, f)` | `out[i] = f(a[i], b[i])`. | same, plus `invalid_argument` if `a` and `b` differ in length |
| `reduce(in, init, op)` | Left fold with your operation. | none |
| `try_sum<Acc>(in)` | Checked sum. Empty gives 0. | `integer_overflow` |
| `try_dot<Acc>(a, b)` | Checked dot product. | `invalid_argument` (lengths), `integer_overflow` |
| `try_min(in)` / `try_max(in)` | Smallest / largest element. | `container_empty` |
| `try_argmin(in)` / `try_argmax(in)` | Index of the first smallest / largest. | `container_empty` |
| `try_inclusive_scan(out, in)` | `out[i] = in[0] + ... + in[i]`. | `out_of_bounds`, `invalid_argument`, `integer_overflow` |
| `try_exclusive_scan(out, in, init)` | `out[0] = init`, `out[i] = init + in[0] + ... + in[i-1]`. | same |

Pass read-only inputs as `span<const T>`.

## Example

```cpp
#include <reloco/kernel/kernel.hpp>
#include <reloco/span.hpp>
#include <cstdint>
#include <vector>

namespace k = reloco::kernel;

std::vector<std::uint8_t> samples{200, 100, 50};
std::vector<std::int64_t> running(3);

// Why a 64-bit accumulator: 200 + 100 would wrap in 8 bits. The Acc template argument is the type of the sum.
// try_sum returns result<std::int64_t>; it is an error value (integer_overflow) only if even 64 bits overflow.
auto total = k::try_sum<std::int64_t>(reloco::span<const std::uint8_t>(samples));

// Prefix sums into `running` (first argument is the output, second the input, element type of the output is the
// accumulator). Fails with out_of_bounds if `running` were shorter than `samples`.
auto ok = k::try_inclusive_scan(reloco::span<std::int64_t>(running), reloco::span<const std::uint8_t>(samples));
// running == {200, 300, 350}

// Map into a different buffer. A buffer overlapping `samples` would be rejected with invalid_argument.
std::vector<std::uint8_t> halved(3);
auto mapped = k::try_transform(reloco::span<std::uint8_t>(halved), reloco::span<const std::uint8_t>(samples),
                               [](std::uint8_t x) { return static_cast<std::uint8_t>(x / 2); });

// Same buffer in and out is the in-place form; it cannot fail.
k::transform_in_place(reloco::span<std::uint8_t>(halved), [](std::uint8_t x) { return static_cast<std::uint8_t>(x + 1); });

// Largest element, or container_empty for an empty span.
auto largest = k::try_max(reloco::span<const std::uint8_t>(samples)); // 200
```

## Auditing: the scalar reference

Every algorithm has a twin in `reloco::kernel::reference` (`algo/reference.hpp`) with the same signature, results and
errors. The twin is a plain loop that uses the always-checked `span::operator[]` and no binding, proven index or
unchecked accessor, so it is easy to read and obviously correct. Why it matters: an optimized loop that skips checks is
only trustworthy if something independent says it computes the same thing. Run both on the same inputs and compare:

```cpp
std::vector<int> in{1, 2, 3, 4};
std::vector<int> fast(4), slow(4);

// Same call, once per variant. Results (value or error) and output buffers must be identical.
auto a = k::try_transform(reloco::span<int>(fast), reloco::span<const int>(in), [](int x) { return x * 2; });
auto b = k::reference::try_transform(reloco::span<int>(slow), reloco::span<const int>(in), [](int x) { return x * 2; });
bool same = a.has_value() == b.has_value() && fast == slow; // compare errors too when both fail
```

The repository's own tests do this on random inputs, including inputs that must fail (`tests/kernel/audit.hpp` is the
test helper; it is not a production header). When you write your own kernel, keep a scalar twin the same way and audit
against it. The reference variants are not meant for hot paths.

## Writing your own kernel

Copy the shape of `try_transform` (see `algo/elementwise.hpp`): `try_bind` the spans with the length of the loop,
return the error if it fails, then loop over `bound.domain()` and access views with the proven index. See `bind.md`.
