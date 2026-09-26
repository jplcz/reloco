<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Deque containers: `vec_deque<T>`, `inline_vec_deque`, `outline_vec_deque`, `sso_vec_deque`, and the ring-buffer engine

`vec_deque<T>` (`include/reloco/vec_deque.hpp`) is a move-only,
allocator-backed double-ended queue: push/pop/insert/erase at either end run
in amortized O(1), and index access is O(1). It is the fallible,
allocator-explicit analogue of `std::deque` (though, unlike `std::deque`,
it is backed by a single contiguous ring buffer rather than a sequence of
fixed-size chunks -- more like Rust's `std::collections::VecDeque`).
`inline_vec_deque<T, Capacity>` (fixed-capacity, allocator-free),
`outline_vec_deque<T>` (non-owning, over a caller-supplied span), and
`sso_vec_deque<T, InlineCapacity>` (small-size-optimized) round out the
family, mirroring `inline_vector`/`outline_vector`/`sso_vector` exactly.

If you haven't already, read
[Type-erased base containers](type-erased-base-containers.md) first: this
page assumes familiarity with that document's `vector_operations`/
`type_metadata` layering and the "untyped `*_base` classes operate on
`void*` + byte offsets" pattern -- the deque engine (`detail/vector_base.hpp`,
out-of-line bodies in `vector_base.ipp`, right alongside the vector engine)
reuses that same split, just with a ring buffer instead of a flat array.

## Why a ring buffer instead of shifting a flat array

A `vector<T>` used as a queue would need to shift every remaining element
on every `pop_front` (or waste ever-growing dead space at the front). A
ring buffer avoids both: elements live in `[0, cap_)` as usual, but a
`head_` index marks the logical start, and the logical range wraps around
the end of the buffer back to index `0` once `head_ + len_` exceeds `cap_`.
`unowned_deque_base` (the deque's equivalent of `unowned_vector_base`)
tracks exactly four fields:

```cpp
void *data_ = nullptr;
std::size_t head_ = 0; // physical index of the logical front
std::size_t len_ = 0;  // number of live elements
std::size_t cap_ = 0;  // physical buffer size
```

`push_back`/`push_front`/`pop_back`/`pop_front` only ever touch `head_`,
`len_`, and the one or two elements at the affected end -- no other element
moves. Random access (`operator[](logical_index)`) adds `head_` to the
logical index and wraps modulo `cap_`:

```cpp
std::size_t physical_index = head_ + logical_index;
if (physical_index >= cap_)
  physical_index -= cap_;
```

`as_slices()` exposes the two contiguous chunks (`[head_, cap_)` and, if
wrapped, `[0, head_ + len_ - cap_)`) as a `std::pair<span<T>, span<T>>` --
the deque's equivalent of Rust's `VecDeque::as_slices`, and the mechanism
`try_clone`, `try_append`, and `contains` all use to iterate every element
without caring whether the buffer is currently wrapped.

## Layer 1: storage policies

Exactly like `vector_base.hpp`'s vector side, `unowned_deque_base`'s
non-trivial primitives (`try_reserve_base`, `try_make_contiguous_base`,
`rotate_left_base`, `try_erase_at_base`) are declared `RELOCO_API` and
defined once, out-of-line, in `vector_base.ipp` -- they operate on
`void*`/`type_metadata` so they compile and link once regardless of how
many `vec_deque<T>` instantiations a program uses. Only one storage policy
ships today:

| Policy | Backs | Allocator | Growth |
|---|---|---|---|
| `heap_deque_base` | `vec_deque<T>` | `allocator_ref` | Unbounded (up to `max_capacity`) |
| `inline_deque_base` | `inline_vec_deque<T, Capacity>` | none | Fixed at `Capacity` |
| `outline_deque_base` | `outline_vec_deque<T>` | none | Fixed at the bound span's capacity |
| `mixed_deque_base` | `sso_vec_deque<T, InlineCapacity>` | `allocator_ref` (once promoted) | Inline up to `InlineCapacity`, unbounded beyond |

`inline_deque_base`, `outline_deque_base`, and `mixed_deque_base` mirror
`inline_vector_base`/`outline_vector_base`/`mixed_vector_base` on the
vector side exactly -- same constructors, same
`get_inline_storage`/`is_inline`/`get_allocator`/`inline_capacity`/
`max_capacity` surface, same "no move support" rationale for the outline
flavor (see
[Type-erased base containers](type-erased-base-containers.md#why-outline_vector_base-has-no-move-support)).
Adding a further deque flavor follows the identical "fifth vector flavor"
recipe from
[Type-erased base containers](type-erased-base-containers.md#adding-a-fifth-vector-flavor),
just against `typed_deque_base<T, Base>` instead of `typed_vector_base<T,
Base>`.

`try_reserve_base` additionally has to keep the ring buffer's wrap-around
intact across growth: after `expand_in_place`/`reallocate` widens the
buffer, if the buffer was wrapped (`head_ + len_ > cap_`), the head chunk
is shifted right to stay flush against the new, larger capacity, instead of
leaving a hole in the middle of the logical range.

## Layer 2: `typed_deque_base<T, Base>`

`typed_deque_base<T, Base>` is the thin, `T`-templated wrapper -- the deque
equivalent of `typed_vector_base<T, Base>` -- providing the full typed API:

```cpp
auto d = reloco::vec_deque<int>::try_create();
if (!d)
  return; // d.error() is a reloco::error.
auto ok = d->try_push_back(2);
ok = d->try_push_front(1);
ok = d->try_push_back(3);
assert((*d)[0] == 1 && (*d)[1] == 2 && (*d)[2] == 3);
```

| Function | Behavior |
|---|---|
| `try_reserve(new_cap)` | Grows capacity, preserving wrap-around |
| `try_emplace_front(args...)` / `try_push_front(value)` | O(1) amortized |
| `try_emplace_back(args...)` / `try_push_back(value)` | O(1) amortized |
| `try_pop_front()` / `try_pop_back()` | O(1) |
| `operator[]` / `try_at` / `front()` / `back()` | O(1), same checked/`try_*` tri-tier convention as `vector<T>` |
| `as_slices()` | Splits the logical range into its (up to two) physical contiguous spans |
| `try_make_contiguous()` | Unwraps the ring buffer (`head_ == 0`), returning a single `span<T>` |
| `rotate_left(mid)` / `rotate_right(k)` | O(min(mid, len - mid)), zero allocations |
| `try_erase_at(index)` | Shortest-shift removal, O(min(index, len - index)) |
| `try_emplace_at(index, args...)` / `try_insert_at(index, value)` | Shortest-shift insertion (see below) |
| `try_swap_remove_back(index)` / `try_swap_remove_front(index)` | O(1) removal that does not preserve order |
| `contains(value)` | Rust `slice::contains` equivalent, walks both `as_slices()` chunks |
| `try_append(other)` | Moves every element out of `other` (itself possibly wrapped) |
| `truncate(new_len)` / `clear()` | Drop trailing elements / drop everything |

### `rotate_left`/`rotate_right`: Rust `VecDeque::rotate_left`/`rotate_right`

`rotate_left(mid)` shifts the element at logical index `mid` to the front,
appending `[0, mid)` to the back; `rotate_right(k)` is defined in terms of
it (`rotate_left(len() - k)`). `rotate_left_base` picks whichever side is
cheaper to move (`mid` vs. `len - mid`) and, in the special case where the
buffer is completely full (`len_ == cap_`), rotates in true O(1) by only
adjusting `head_` -- no element is touched at all, since every physical
slot is already live.

### `try_insert_at`/`try_emplace_at`: Rust `VecDeque::insert`

Unlike `try_erase_at_base`, which is a bespoke, hand-shifted ring-buffer
routine (see its `.ipp` body for the wrap-handling case analysis),
`try_emplace_at` needs no new low-level primitive at all -- it is composed
entirely out of the public building blocks above:

```cpp
this->rotate_left(index);                     // bring the future predecessor to front
auto pushed = try_emplace_front(args...);      // O(1) amortized
this->rotate_right(index);                     // restore relative order
```

`rotate_left(index)` costs `O(min(index, len - index))` (picked
automatically), `try_emplace_front` places the new element, and
`rotate_right(index)` restores the original relative order with the new
element now sitting at `index`. If the emplace itself fails (e.g. `T`'s
constructor is fallible and reports an error), the first rotation is
undone with a matching `rotate_right(index)` before returning the error,
so the deque is left exactly as it was found -- a strong exception/error
guarantee without any manual rollback bookkeeping.

### `try_swap_remove_back`/`try_swap_remove_front`: Rust `VecDeque::swap_remove_back`/`swap_remove_front`

Both swap the target index with the last (`_back`) or first (`_front`)
element via `std::swap`, then pop that end. This is O(1) instead of
`try_erase_at`'s `O(min(index, len - index))`, at the cost of not
preserving the relative order of the remaining elements -- use it when
order doesn't matter and you want the cheapest possible removal.

## Shared-library considerations

Exactly as with the vector engine, `unowned_deque_base`'s non-trivial
bodies and `heap_deque_base`/`mixed_deque_base`/`inline_deque_base`'s
`deallocate_elements`/`move_construct_from_base` (where non-trivial) are
declared `RELOCO_API` and defined out-of-line in `vector_base.ipp`, guarded
by the same `RELOCO_SHARED_PROVIDE_DEFINITIONS` macro described in
[Shared-library deployments](shared-library.md).

## See also

- [API reference](reference.md) for the full per-type quick reference for
  `vec_deque<T>`/`inline_vec_deque<T, Capacity>`/`outline_vec_deque<T>`/
  `sso_vec_deque<T, InlineCapacity>`.
- [Type-erased base containers](type-erased-base-containers.md) for the
  vector side of the same engine, and the recipe for adding a sixth deque
  flavor.
