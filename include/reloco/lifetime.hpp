// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file lifetime.hpp @brief Compiler-specific lifetime, access, nullability, and Clang safe-buffers annotations. */

#include "detail/compat.hpp"
#include <memory>
#include <type_traits>
#include <utility>

// ============================================================================
// Lifetime and Bounds Safety Attribute Macros
// ============================================================================

#if RELOCO_HAS_CPP_ATTRIBUTE(clang::lifetimebound)
#define RELOCO_LIFETIMEBOUND [[clang::lifetimebound]]
#elif RELOCO_HAS_ATTRIBUTE(lifetimebound)
#define RELOCO_LIFETIMEBOUND __attribute__((lifetimebound))
#else
#define RELOCO_LIFETIMEBOUND
#endif

#if RELOCO_HAS_CPP_ATTRIBUTE(gsl::Owner)
#define RELOCO_OWNER [[gsl::Owner]]
#else
#define RELOCO_OWNER
#endif

#if RELOCO_HAS_CPP_ATTRIBUTE(gsl::Pointer)
#define RELOCO_POINTER [[gsl::Pointer]]
#else
#define RELOCO_POINTER
#endif

#if RELOCO_HAS_CPP_ATTRIBUTE(clang::unsafe_buffer_usage)
#define RELOCO_UNSAFE_BUFFER_USAGE [[clang::unsafe_buffer_usage]]
#elif RELOCO_HAS_ATTRIBUTE(unsafe_buffer_usage)
#define RELOCO_UNSAFE_BUFFER_USAGE __attribute__((unsafe_buffer_usage))
#else
#define RELOCO_UNSAFE_BUFFER_USAGE
#endif

/** Declares that another parameter or the return value captures a borrow. */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::lifetime_capture_by)
#define RELOCO_LIFETIME_CAPTURE_BY(...) [[clang::lifetime_capture_by(__VA_ARGS__)]]
#elif RELOCO_HAS_ATTRIBUTE(lifetime_capture_by)
#define RELOCO_LIFETIME_CAPTURE_BY(...) __attribute__((lifetime_capture_by(__VA_ARGS__)))
#else
#define RELOCO_LIFETIME_CAPTURE_BY(...)
#endif

/** Declares that a constructor or member function captures a borrow in `this`. */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::lifetime_capture_by_this)
#define RELOCO_LIFETIME_CAPTURE_BY_THIS [[clang::lifetime_capture_by_this]]
#elif RELOCO_HAS_ATTRIBUTE(lifetime_capture_by_this)
#define RELOCO_LIFETIME_CAPTURE_BY_THIS __attribute__((lifetime_capture_by_this))
#else
#define RELOCO_LIFETIME_CAPTURE_BY_THIS RELOCO_LIFETIME_CAPTURE_BY(this)
#endif

/**
 * Declares that a method completely resets the object's state.
 * Suppresses use-after-move warnings when the object is reused after calling this method.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::reinitializes)
#define RELOCO_REINITIALIZES [[clang::reinitializes]]
#elif RELOCO_HAS_ATTRIBUTE(reinitializes)
#define RELOCO_REINITIALIZES __attribute__((reinitializes))
#else
#define RELOCO_REINITIALIZES
#endif

// ============================================================================
// Clang Safe Buffers Pragma Control Blocks
// ============================================================================

/**
 * @def RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
 * @brief Suppresses or opts a block of code out of Clang's -Wunsafe-buffer-usage checks.
 *
 * Essential for implementing custom underlying containers, span bounds checkers,
 * or performance-critical low-level routines.
 */
/**
 * @def RELOCO_END_UNSAFE_BUFFER_USAGE
 * @brief Closes a block opened by RELOCO_BEGIN_UNSAFE_BUFFER_USAGE.
 */
#define RELOCO_PRAGMA(x) _Pragma(#x)

#if defined(__clang__)
#define RELOCO_BEGIN_UNSAFE_BUFFER_USAGE RELOCO_PRAGMA(clang unsafe_buffer_usage begin)
#define RELOCO_END_UNSAFE_BUFFER_USAGE RELOCO_PRAGMA(clang unsafe_buffer_usage end)
#else
#define RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
#define RELOCO_END_UNSAFE_BUFFER_USAGE
#endif

// ============================================================================
// reloco-tidy Borrow Checker Exemption
// ============================================================================

/**
 * @def RELOCO_BORROW_EXEMPT
 * @brief Exempts one function (or every member of a class) from reloco-tidy's
 * `reloco-borrow-check`, for code that deliberately aliases, e.g. swap/exchange primitives,
 * self-referential containers or split-borrow helpers. Place it before the declaration:
 * `RELOCO_BORROW_EXEMPT void swap(T &a, T &b) noexcept;`. Compiles to nothing for GCC/MSVC.
 * Every use should carry a comment saying why the aliasing is sound.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_BORROW_EXEMPT [[clang::annotate("reloco_borrow_exempt")]]
#else
#define RELOCO_BORROW_EXEMPT
#endif

/**
 * @def RELOCO_COPYABLE
 * @brief Marks a class/struct as implicitly copyable, like Rust's `Copy` trait. reloco-tidy's
 * `reloco-implicit-copy` warns whenever a class without this marker is copied implicitly
 * (copy-initialisation, by-value argument, copy assignment) instead of moved. Place it after
 * the class key: `struct RELOCO_COPYABLE point { int x, y; };`. Views (`RELOCO_POINTER`) are
 * always copyable. An explicit copy, `T(x)`, `T y(x)` or `static_cast<T>(x)`, is the
 * equivalent of Rust's `.clone()` and is allowed for any type. Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_COPYABLE [[clang::annotate("reloco_copyable")]]
#else
#define RELOCO_COPYABLE
#endif

/**
 * @def RELOCO_OUT
 * @brief Marks a pointer or non-const reference parameter as an out-parameter: the function
 * must write it (`*x = v;` for `T *x`, `x = v;` for `T &x`, or hand it to a callee that takes a
 * non-const pointer/reference) on every path that returns, and must not read it before
 * the first write. reloco-tidy's `reloco-out-param` enforces this. Place it after the
 * parameter name: `void get(int *x RELOCO_OUT) noexcept;`. Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_OUT [[clang::annotate("reloco_out")]]
#else
#define RELOCO_OUT
#endif

/**
 * @def RELOCO_MUST_CONSUME
 * @brief Marks a class as a linear resource (Rust's "must be consumed" / explicit drop): a local
 * or by-value parameter of this type must be consumed on every path that leaves the function,
 * by `std::move(x)`, `return x;`, taking its address (escape), or a member marked
 * `RELOCO_CONSUMES`. reloco-tidy's `reloco-leak` reports paths that drop it silently. Place it after
 * the class key: `struct RELOCO_MUST_CONSUME txn { RELOCO_CONSUMES void commit() noexcept; };`.
 * Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_MUST_CONSUME [[clang::annotate("reloco_must_consume")]]
#else
#define RELOCO_MUST_CONSUME
#endif

/**
 * @def RELOCO_CONSUMES
 * @brief Marks a member function of a RELOCO_MUST_CONSUME class as consuming its object: after
 * the call the object counts as released for `reloco-leak`. Place it first in the declaration
 * (an attribute after `noexcept` would apply to the function type): `RELOCO_CONSUMES void commit() noexcept;`. Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_CONSUMES [[clang::annotate("reloco_consumes")]]
#else
#define RELOCO_CONSUMES
#endif

/**
 * @def RELOCO_UNGUARDED
 * @brief Marks a data member that lives next to a mutex but is deliberately not protected by
 * it (immutable after construction, only touched by one thread, protected by another
 * mechanism). Without it, `reloco-lock-guard` requires `RELOCO_GUARDED_BY(mutex)` on every mutable
 * member of a class that owns a mutex. Place it after the member name:
 * `int generation_ RELOCO_UNGUARDED;`. Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_UNGUARDED [[clang::annotate("reloco_unguarded")]]
#else
#define RELOCO_UNGUARDED
#endif

/**
 * @def RELOCO_SEND
 * @brief Marks a class as safe to move to another thread (Rust's `Send`), overriding the structural
 * rule of `reloco-send-sync` (a class is Send when all members and bases are Send, owners are Send when
 * their template arguments are, and raw pointers, references and views are not). Place after the class key:
 * `struct RELOCO_SEND channel_end { ... };`. Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_SEND [[clang::annotate("reloco_send")]]
#else
#define RELOCO_SEND
#endif

/**
 * @def RELOCO_NOT_SEND
 * @brief Marks a class as never movable to another thread (thread-affine handles, TLS-backed state), even if
 * its members look Send. Place after the class key. Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_NOT_SEND [[clang::annotate("reloco_not_send")]]
#else
#define RELOCO_NOT_SEND
#endif

/**
 * @def RELOCO_SYNC
 * @brief Marks a class as safe to share by reference between threads (Rust's `Sync`): it is immutable,
 * atomic, or guards all mutation with a mutex. Without it, `reloco-send-sync` accepts types that are atomic, a
 * mutex, all-const, or a mutex owner whose other members are guarded. Place after the class key.
 * Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_SYNC [[clang::annotate("reloco_sync")]]
#else
#define RELOCO_SYNC
#endif

/**
 * @def RELOCO_REQUIRE_SEND
 * @brief Marks a parameter (callable or argument handed to another thread/task, e.g. `spawn`) as requiring
 * a Send argument; `reloco-send-sync` checks every call. Lambdas count as Send only when all captures are by
 * value and Send. Place after the parameter name: `void spawn(F fn RELOCO_REQUIRE_SEND);`. Compiles to nothing
 * for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_REQUIRE_SEND [[clang::annotate("reloco_require_send")]]
#else
#define RELOCO_REQUIRE_SEND
#endif

/**
 * @def RELOCO_REQUIRE_SYNC
 * @brief Marks a reference/pointer parameter whose referent is shared between threads and must be Sync
 * (`void share(const T &v RELOCO_REQUIRE_SYNC);`). Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_REQUIRE_SYNC [[clang::annotate("reloco_require_sync")]]
#else
#define RELOCO_REQUIRE_SYNC
#endif

/**
 * @def RELOCO_NS
 * @brief Poisons a pointer type with an address-space tag, e.g. `RELOCO_NS(user)` for system-call arguments or
 * `RELOCO_NS(guest)` for memory coming from a VM. Such a pointer is only a number in a foreign address space: it
 * may not be dereferenced, subscripted, passed where a normal pointer (or a pointer of another tag) is expected,
 * assigned across tags or cast between tags. Only a function marked `RELOCO_NS_TRANSLATOR` may turn it into a real
 * pointer. Enforced by `reloco-ns-pointer`. Place it after the `*` it qualifies:
 * `long sys_read(int fd, char *RELOCO_NS(user) buf, size_t n);` or in an alias
 * `using user_ptr = char *RELOCO_NS(user);`. Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate_type)
#define RELOCO_NS(tag) [[clang::annotate_type("reloco_ns", #tag)]]
#else
#define RELOCO_NS(tag)
#endif

/**
 * @def RELOCO_STATIC_STR
 * @brief Declares that a `const char *` has static storage duration (a string literal, or storage that lives
 * for the whole program), so it can never dangle. `reloco-no-raw-pointer` accepts such a pointer; any other
 * `const char *` must be a `reloco::string_view`. `reloco-static-string` verifies that every value returned or
 * passed through a RELOCO_STATIC_STR declaration really is static. Place it after the `*`:
 * `const char *RELOCO_STATIC_STR name() const noexcept;`. Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate_type)
#define RELOCO_STATIC_STR [[clang::annotate_type("reloco_static_str")]]
#else
#define RELOCO_STATIC_STR
#endif

/**
 * @def RELOCO_NS_TRANSLATOR
 * @brief Marks a function allowed to convert `RELOCO_NS` pointers: its body may dereference and cast them, and it
 * hands back a real pointer or copies the data out. Place it first in the declaration:
 * `RELOCO_NS_TRANSLATOR const char *translate(const char *RELOCO_NS(user) p, size_t n) noexcept;`.
 * Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_NS_TRANSLATOR [[clang::annotate("reloco_ns_translator")]]
#else
#define RELOCO_NS_TRANSLATOR
#endif

/**
 * @def RELOCO_EARLY_STATIC
 * @brief Marks a class (or a single static-storage variable) that must work during bare-metal startup, before
 * the C++ runtime has run constructors or registered destructors. `reloco-early-static` requires the class to be a
 * literal type with only constexpr constructors and a trivial destructor, and every static/global object of it to
 * be constant-initialised (`constexpr` or `constinit`), so the compiler emits it into `.data`/`.rodata` and no
 * dynamic initialiser or `atexit` registration is generated. Place it after the class key:
 * `struct RELOCO_EARLY_STATIC uart_regs { constexpr uart_regs() noexcept = default; };`.
 * Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_EARLY_STATIC [[clang::annotate("reloco_early_static")]]
#else
#define RELOCO_EARLY_STATIC
#endif

/**
 * @def RELOCO_CHECKED
 * @brief Declares that a class's `operator[]`, unary `operator*` and `operator->` perform bounds
 * or null checking (e.g. return a `result`, assert, or clamp). reloco-tidy's `reloco-checked-operator`
 * treats every *use* of those operators on a type that is not RELOCO_CHECKED as a violation, and
 * flags definitions that lack the marker. Put it before the operator declaration, or before the
 * class to cover all three: `RELOCO_CHECKED T &operator[](size_t i) & noexcept;`.
 * Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_CHECKED [[clang::annotate("reloco_checked")]]
#else
#define RELOCO_CHECKED
#endif

/**
 * @def RELOCO_SAFE_MATH
 * @brief Opt-in marker on a function or class: reloco-tidy's `reloco-safe-math` then rejects raw
 * integer `+ - * / % << >>`, `++`, `--` and unary `-` on non-constant operands inside it, requiring
 * the checked/saturating/wrapping helpers (`reloco/checked.hpp`, `saturating.hpp`, `wrapping.hpp`).
 * Code without the marker is never checked. Compiles to nothing for GCC/MSVC.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)
#define RELOCO_SAFE_MATH [[clang::annotate("reloco_safe_math")]]
#else
#define RELOCO_SAFE_MATH
#endif

// ============================================================================
// reloco-tidy Safety Check Regions
// ============================================================================

/**
 * @def RELOCO_BEGIN_SAFE_CODE
 * @brief Opts a block of code in to the reloco-tidy safety checks.
 *
 * Projects that merely use reloco can run reloco-tidy with `SafeCodeMode: opt-in`
 * (see docs/safety-checker.md): only code between RELOCO_BEGIN_SAFE_CODE and
 * RELOCO_END_SAFE_CODE is checked, so third-party sources are never reported.
 * In the default `all` mode (reloco, microfmt, structo) the block is checked like
 * everything else.
 *
 * The macros expand to `#pragma region` / `#pragma endregion`, which every
 * supported compiler (GCC, Clang, MSVC) accepts silently, including under
 * -Wall -Wextra -Wpedantic -Wunknown-pragmas; they have no effect on compilation.
 * Regions do not nest: the innermost most-recent marker wins.
 */
/**
 * @def RELOCO_END_SAFE_CODE
 * @brief Closes a block opened by RELOCO_BEGIN_SAFE_CODE.
 */
/**
 * @def RELOCO_BEGIN_UNSAFE_CODE
 * @brief Opts a block out of the reloco-tidy safety checks (for deliberate low-level code
 * or vendored code inside an otherwise checked file). Prefer the unsafe_* tier and
 * RELOCO_UNSAFE_BUFFER_USAGE where possible; every use should carry a reason comment.
 */
/**
 * @def RELOCO_END_UNSAFE_CODE
 * @brief Closes a block opened by RELOCO_BEGIN_UNSAFE_CODE.
 */
#define RELOCO_BEGIN_SAFE_CODE RELOCO_PRAGMA(region reloco_safe_begin)
#define RELOCO_END_SAFE_CODE RELOCO_PRAGMA(endregion reloco_safe_end)
#define RELOCO_BEGIN_UNSAFE_CODE RELOCO_PRAGMA(region reloco_unsafe_begin)
#define RELOCO_END_UNSAFE_CODE RELOCO_PRAGMA(endregion reloco_unsafe_end)

// ============================================================================
// GCC False-Positive Bounds-Checking Pragma Control Blocks
// ============================================================================

/**
 * @def RELOCO_BEGIN_SUPPRESS_GCC_ADDRESS_FALSE_POSITIVE
 * @brief Suppresses GCC 11/12's `-Waddress` ("the address of ... will never
 * be NULL") on `if (ops->fn)` checks inside type-erased resolvers.
 *
 * Reason: the same generic code is instantiated for every `T`, and the
 * `type_operations` slot is a captureless lambda converted to a function
 * pointer for some `T` and `nullptr` for others (e.g. trivially
 * destructible types). The null check is therefore required for the
 * `nullptr` instantiations; GCC only sees the non-null instantiation and
 * flags it. The check is correct and cheap, so it is kept rather than
 * restructured. No-op on Clang/MSVC.
 */
/**
 * @def RELOCO_END_SUPPRESS_GCC_ADDRESS_FALSE_POSITIVE
 * @brief Closes a block opened by RELOCO_BEGIN_SUPPRESS_GCC_ADDRESS_FALSE_POSITIVE.
 */

/**
 * @def RELOCO_BEGIN_SUPPRESS_GCC_BOUNDS_FALSE_POSITIVE
 * @brief Suppresses GCC's `-Warray-bounds`/`-Wstringop-overread`/
 * `-Wstringop-overflow`/`-Waggressive-loop-optimizations` false positives
 * on generic, runtime-bounds-checked code: GCC's static range analysis can
 * misjudge a branch guarded by an early-return runtime check (e.g. `if
 * (index >= size) return ...;`) as reachable with an out-of-range
 * index/trip-count, and warns on the (never actually taken) pointer
 * arithmetic/`memmove` inside it. No-op on Clang/MSVC, which do not
 * exhibit this false positive.
 */
/**
 * @def RELOCO_END_SUPPRESS_GCC_BOUNDS_FALSE_POSITIVE
 * @brief Closes a block opened by RELOCO_BEGIN_SUPPRESS_GCC_BOUNDS_FALSE_POSITIVE.
 */
#if defined(__GNUC__) && !defined(__clang__)
#define RELOCO_BEGIN_SUPPRESS_GCC_BOUNDS_FALSE_POSITIVE                                                                \
  RELOCO_PRAGMA(GCC diagnostic push)                                                                                   \
  RELOCO_PRAGMA(GCC diagnostic ignored "-Warray-bounds")                                                               \
  RELOCO_PRAGMA(GCC diagnostic ignored "-Wstringop-overread")                                                          \
  RELOCO_PRAGMA(GCC diagnostic ignored "-Wstringop-overflow")                                                          \
  RELOCO_PRAGMA(GCC diagnostic ignored "-Waggressive-loop-optimizations")
#define RELOCO_END_SUPPRESS_GCC_BOUNDS_FALSE_POSITIVE RELOCO_PRAGMA(GCC diagnostic pop)
#define RELOCO_BEGIN_SUPPRESS_GCC_ADDRESS_FALSE_POSITIVE                                                               \
  RELOCO_PRAGMA(GCC diagnostic push)                                                                                   \
  RELOCO_PRAGMA(GCC diagnostic ignored "-Waddress")
#define RELOCO_END_SUPPRESS_GCC_ADDRESS_FALSE_POSITIVE RELOCO_PRAGMA(GCC diagnostic pop)
#else
#define RELOCO_BEGIN_SUPPRESS_GCC_ADDRESS_FALSE_POSITIVE
#define RELOCO_END_SUPPRESS_GCC_ADDRESS_FALSE_POSITIVE
#define RELOCO_BEGIN_SUPPRESS_GCC_BOUNDS_FALSE_POSITIVE
#define RELOCO_END_SUPPRESS_GCC_BOUNDS_FALSE_POSITIVE
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define RELOCO_BEGIN_SUPPRESS_GCC_UNINITIALIZED_FALSE_POSITIVE                                                         \
  RELOCO_PRAGMA(GCC diagnostic push)                                                                                   \
  RELOCO_PRAGMA(GCC diagnostic ignored "-Wmaybe-uninitialized")                                                        \
  RELOCO_PRAGMA(GCC diagnostic ignored "-Wuninitialized")
#define RELOCO_END_SUPPRESS_GCC_UNINITIALIZED_FALSE_POSITIVE RELOCO_PRAGMA(GCC diagnostic pop)
#else
#define RELOCO_BEGIN_SUPPRESS_GCC_UNINITIALIZED_FALSE_POSITIVE
#define RELOCO_END_SUPPRESS_GCC_UNINITIALIZED_FALSE_POSITIVE
#endif

/**
 * @def RELOCO_BEGIN_SUPPRESS_PEDANTIC_INT128
 * @brief Suppresses the `-Wpedantic` "ISO C++ does not support '__int128'"
 * warning around deliberate, feature-tested (`__SIZEOF_INT128__`) use of the
 * `__int128`/`unsigned __int128` GCC/Clang extension. No-op on MSVC, which
 * doesn't support the extension (and thus never reaches this code) in the
 * first place.
 */
/**
 * @def RELOCO_END_SUPPRESS_PEDANTIC_INT128
 * @brief Closes a block opened by RELOCO_BEGIN_SUPPRESS_PEDANTIC_INT128.
 */
#if defined(__GNUC__) || defined(__clang__)
#define RELOCO_BEGIN_SUPPRESS_PEDANTIC_INT128                                                                          \
  RELOCO_PRAGMA(GCC diagnostic push)                                                                                   \
  RELOCO_PRAGMA(GCC diagnostic ignored "-Wpedantic")
#define RELOCO_END_SUPPRESS_PEDANTIC_INT128 RELOCO_PRAGMA(GCC diagnostic pop)
#else
#define RELOCO_BEGIN_SUPPRESS_PEDANTIC_INT128
#define RELOCO_END_SUPPRESS_PEDANTIC_INT128
#endif

// ============================================================================
// Nullability Attributes (GCC / Clang Static Analyzer)
// ============================================================================

#if RELOCO_HAS_ATTRIBUTE(nonnull)
#define RELOCO_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#define RELOCO_NONNULL(...)
#endif

// ============================================================================
// Function Purity & Constness (Optimizer & Side-Effect Safety)
// ============================================================================

#if RELOCO_HAS_ATTRIBUTE(pure)
#define RELOCO_ATTR_PURE __attribute__((pure))
#else
#define RELOCO_ATTR_PURE
#endif

#if RELOCO_HAS_ATTRIBUTE(const)
#define RELOCO_ATTR_CONST __attribute__((const))
#else
#define RELOCO_ATTR_CONST
#endif

// ============================================================================
// Compiler Buffer Access Attributes (GCC/Clang -Wstringop-overflow)
// ============================================================================

#if RELOCO_HAS_ATTRIBUTE(access)
#define RELOCO_ATTR_ACCESS(mode, ptr_idx) __attribute__((access(mode, ptr_idx)))
#define RELOCO_ATTR_ACCESS_SIZE(mode, ptr_idx, size_idx) __attribute__((access(mode, ptr_idx, size_idx)))
#else
#define RELOCO_ATTR_ACCESS(mode, ptr_idx)
#define RELOCO_ATTR_ACCESS_SIZE(mode, ptr_idx, size_idx)
#endif

// ============================================================================
// Modern Memory & String Safety
// ============================================================================

/**
 * Marks char arrays as explicitly NOT null-terminated. Prevents compiler warnings
 * when using string functions (like strncpy) on raw GDB payload buffers.
 */
#if RELOCO_HAS_ATTRIBUTE(nonstring)
#define RELOCO_NONSTRING __attribute__((nonstring))
#else
#define RELOCO_NONSTRING
#endif

/**
 * Pairs a GCC heap-like allocator with its specific deallocator for
 * `-Wmismatched-dealloc`.
 */
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 11 && RELOCO_HAS_ATTRIBUTE(malloc)
#define RELOCO_MALLOC_PAIR(deallocator) __attribute__((malloc(deallocator)))
#else
#define RELOCO_MALLOC_PAIR(deallocator)
#endif

/**
 * Informs the compiler that a returned pointer (e.g., from the scratch_allocator)
 * is guaranteed to have a specific byte alignment, allowing aggressive SIMD/vectorization optimizations.
 */
#if RELOCO_HAS_ATTRIBUTE(assume_aligned)
#define RELOCO_ASSUME_ALIGNED(alignment) __attribute__((assume_aligned(alignment)))
#else
#define RELOCO_ASSUME_ALIGNED(alignment)
#endif

// ============================================================================
// Compile-Time Diagnostics (Clang Specific)
// ============================================================================

/**
 * Triggers a custom compile-time error/warning if a condition is met.
 * Perfect for preventing invalid GDB packet sizes or unaligned scratch buffers at compile time.
 * Usage: RELOCO_DIAGNOSE_IF(size > 1024, "Packet size too large", "error")
 */
#if defined(__clang__) && RELOCO_HAS_ATTRIBUTE(diagnose_if)
#define RELOCO_DIAGNOSE_IF(cond, msg, type) __attribute__((diagnose_if(cond, msg, type)))
#else
#define RELOCO_DIAGNOSE_IF(cond, msg, type)
#endif

// ============================================================================
// Control Flow & State Machine Optimizations
// ============================================================================

/**
 * Requires a guaranteed tail call when placed on a return statement.
 */
#if RELOCO_HAS_CPP_ATTRIBUTE(clang::musttail)
#define RELOCO_MUSTTAIL [[clang::musttail]]
#else
#define RELOCO_MUSTTAIL
#endif

/** Marks error-handling or fallback paths (like NAK generation) to be moved out of the hot instruction cache. */
#if RELOCO_HAS_ATTRIBUTE(cold)
#define RELOCO_COLD __attribute__((cold))
#else
#define RELOCO_COLD
#endif

#if RELOCO_HAS_ATTRIBUTE(hot)
#define RELOCO_HOT __attribute__((hot))
#else
#define RELOCO_HOT
#endif

// ============================================================================
// Consumed State / Typestate Annotations (Clang Static Analyzer)
// ============================================================================
// These let Clang's -Wconsumed analysis track monotonic object state.

/**
 * Marks a class as having a trackable lifetime state (e.g., "unconsumed", "consumed").
 * @example class RELOCO_CONSUMABLE(unconsumed) packet_writer { ...
 */
#if defined(__clang__) && RELOCO_HAS_ATTRIBUTE(consumable)
#define RELOCO_CONSUMABLE(state) __attribute__((consumable(state)))
#else
#define RELOCO_CONSUMABLE(state)
#endif

/**
 * Restricts a method so it can only be called in a specific state.
 * Clang requires the state names as quoted string literals here (unlike
 * @c RELOCO_CONSUMABLE, @c RELOCO_SET_TYPESTATE, and
 * @c RELOCO_RETURN_TYPESTATE, which take bare identifiers).
 * @example void write() RELOCO_CALLABLE_WHEN("unconsumed");
 */
#if defined(__clang__) && RELOCO_HAS_ATTRIBUTE(callable_when)
#define RELOCO_CALLABLE_WHEN(...) __attribute__((callable_when(__VA_ARGS__)))
#else
#define RELOCO_CALLABLE_WHEN(...)
#endif

/**
 * Transitions the lifetime state of the object upon calling this method.
 * @example string_view finalize() RELOCO_SET_TYPESTATE(consumed);
 */
#if defined(__clang__) && RELOCO_HAS_ATTRIBUTE(set_typestate)
#define RELOCO_SET_TYPESTATE(state) __attribute__((set_typestate(state)))
#else
#define RELOCO_SET_TYPESTATE(state)
#endif

/**
 * Indicates what state an object is in when returned from a function.
 */
#if defined(__clang__) && RELOCO_HAS_ATTRIBUTE(return_typestate)
#define RELOCO_RETURN_TYPESTATE(state) __attribute__((return_typestate(state)))
#else
#define RELOCO_RETURN_TYPESTATE(state)
#endif

/**
 * Informs the analyzer that if this method returns true, the object is in the given state.
 * @example bool has_value() const RELOCO_TEST_TYPESTATE(unconsumed);
 */
#if defined(__clang__) && RELOCO_HAS_ATTRIBUTE(test_typestate)
#define RELOCO_TEST_TYPESTATE(state) __attribute__((test_typestate(state)))
#else
#define RELOCO_TEST_TYPESTATE(state)
#endif

// ============================================================================
// Unsafe Pointer Utilities & Unwrapping Boundaries
// ============================================================================

namespace reloco::unsafe {

template <typename To, typename From> [[nodiscard]] constexpr To *ptr_cast(From *ptr) noexcept {
  return reinterpret_cast<To *>(ptr);
}

template <typename To, typename From> [[nodiscard]] constexpr const To *ptr_cast(const From *ptr) noexcept {
  return reinterpret_cast<const To *>(ptr);
}

template <typename T> [[nodiscard]] constexpr T *unchecked_address(T &obj) noexcept {
  return const_cast<T *>(std::addressof(obj));
}

} // namespace reloco::unsafe