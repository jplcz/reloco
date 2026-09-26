<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Ring buffers: `ring_buffer<T>`, `inline_ring_buffer`, `outline_ring_buffer`, `sso_ring_buffer`, and `ring_buffer_ref`

`ring_buffer<T>` (`include/reloco/ring_buffer.hpp`) is a byte/POD-oriented
circular buffer built for streaming I/O: framing protocol parsers, log
ring buffers, socket/pipe staging areas, and any producer/consumer loop
that needs to write and read raw bytes (or trivially copyable structs)
without shifting memory. Unlike `vec_deque<T>` (see
[Deque containers](deque-containers.md)), which is a general-purpose,
per-element deque for arbitrary `T`, `ring_buffer<T>` is intentionally
narrower: `T` must be `std::is_trivially_copyable`, and the API is built
around bulk byte transfer (`try_write`/`read`/`read_slices`/`write_slices`)
and zero-copy framing (`try_consume_frame`/`try_write_frame_evicting`)
rather than per-element construction/destruction. If you need a deque of
non-trivial `T` with per-element `push_front`/`push_back`/`insert`/`erase`,
use `vec_deque<T>`; if you need a lock-free single-producer/single-consumer
queue for cross-thread handoff, see
[Lock-free SPSC ring buffers](atomic-ring-buffer.md) instead.

```cpp
reloco::ring_buffer<char> rb;
auto ok = rb.try_reserve(64);
if (!ok)
  return; // ok.error() is a reloco::error.

std::ignore = rb.try_write(reloco::span<const char>("hello", 5));
char out[5];
std::size_t n = rb.read(reloco::span<char>(out, 5)); // n == 5, consumes
```

Four flavors mirror the vector/deque family exactly:

| Type | Storage | Growable |
|---|---|---|
| `ring_buffer<T>` | Heap (`allocator_ref`) | Yes, unbounded |
| `inline_ring_buffer<T, Capacity>` | Embedded byte array | No, fixed at `Capacity` |
| `outline_ring_buffer<T>` | Caller-supplied `span<U>` | No, fixed at the bound span's capacity |
| `sso_ring_buffer<T, InlineCapacity>` | Embedded, promotes to heap | Inline up to `InlineCapacity`, unbounded beyond |

Plus `ring_buffer_ref<T>`, an independent, copyable cursor over any of the
above (see [`ring_buffer_ref`: an independent cursor](#ring_buffer_ref-an-independent-cursor)).

## Why a ring buffer instead of a flat, growable array

A flat buffer used as a byte queue either has to shift every remaining
byte on every read (to keep the live data at index 0), or waste
ever-growing dead space at the front. A ring buffer avoids both:
`unowned_trivial_ring_base` tracks four fields --

```cpp
void *data_ = nullptr;
std::size_t head_ = 0; // physical index of the logical front
std::size_t len_ = 0;  // number of live elements
std::size_t cap_ = 0;  // physical buffer size
```

-- and every mutating operation only ever touches `head_`, `len_`, and the
bytes actually being written/read; nothing else moves. This is the same
`head_`/`len_`/`cap_` layout `vec_deque<T>`'s `unowned_deque_base` uses
(see [Deque containers](deque-containers.md)), but here the "elements"
are almost always raw bytes, so the whole API is built around bulk
`memcpy`-driven transfer instead of per-element construction.

## Layer 1: `unowned_trivial_ring_base` (byte-level engine)

`unowned_trivial_ring_base` is the untyped, `void*`-based engine: `size()`,
`capacity()`, `free_space()`, `empty()`, `full()`, `clear()`, `consume(n)`
(drop from the front), `truncate(n)` (drop from the back), `unwrite(n)`
(undo the last `n` writes), plus the four heavier, allocation/copy-driving
primitives declared `RELOCO_API` and defined out-of-line in
`ring_buffer.ipp` (mirroring the `vector_base.hpp`/`vector_base.ipp` split
described in
[Type-erased base containers](type-erased-base-containers.md)):
`try_reserve_base`, `try_write_base` (lossless bulk write, fails if it
doesn't fit), `write_overwrite_base` (lossy bulk write, evicts the oldest
bytes), `read_base` (consuming bulk read).

## Layer 2: `unowned_ring_base<T>` (typed API)

`unowned_ring_base<T>` is the `T`-templated layer providing the full typed
surface every leaf container inherits. `T` must be
`std::is_trivially_copyable`.

### Bulk transfer and zero-copy slices

| Function | Behavior |
|---|---|
| `try_write(span<const T>)` | Lossless bulk write; fails with `error::capacity_exceeded` if it doesn't fit |
| `write_overwrite(span<const T>)` | Lossy bulk write; evicts the oldest elements to make room (streaming logs) |
| `read(span<T>)` | Consuming bulk read; returns the number of elements actually read |
| `read_slices(limit)` | Up to two contiguous `span<const T>` chunks of readable data -- pass directly to `writev`/`WSASend` |
| `write_slices()` | Up to two contiguous `span<T>` chunks of writable free space -- pass directly to `readv`/`WSARecv` for DMA |
| `peek(dest, offset)` | Copies without consuming |
| `make_contiguous()` | Rust `VecDeque::make_contiguous`: unwraps the buffer in place (`head_ == 0`), zero extra allocations |
| `transfer_to(dest, max_count)` | Drains directly from this buffer into another `unowned_ring_base`, at most two `memcpy`s |

### Single-element mutation

`push_back_overwrite`/`push_front_overwrite` (lossy, never fail, drop the
opposite end's oldest element when full), `try_pop_front`/`try_pop_back`
(fallible, `result<T>`), and `push_back` (the lossy, `void`-returning
overload required to satisfy `std::back_inserter`).

### Element access (tri-tier convention)

Same checked/`try_*` split as every other reloco container (see
[Container contract](container-contract.md)): `operator[]`/`front()`/
`back()` are `RELOCO_ASSERT`-hardened (checked tier -- traps in debug
builds, UB-if-disabled in release, matching `std::vector::operator[]`'s
speed with an opt-in safety net), while `try_at(index)`/`try_front()`/
`try_back()` return `result<T*>`/`result<T*>` instead of asserting. Every
accessor is ref-qualified `&`/`const &` and `RELOCO_BLOCK_RVALUE_ACCESS(T)`
deletes the `&&`/`const &&` overloads explicitly, so calling any accessor
on a temporary ring buffer is a compile error rather than a dangling
pointer into an about-to-be-destroyed object.

### Heterogeneous object I/O

`try_write_object`/`write_object_overwrite`/`try_write_span` write a
trivially copyable `U` (or a `span<const U>`) directly into a
`ring_buffer<T>` of a *different* element type `T` (e.g. serializing
`uint32_t`s into a `ring_buffer<uint8_t>`), `static_assert`-ing
`sizeof(U) % sizeof(T) == 0`. `try_read_object<U>()` and
`try_peek_object<U>(offset)` reverse the process (`try_peek_object` does
not consume), both `memcpy`-based to sidestep strict-aliasing and
alignment UB.

### Stream parsing and frame decoding

The frame-parsing API is the reason `ring_buffer<T>` exists as a distinct
type from `vec_deque<T>`: it lets you incrementally decode
length-prefixed, header+payload wire protocols directly against the ring
buffer without ever copying the payload out first.

- `try_consume_frame<Header>(validator, processor)`: peeks a `Header`,
  calls `validator(header) -> {is_valid, total_size}` to determine
  whether the frame is well-formed and how large it is, and if
  `len() >= total_size`, calls
  `processor(header, chunk1, chunk2)` with the (possibly
  wrap-split) payload spans, then consumes the whole frame. Returns
  `false` while waiting for more data to arrive, and clears the buffer
  and returns `error::invalid_argument` if the validator rejects the
  header (self-healing against corruption/desync).
- `try_peek_frame<Header>(validator, processor)`: the same, but `const`
  and non-consuming -- since it cannot mutate on corruption, it just
  returns the error instead of clearing.
- `try_write_frame_evicting<Header>(header, payload, validator)`: the
  write-side counterpart for lossy ring buffers (e.g. a bounded log): if
  there isn't room for the new frame, it evicts *complete* frames from the
  front (using the same validator) until there is, rather than shredding
  a frame's bytes on overwrite. Fails with `error::capacity_exceeded` if
  the frame is larger than the entire buffer's capacity.
- `try_read_frame<Header>(get_total_size)`: the simplest variant, for
  buffers where you're willing to pay for `make_contiguous()` up front --
  returns a single contiguous `span<const T>` covering the whole frame
  once it has fully arrived (caller must `consume()` afterward).
- `peek_struct<U>(offset)`: byte-oriented (`sizeof(T) == 1`) primitive
  reading a POD struct across the wrap boundary without consuming;
  `try_read_frame` builds on this to peek the header.

### Delimiter and sequence search

`find(value, offset)` and `find_sequence(span<const T> seq, offset)` walk
the logical range (transparently handling the wrap boundary) to locate a
single delimiter byte (e.g. `'\n'`) or a multi-byte sequence (e.g.
`"\r\n\r\n"`), returning the logical index if found.
`consume_until(predicate)` combines search with eviction: drops elements
from the front until `predicate` matches (without consuming the matching
element itself), returning the number of elements dropped.

### Zero-copy allocation and commit

For producers that want to write directly into ring-buffer memory instead
of staging into a temporary and calling `try_write`:
`allocate_contiguous(limit)` returns a single writable `span<T>` (at most
up to the physical end of the buffer -- may be shorter than the true free
space if it wraps), `allocate_slices(limit)` returns up to two spans
covering *all* free space (scatter-gather, perfect for `readv`),
`try_allocate_object<U>()` returns a `U*` into that space for
placement-new-style construction, and `commit(count)` finalizes the
write by advancing `len_`. `align_write_head(alignment)` (byte-oriented
only) inserts padding until the next write index satisfies an alignment
requirement, useful before allocating an over-aligned struct.

### The `write_tx` RAII transaction

`begin_write(limit)` returns a `write_tx`: an RAII scatter-gather write
transaction that pre-computes `chunk1()`/`chunk2()` (the writable spans)
and requires an explicit `commit(count)` call before it can be safely
discarded -- if you never call `commit`, the destructor is a no-op and
nothing was ever appended (zero-overhead rollback on any early return,
e.g. a partial network write). `write_tx` is annotated with the
`-Wconsumed` typestate macros (`RELOCO_CONSUMABLE(unconsumed)`,
`RELOCO_RETURN_TYPESTATE`, `RELOCO_CALLABLE_WHEN(unconsumed)`,
`RELOCO_SET_TYPESTATE(consumed)`) described in
[Lifetime safety](lifetime-safety.md), so a `write_tx` that is dropped
without a decision either way is a compiler warning under Clang's
consumed-analysis, not just a runtime no-op -- and `chunk1()`/`chunk2()`/
`total_allocated()` are all rejected by the analyzer once `commit()` has
already run. It is non-copyable but movable (steals the pending
transaction and invalidates the source).

```cpp
auto tx = rb.begin_write();
std::size_t written = fill_from_socket(tx.chunk1(), tx.chunk2());
tx.commit(written); // or just let tx go out of scope to roll back
```

### Iterators

`ring_iterator<IsConst>` is a hardened random-access iterator over the
logical range: `operator*`/`operator->` `RELOCO_ASSERT` that the iterator
is initialized and in-bounds, and increment/decrement/`+=`/`-=` all
`RELOCO_ASSERT` that the resulting logical index stays within
`[0, len()]` -- including catching unsigned-underflow from decrementing
before `begin()`, since that wraps to `SIZE_MAX`, which trivially fails
the same `<= len()` check. `begin()`/`end()`/`cbegin()`/`cend()` all
return this iterator; `find_sequence` and `consume_until` are implemented
in terms of it via `std::search`/`std::find_if`.

## Layer 3: storage policies

Exactly like `vector_base.hpp`/`vec_deque`'s storage policies, each
non-trivial primitive (`try_reserve_base`, etc.) is declared `RELOCO_API`
and defined once, out-of-line, in `ring_buffer.ipp`. Four storage policies
back the four public containers:

| Policy | Backs | Allocator | Growth |
|---|---|---|---|
| `heap_trivial_ring_base` | `ring_buffer<T>` | `allocator_ref` | Unbounded (up to `max_capacity`) |
| `inline_trivial_ring_base` | `inline_ring_buffer<T, Capacity>` | none | Fixed at `Capacity` |
| `outline_trivial_ring_base` | `outline_ring_buffer<T>` | none | Fixed at the bound span's capacity |
| `mixed_trivial_ring_base` | `sso_ring_buffer<T, InlineCapacity>` | `allocator_ref` (once promoted) | Inline up to `InlineCapacity`, unbounded beyond |

Each policy provides the same
`destroy_elements`/`move_construct_from_base`/`move_assign_from_base`/
`get_inline_storage`/`is_inline`/`get_allocator`/`inline_capacity`/
`max_capacity` surface `vector_base.hpp`'s policies do, so
`typed_ring_buffer<T, Base>` (Layer 4) never needs to know which policy
it was instantiated with.

## Layer 4: `typed_ring_buffer<T, Base>`

The thin, capacity-management layer every leaf container derives from:
`max_capacity()`, `try_reserve(new_cap)`, `try_push_back(value)`/
`try_push_front(value)` (both grow the buffer by 1.5x + 1 when full,
mirroring `vector<T>`'s growth factor), `try_resize(new_len, value)`
(truncates trivially if shrinking, else reserves and fills), and
`try_clone(alloc)` (deep-copies, using `read_slices()` to linearize the
source's layout into the destination in at most two `memcpy`s regardless
of whether the source is currently wrapped).

## The four public containers

### `ring_buffer<T>`

Heap-backed, move-only (mirrors `vector<T>`): `ring_buffer<T> rb(alloc)`
default-constructs empty (`capacity() == 0`); call `try_reserve`/
`try_push_back`/`try_push_front` to grow. Not copyable (use `try_clone`).

### `inline_ring_buffer<T, Capacity>`

Fixed-capacity, allocator-free, embedded byte storage
(`alignas(T) std::byte storage_[Capacity * sizeof(T)]`). Unlike
`inline_vector`/`inline_vec_deque`, `inline_ring_buffer` **is** copyable
(copy constructor/assignment deep-copy via `read_slices()` +
`try_write()`), since `T` is always trivially copyable here -- there's no
reason to force callers through `try_clone()` when a plain copy can never
fail. Every growing operation fails with `error::capacity_exceeded` once
`size() == Capacity`.

### `outline_ring_buffer<T>`

Non-owning, bound once to a caller-supplied `span<U>` (cross-casting: `U`
need not equal `T`, as long as the byte length divides evenly). Never
copyable or movable -- like `outline_vector`/`outline_vec_deque`, there is
no sound way to steal a borrowed span. Growth is capped at the bound
span's byte capacity divided by `sizeof(T)`.

### `sso_ring_buffer<T, InlineCapacity>`

Small-size-optimized: up to `InlineCapacity` elements live in an embedded
byte array; growth beyond that promotes to an `allocator_ref`-backed heap
allocation, linearizing the inline data in the process (mirrors
`sso_vector`/`sso_vec_deque` exactly). `is_inline()` reports whether
`*this` is still using its embedded buffer. Not copyable (use
`try_clone`), movable (steals the heap pointer if spilled, else copies the
inline elements and resets the source).

## `ring_buffer_ref`: an independent cursor

Unlike every other `_ref` type in reloco (which are lightweight,
`RELOCO_POINTER`-tagged views that alias the *same* state as their
source), `ring_buffer_ref<T>` takes an immutable borrow of an existing
ring buffer's *current* `data_`/`head_`/`len_`/`cap_` and copies that
state into its own independent cursor:

```cpp
reloco::ring_buffer<char> rb;
std::ignore = rb.try_reserve(16);
std::ignore = rb.try_write(reloco::span<const char>("abc", 3));

reloco::ring_buffer_ref<char> cursor(rb); // independent snapshot
cursor.consume(1); // advances cursor's own head_, does NOT touch rb
assert(rb.size() == 3 && cursor.size() == 2);
```

This makes it useful for speculative/lookahead parsing: clone a cursor,
try to decode a frame against it, and only apply the equivalent
`consume()` to the real buffer once you know the frame is complete and
valid -- without needing a rollback path on the original buffer. Because
it stores state by value (not a pointer back to the source), it is
copyable and movable (move is defined as a copy, since there's nothing to
steal). It can also wrap raw external memory directly via the
`span<T>`-taking constructors, bypassing a source ring buffer entirely.

## See also

- [API reference](reference.md) for the full per-type quick reference for
  `ring_buffer<T>`/`inline_ring_buffer<T, Capacity>`/
  `outline_ring_buffer<T>`/`sso_ring_buffer<T, InlineCapacity>`/
  `ring_buffer_ref<T>`.
- [Deque containers](deque-containers.md) for the per-element,
  non-trivial-`T` sibling of this engine (`vec_deque<T>`).
- [Lock-free SPSC ring buffers](atomic-ring-buffer.md) for the
  cross-thread, allocation-free counterpart (`spsc_ring_buffer<T>` and
  friends).
- [Lifetime safety](lifetime-safety.md) for the `-Wconsumed` typestate
  annotations used by `write_tx`.
- [Container contract](container-contract.md) for the tri-tier accessor
  and rvalue-protection conventions every container (including this one)
  follows.
