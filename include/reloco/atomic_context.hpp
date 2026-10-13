// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/**
 * @file atomic_context.hpp
 * @brief Annotations for reloco-tidy's `reloco-atomic-context` check: "might sleep" functions, "atomic only"
 * functions (per-CPU data), and the scopes that establish atomic context (interrupts or preemption disabled, a
 * spinlock held).
 *
 * All macros compile to nothing outside Clang. They only feed the static checker; they change no behaviour.
 * See docs/atomic-context.md for the full rule set and examples.
 */

#include "detail/compat.hpp"

#if RELOCO_HAS_CPP_ATTRIBUTE(clang::annotate)

/**
 * @def RELOCO_MIGHT_SLEEP
 * @brief The function (or, on a class, every member function and constructor of the class) may block or
 * reschedule: mutexes, condition variables, allocation that may wait, waiting for I/O. Calling it inside an atomic
 * scope is a violation. A function that only calls such functions is inferred to sleep too, so it needs no marker.
 */
#define RELOCO_MIGHT_SLEEP [[clang::annotate("reloco_might_sleep")]]

/**
 * @def RELOCO_ATOMIC_ONLY
 * @brief The function (or class) may only be called in atomic context, for example per-CPU data accessors that are
 * only meaningful while the thread cannot migrate. The body is itself analysed as atomic, so it must not sleep.
 */
#define RELOCO_ATOMIC_ONLY [[clang::annotate("reloco_atomic_only")]]

/**
 * @def RELOCO_ATOMIC_ENTRY
 * @brief The function is entered by hardware or by foreign code in atomic context (interrupt handler, IPI
 * handler, spinlock-held callback). Its body is analysed as atomic, but callers need not be atomic.
 */
#define RELOCO_ATOMIC_ENTRY [[clang::annotate("reloco_atomic_entry")]]

/**
 * @def RELOCO_ATOMIC_SCOPE
 * @brief On a class: a local object of this type holds an atomic scope from its construction to the end of its
 * enclosing block (RAII guards such as an IRQ guard, a preemption guard or a spinlock guard). Early `return`,
 * `break` and `continue` end the scope exactly where the destructor runs.
 */
#define RELOCO_ATOMIC_SCOPE [[clang::annotate("reloco_atomic_scope")]]

/**
 * @def RELOCO_ATOMIC_LOCK
 * @brief On a class: a lock whose holder runs in atomic context (a spinlock). Together with RELOCO_ATOMIC_GUARD it
 * makes `unique_lock<spin>`-style guards atomic scopes.
 */
#define RELOCO_ATOMIC_LOCK [[clang::annotate("reloco_atomic_lock")]]

/**
 * @def RELOCO_ATOMIC_GUARD
 * @brief On a class template (`lock_guard`, `unique_lock`): a specialisation over a RELOCO_ATOMIC_LOCK type is an
 * atomic scope for the lifetime of the guard object; over any other lock type it is not.
 */
#define RELOCO_ATOMIC_GUARD [[clang::annotate("reloco_atomic_guard")]]

/**
 * @def RELOCO_ENTER_ATOMIC
 * @brief On a function: calling it starts an atomic scope that lasts until a matching RELOCO_LEAVE_ATOMIC call
 * (`disable_preemption()`, `spin_lock()`). Prefer an RAII class with RELOCO_ATOMIC_SCOPE.
 */
#define RELOCO_ENTER_ATOMIC [[clang::annotate("reloco_enter_atomic")]]

/** @def RELOCO_LEAVE_ATOMIC @brief On a function: calling it ends one scope started by RELOCO_ENTER_ATOMIC. */
#define RELOCO_LEAVE_ATOMIC [[clang::annotate("reloco_leave_atomic")]]

/**
 * @def RELOCO_ATOMIC_CALLBACK
 * @brief On a parameter (after its name): a lambda passed for it runs in atomic context, e.g. the body handed to
 * `with_irq_disabled(F fn RELOCO_ATOMIC_CALLBACK)`. The lambda body is analysed with one extra atomic scope.
 */
#define RELOCO_ATOMIC_CALLBACK [[clang::annotate("reloco_atomic_callback")]]

/**
 * @def RELOCO_ATOMIC_TOKEN
 * @brief On a class: a proof-of-atomic-context token (`critical_section_token`, `preemption_disabled_token`). A
 * function that takes one by value or reference is analysed as atomic, and passing it on is accepted anywhere.
 */
#define RELOCO_ATOMIC_TOKEN [[clang::annotate("reloco_atomic_token")]]

/**
 * @def RELOCO_ATOMIC_EXEMPT
 * @brief Exempts a function, or every member of a class, from `reloco-atomic-context`: its body is not checked and
 * it is never inferred to sleep. Put the reason in a comment, for example "sleeps only when the flags allow it".
 */
#define RELOCO_ATOMIC_EXEMPT [[clang::annotate("reloco_atomic_exempt")]]

/**
 * @brief Marks a class or function as part of a lock implementation (or a wrapper that hands lock ownership to a
 * guard). `reloco-lock-balance` does not require its bodies to release what they acquire, and `reloco-lock-alias`
 * allows pointers to locks there. Only use it where holding a lock past the function's end is the purpose.
 */
#define RELOCO_LOCK_IMPL [[clang::annotate("reloco_lock_impl")]]

/**
 * @brief Lock API expressed as free functions (`mtx_lock(&m)`), for legacy code that cannot be rewritten onto
 * member-function locks. @p n is the zero-based index of the parameter that names the lock (a pointer, or a
 * reference). `reloco-lock-balance` then pairs the calls exactly like `m.lock()` / `m.unlock()`:
 * `RELOCO_ACQUIRES` and `RELOCO_ACQUIRES_SHARED` take the lock, `RELOCO_RELEASES` releases it, and the
 * `RELOCO_TRY_ACQUIRES` variants hold it only when the function returns true (non-zero).
 * Example: `RELOCO_ACQUIRES(0) void mtx_lock(struct mtx *m);`
 */
#define RELOCO_ACQUIRES(n) [[clang::annotate("reloco_acquires:" #n)]]
#define RELOCO_ACQUIRES_SHARED(n) [[clang::annotate("reloco_acquires_shared:" #n)]]
#define RELOCO_RELEASES(n) [[clang::annotate("reloco_releases:" #n)]]
#define RELOCO_TRY_ACQUIRES(n) [[clang::annotate("reloco_try_acquires:" #n)]]
#define RELOCO_TRY_ACQUIRES_SHARED(n) [[clang::annotate("reloco_try_acquires_shared:" #n)]]

/**
 * @brief Marks a lock type from legacy code as one whose pointers are legitimate (C APIs pass `struct mtx *`
 * everywhere). `reloco-lock-alias` ignores such types; `reloco-lock-balance` still follows `&m` and pointer
 * parameters inside a single function. Apply it without editing the legacy header, see docs/lock-annotations.md.
 */
#define RELOCO_LEGACY_LOCK [[clang::annotate("reloco_legacy_lock")]]

#else

#define RELOCO_MIGHT_SLEEP
#define RELOCO_ATOMIC_ONLY
#define RELOCO_ATOMIC_ENTRY
#define RELOCO_ATOMIC_SCOPE
#define RELOCO_ATOMIC_LOCK
#define RELOCO_ATOMIC_GUARD
#define RELOCO_ENTER_ATOMIC
#define RELOCO_LEAVE_ATOMIC
#define RELOCO_ATOMIC_CALLBACK
#define RELOCO_ATOMIC_TOKEN
#define RELOCO_ATOMIC_EXEMPT
#define RELOCO_LOCK_IMPL
#define RELOCO_ACQUIRES(n)
#define RELOCO_ACQUIRES_SHARED(n)
#define RELOCO_RELEASES(n)
#define RELOCO_TRY_ACQUIRES(n)
#define RELOCO_TRY_ACQUIRES_SHARED(n)
#define RELOCO_LEGACY_LOCK

#endif
