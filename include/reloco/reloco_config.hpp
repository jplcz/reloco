// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file reloco_config.hpp
 * @brief Single build-time customization entry point for every optional
 * reloco feature macro.
 *
 * This header is included first, before anything else, by
 * `reloco/detail/compat.hpp` (the library's common ancestor header), which
 * makes it the earliest point at which configuration can be injected
 * regardless of which reloco header an application includes first.
 *
 * User overrides must not be made by editing this file. Instead, define
 * `RELOCO_CONFIG` (via a compiler `-D` flag, e.g. `-DRELOCO_CONFIG=1`) to
 * opt in to including a header named `reloco_user_config.hpp`, which must be
 * reachable on the compiler's include search path (e.g. in an
 * application-owned include directory listed before reloco's own `include/`
 * in the include path). When `RELOCO_CONFIG` is defined,
 * `reloco_user_config.hpp` is included here, before any library header
 * defines its own default, so every `#define` it contains takes precedence
 * over the library defaults below and over the individual
 * `#ifndef`-guarded defaults each feature header applies on its own.
 *
 * Every macro below may alternatively be set directly with a compiler `-D`
 * flag instead of (or in addition to) `reloco_user_config.hpp`; both
 * approaches are equivalent since library headers only ever apply a default
 * when the macro is not already defined.
 *
 * `reloco_user_config.hpp` must not `#include` any reloco header
 * (directly or transitively). It is reached from `detail/compat.hpp`'s very
 * first line, before that header has defined even its own feature-detection
 * macros (`RELOCO_HAS_ATTRIBUTE` and friends); any reloco header pulled in
 * from here would re-enter `compat.hpp` while it is still on the include
 * stack, so `#pragma once` would skip it and leave those macros undefined,
 * breaking the build. Keep `reloco_user_config.hpp` to plain `#define`s;
 * customization points that need a real type or definition (like
 * `RELOCO_DEFAULT_ALLOCATOR_CUSTOM` below) work by opting out of the
 * library's own definition, so the actual override can be written as an
 * ordinary out-of-line definition in its own header, included by the
 * application through the normal path rather than from here.
 */

#if defined(RELOCO_CONFIG)
#include "reloco_user_config.hpp"
#endif

// ============================================================================
// Available customization points
// ============================================================================
//
// RELOCO_KERNEL
//     Define to build for kernel/freestanding targets. Requires
//     RELOCO_KERNEL_PANIC (see below).
//
// RELOCO_KERNEL_PANIC(expression, file, line, message)
//     Required when RELOCO_KERNEL is defined. Must be defined as a
//     function-like macro that reports assertion failures through the
//     target's own panic/log facility (printf-like, or a sequence of raw
//     string writes as a last resort).
//
// RELOCO_TRAP()
//     Must not return. Defaults to a compiler debug trap, falling back to
//     std::abort().
//
// RELOCO_UNREACHABLE() / RELOCO_HAS_UNREACHABLE
//     Optimizer hint for unreachable code. Set RELOCO_HAS_UNREACHABLE to 0
//     alongside a no-op RELOCO_UNREACHABLE() if the platform has none.
//
// RELOCO_DISABLE_ASSERT / RELOCO_DISABLE_ASSERT_STDIO
//     Disable RELOCO_ASSERT/RELOCO_DEBUG_ASSERT entirely, or just their
//     default std::fprintf(stderr, ...) diagnostic, respectively.
//
// RELOCO_DEBUG
//     Force RELOCO_DEBUG_ASSERT to stay active even when NDEBUG is defined,
//     instead of compiling down to RELOCO_UNREACHABLE()/no-op.
//
// RELOCO_DEFAULT_ALLOCATOR_CUSTOM
//     Define (to any value) to take over reloco::reloco_global_alloc::
//     default_allocator(), the hook reloco::default_allocator() (see
//     reloco/default_allocator.hpp) forwards to. Defining it suppresses the
//     library's own definition (allocator<heap_allocator_tag>::ref(), the
//     process heap) so exactly one definition -- yours -- exists.
//
//     Since reloco_user_config.hpp cannot #include reloco/allocator.hpp
//     (see above), define the Tag/allocator_traits<Tag> specialization and
//     the out-of-line hook definition in their own header, included by the
//     application through the normal path, not from reloco_user_config.hpp:
//
//         // reloco_user_config.hpp
//         #define RELOCO_DEFAULT_ALLOCATOR_CUSTOM
//
//         // my_arena_allocator.hpp, included normally elsewhere by the app
//         #include <reloco/allocator.hpp>
//         #include <reloco/default_allocator.hpp>
//         struct my_arena_tag {};
//         template <> struct reloco::allocator_traits<my_arena_tag> { ... };
//         inline reloco::allocator_ref
//         reloco::reloco_global_alloc::default_allocator() noexcept {
//           static my_arena arena;
//           return reloco::allocator<my_arena_tag>(arena).ref();
//         }
//
// RELOCO_SSO_STRING_CAPACITY
//     Number of characters (excluding the trailing null terminator) that
//     reloco::basic_sso_string<CharT, TraitsT> (see reloco/sso_string.hpp)
//     stores inline before falling back to a heap allocation. Defaults to
//     15. This is a single process-wide constant, not a template
//     parameter, so every basic_sso_string instantiation shares it.
//
// RELOCO_MUTEX_BACKEND_STD / RELOCO_MUTEX_BACKEND_PTHREAD
//     Define exactly one (any value) to force which built-in backend
//     reloco/mutex.hpp uses for reloco::mutex/recursive_mutex/
//     error_checking_mutex/shared_mutex/condition_variable:
//       - RELOCO_MUTEX_BACKEND_STD: wraps <mutex>/<shared_mutex>/
//         <condition_variable>, portable to any hosted C++17 target.
//       - RELOCO_MUTEX_BACKEND_PTHREAD: wraps <pthread.h> directly, for
//         POSIX targets.
//     If neither is defined, mutex.hpp auto-selects PTHREAD when
//     <pthread.h> is available (RELOCO_HAS_INCLUDE), otherwise STD.
//
// RELOCO_MUTEX_BACKEND_CUSTOM
//     Define (to any value) to take over reloco::mutex/recursive_mutex/
//     error_checking_mutex/shared_mutex/condition_variable entirely --
//     suppresses both built-in backends above, so an application
//     targeting a platform with neither pthread nor a hosted <mutex> (an
//     RTOS, a Win32-native backend, a freestanding target, ...) can supply
//     its own, matching public API. Unlike RELOCO_DEFAULT_ALLOCATOR_CUSTOM
//     (an ordinary out-of-line definition, included anywhere by the
//     application before first use), these are concrete classes other
//     reloco headers reference directly, so reloco/mutex.hpp instead
//     #includes a fixed path, "detail/porting/mutex.hpp", right where the
//     built-in backend would otherwise define them -- independent of
//     where else the application includes its replacement from. That
//     file does not ship in this repository (only
//     detail/porting/mutex.template.hpp, a documentation-only scaffold,
//     does); supply your own, most conveniently via the
//     JPLCZ_RELOCO_PORTING_HEADERS CMake variable (see CMakeLists.txt),
//     which copies it into that exact path and defines
//     RELOCO_MUTEX_BACKEND_CUSTOM for you. See reloco/mutex.hpp for the
//     full rationale and the exact API each class must provide.
//
// RELOCO_THREAD_BACKEND_STD / RELOCO_THREAD_BACKEND_PTHREAD /
// RELOCO_THREAD_BACKEND_CUSTOM
//     Same shape as the RELOCO_MUTEX_BACKEND_* family above, for
//     reloco::thread/thread_id/this_thread::get_id()/this_thread::yield()
//     (see reloco/thread.hpp). RELOCO_THREAD_BACKEND_CUSTOM's replacement
//     lives at the fixed path "detail/porting/thread.hpp" (scaffold:
//     detail/porting/thread.template.hpp).
//
// RELOCO_SPIN_LOCK_BACKEND_CUSTOM
//     Define (to any value) to take over reloco::spin_lock entirely --
//     the built-in implementation is plain std::atomic<bool> and works
//     unchanged even in a freestanding/kernel build, but a kernel target
//     usually wants its own native spinlock instead (tied into its own
//     interrupt-masking/preemption-disabling conventions, e.g. FreeBSD's
//     MTX_SPIN or Linux's raw_spinlock_t). Same fixed-include mechanism as
//     RELOCO_MUTEX_BACKEND_CUSTOM above: reloco/spin_lock.hpp #includes
//     "detail/porting/spin_lock.hpp" (scaffold:
//     detail/porting/spin_lock.template.hpp) in place of its own
//     definition.
//
// JPLCZ_RELOCO_PORTING_HEADERS (CMake variable, not a preprocessor macro)
//     A directory containing any of mutex.hpp/thread.hpp/spin_lock.hpp/
//     tls_provider.hpp (see reloco/detail/porting/*.template.hpp for the
//     shape each must have). Set before configuring reloco (top-level or
//     via add_subdirectory/FetchContent); CMakeLists.txt copies whichever
//     of those files exist there into
//     <install-prefix>/include/reloco/detail/porting/ (build tree during
//     the build, then installed alongside reloco's own headers -- it
//     never overwrites/replaces reloco's own headers, only adds files
//     under detail/porting/) and defines the matching
//     RELOCO_*_BACKEND_CUSTOM/RELOCO_TLS_MODEL macro on the jplcz_reloco
//     INTERFACE target automatically, so every consumer linking against
//     it (including a downstream find_package(jplcz_reloco) consumer, via
//     the exported target) picks up the replacement with no further
//     per-consumer configuration.
//
// RELOCO_ENABLE_EXPORT
//     Define (to any value) to make RELOCO_EXPORT (reloco/detail/
//     compat.hpp) expand to __attribute__((visibility("default"))) on a
//     backend that supports it (GCC/Clang), instead of its default no-op.
//     RELOCO_EXPORT marks the handful of entities whose *address* -- of a
//     static data member, of the storage a class-wide static data member
//     points at, or of a function-local static inside an inline function
//     -- reloco relies on being the same across every translation unit
//     that instantiates/defines them for the same template argument(s):
//       - reloco/type_id.hpp's type_id_tag<T>, backing
//         reloco::type_id (and reloco::any built on it) identifying a type
//         without RTTI by that address.
//       - reloco/fallible_singleton.hpp's fallible_singleton<T> and
//         atomic_fallible_singleton<T>, whose entire point is
//         that every instance() caller for a given T shares one storage/
//         state pair.
//       - reloco/default_allocator.hpp's reloco_global_alloc, whose
//         default_allocator() hook a custom (RELOCO_DEFAULT_ALLOCATOR_
//         CUSTOM) implementation typically backs with a function-local
//         static (see that header's example); an inline function's local
//         statics need the function itself to keep default visibility to
//         stay one merged instance.
//     Left at its default, a consumer building with -fvisibility=hidden (a
//     common default for shared libraries) would give each shared object
//     its own private copy of these symbols, silently breaking type_id
//     equality, splitting one singleton into several, or giving each
//     shared object its own separate default-allocator state, for anything
//     shared across that boundary. This is opt-in, rather than always on,
//     because it only matters if type_id/any values, singleton instance()
//     calls, or the default allocator, are actually shared across a
//     shared-object boundary; a single binary, or a -fvisibility=hidden
//     build with no such sharing, pays no cost either way and needs no
//     override. No-op regardless on a backend without an equivalent
//     attribute (e.g. MSVC); see reloco/type_id.hpp for the full rationale
//     and Windows caveat.
//
//     Only takes effect for a given T if T itself also has default
//     visibility: GCC/Clang compute a template instantiation's visibility
//     as the minimum of the template's own visibility and each template
//     argument's, so this alone does not make an ordinary consumer-defined
//     class shared across -fvisibility=hidden shared objects -- that class
//     needs its own __attribute__((visibility("default"))) (or
//     equivalent) too. Fundamental types (e.g. int) always qualify. This
//     restriction does not apply to reloco_global_alloc, which is not a
//     template.
//
// RELOCO_IMPLICIT_TYPEID
//     Define (to any value) to let reloco/type_id.hpp's type_id::name()
//     fall back to typeid(T).name() (implementation-defined, typically a
//     mangled name) as a last resort for a T with no explicit
//     RELOCO_TYPE_ID_NAME registration, instead of nullptr. Only takes
//     effect if RTTI is also enabled in the translation unit (see
//     RELOCO_HAS_RTTI in reloco/detail/compat.hpp) -- reloco never
//     requires RTTI for anything, so -fno-rtti/`/GR-` keep working exactly
//     as before regardless of this macro, and this macro alone (without
//     RTTI enabled) changes nothing. An explicit RELOCO_TYPE_ID_NAME
//     registration always wins over this fallback for the T it names.
//
// RELOCO_SHARED / RELOCO_SHARED_BUILD
//     Opt in to deduplicating both (a) reloco's own non-template concrete
//     backends -- mutex/recursive_mutex/shared_mutex/condition_variable/
//     error_checking_mutex (mutex.hpp), the heap/stack allocator backends
//     (heap_allocator.hpp/stack_allocator.hpp), the default allocator hook
//     (default_allocator.hpp), and the std::error_code bridge
//     (error_std.hpp, opt-in) -- via RELOCO_API/
//     RELOCO_SHARED_PROVIDE_DEFINITIONS (see reloco/detail/compat.hpp),
//     and (b) explicit reloco class-template instantiations
//     (reloco::vector<MyType>, reloco::optional<MyType>,
//     reloco::flat_map<K, V>, reloco's own basic_string<char>/
//     basic_sso_string<char>/basic_string_view<char>, ...) via
//     RELOCO_TYPE_INSTANCE, across a multi-`.so` deployment instead of
//     each shared object compiling its own copy. RELOCO_SHARED must be
//     defined consistently by every translation unit in the program;
//     exactly one of them -- the one building the actual shared library --
//     must additionally define RELOCO_SHARED_BUILD. See
//     reloco/reloco_extern.hpp (RELOCO_TYPE_INSTANCE(Type)),
//     reloco/reloco_compile.hpp (an umbrella covering both reloco's own
//     concrete backends and its char-based string aliases -- not the
//     wchar_t ones, and not error_std.hpp, both deliberately opt-in), and
//     docs/shared-library.md for the full walkthrough.
//
//     RELOCO_SHARED alone (without also listing your own application
//     types via RELOCO_TYPE_INSTANCE) still deduplicates reloco's own
//     built-in concrete backends above, mirroring jplcz_microfmt's
//     MICROFMT_SHARED for its own built-in formatters -- but has no
//     effect on any application-supplied class-template instantiation
//     until RELOCO_TYPE_INSTANCE lists it explicitly.
//
// RELOCO_DISABLE_SANITIZER_ANNOTATIONS
//     Define (to any value) to force reloco/detail/sanitizer.hpp's memory
//     poisoning annotations off even when AddressSanitizer is otherwise
//     auto-detected (__SANITIZE_ADDRESS__ / __has_feature(address_sanitizer))
//     and even if RELOCO_USE_VALGRIND is also defined.
//
// RELOCO_USE_VALGRIND
//     Opt in to Valgrind/Memcheck memory annotations in
//     reloco/detail/sanitizer.hpp (MAKE_MEM_NOACCESS/MAKE_MEM_UNDEFINED via
//     <valgrind/memcheck.h>). Unlike AddressSanitizer, "running under
//     Valgrind" cannot be auto-detected at compile time, and
//     <valgrind/memcheck.h> is a separate, not-always-installed header, so
//     this is opt-in only and degrades to a no-op if the header is not
//     found on the include path.
//
// RELOCO_MUTEX_NO_MONOTONIC_CLOCK
//     Forces reloco/mutex.hpp's RELOCO_MUTEX_BACKEND_PTHREAD backend to
//     initialize condition_variable's underlying pthread_cond_t against
//     the default CLOCK_REALTIME, even on a platform that would otherwise
//     be auto-detected as supporting pthread_condattr_setclock(CLOCK_
//     MONOTONIC) (POSIX's optional "Clock Selection" feature). Has no
//     effect on RELOCO_MUTEX_BACKEND_STD (always backed directly by
//     std::condition_variable::wait_for, whose own clock is fixed by the
//     standard library implementation).
//
// RELOCO_TLS_MODEL
//     Selects which backend reloco/tls_provider.hpp's public
//     tls_provider<T, Tag> uses for genuinely per-thread storage:
//       - RELOCO_TLS_MODEL_THREAD_LOCAL (default): backed by C++11
//         thread_local. Portable to any hosted C++17 target.
//       - RELOCO_TLS_MODEL_PTHREAD: backed by pthread_key_create/
//         pthread_getspecific/pthread_setspecific, for POSIX targets that
//         want to avoid compiler thread_local support.
//       - RELOCO_TLS_MODEL_OS: #includes the fixed path
//         "detail/porting/tls_provider.hpp" (scaffold:
//         detail/porting/tls_provider.template.hpp) in place of a
//         built-in definition; a kernel/RTOS port supplies one generic
//         tls_provider<T, Tag> against its own per-task storage, the same
//         fixed-include escape hatch RELOCO_MUTEX_BACKEND_CUSTOM/
//         RELOCO_THREAD_BACKEND_CUSTOM/RELOCO_SPIN_LOCK_BACKEND_CUSTOM
//         provide elsewhere (also settable via JPLCZ_RELOCO_PORTING_
//         HEADERS, see above).
//       - RELOCO_TLS_MODEL_SINGLE: one global static instance (not
//         actually per-thread), for single-threaded builds.
//     tls_provider<T, Tag>::get()/set() are fallible (return
//     result<...>) and accept an allocator_ref (default
//     default_allocator()) used for any actual heap allocation the
//     backend needs (RELOCO_TLS_MODEL_PTHREAD's heap-allocated
//     specialization only -- every other model/specialization never
//     actually fails). tls_provider is a public reloco API (used
//     internally by, e.g., the per-thread parker behind
//     this_thread::park(), but also usable directly); see
//     reloco/tls_provider.hpp.
