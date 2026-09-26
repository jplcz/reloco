<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Lock-free SPSC ring buffers: `spsc_ring_buffer<T>`, `inline_spsc_ring_buffer`, `outline_spsc_ring_buffer`, `heap_spsc_ring_buffer`

`spsc_ring_buffer<T>` (`include/reloco/atomic_ring_buffer.hpp`) is a
lock-free, single-producer/single-consumer (SPSC) queue for handing
trivially copyable data between exactly two threads without a mutex.
It is deliberately narrower than
[`ring_buffer<T>`](ring-buffer.md): capacity **must** be a power of two
(enforced with `RELOCO_ASSERT`/`static_assert`, enabling `& mask_` instead
of `% cap_` on every hot-path index computation), there is no `try_reserve`
(capacity is fixed for the lifetime of the object), and every container in
the family is **unconditionally non-copyable and non-movable** -- moving
or copying an object containing `std::atomic` members and being actively
read/written by another thread is inherently unsound, so the family
deletes all four special members rather than trying to make it "work".

```cpp
reloco::heap_spsc_ring_buffer<int> q;
auto ok = q.try_initialize(16); // rounds up to a power of 2

// Producer thread:
std::ignore = q.try_push(42);

// Consumer thread:
auto tx = q.begin_read();
if (tx) {
  for (int v : tx.chunk1())
    consume(v);
  tx.consume(tx.available());
}
```

## Why this needs its own engine (not `unowned_ring_base<T>`)

[`ring_buffer<T>`](ring-buffer.md)'s `head_`/`len_`/`cap_` fields are plain
`std::size_t`, safe only when a single thread (or an external lock)
serializes every access. `spsc_ring_buffer<T>` instead splits state across
two cache lines to prevent false sharing between the producer and
consumer:

```cpp
alignas(cache_line_size) std::atomic<std::size_t> write_idx_{0};
std::size_t cached_read_idx_{0};   // producer's stale, locally-cached copy of read_idx_

alignas(cache_line_size) std::atomic<std::size_t> read_idx_{0};
std::size_t cached_write_idx_{0};  // consumer's stale, locally-cached copy of write_idx_

alignas(cache_line_size) T *data_{nullptr};
std::size_t cap_{0};
std::size_t mask_{0}; // cap_ - 1, since capacity is always a power of 2
```

`cache_line_size` is `std::hardware_destructive_interference_size` where
available (falling back to a hardcoded 64 bytes on older compilers/
libraries). Unlike `ring_buffer<T>`, `write_idx_`/`read_idx_` are
**monotonically increasing** counters (never wrapped modulo `cap_`
directly) -- the physical index is only computed at the point of use via
`idx & mask_`, so the atomics never need special-casing to distinguish
"empty" from "full" the way a wrapped head/tail pair would.

### The demand-driven cache refresh

The producer only needs to know the *consumer's* progress to compute free
space, and vice versa -- but an `acquire` load on the other side's atomic
on every single call would defeat much of the point of a lock-free queue.
Instead, each side keeps a locally cached copy (`cached_read_idx_`/
`cached_write_idx_`) and only refreshes it (with the appropriate
`acquire` load) when the cached value alone isn't enough to satisfy the
current request:

```cpp
if (cap_ - (w - r) < min_space) {
  r = read_idx_.load(std::memory_order_acquire); // refresh only on demand
  cached_read_idx_ = r;
}
```

This means that in the common case (the queue isn't near-empty/near-full),
`write_slices`/`read_slices` touch only relaxed loads of the caller's own
atomic plus a plain local read -- no cross-core cache-line ping-pong at
all.

## Producer API (call only from the writer thread)

| Function | Behavior |
|---|---|
| `write_slices(min_space)` | Up to two writable `span<T>` chunks, refreshing the cached read index only if the currently-cached free space is below `min_space` |
| `commit(count)` | Publishes `count` newly written elements with a `release` store to `write_idx_` |
| `try_write(span<const T>)` | Bulk write; fails with `error::capacity_exceeded` if it doesn't fit |
| `try_push(value)` | Single-element write (Rust `push`) |
| `try_write_object<U>(obj)` | Writes a trivially copyable `U` across possibly two chunks, zero-copy |
| `try_emplace<U>(args...)` | `RELOCO_UNSAFE_BUFFER_USAGE`-gated placement-new directly into the queue's memory, bypassing the stack entirely (like Rust's `MaybeUninit` placement) |

### The `write_tx` RAII transaction

`begin_write(min_space)` returns a `write_tx`, exactly mirroring
[`ring_buffer<T>`'s `write_tx`](ring-buffer.md#the-write_tx-raii-transaction):
`chunk1()`/`chunk2()` expose the pre-computed writable spans,
`total_allocated()` reports their combined size, and `commit(count)`
finalizes the write and invalidates the transaction. It carries the same
`-Wconsumed` typestate annotations
(`RELOCO_CONSUMABLE(unconsumed)`/`RELOCO_RETURN_TYPESTATE`/
`RELOCO_CALLABLE_WHEN(unconsumed)`/`RELOCO_SET_TYPESTATE(consumed)`, see
[Lifetime safety](lifetime-safety.md)), and an uncommitted `write_tx`
going out of scope is a pure no-op -- nothing was ever published to
`write_idx_`, so there is nothing to roll back.

## Consumer API (call only from the reader thread)

| Function | Behavior |
|---|---|
| `read_slices(min_elements)` | Up to two readable `span<const T>` chunks, refreshing the cached write index only if the currently-cached available count is below `min_elements` |
| `consume(count)` | Publishes that `count` elements were consumed with a `release` store to `read_idx_` |
| `try_read_object<U>(dest)` | Consuming read of a trivially copyable `U`, across possibly two chunks |
| `try_peek_object<U>(offset)` | Non-consuming read, for checking headers before deciding to consume |
| `try_consume_frame<Header>(validator, processor)` | Zero-copy, length-prefixed frame decoding -- same contract as [`ring_buffer<T>::try_consume_frame`](ring-buffer.md#stream-parsing-and-frame-decoding), adapted to the atomic engine's cached-index refresh |
| `read_with(func)` | Rust-style closure extraction: `func(chunk1, chunk2)` returns the number of elements it consumed, which is then committed automatically |
| `consume_while(predicate)` | Drops elements from the front while `predicate` matches; returns the number dropped |

### The `read_tx` RAII transaction

`begin_read(min_elements)` mirrors `begin_write`: `chunk1()`/`chunk2()`/
`available()` expose the readable spans, and `consume(count)` finalizes
and invalidates the transaction. `operator bool()` reports whether any
data was actually available at construction time (`spans_.first` non-empty).

## The three public containers

| Type | Storage | Notes |
|---|---|---|
| `heap_spsc_ring_buffer<T>` | `allocator_ref` | Default-constructs inert (`data_ == nullptr`); call `try_initialize(capacity)` once before use. Deallocates in its destructor. |
| `inline_spsc_ring_buffer<T, Capacity>` | Embedded byte array | `Capacity` must be a power of two, `static_assert`-checked at compile time. Zero-allocation. |
| `outline_spsc_ring_buffer<T>` | Caller-supplied `span<U>` | Automatically rounds the usable length **down** to the nearest power of two (never up, since it cannot claim bytes past the caller's span). |

All three are unconditionally non-copyable and non-movable, for the
reasons described above -- there is no `try_clone`, and none is planned.

`round_up_power_of_2`/`round_down_power_of_2` are exposed as `static`
helpers on `spsc_ring_buffer<T>` (bit-twiddling, branchless, O(1)) so the
three leaf containers -- and callers computing capacities ahead of time --
share one implementation.

## See also

- [Ring buffers](ring-buffer.md) for the single-threaded, growable
  sibling of this engine (`ring_buffer<T>` and friends), including the
  full frame-parsing (`try_consume_frame`/`try_write_frame_evicting`)
  contract this type's `try_consume_frame` mirrors.
- [Thread-transfer/-sharing safety](send-sync.md) for `is_send<T>`/
  `is_sync<T>`, the traits describing which types are sound to hand to or
  share across threads -- relevant background for why this family deletes
  all copy/move operations instead of trying to support them.
- [Lifetime safety](lifetime-safety.md) for the `-Wconsumed` typestate
  annotations used by `write_tx`/`read_tx`.
