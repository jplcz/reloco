<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# malloc_interposer

A `LD_PRELOAD`-able shared object that replaces glibc's
`malloc`/`free`/`calloc`/`realloc`/`posix_memalign`/`aligned_alloc`/
`memalign`/`valloc`/`reallocarray` with
`reloco::malloc_allocator<reloco::mutex>` (see
[`malloc_allocator.hpp`](../../include/reloco/malloc_allocator.hpp)),
sourcing arenas directly from anonymous `mmap()`. It also redirects
`reloco::default_allocator()` itself to the same heap. See
[`interposer.cpp`](interposer.cpp) for the full rationale, the
allocation-free lazy-initialization scheme, and known limitations.

This is a **stress test**, not a production allocator or a build artifact
of reloco itself: it's deliberately a standalone CMake project (its own
`project()`, not an `add_subdirectory()` of the main build) and is never
built by `jplcz_reloco_tests`/`jplcz_reloco_demos`/CI -- run any
real-world, dynamically-linked program under it to see how
`malloc_allocator` holds up against a workload no unit test can
reproduce: arbitrary sizes/alignments/lifetimes, real concurrency, and
code with no idea `malloc_allocator` exists.

## Building

```sh
cmake -S demos/malloc_interposer -B demos/malloc_interposer/build
cmake --build demos/malloc_interposer/build
```

reloco is header-only, so no other dependency is needed; `RELOCO_INCLUDE_DIR`
(a cache variable) defaults to `../../include`, i.e. this works out of the
box from inside a reloco checkout, but can be pointed at any other copy of
reloco's `include/` directory -- this directory can be copied out of the
reloco tree and built fully independently.

## Usage

```sh
LD_PRELOAD=$PWD/demos/malloc_interposer/build/libreloco_malloc_interposer.so some_program
```

For example, to confirm it's actually routing allocations through
`mmap()`/`munmap()` rather than glibc's own heap:

```sh
LD_PRELOAD=$PWD/demos/malloc_interposer/build/libreloco_malloc_interposer.so \
  strace -f -e trace=mmap,munmap -c some_program
```

Try it against anything handy -- an interpreter (`python3`, `bash`), a
compiler invocation, or your own multi-threaded test program -- to
exercise `malloc_allocator` under real, adversarial allocation patterns.

## Known limitations

See the "Known limitations" section of [`interposer.cpp`](interposer.cpp)'s
file-level doc comment: `realloc(ptr, 0)` semantics differ slightly from
glibc, several glibc-specific introspection APIs
(`malloc_usable_size`/`mallopt`/`mallinfo`/...) are not implemented, it is
not safe to `dlclose()` once loaded, and it is not async-signal-safe --
all expected for a `malloc` replacement, including glibc's own.
