<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Capacity absorption: binding `mem_block::size` to your allocator's real granularity

`reloco::mem_block` (`include/reloco/allocator.hpp`) is the value every
`allocator_traits<Tag>::allocate`/`expand_in_place`/`reallocate` returns:

```cpp
struct [[nodiscard]] mem_block {
  void *ptr;
  std::size_t size; // actual capacity, guaranteed >= requested bytes
};
```

`vector`/`sso_vector`/`basic_string`/`basic_sso_string` all **absorb**
`size` back into their own `cap_` instead of keeping the originally
requested size: after a grow, `cap_` is *exactly* what the backend
reported, in element units (`res->size / sizeof(T)`, or `- 1` for the
null-terminated string types), never the smaller number the caller asked
for. This guide explains why, when it helps, when it actively hurts, and
how to bind it to a real allocator backend.

## Why containers absorb the extra capacity

Every non-trivial allocator rounds requests up to *some* granularity it
already had to pay for regardless of what the caller asked for: alignment
padding, page-size rounding, or size-class/bin rounding in a slab/pool
allocator. If a container ignores that and only remembers the size it
asked for, every one of those already-paid-for bytes is invisible to
`capacity()`/`reserve()`/growth math: the container reallocates again the
moment it needs one more element, even though the previous block already
had room, and repeats this on every subsequent growth step forever.
Absorbing the backend's real `size` turns that rounding from wasted slack
into usable capacity, for free — no extra allocation, no extra bytes
committed beyond what the backend already committed on its own.

## The critical round-trip contract: `old_size`/`bytes` is the *last returned* size, not the original request

This is the one detail that matters most for a custom backend: once a
container has absorbed a bigger `size` into `cap_`, every subsequent call
back into the same backend for that block — `expand_in_place`'s
`old_size`, `reallocate`'s `old_size`, `deallocate`'s `bytes` — is computed
from that absorbed `cap_`, **not** from whatever smaller size was
originally requested. For example (`vector.hpp`):

```cpp
if (auto res = alloc_.expand_in_place(data_, cap_ * sizeof(T), required_bytes); res) {
  cap_ = *res / sizeof(T); // absorb again
  ...
}
...
alloc_.deallocate(data_, cap_ * sizeof(T)); // cap_, not the original request
```

A backend that reports a bigger `size` than requested **must** be able to
accept that same bigger size back on every later call for that block. A
naive backend that only tracks "the size the caller originally asked
for" and asserts/rejects an unexpected larger `old_size`/`bytes` will
break the first time a container reuses the absorbed capacity. If your
backend cannot honor an arbitrary previously-returned size symmetrically
across `allocate`/`expand_in_place`/`reallocate`/`deallocate`, do not
report a rounded-up `size` in the first place — report exactly `bytes`
(see below).

## Two very different reasons to report a bigger `size`, and when each applies

`mem_block::size` being allowed to exceed the request serves two distinct
audiences; conflating them is the most common mistake when binding a
custom backend.

### 1. You are the author of your own general-purpose/arena allocator (the primary intended use)

If `allocator_traits<Tag>` is a thin adapter over an allocator **you
wrote** — a bump/arena allocator, a slab allocator with fixed size
classes (16/32/64/... byte bins), a pool allocator, a buddy allocator —
then rounding up to the next bin/page/alignment boundary is not
"extra, dangerous headroom you're granting the caller": it is memory
your own allocator *already reserved and committed* for that block as an
unavoidable consequence of how its internal bookkeeping works. Nothing
external is watching that region for misuse (no sanitizer redzone, no
guard page, no separate bookkeeping structure relies on the smaller
number), so reporting the true bin/slab size back through `mem_block::size`
costs nothing and is exactly what this feature is for: `reloco`'s
containers get to use memory your allocator already owns instead of
wasting it. `stack_allocator.hpp`'s bump-pointer backend
(`include/reloco/stack_allocator.hpp`) is the simplest example of this
shape in-tree, though it currently reports the exact requested size —
see the worked example below for how a size-classed backend would differ.

### 2. You are wrapping a general-purpose *system* allocator (secondary, use with care)

The built-in `heap_allocator_tag` (`include/reloco/heap_allocator.hpp`,
`.ipp`) wraps `std::malloc`/`std::realloc`/`std::aligned_alloc` and
deliberately reports back exactly the requested `bytes`/`new_size`, even
though glibc's malloc (like most system allocators) actually rounds every
request up to its own internal chunk-size granularity. That headroom is
real, but it belongs to a general-purpose allocator whose internals
reloco does not own or control — querying and using it is optional, and
some platforms have no portable way to query it at all (see below).

If you do want that headroom, most system allocators expose a
"how much did you actually give me" query:

| Allocator                | Query                                                    | Header                  |
|--------------------------|-----------------------------------------------------------|--------------------------|
| glibc                    | `malloc_usable_size(ptr)`                                  | `<malloc.h>`             |
| macOS/BSD `malloc`       | `malloc_size(ptr)`                                         | `<malloc/malloc.h>`      |
| MSVC CRT                 | `_msize(ptr)` (`_aligned_msize(ptr, alignment, 0)` for `_aligned_malloc`) | `<malloc.h>` |
| mimalloc                 | `mi_malloc_usable_size(ptr)` / `mi_usable_size(ptr)`        | `<mimalloc.h>`           |
| jemalloc                 | `je_malloc_usable_size(ptr)` / `sallocx(ptr, 0)`             | `<jemalloc/jemalloc.h>`  |
| tcmalloc                 | `tc_malloc_size(ptr)` / `MallocExtension::GetAllocatedSize` | `<gperftools/tcmalloc.h>`, `<gperftools/malloc_extension.h>` |
| musl libc                | none portable — do not attempt this on musl                | —                        |

A worked example, binding a `your_heap_allocator_tag` to glibc via
`malloc_usable_size`:

```cpp
struct your_heap_allocator_tag {};

template <> struct reloco::allocator_traits<your_heap_allocator_tag> {
  using context_type = void;

  static result<mem_block> allocate(std::size_t bytes, std::size_t alignment) noexcept {
    void *ptr = alignment <= alignof(std::max_align_t)
        ? std::malloc(bytes == 0 ? 1 : bytes)
        : detail::heap_aligned_alloc(alignment, bytes == 0 ? alignment : bytes);
    if (!ptr) return unexpected(reloco::error::allocation_failed);
#if !defined(RELOCO_SANITIZE_MEMORY)
    // Report the real chunk size: containers get to use the rounding
    // glibc already committed, instead of reallocating one element later.
    return mem_block{ptr, malloc_usable_size(ptr)};
#else
    // See "Sanitizers" below: never claim more than what was requested
    // when a memory-checking tool is active.
    return mem_block{ptr, bytes};
#endif
  }

  static void deallocate(void *ptr, std::size_t /* bytes: last-reported size, see above */) noexcept {
    std::free(ptr);
  }

  // reallocate() follows the same pattern: realloc(), then wrap the
  // result's malloc_usable_size() the same way, guarded identically.
};
```

Note the `deallocate`/`realloc` calls above never need to pass the
absorbed size back to `free`/`realloc` themselves — glibc's own
`free`/`realloc` recover the real block size from their own heap
metadata, not from the caller. This is specific to malloc-family
allocators; a size-classed allocator you wrote yourself typically *does*
need the caller-supplied size (or must recompute the same bin from it),
which is exactly why the round-trip contract above matters for that case.

## Sanitizers (ASan/MSan/HWASan) and Valgrind: never report more than the exact request

**When building under AddressSanitizer, MemorySanitizer, HWASan, or
Valgrind, an allocator backend must report exactly the requested
`bytes`/`new_size`, never a rounded-up or queried "usable size".** These
tools instrument the system allocator itself: the bytes between your
requested size and the real, larger underlying chunk are deliberately
poisoned redzone, used to catch heap-buffer-overflow and use-after-free
bugs. If a backend queries and reports that larger size (e.g. via
`malloc_usable_size` under ASan — which, notably, ASan's own interceptor
already special-cases to return the *requested*, not real, size for
exactly this reason), and a container then absorbs it into `cap_` and
later writes into that "capacity", every one of those writes lands in
poisoned redzone memory. The sanitizer is not wrong to flag it — that
memory was never actually available; claiming otherwise defeats the
overflow detection the sanitizer exists to provide, turning true
diagnostic hits into what looks like container false-positives.

Guard any capacity-absorbing query in your own backend the same way the
worked example above does:

```cpp
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_MEMORY__) || \
    (defined(__has_feature) && (__has_feature(address_sanitizer) || __has_feature(memory_sanitizer))) || \
    defined(RELOCO_SANITIZE_MEMORY) // your own build-system flag, e.g. under Valgrind
  return mem_block{ptr, bytes};
#else
  return mem_block{ptr, malloc_usable_size(ptr)};
#endif
```

This restriction is specific to **case 2 above** (wrapping a general
system allocator instrumented by the sanitizer). It does not apply to
**case 1**: your own arena/slab/pool allocator's internal bins are not
what a sanitizer's malloc interceptor instruments or poisons, so reporting
their true size remains exactly as safe under a sanitizer build as
without one — sanitizers only add redzones around the *system* allocator
calls your arena itself may or may not make internally (e.g. once per
whole arena/slab, not once per `allocate()` call).

## See also

- [`reloco/allocator.hpp`](../include/reloco/allocator.hpp) — `mem_block`,
  `allocator_traits<Tag>`'s contract doc comment, and `allocator_ref`.
- [`reloco/heap_allocator.hpp`](../include/reloco/heap_allocator.hpp)/
  [`.ipp`](../include/reloco/heap_allocator.ipp) — the built-in
  non-absorbing process-heap backend this guide's worked example extends.
- [`reloco/stack_allocator.hpp`](../include/reloco/stack_allocator.hpp) —
  the built-in bump-pointer backend, a stateful `context_type` example.
- [Extending reloco](extending.md) — the tag + `*_traits<Tag>` +
  `context_type` provider pattern this guide assumes.
