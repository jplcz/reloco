// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file compat.hpp
 * @brief Compiler and language-version feature detection for reloco. */

#include "../reloco_config.hpp"

#include <cstdlib>

#if defined(_MSVC_LANG)
#define RELOCO_CXX_STANDARD _MSVC_LANG
#else
#define RELOCO_CXX_STANDARD __cplusplus
#endif

#define RELOCO_CXX11 (RELOCO_CXX_STANDARD >= 201103L)
#define RELOCO_CXX14 (RELOCO_CXX_STANDARD >= 201402L)
#define RELOCO_CXX17 (RELOCO_CXX_STANDARD >= 201703L)
#define RELOCO_CXX20 (RELOCO_CXX_STANDARD >= 202002L)
#define RELOCO_CXX23 (RELOCO_CXX_STANDARD > 202002L)

#if defined(__has_include)
#define RELOCO_HAS_INCLUDE(header) __has_include(header)
#else
#define RELOCO_HAS_INCLUDE(header) 0
#endif

#if defined(__has_cpp_attribute)
#define RELOCO_HAS_CPP_ATTRIBUTE(attribute) __has_cpp_attribute(attribute)
#else
#define RELOCO_HAS_CPP_ATTRIBUTE(attribute) 0
#endif

#if defined(__has_attribute)
#define RELOCO_HAS_ATTRIBUTE(attribute) __has_attribute(attribute)
#else
#define RELOCO_HAS_ATTRIBUTE(attribute) 0
#endif

#if RELOCO_CXX17 && RELOCO_HAS_CPP_ATTRIBUTE(nodiscard)
#define RELOCO_NODISCARD [[nodiscard]]
#else
#define RELOCO_NODISCARD
#endif

#if RELOCO_CXX17 && RELOCO_HAS_CPP_ATTRIBUTE(maybe_unused)
#define RELOCO_MAYBE_UNUSED [[maybe_unused]]
#else
#define RELOCO_MAYBE_UNUSED
#endif

#if RELOCO_CXX20 && RELOCO_HAS_CPP_ATTRIBUTE(no_unique_address)
#define RELOCO_NO_UNIQUE_ADDRESS [[no_unique_address]]
#elif defined(_MSC_VER) && RELOCO_HAS_CPP_ATTRIBUTE(msvc::no_unique_address)
#define RELOCO_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define RELOCO_NO_UNIQUE_ADDRESS
#endif

#if RELOCO_CXX20 && RELOCO_HAS_CPP_ATTRIBUTE(likely)
#define RELOCO_LIKELY [[likely]]
#define RELOCO_UNLIKELY [[unlikely]]
#else
#define RELOCO_LIKELY
#define RELOCO_UNLIKELY
#endif

#if defined(__clang__) || defined(__GNUC__)
#define RELOCO_ALWAYS_INLINE __attribute__((always_inline))
#elif defined(_MSC_VER)
#include <intrin.h>
#define RELOCO_ALWAYS_INLINE
#else
#define RELOCO_ALWAYS_INLINE
#endif

// Whether RTTI (`typeid`/`<typeinfo>`/`dynamic_cast`) is available in this
// translation unit. `__cpp_rtti` is the portable feature-test macro GCC and
// Clang only define when RTTI is enabled (i.e. undefined under
// `-fno-rtti`); MSVC instead defines `_CPPRTTI` under its default `/GR`
// (undefined under `/GR-`). reloco itself never requires RTTI for anything
// -- this only exists to let a handful of explicitly opt-in, best-effort
// features (see `RELOCO_IMPLICIT_TYPEID` in `type_id.hpp`) use it *if* the
// consumer has it enabled, without ever requiring it.
#if defined(__cpp_rtti) || defined(_CPPRTTI)
#define RELOCO_HAS_RTTI 1
#else
#define RELOCO_HAS_RTTI 0
#endif

// Forces default (exported) symbol visibility on an entity regardless of
// the translation unit's own `-fvisibility=hidden`/`-fvisibility-inlines-
// hidden` default. Needed for any implicitly-`inline` (since C++17) static
// data member whose *address* -- not its value -- is load-bearing: with
// hidden visibility, two shared objects (two plugins, an app and a shared
// library, ...) that each implicitly instantiate the same template for the
// same type get their own separate, non-merged copy of that symbol, so
// comparing addresses across the shared-object boundary silently breaks.
// `type_id.hpp`'s `detail::type_id_tag<T>` (backing `reloco::type_id`, and
// through it `any.hpp`'s type-erasure) and `fallible_singleton.hpp`'s
// `fallible_singleton<T>`/`atomic_fallible_singleton<T, LockTraits>`
// storage are exactly that case: see their own docs. Reduces to a no-op
// (and does not otherwise affect the entity's linkage/inlining) on a
// backend without an equivalent attribute, such as MSVC, or a
// single-binary target where the concern does not apply, such as most
// kernel/freestanding targets.
//
// Applying this to a class template only affects instantiations for a
// template argument T that itself has default visibility: GCC/Clang
// compute a template instantiation's visibility as the minimum of the
// template's own visibility and each template argument's, so an ordinary
// consumer-defined T compiled under `-fvisibility=hidden` without its own
// explicit default-visibility annotation still yields a hidden
// instantiation regardless of this attribute -- correctly so, since that
// mirrors the visibility the consumer chose for T's own symbols.
//
// This does not, on its own, make reloco safe to build as its own shared
// library (that would additionally need a build-vs-consume
// dllexport/dllimport split on Windows, which header-only reloco does not
// provide): it only keeps a handful of specific address-identity symbols
// mergeable when reloco's headers are compiled, as usual, directly into
// each consumer's own binary/shared library.
//
// Opt-in: a `-fvisibility=hidden` build that never crosses a shared-object
// boundary with a shared `T` pays no cost either way, so this is off by
// default and does nothing unless the consumer defines
// `RELOCO_ENABLE_EXPORT` (to any value, before including any reloco
// header) to acknowledge they want these specific symbols kept exported.
// See `reloco_config.hpp` for the customization point.
#if defined(RELOCO_ENABLE_EXPORT) && RELOCO_HAS_ATTRIBUTE(visibility)
#define RELOCO_EXPORT __attribute__((visibility("default")))
#else
#define RELOCO_EXPORT
#endif

// ============================================================================
// Shared-library build/consume support for reloco's own non-template
// concrete entities (RELOCO_SHARED / RELOCO_SHARED_BUILD)
// ============================================================================
//
// reloco is header-only: even a genuinely non-template, "heavy" member
// function (e.g. `mutex::lock()`'s pthread/std::mutex call, `allocator_
// traits<heap_allocator_tag>::allocate()`'s malloc/aligned_alloc plumbing,
// `detail::error_category_impl::message()`'s long switch) is plain
// `inline` and lives directly in its header (see e.g. `mutex.hpp`,
// `heap_allocator.hpp`, `error_std.hpp`), so -- unlike a class *template*,
// which `reloco_extern.hpp`'s `RELOCO_TYPE_INSTANCE` already covers --
// every translation unit that `#include`s one of these headers still gets
// its own copy, and the linker only merges duplicate copies within a
// single link step, never across separate shared-object boundaries.
//
// This is the exact same problem `jplcz_microfmt`'s `MICROFMT_SHARED`/
// `MICROFMT_API` solve for microfmt's own built-in concrete formatters,
// applied to reloco's much smaller set of non-template concrete entities.
// Every one of them still ships a matching `*.ipp` sibling (`mutex_
// pthread.ipp`/`mutex_std.ipp`/`mutex_common.ipp`, `heap_allocator.ipp`,
// `stack_allocator.ipp`, `error_std.ipp`) hosting its out-of-line bodies,
// included directly from the owning header, guarded on
// `RELOCO_SHARED_PROVIDE_DEFINITIONS` below -- there is no separate
// umbrella header to hand-maintain beyond `reloco_compile.hpp` (see that
// header and `docs/shared-library.md`).
//
// Define RELOCO_SHARED (to any value, before including any reloco header,
// consistently across every translation unit in the program) to switch
// every one of these entities to a plain declaration instead: ordinary
// consumers then link against one shared definition rather than each
// instantiating their own copy.
//
// Exactly one translation unit in the whole program -- the one building
// the actual shared library meant to host these definitions, typically via
// `#include <reloco/reloco_compile.hpp>` -- must additionally define
// RELOCO_SHARED_BUILD (to any value) before including any reloco header.
// That TU alone re-imports the `*.ipp` bodies as exported, out-of-line
// definitions; every other TU (RELOCO_SHARED defined, RELOCO_SHARED_BUILD
// not) only sees declarations and must be linked against that library.
//
// `RELOCO_API` decorates every entity affected by this split, mirroring
// `MICROFMT_API` exactly:
//   - RELOCO_SHARED not defined (default): `RELOCO_API` -> `inline`;
//     current, unchanged header-only behavior.
//   - RELOCO_SHARED defined, RELOCO_SHARED_BUILD not defined (consume):
//     `RELOCO_API` -> plain declaration, no body (+ `__declspec(dllimport)`
//     on MSVC).
//   - RELOCO_SHARED and RELOCO_SHARED_BUILD both defined (build):
//     `RELOCO_API` -> exported, out-of-line definition
//     (`__declspec(dllexport)` on MSVC, default visibility elsewhere).
//
// `RELOCO_SHARED_PROVIDE_DEFINITIONS` is 1 exactly when the current TU
// should pull in `*.ipp` bodies at all (default header-only mode, or the
// RELOCO_SHARED_BUILD library-build TU) and 0 when it should only see
// declarations (ordinary RELOCO_SHARED consumer).
//
// This is opt-in and off by default: a plain header-only build (the
// overwhelming common case, and every existing consumer) is entirely
// unaffected. It is also an entirely separate concern from RELOCO_EXPORT
// above: RELOCO_EXPORT is about keeping one *address* (a static data
// member's identity) merged across shared objects regardless of
// `-fvisibility=hidden`; RELOCO_API is about not *duplicating* a
// non-template function's compiled *body* into every shared object in the
// first place.
#if !defined(RELOCO_SHARED)
#define RELOCO_API inline
#define RELOCO_SHARED_PROVIDE_DEFINITIONS 1
#elif defined(RELOCO_SHARED_BUILD)
#if defined(_MSC_VER)
#define RELOCO_API __declspec(dllexport)
#elif RELOCO_HAS_ATTRIBUTE(visibility)
#define RELOCO_API __attribute__((visibility("default")))
#else
#define RELOCO_API
#endif
#define RELOCO_SHARED_PROVIDE_DEFINITIONS 1
#else
#if defined(_MSC_VER)
#define RELOCO_API __declspec(dllimport)
#else
#define RELOCO_API
#endif
#define RELOCO_SHARED_PROVIDE_DEFINITIONS 0
#endif

// RELOCO_API_CONSTEXPR is RELOCO_API for an entity that is `constexpr` in
// the default header-only build (where RELOCO_API is plain `inline`, so
// adding `constexpr` costs nothing and preserves compile-time callability
// exactly as before) but must drop `constexpr` under RELOCO_SHARED: a
// `constexpr` function is implicitly `inline`, which would force every
// RELOCO_SHARED_BUILD-declared-only consumer to still carry a definition,
// defeating the whole build/consume split.
#if !defined(RELOCO_SHARED)
#define RELOCO_API_CONSTEXPR constexpr RELOCO_API
#else
#define RELOCO_API_CONSTEXPR RELOCO_API
#endif

#if !defined(RELOCO_TRAP)
#if defined(__clang__) || defined(__GNUC__)
#define RELOCO_TRAP() __builtin_trap()
#elif defined(_MSC_VER)
#define RELOCO_TRAP() __debugbreak()
#else
#define RELOCO_TRAP() std::abort()
#endif
#endif

#if !defined(RELOCO_UNREACHABLE)
#if defined(__clang__) || defined(__GNUC__)
#define RELOCO_UNREACHABLE() __builtin_unreachable()
#if !defined(RELOCO_HAS_UNREACHABLE)
#define RELOCO_HAS_UNREACHABLE 1
#endif
#elif defined(_MSC_VER)
#define RELOCO_UNREACHABLE() __assume(0)
#if !defined(RELOCO_HAS_UNREACHABLE)
#define RELOCO_HAS_UNREACHABLE 1
#endif
#else
#define RELOCO_UNREACHABLE() ((void)0)
#if !defined(RELOCO_HAS_UNREACHABLE)
#define RELOCO_HAS_UNREACHABLE 0
#endif
#endif
#elif !defined(RELOCO_HAS_UNREACHABLE)
#define RELOCO_HAS_UNREACHABLE 1
#endif

#if RELOCO_CXX20 && RELOCO_HAS_INCLUDE(<span>)
#include <span>
#define RELOCO_HAS_STD_SPAN 1
#else
#define RELOCO_HAS_STD_SPAN 0
#endif

#if RELOCO_CXX20
#define RELOCO_CONSTEXPR20 constexpr
#else
#define RELOCO_CONSTEXPR20
#endif

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || (defined(_MSC_VER) && defined(_CPPUNWIND))
#define RELOCO_HAS_EXCEPTIONS 1
#else
#define RELOCO_HAS_EXCEPTIONS 0
#endif

// Clang Thread Safety Analysis (-Wthread-safety): lets `mutex.hpp`'s types
// and lock/unlock methods be statically checked for correct lock
// acquire/release pairing and detect potential race conditions on
// annotated data. Clang-only -- `RELOCO_HAS_ATTRIBUTE` correctly reports
// `false` on GCC/MSVC, so these expand to nothing there and impose no
// runtime cost or portability constraint on other compilers.
#if RELOCO_HAS_ATTRIBUTE(capability)
#define RELOCO_CAPABILITY(name) __attribute__((capability(name)))
#else
#define RELOCO_CAPABILITY(name)
#endif

#if RELOCO_HAS_ATTRIBUTE(scoped_lockable)
#define RELOCO_SCOPED_CAPABILITY __attribute__((scoped_lockable))
#else
#define RELOCO_SCOPED_CAPABILITY
#endif

#if RELOCO_HAS_ATTRIBUTE(guarded_by)
#define RELOCO_GUARDED_BY(x) __attribute__((guarded_by(x)))
#else
#define RELOCO_GUARDED_BY(x)
#endif

#if RELOCO_HAS_ATTRIBUTE(pt_guarded_by)
#define RELOCO_PT_GUARDED_BY(x) __attribute__((pt_guarded_by(x)))
#else
#define RELOCO_PT_GUARDED_BY(x)
#endif

#if RELOCO_HAS_ATTRIBUTE(acquire_capability)
#define RELOCO_ACQUIRE(...) __attribute__((acquire_capability(__VA_ARGS__)))
#else
#define RELOCO_ACQUIRE(...)
#endif

#if RELOCO_HAS_ATTRIBUTE(acquire_shared_capability)
#define RELOCO_ACQUIRE_SHARED(...) __attribute__((acquire_shared_capability(__VA_ARGS__)))
#else
#define RELOCO_ACQUIRE_SHARED(...)
#endif

#if RELOCO_HAS_ATTRIBUTE(release_capability)
#define RELOCO_RELEASE(...) __attribute__((release_capability(__VA_ARGS__)))
#else
#define RELOCO_RELEASE(...)
#endif

#if RELOCO_HAS_ATTRIBUTE(release_shared_capability)
#define RELOCO_RELEASE_SHARED(...) __attribute__((release_shared_capability(__VA_ARGS__)))
#else
#define RELOCO_RELEASE_SHARED(...)
#endif

#if RELOCO_HAS_ATTRIBUTE(try_acquire_capability)
#define RELOCO_TRY_ACQUIRE(...) __attribute__((try_acquire_capability(__VA_ARGS__)))
#else
#define RELOCO_TRY_ACQUIRE(...)
#endif

#if RELOCO_HAS_ATTRIBUTE(try_acquire_shared_capability)
#define RELOCO_TRY_ACQUIRE_SHARED(...) __attribute__((try_acquire_shared_capability(__VA_ARGS__)))
#else
#define RELOCO_TRY_ACQUIRE_SHARED(...)
#endif

#if RELOCO_HAS_ATTRIBUTE(assert_capability)
#define RELOCO_ASSERT_CAPABILITY(x) __attribute__((assert_capability(x)))
#else
#define RELOCO_ASSERT_CAPABILITY(x)
#endif

#if RELOCO_HAS_ATTRIBUTE(assert_shared_capability)
#define RELOCO_ASSERT_SHARED_CAPABILITY(x) __attribute__((assert_shared_capability(x)))
#else
#define RELOCO_ASSERT_SHARED_CAPABILITY(x)
#endif

#if RELOCO_HAS_ATTRIBUTE(requires_capability)
#define RELOCO_REQUIRES(...) __attribute__((requires_capability(__VA_ARGS__)))
#else
#define RELOCO_REQUIRES(...)
#endif

#if RELOCO_HAS_ATTRIBUTE(requires_shared_capability)
#define RELOCO_REQUIRES_SHARED(...) __attribute__((requires_shared_capability(__VA_ARGS__)))
#else
#define RELOCO_REQUIRES_SHARED(...)
#endif

#if RELOCO_HAS_ATTRIBUTE(locks_excluded)
#define RELOCO_LOCKS_EXCLUDED(...) __attribute__((locks_excluded(__VA_ARGS__)))
#else
#define RELOCO_LOCKS_EXCLUDED(...)
#endif

#if RELOCO_HAS_ATTRIBUTE(no_thread_safety_analysis)
#define RELOCO_NO_THREAD_SAFETY_ANALYSIS __attribute__((no_thread_safety_analysis))
#else
#define RELOCO_NO_THREAD_SAFETY_ANALYSIS
#endif

/** Marks an enum as a bitmask/flag type for enhanced static analysis. */
#if RELOCO_HAS_ATTRIBUTE(flag_enum)
#define RELOCO_FLAG_ENUM __attribute__((flag_enum))
#else
#define RELOCO_FLAG_ENUM
#endif

/** Restricts enum values for better switch-exhaustiveness checking. */
#if RELOCO_HAS_ATTRIBUTE(enum_extensibility)
#define RELOCO_ENUM_EXTENSIBILITY(val) __attribute__((enum_extensibility(val)))
#else
#define RELOCO_ENUM_EXTENSIBILITY(val)
#endif

/**
 * Informs the compiler that a function returning a pointer is guaranteed
 * never to return nullptr.
 */
#if RELOCO_HAS_ATTRIBUTE(returns_nonnull)
#define RELOCO_RETURNS_NONNULL __attribute__((returns_nonnull))
#else
#define RELOCO_RETURNS_NONNULL
#endif

/**
 * Informs the compiler that the function returns a newly allocated buffer
 * whose size is determined by the specified parameter index(es).
 * @param pos1 1-based index of the size argument.
 * @param pos2 Optional 2-based index of the element size argument.
 */
#if RELOCO_HAS_ATTRIBUTE(alloc_size)
#define RELOCO_ATTR_ALLOC_SIZE(pos1) __attribute__((alloc_size(pos1)))
#define RELOCO_ATTR_ALLOC_SIZE2(pos1, pos2) __attribute__((alloc_size(pos1, pos2)))
#else
#define RELOCO_ATTR_ALLOC_SIZE(pos1)
#define RELOCO_ATTR_ALLOC_SIZE2(pos1, pos2)
#endif

/**
 * Informs the compiler that the returned pointer of an allocation function
 * is aligned to the byte boundary specified by the parameter at the given index.
 * @param pos 1-based index of the alignment argument.
 */
#if RELOCO_HAS_ATTRIBUTE(alloc_align)
#define RELOCO_ATTR_ALLOC_ALIGN(pos) __attribute__((alloc_align(pos)))
#else
#define RELOCO_ATTR_ALLOC_ALIGN(pos)
#endif
