// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file malloc_allocator_libc.cpp
 * @brief Drop-in C `malloc`/`free`/`calloc`/`realloc`/`aligned_alloc` and
 * the full C++ set of replaceable `operator new`/`operator delete`
 * overloads, all implemented on top of a single, caller-supplied
 * `reloco::malloc_allocator<Mutex>` -- for embedded/freestanding targets
 * that need a working heap and `new`/`delete` but would rather not hand-rig
 * their own from scratch.
 *
 * This file is meant to be compiled as exactly one translation unit of the
 * target application/firmware -- either add it directly to the build as a
 * source file, or `#include` it from exactly one of your own `.cpp` files.
 * Either way, `RELOCO_MALLOC_ALLOCATOR` (see below) must already be
 * `#define`d by the time it is reached, so it is usually simplest to give
 * it on the command line (`-DRELOCO_MALLOC_ALLOCATOR=...`) or from a
 * force-included project-wide config header, rather than editing this file.
 *
 * @warning Not meant to coexist with a hosted libc's own heap (e.g. glibc
 * on Linux): a hosted libc's *own* internals (notably its stdio buffering)
 * typically call back into functions this file does not replace, such as
 * `malloc_usable_size()`, expecting them to understand pointers returned
 * by *its* allocator -- handing them a pointer from an entirely different
 * allocator corrupts memory in ways this file cannot prevent. This is the
 * right tool for a freestanding/embedded target with no competing libc
 * heap at all (the intended use case); for *replacing* a hosted libc's
 * heap process-wide instead, see `demos/malloc_interposer/`, which
 * interposes the complete symbol set (including `malloc_usable_size()`)
 * via `LD_PRELOAD`.
 *
 * ## Required configuration: `RELOCO_MALLOC_ALLOCATOR`
 *
 * Must be `#define`d, before this file is compiled, to an expression
 * naming an existing `reloco::malloc_allocator<Mutex>` lvalue (any `Mutex`
 * -- this file never names the type, so it works unchanged whichever
 * `Lock` you picked, including a target-specific one via
 * `RELOCO_MUTEX_BACKEND_CUSTOM`). Typically a function call returning a
 * reference to a function-local `static` (so construction happens lazily,
 * on first use, exactly like `reloco::malloc_allocator` itself expects --
 * see `malloc_allocator.hpp`):
 *
 * @code
 * // my_heap.hpp, included by the application before this file is compiled.
 * #include <reloco/malloc_allocator.hpp>
 *
 * inline reloco::malloc_allocator<reloco::mutex> &my_heap() noexcept {
 *   static reloco::malloc_allocator<reloco::mutex> heap(my_upstream_allocator_ref(), 65536);
 *   return heap;
 * }
 *
 * // Before compiling this file (command line, or a force-included header):
 * #define RELOCO_MALLOC_ALLOCATOR my_heap()
 * @endcode
 *
 * ## Out-of-memory handling (no exceptions)
 *
 * Every function here is `noexcept` -- except the four plain (non-aligned,
 * non-nothrow, no-exceptions-ordinarily-wouldn't-apply) `operator new`/
 * `operator new[]` overloads, which the standard itself declares *without*
 * `noexcept` in `<new>` (a replacement must match that exact exception
 * specification, so redeclaring them `noexcept` here would conflict and
 * fail to compile) -- including the ordinarily-throwing
 * `operator new`/`operator new[]` overloads: embedded/freestanding targets
 * very often build with exceptions disabled entirely, so throwing
 * `std::bad_alloc` is not an option regardless of what their declared type
 * says. On allocation failure, the throwing `operator new` overloads
 * instead invoke `RELOCO_MALLOC_ALLOCATOR_ON_OOM()`
 * (default: `RELOCO_TRAP()`, see `detail/compat.hpp` -- a debug trap/
 * `std::abort()`), which must not return; the `nothrow`/C-API functions
 * never call it and simply return `nullptr` instead, exactly like normal
 * `malloc`/the `nothrow` `operator new` overloads already do.
 *
 * ## `errno`: opt-in, no-op by default
 *
 * Plenty of freestanding/embedded targets have no `<cerrno>`/`errno` at
 * all, so nothing here touches it unless explicitly asked to:
 * `RELOCO_MALLOC_ALLOCATOR_SET_ERRNO(code)` defaults to a no-op; define it
 * yourself (after `#include <cerrno>`) to make the C API set `errno` the
 * usual way:
 *
 * @code
 * #include <cerrno>
 * #define RELOCO_MALLOC_ALLOCATOR_SET_ERRNO(code) (errno = (code))
 * @endcode
 *
 * `posix_memalign` is gated the same way, but behind its own separate
 * opt-in, `RELOCO_MALLOC_ALLOCATOR_PROVIDE_POSIX_MEMALIGN` (define to any
 * value), since its *return value* -- not `errno` -- is `EINVAL`/`ENOMEM`,
 * so it needs `<cerrno>` regardless of whether
 * `RELOCO_MALLOC_ALLOCATOR_SET_ERRNO` is overridden at all; this file only
 * `#include <cerrno>` when that opt-in is defined, so it is never pulled
 * in unasked for.
 *
 * ## What's provided
 *
 * - C: `malloc`, `free`, `calloc`, `realloc`, `aligned_alloc` (always);
 *   `posix_memalign` (opt-in, see above).
 * - C++: the complete C++17 set of 20 replaceable allocation/deallocation
 *   functions -- every `operator new`/`operator new[]`/`operator delete`/
 *   `operator delete[]` overload taking some combination of a size, a
 *   `std::align_val_t`, and/or a `std::nothrow_t` -- so every standard
 *   library container, `new`-expression, and `delete`-expression in the
 *   program (including over-aligned types, sized deallocation, and
 *   array-`new`) is routed through the same `malloc_allocator`.
 *
 * All allocation (the C API and the non-`nothrow`, non-aligned `operator
 * new` overloads alike) goes through `malloc_allocator::memalign()`
 * uniformly, requesting `alignof(std::max_align_t)` for the C API and the
 * non-aligned `operator new` overloads (matching what plain `malloc`/
 * ordinary `new` guarantee) and the caller-supplied `std::align_val_t` for
 * the aligned overloads -- there is no special-casing between "normal" and
 * "over-aligned" requests anywhere in this file, since `malloc_allocator`
 * itself already handles that uniformly (see `malloc_allocator.hpp`).
 */

#include <reloco/int_ops.hpp>
#include <reloco/malloc_allocator.hpp>

#include <cstddef>
#include <cstring>
#include <new>

#if !defined(RELOCO_MALLOC_ALLOCATOR)
#error                                                                                                                 \
    "malloc_allocator_libc.cpp requires RELOCO_MALLOC_ALLOCATOR to be #define'd (before this file is compiled) to an expression naming a reloco::malloc_allocator<Mutex> lvalue -- see this file's header comment."
#endif

#if !defined(RELOCO_MALLOC_ALLOCATOR_ON_OOM)
#define RELOCO_MALLOC_ALLOCATOR_ON_OOM() RELOCO_TRAP()
#endif

#if !defined(RELOCO_MALLOC_ALLOCATOR_SET_ERRNO)
#define RELOCO_MALLOC_ALLOCATOR_SET_ERRNO(code) ((void)0)
#endif

#if defined(RELOCO_MALLOC_ALLOCATOR_PROVIDE_POSIX_MEMALIGN)
#include <cerrno>
#endif

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

// A plain, non-static helper rather than inlining RELOCO_MALLOC_ALLOCATOR
// everywhere below: keeps every call site a short, uniform `heap()`,
// while `decltype(auto)` + the extra parentheses around the return
// expression preserve exactly whatever reference RELOCO_MALLOC_ALLOCATOR
// itself names (e.g. a function call returning a function-local static's
// reference -- re-evaluated on every call, not cached here, which is what
// gives "construct on first use" semantics to whatever
// RELOCO_MALLOC_ALLOCATOR expands to, not this helper).
decltype(auto) heap() noexcept { return (RELOCO_MALLOC_ALLOCATOR); }

} // namespace

// malloc(): natural alignment (alignof(std::max_align_t)), matching what
// plain malloc() always guarantees regardless of what the caller intends
// to store in it.
extern "C" void *malloc(std::size_t size) noexcept {
  auto res = heap().memalign(alignof(std::max_align_t), size);
  if (!res) {
    RELOCO_MALLOC_ALLOCATOR_SET_ERRNO(ENOMEM);
    return nullptr;
  }
  return res->ptr;
}

extern "C" void free(void *ptr) noexcept { heap().free(ptr); }

extern "C" void *calloc(std::size_t nmemb, std::size_t size) noexcept {
  auto checked_total = reloco::checked_mul(nmemb, size);
  if (!checked_total) {
    RELOCO_MALLOC_ALLOCATOR_SET_ERRNO(ENOMEM);
    return nullptr;
  }
  auto res = heap().memalign(alignof(std::max_align_t), *checked_total);
  if (!res) {
    RELOCO_MALLOC_ALLOCATOR_SET_ERRNO(ENOMEM);
    return nullptr;
  }
  // malloc_allocator carves blocks out of arenas obtained from raw, never
  // pre-zeroed, upstream memory (and reuses freed blocks verbatim), so
  // calloc() must zero-fill explicitly -- unlike e.g. a fresh anonymous
  // mmap()/sbrk() page, nothing upstream of it guarantees zeroed memory.
  std::memset(res->ptr, 0, *checked_total);
  return res->ptr;
}

extern "C" void *realloc(void *ptr, std::size_t size) noexcept {
  auto res = heap().realloc(ptr, size);
  if (!res) {
    RELOCO_MALLOC_ALLOCATOR_SET_ERRNO(ENOMEM);
    return nullptr;
  }
  return res->ptr;
}

// C11/C++17 standard API (not a glibc/POSIX extension like
// posix_memalign): always provided, no <cerrno> dependency -- `alignment`
// must be a power of two and `size` a multiple of it per the standard's
// own contract, which this file does not re-validate (matching ordinary
// libc implementations, which likewise leave violating that contract as
// undefined behavior rather than a checked runtime error).
extern "C" void *aligned_alloc(std::size_t alignment, std::size_t size) noexcept {
  auto res = heap().memalign(alignment, size);
  if (!res) {
    RELOCO_MALLOC_ALLOCATOR_SET_ERRNO(ENOMEM);
    return nullptr;
  }
  return res->ptr;
}

#if defined(RELOCO_MALLOC_ALLOCATOR_PROVIDE_POSIX_MEMALIGN)
// POSIX extension: returns an error code directly (never through errno,
// per POSIX), so it needs EINVAL/ENOMEM regardless of whether
// RELOCO_MALLOC_ALLOCATOR_SET_ERRNO does anything at all -- hence the
// separate opt-in guarding the <cerrno> #include above.
extern "C" int posix_memalign(void **memptr, std::size_t alignment, std::size_t size) noexcept {
  if (alignment % sizeof(void *) != 0 || (alignment & (alignment - 1)) != 0)
    return EINVAL;
  auto res = heap().memalign(alignment, size);
  if (!res)
    return ENOMEM;
  *memptr = res->ptr;
  return 0;
}
#endif

// ============================================================================
// C++ replaceable allocation/deallocation functions (the complete C++17
// set: 8 `operator new`/`operator new[]` overloads + 12 `operator
// delete`/`operator delete[]` overloads).
// ============================================================================

namespace {

[[nodiscard]] void *new_impl(std::size_t size, std::size_t alignment) noexcept {
  auto res = heap().memalign(alignment, size);
  if (!res)
    RELOCO_MALLOC_ALLOCATOR_ON_OOM(); // must not return.
  return res->ptr;
}

[[nodiscard]] void *new_impl_nothrow(std::size_t size, std::size_t alignment) noexcept {
  auto res = heap().memalign(alignment, size);
  return res ? res->ptr : nullptr;
}

} // namespace

// -- Non-aligned (alignof(std::max_align_t)) --------------------------------

// Not `noexcept`: the standard declares these two (and their aligned
// counterparts below) without `noexcept` in <new>, and a replacement must
// match that exact exception specification -- redeclaring them `noexcept`
// here would conflict with <new>'s declaration and fail to compile. They
// still never throw in practice (RELOCO_MALLOC_ALLOCATOR_ON_OOM() aborts
// instead, per this file's header comment), which is all that matters for
// a no-exceptions build: nothing here relies on the *type system* seeing
// them as noexcept, only on them never actually throwing.
void *operator new(std::size_t size) { return new_impl(size, alignof(std::max_align_t)); }

void *operator new[](std::size_t size) { return new_impl(size, alignof(std::max_align_t)); }

void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
  return new_impl_nothrow(size, alignof(std::max_align_t));
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept {
  return new_impl_nothrow(size, alignof(std::max_align_t));
}

void operator delete(void *ptr) noexcept { heap().free(ptr); }

void operator delete[](void *ptr) noexcept { heap().free(ptr); }

void operator delete(void *ptr, const std::nothrow_t &) noexcept { heap().free(ptr); }

void operator delete[](void *ptr, const std::nothrow_t &) noexcept { heap().free(ptr); }

// Sized deallocation (C++14): the size is accepted purely to satisfy the
// signature -- malloc_allocator::free() recovers the real block size from
// its own header (see malloc_allocator.hpp), so there is nothing extra to
// do with it here, exactly like the unsized overloads above.
void operator delete(void *ptr, std::size_t) noexcept { heap().free(ptr); }

void operator delete[](void *ptr, std::size_t) noexcept { heap().free(ptr); }

// -- Aligned (C++17) ---------------------------------------------------------

// Also not `noexcept` -- see the comment on the non-aligned throwing
// overloads above; the same rationale applies here.
void *operator new(std::size_t size, std::align_val_t alignment) {
  return new_impl(size, static_cast<std::size_t>(alignment));
}

void *operator new[](std::size_t size, std::align_val_t alignment) {
  return new_impl(size, static_cast<std::size_t>(alignment));
}

void *operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept {
  return new_impl_nothrow(size, static_cast<std::size_t>(alignment));
}

void *operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept {
  return new_impl_nothrow(size, static_cast<std::size_t>(alignment));
}

void operator delete(void *ptr, std::align_val_t) noexcept { heap().free(ptr); }

void operator delete[](void *ptr, std::align_val_t) noexcept { heap().free(ptr); }

void operator delete(void *ptr, std::size_t, std::align_val_t) noexcept { heap().free(ptr); }

void operator delete[](void *ptr, std::size_t, std::align_val_t) noexcept { heap().free(ptr); }

RELOCO_END_UNSAFE_BUFFER_USAGE
