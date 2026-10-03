// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file interposer.cpp
 * @brief `LD_PRELOAD`-able glibc `malloc`/`free`/`calloc`/`realloc`/
 * `posix_memalign`/`aligned_alloc`/`memalign`/`reallocarray` interposer,
 * backed entirely by `reloco::malloc_allocator<reloco::mutex>` -- built as
 * a real-world stress test for `malloc_allocator`, not as a production
 * allocator: every allocation any dynamically-linked program makes (its
 * own code, libstdc++/libc++, and any other library it loads) gets routed
 * through `malloc_allocator`'s boundary-tag heap instead of glibc's own,
 * which is a far more varied, adversarial workload (arbitrary sizes,
 * alignments, lifetimes, and concurrency patterns from code that has no
 * idea `malloc_allocator` even exists) than any unit test can exercise.
 *
 * ## How it's wired up
 *
 * `malloc_allocator`'s own upstream `allocator_ref` here is a small
 * `mmap`/`munmap`-based `allocator_traits` backend (see
 * `mmap_allocator_tag` below): every arena `malloc_allocator` asks for is
 * obtained via an anonymous `mmap()` (rounded up to a whole page, which
 * `munmap()` requires), never through glibc's own `malloc` family -- this
 * is what makes the interposer safe to install over glibc's allocator
 * rather than circular: nothing in this translation unit, nor in
 * `malloc_allocator`/`allocator.hpp` themselves, ever calls `malloc`
 * (reloco is designed to avoid hidden heap allocation throughout; see
 * `reloco`'s own docs), so there is no risk of this file's `malloc()`
 * recursing into itself via some library call it makes internally.
 *
 * `reloco::mutex` (pthread-backed on Linux) guards the single shared
 * `malloc_allocator` instance, so this interposer is safe under real
 * multi-threaded applications, not just single-threaded ones.
 *
 * `reloco::default_allocator()` itself is also redirected to this same
 * `malloc_allocator` instance (via `RELOCO_DEFAULT_ALLOCATOR_CUSTOM`; see
 * `default_allocator.hpp`), just in case anything reloco-based ends up
 * running in the interposed process and reaching for it directly -- so
 * there is exactly one heap in play process-wide, not a second,
 * un-stress-tested one hiding behind `reloco::default_allocator()`.
 *
 * ## Lazy, allocation-free initialization
 *
 * The shared `malloc_allocator<reloco::mutex>` instance is constructed
 * exactly once, on first use, via `pthread_once()` into a plain static
 * byte buffer (placement-`new`, never destroyed) -- deliberately NOT a
 * namespace-scope object with a normal constructor/destructor, nor even a
 * function-local `static` with the usual magic-statics guard, because:
 *
 * - Startup: the very first `malloc`/`free`/... call this process ever
 *   makes may come from the dynamic loader itself, or from very early
 *   libc/libstdc++ startup code, before this library's own ELF
 *   constructors (`.init_array`) would have had a chance to run. A plain
 *   `pthread_once_t`/raw buffer pair has no constructor of its own to
 *   race against that -- it is usable correctly from the very first
 *   instruction this shared object's code ever executes.
 * - Shutdown: letting the instance run through an ordinary static
 *   destructor would register it with `__cxa_atexit`, to be destroyed
 *   somewhere in the middle of the process's teardown sequence -- at
 *   which point other libraries' own static destructors (running before
 *   or after, in an order this file has no control over) may still call
 *   `free()` on blocks this heap owns. Never destroying it (matching how
 *   glibc's own `malloc` arena state is never cleanly torn down either)
 *   sidesteps that ordering question entirely; the OS reclaims every
 *   `mmap()`-ed arena when the process exits regardless.
 *
 * ## Known limitations (this is a demo/stress-test, not a drop-in libc)
 *
 * - `realloc(ptr, 0)`: forwarded to `malloc_allocator::realloc`, which
 *   treats it as an ordinary zero-byte reallocation (returns a valid,
 *   zero-usable-size, pointer) rather than glibc's traditional
 *   free-and-return-`NULL` behavior (itself not something C or POSIX
 *   actually mandates).
 * - `malloc_usable_size()`, `mallopt()`/`mallinfo()`/`malloc_stats()`,
 *   `pvalloc()`, and glibc's "independent_"-prefixed extensions are not
 *   implemented.
 * - Not safe to `dlclose()`/unload once loaded: it never releases its
 *   state, and every live allocation anywhere in the process depends on
 *   it.
 * - Not reentrant from a signal handler: like glibc's own `malloc`,
 *   calling into this interposer from a signal handler that interrupted
 *   another call already holding `lock_` deadlocks (`reloco::mutex` is
 *   non-recursive) -- again, no different from the real constraint on
 *   async-signal-safety glibc's `malloc` itself has.
 *
 * ## Usage
 *
 * @code{.sh}
 * cmake -S demos/malloc_interposer -B demos/malloc_interposer/build
 * cmake --build demos/malloc_interposer/build
 * LD_PRELOAD=$PWD/demos/malloc_interposer/build/libreloco_malloc_interposer.so some_program
 * @endcode
 */

// Must be defined before the first reloco header is included anywhere in
// this translation unit: suppresses default_allocator.hpp's own built-in
// heap_allocator_tag-backed definition of reloco_global_alloc::
// default_allocator() so this file's replacement below is the only one.
#define RELOCO_DEFAULT_ALLOCATOR_CUSTOM

#include <reloco/default_allocator.hpp>
#include <reloco/malloc_allocator.hpp>
#include <reloco/mutex.hpp>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <new>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

// Growth granularity for malloc_allocator's own arenas: each refill
// mmap()s (at least) this many bytes. 1 MiB keeps the number of distinct
// mmap() mappings (and therefore page-table/VMA overhead) reasonable for
// a real application's working set, while still being returned to the OS
// promptly, arena by arena, as malloc_allocator's own free()-time
// coalescing empties one out entirely -- see malloc_allocator.hpp's file
// docs for that behavior.
constexpr std::size_t kDefaultArenaBytes = 1u << 20;

std::size_t page_size() noexcept {
  static const std::size_t size = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  return size;
}

std::size_t round_up_to_page(std::size_t bytes) noexcept {
  const std::size_t page = page_size();
  return (bytes + page - 1) & ~(page - 1);
}

// Tag for malloc_allocator's own upstream: hands out whole pages via
// anonymous mmap()/munmap(), never through malloc() -- see the file-level
// docs for why that non-circularity matters here specifically.
struct mmap_allocator_tag {};

} // namespace

namespace reloco {

template <> struct allocator_traits<mmap_allocator_tag> {
  using context_type = void;

  [[nodiscard]] static result<mem_block> allocate(std::size_t bytes, std::size_t /*alignment*/) noexcept {
    // malloc_allocator only ever requests alignof(std::max_align_t)
    // (typically 16) for its arenas; mmap()'s result is always
    // page-aligned (4096 on every mainstream Linux target), which
    // trivially satisfies that, so the requested alignment needs no
    // further handling here -- only the *length* needs rounding up,
    // which munmap() requires anyway.
    const std::size_t rounded = round_up_to_page(bytes);
    void *ptr = ::mmap(nullptr, rounded, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED)
      return unexpected(error::allocation_failed);
    return mem_block{ptr, rounded};
  }

  static void deallocate(void *ptr, std::size_t bytes) noexcept { ::munmap(ptr, bytes); }
};

} // namespace reloco

namespace {

using heap_type = reloco::malloc_allocator<reloco::mutex>;

// Storage for the one process-wide heap instance: a plain byte buffer,
// placement-new'd into on first use and never destroyed -- see the
// file-level docs ("Lazy, allocation-free initialization") for why this
// is deliberately not an ordinary namespace-scope or function-local
// `static` object.
alignas(heap_type) unsigned char g_heap_storage[sizeof(heap_type)];
pthread_once_t g_heap_once = PTHREAD_ONCE_INIT;
heap_type *g_heap = nullptr;

void init_heap() noexcept {
  g_heap = new (static_cast<void *>(g_heap_storage))
      heap_type(reloco::allocator<mmap_allocator_tag>::ref(), kDefaultArenaBytes);
}

heap_type &heap() noexcept {
  ::pthread_once(&g_heap_once, &init_heap);
  return *g_heap;
}

} // namespace

// Also replaces reloco::default_allocator() process-wide with the same
// heap this file's own malloc()/free()/... use: any reloco-based code
// running in this process (e.g. this file, or a library it's linked
// against that reaches for reloco::default_allocator() without an
// explicit allocator_ref) ends up exercising malloc_allocator exactly
// the same way plain malloc()/free() calls do, rather than silently
// falling back to a separate heap_allocator_tag/std::malloc instance
// that this interposer never sees or stress-tests. Requires
// RELOCO_DEFAULT_ALLOCATOR_CUSTOM (defined above, before the first
// reloco include) to suppress the library's own built-in definition --
// see default_allocator.hpp's docs.
namespace reloco {

inline allocator_ref reloco_global_alloc::default_allocator() noexcept { return ::heap().ref(); }

} // namespace reloco

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

extern "C" {

void *malloc(std::size_t size) noexcept {
  auto res = heap().malloc(size);
  if (!res) {
    errno = ENOMEM;
    return nullptr;
  }
  return res->ptr;
}

void free(void *ptr) noexcept { heap().free(ptr); }

void *calloc(std::size_t nmemb, std::size_t size) noexcept {
  std::size_t total;
  if (__builtin_mul_overflow(nmemb, size, &total)) {
    errno = ENOMEM;
    return nullptr;
  }
  auto res = heap().malloc(total);
  if (!res) {
    errno = ENOMEM;
    return nullptr;
  }
  std::memset(res->ptr, 0, total);
  return res->ptr;
}

void *realloc(void *ptr, std::size_t size) noexcept {
  auto res = heap().realloc(ptr, size);
  if (!res) {
    errno = ENOMEM;
    return nullptr;
  }
  return res->ptr;
}

void *reallocarray(void *ptr, std::size_t nmemb, std::size_t size) noexcept {
  std::size_t total;
  if (__builtin_mul_overflow(nmemb, size, &total)) {
    errno = ENOMEM;
    return nullptr;
  }
  return realloc(ptr, total);
}

int posix_memalign(void **memptr, std::size_t alignment, std::size_t size) noexcept {
  if (alignment % sizeof(void *) != 0 || (alignment & (alignment - 1)) != 0)
    return EINVAL;
  auto res = heap().memalign(alignment, size);
  if (!res)
    return ENOMEM;
  *memptr = res->ptr;
  return 0;
}

void *aligned_alloc(std::size_t alignment, std::size_t size) noexcept {
  auto res = heap().memalign(alignment, size);
  if (!res) {
    errno = ENOMEM;
    return nullptr;
  }
  return res->ptr;
}

// Legacy glibc API; still called directly by some real-world libraries.
void *memalign(std::size_t alignment, std::size_t size) noexcept {
  auto res = heap().memalign(alignment, size);
  if (!res) {
    errno = ENOMEM;
    return nullptr;
  }
  return res->ptr;
}

void *valloc(std::size_t size) noexcept {
  auto res = heap().memalign(page_size(), size);
  if (!res) {
    errno = ENOMEM;
    return nullptr;
  }
  return res->ptr;
}

} // extern "C"

RELOCO_END_UNSAFE_BUFFER_USAGE
