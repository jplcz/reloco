<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Over-alignment: `alignment_of<T>`

## Why containers need more than `alignof(T)`

Ordinary types already get correctly aligned storage everywhere: `alignof(T)`
is exactly what the compiler's ABI requires, and every allocator call reloco
containers make already forwards it. That is enough for correctness, but not
always enough for performance: SIMD load/store intrinsics (SSE `128-bit`,
AVX `256-bit`, AVX-512 `512-bit`, NEON, ...) are fastest -- or on stricter
targets, only valid at all -- against pointers aligned to their vector width,
which is frequently *larger* than `alignof(T)` for the element type actually
being processed (e.g. 32-byte-aligned `float` data for an AVX loop, even
though `alignof(float) == 4`).

Getting that alignment today means either wrapping every element in a
hand-rolled `alignas(32)` struct (which also changes `sizeof(T)`/padding for
every other use of `T`, not just the allocation you care about) or bypassing
the container entirely and managing a raw over-aligned buffer yourself.
`reloco::alignment_of<T>` is a customization point that lets a container's
*storage* be over-aligned without touching `T`'s own definition.

## `reloco::alignment_of<T>`

`include/reloco/alignment.hpp` defines the customization point:

```cpp
template <typename T> struct alignment_of : std::integral_constant<std::size_t, alignof(T)> {};

template <typename T> inline constexpr std::size_t alignment_of_v = alignment_of<T>::value;

template <typename T> inline constexpr std::size_t effective_alignment_v = /* see below */;
```

`alignment_of<T>::value` defaults to `alignof(T)`, so nothing changes for
ordinary types. `effective_alignment_v<T>` is what every reloco container
actually uses: `std::max(alignof(T), alignment_of_v<T>)`, so specializing the
trait can only ever *increase* the alignment a container requests, never
weaken it below what `T` already requires. `effective_alignment_v<T>` also
`static_assert`s that `alignment_of_v<T>` is a power of two the first time it
is used for `T`.

Specialize it exactly like `is_trivially_relocatable<T>` (see
[Trivial relocation](relocatable.md)) -- once, for the element type, and
every container instantiated with that type across the whole program picks
it up automatically:

```cpp
struct alignas(4) vec4f { float x, y, z, w; };

template <> struct reloco::alignment_of<vec4f> : std::integral_constant<std::size_t, 32> {};

reloco::vector<vec4f> positions;        // heap allocation is 32-byte aligned
reloco::array<vec4f, 64> local_batch;   // inline storage is 32-byte aligned
reloco::inline_vector<vec4f, 16> scratch; // inline storage is 32-byte aligned
```

## Which containers honor it

| Container | Effect |
|---|---|
| `reloco::vector<T>` | `try_reserve`/`shrink_to_fit` request `effective_alignment_v<T>` from the allocator instead of `alignof(T)` |
| `reloco::array<T, N>` | The `T data_[N]` storage member is declared `alignas(effective_alignment_v<T>)` |
| `reloco::inline_vector<T, Capacity>` | The raw `std::byte` storage buffer is declared `alignas(effective_alignment_v<T>)` |

`flat_set<T>`/`flat_map<Key, Mapped>` and their `inline_*` counterparts are
built on `vector<T>`/`inline_vector<T, Capacity>`, so they inherit the same
alignment automatically without any change of their own.

`data()`/`unsafe_data()`/`begin()` on these containers are additionally
annotated with `RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>)` (see
[Lifetime safety](lifetime-safety.md)), a GCC/Clang attribute that tells the
optimizer every pointer these accessors return is aligned to at least that
value. This lets the compiler auto-vectorize a caller's loop over the
returned pointer without an alignment-check prologue -- but it also makes a
misaligned return undefined behavior, so it is only ever applied where the
container's own construction/allocation logic already guarantees the
alignment holds.

## What this does *not* do

`alignment_of<T>` only controls how reloco containers allocate/embed storage
for `T`. It is not a SIMD library: reloco ships no vector-width types, no
intrinsics wrappers, and no auto-vectorized algorithms. Pair it with a SIMD
library of your choice (e.g. `xsimd`, `std::experimental::simd`, Highway) by
handing that library `data()`/`unsafe_data()` from a container whose element
type specializes `alignment_of<T>` to match the SIMD width you target.
