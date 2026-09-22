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
