// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file fault_injection.hpp
 * @brief Header-based, C++-only fault injection framework for
 * deterministically reproducing concurrency races and other "impossible
 * timing" bugs in single-threaded tests.
 *
 * @details
 * A `RELOCO_FAULT_POINT(Tag)`/`RELOCO_FAULT_POINT_ARGS(Tag, ...)` call
 * marks one location in production code as an injectable fault point,
 * identified by a caller-defined tag type @p Tag (an ordinary, otherwise
 * unused type, e.g. `struct my_race_point {};`, exactly like the `Tag`
 * parameter `tls_provider<T, Tag>`/`pool_allocator_tag<Lock>` already use
 * elsewhere in reloco). By default -- whenever the consumer has not
 * defined `RELOCO_ENABLE_FAULT_INJECTION` before including this header --
 * every fault point macro expands to nothing: the whole mechanism compiles
 * out completely, at zero cost, in a normal build.
 *
 * When `RELOCO_ENABLE_FAULT_INJECTION` *is* defined, a test arms a fault
 * point by constructing a `reloco::fault_injector<Tag, Args...>` -- a
 * caller-owned, non-copyable, non-movable scoped control block, typically
 * a plain stack local -- carrying a hook closure. While that
 * `fault_injector` is alive, every `RELOCO_FAULT_POINT_ARGS(Tag, args...)`
 * reached *on the same thread* invokes the hook with references to
 * `args...`, letting a test mutate the exact local state the code under
 * test is about to act on -- deterministically simulating what a
 * concurrent CPU/ISR could have changed out from under it, without
 * needing an actual second thread or any real race to reliably reproduce:
 *
 * @code
 * // Production code:
 * struct commit_race_point {};
 *
 * void producer_commit(std::uint64_t &write_idx, std::uint64_t count) {
 *   std::uint64_t w = write_idx;
 *   RELOCO_FAULT_POINT_ARGS(commit_race_point, w); // hook may mutate `w` right here
 *   write_idx = w + count;
 * }
 *
 * // Test code:
 * TEST(Commit, SurvivesConcurrentIndexMutation) {
 *   auto hook = [](std::uint64_t &w) { w = 0xDEADBEEF; };
 *   reloco::fault_injector<commit_race_point, std::uint64_t> fi(hook);
 *   // ... call producer_commit() and assert on the resulting (mis)behavior ...
 * }
 * @endcode
 *
 * @par Storage: one TLS slot for the whole program, not one per `Tag`
 * The framework itself never allocates, and by default uses a *single*
 * thread-local, pointer-sized slot for its own bookkeeping -- shared by
 * every `Tag` a program ever defines, backed by `tls_provider<void *,
 * detail::fault_root_tag>` (always resolving to that provider's
 * zero-allocation raw-pointer specialization, on every `RELOCO_TLS_MODEL`;
 * see `tls_provider.hpp`). This is deliberate: some backends have a
 * severely limited number of distinct TLS slots (e.g.
 * `RELOCO_TLS_MODEL_PTHREAD`'s `pthread_key_create()` is only guaranteed
 * `PTHREAD_KEYS_MAX` keys process-wide -- as few as 128 on some libcs --
 * and a small fixed-size per-task slot table is typical of a custom
 * `RELOCO_TLS_MODEL_OS` port on an RTOS/kernel), so a design that spent
 * one TLS slot per `Tag` would risk exhausting them in any codebase with
 * more than a handful of fault points. Instead, that one slot holds the
 * head of an intrusive, per-thread singly linked stack of *every*
 * currently active `fault_injector`, across every `Tag` -- and it never
 * owns any of them. Every `fault_injector` is created, owned, and
 * destroyed entirely by the caller (a stack local in the common case);
 * its constructor and destructor just link/unlink it onto that one shared
 * stack (via its own `prev` member, inherited from `detail::fault_node`),
 * so nested `fault_injector`s -- for the same `Tag` or different ones --
 * compose correctly: the innermost one active for a given `Tag` is found
 * first when the stack is walked, and destroying any `fault_injector`
 * restores the stack to exactly what it was before that instance was
 * constructed, in strict LIFO/RAII order -- exactly like nested
 * `scope_guard`s. Because all `fault_injector`s on a thread now share one
 * stack, they must *all* be destroyed in strict reverse-construction
 * order (not just same-`Tag` ones) -- automatic for ordinary nested
 * scopes, and asserted in debug builds.
 *
 * Finding the right `fault_injector` for a given `Tag`/`Args...` means
 * walking that shared stack looking for the first (innermost) entry whose
 * signature matches -- `O(number of currently active fault_injectors on
 * this thread)`, rather than `O(1)`. In practice that count is a handful
 * at most (how many fault points a single test exercises at once), so
 * this trade is strongly worth making to keep TLS usage constant
 * regardless of how many fault points a codebase defines.
 *
 * @par Opting back into one TLS slot per `Tag`
 * On a backend where TLS slots are effectively unlimited --
 * `RELOCO_TLS_MODEL_THREAD_LOCAL` (an ordinary `thread_local` variable)
 * and `RELOCO_TLS_MODEL_SINGLE` (a single global static, meaningful only
 * in a single-threaded build) -- this header defines
 * `RELOCO_FAULT_INJECTION_UNLIMITED_TLS` itself, automatically, unless
 * the consumer already defined it (to `0` to force the shared-stack
 * design even there, or to `1` to force the per-`Tag` design under
 * `RELOCO_TLS_MODEL_PTHREAD`/`RELOCO_TLS_MODEL_OS` despite the slot-count
 * risk that describes) before including this header. When in effect,
 * every `Tag` gets back its own, private `tls_provider<void *, Tag>`
 * slot holding a private per-`Tag` stack, restoring `O(1)` lookup at the
 * cost of one TLS slot per distinct `Tag` the program defines -- free on
 * `THREAD_LOCAL`/`SINGLE`, since each such slot is just another
 * `thread_local`/`static` variable rather than a scarce OS-provided key.
 * The public API (`fault_injector`, `fault_armed`, the two macros) and
 * all caller-visible behavior -- caller ownership, no allocation,
 * stackable, strict LIFO, thread-local scope -- are identical either way;
 * only the TLS-slot budget vs. lookup complexity trade-off changes, and
 * only same-`Tag` nesting order matters when this macro is in effect
 * (nesting across different `Tag`s no longer shares any state, so their
 * relative construction/destruction order is unconstrained).
 *
 * @par Thread-locality is deliberate
 * Arming is thread-local by construction: a `fault_injector` armed on one
 * thread has no effect on `RELOCO_FAULT_POINT`/`RELOCO_FAULT_POINT_ARGS`
 * reached on a different thread. To inject into a specific worker thread
 * (e.g. one spawned via `reloco::spawn`/`thread_scope::spawn`), arm the
 * `fault_injector` from inside that thread's own closure, before it
 * reaches the fault point -- this is what makes the mechanism
 * deterministic and repeatable instead of racing against real concurrency.
 *
 * @par The `Tag` <-> `Args...` contract, and how it is checked
 * By default, every `fault_injector<Tag, Args...>` records its own
 * signature -- both `Tag` and the exact `Args...` list -- as a
 * `reloco::type_id` (see `type_id.hpp`), computed without RTTI and
 * without any allocation. A fault point looks for the first stack entry
 * whose recorded signature matches its own `Tag`/`Args...`, so mismatched
 * `Args...` for the same `Tag` are never confused with each other at
 * runtime (they simply don't match, so the fault point behaves as
 * unarmed) -- unlike a design that trusted the caller never to mix them.
 * When `RELOCO_FAULT_INJECTION_UNLIMITED_TLS` is in effect (see the
 * file-level docs' "Opting back into one TLS slot per `Tag`" section --
 * on by default under `RELOCO_TLS_MODEL_THREAD_LOCAL`/`_SINGLE`), each
 * `Tag` has its own private TLS slot instead, so this check is
 * unnecessary and not performed -- a fault point unconditionally trusts
 * the `Args...` it was written with match every `fault_injector<Tag,
 * ...>` ever constructed for that `Tag`. Either way, pick a distinct,
 * single-purpose `Tag` type per fault point (matching every other reloco
 * `Tag` parameter's convention, e.g. `tls_provider<T, Tag>`), and exactly
 * one `Args...` signature per `Tag`, and this is never in practice
 * ambiguous.
 *
 * @par Convenience macros for the common case
 * `RELOCO_FAULT_TAG(name)` declares a fault-point `Tag` type in one line
 * (`struct name {};`), and `RELOCO_FAULT_INJECTOR(var, Tag, ...)` declares
 * both the hook local and the `fault_injector` local that arms `Tag` with
 * it, deducing `Args...` from the hook's own call signature instead of
 * spelling them out a second time -- and, since the macro always
 * declares the hook as a named local immediately before the
 * `fault_injector` that borrows it, it structurally cannot fall into the
 * dangling-inline-lambda pitfall described above:
 *
 * @code
 * RELOCO_FAULT_TAG(commit_race_point);
 * // ...
 * TEST(Commit, SurvivesConcurrentIndexMutation) {
 *   RELOCO_FAULT_INJECTOR(fi, commit_race_point, [](std::uint64_t &w) { w = 0xDEADBEEF; });
 *   // ... call producer_commit() and assert on the resulting (mis)behavior ...
 * }
 * @endcode
 *
 * @see [Fault injection](../docs/fault-injection.md) for the full guide,
 * design rationale, and worked examples.
 */

#pragma once

#include "function_ref.hpp"
#include "lifetime.hpp"

#if defined(RELOCO_ENABLE_FAULT_INJECTION)
#include "detail/assert.hpp"
#include "tls_provider.hpp"

// Auto-select the per-`Tag` (unlimited-slot-budget backends) design
// unless the consumer already picked one explicitly -- see the
// file-level docs' "Opting back into one TLS slot per `Tag`" section.
#if !defined(RELOCO_FAULT_INJECTION_UNLIMITED_TLS)
#if (RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_THREAD_LOCAL) || (RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_SINGLE)
#define RELOCO_FAULT_INJECTION_UNLIMITED_TLS 1
#else
#define RELOCO_FAULT_INJECTION_UNLIMITED_TLS 0
#endif
#endif

#if !RELOCO_FAULT_INJECTION_UNLIMITED_TLS
#include "type_id.hpp"
#endif
#endif

namespace reloco {

#if defined(RELOCO_ENABLE_FAULT_INJECTION)

namespace detail {

/** @brief `tls_provider<T, Tag>::get()` returns `result<T>` by value on
 * backends with no addressable per-thread storage to reference (e.g.
 * `RELOCO_TLS_MODEL_PTHREAD`'s raw-pointer specialization, selected here
 * since `T = void *`), and `result<std::reference_wrapper<T>>` on every
 * other backend (see `tls_provider.hpp`'s own file-level docs). This
 * unwraps either shape down to the plain `void *` value, so the code
 * below stays portable across every `RELOCO_TLS_MODEL`. */
template <typename R> [[nodiscard]] inline void *tls_slot_value(const R &value) noexcept {
  if constexpr (std::is_same_v<R, std::reference_wrapper<void *>>) {
    return value.get();
  } else {
    return value;
  }
}

#if RELOCO_FAULT_INJECTION_UNLIMITED_TLS

/** @brief `Tag`'s own, private thread-local slot: holds the head of this
 * thread's intrusive stack of currently active `fault_injector<Tag,
 * ...>`s (type-erased as `void *`, `nullptr` if none). One such slot per
 * distinct `Tag` a program defines -- see the file-level docs' "Opting
 * back into one TLS slot per `Tag`" section. */
template <typename Tag> using fault_slot = tls_provider<void *, Tag>;

#else

/** @brief Tag identifying the single, program-wide thread-local slot
 * every `fault_injector`, for every `Tag`, shares -- see the file-level
 * docs' "Storage" section for why this framework spends only one TLS
 * slot in total, rather than one per fault-point `Tag`. */
struct fault_root_tag {};

/** @brief The single, program-wide thread-local slot: holds the head of
 * this thread's intrusive stack of currently active `fault_injector`s
 * (type-erased as `void *`, `nullptr` if none), across every `Tag`.
 * Always pointer-sized, so `tls_provider<void *, fault_root_tag>` always
 * resolves to its zero-allocation raw-pointer specialization, on every
 * `RELOCO_TLS_MODEL` -- arming and disarming a fault point never
 * allocates, and never creates more than this one TLS slot regardless of
 * how many fault-point `Tag`s a program defines. */
using fault_root_slot = tls_provider<void *, fault_root_tag>;

/** @brief Non-template base every `fault_injector<Tag, Args...>`
 * publicly derives from, so the single shared stack (which does not know
 * any particular instantiation's `Tag`/`Args...` ahead of time) can be
 * walked, and each node's recorded `sig` compared against a fault point's
 * own `Tag`/`Args...`, before `static_cast`-ing back to the concrete
 * `fault_injector<Tag, Args...>*` once a match is found. Plain data, no
 * virtual functions -- the `static_cast` back down is well-defined single,
 * non-virtual inheritance. */
struct fault_node {
  fault_node *prev{nullptr};
  type_id sig{};
};

/** @brief Never instantiated as an object -- only used as a template
 * argument to `type_id_of<fault_signature<Tag, Args...>>()`, so every
 * distinct `(Tag, Args...)` combination gets its own process-wide
 * identity to record in `fault_node::sig` and compare against. */
template <typename Tag, typename... Args> struct fault_signature {};

/** @brief Reads the single, program-wide TLS slot's current head. */
[[nodiscard]] inline fault_node *fault_root_head() noexcept {
  auto current = fault_root_slot::get();
  RELOCO_ASSERT(current.has_value(), "reloco::fault_injection: TLS slot unavailable");
  return static_cast<fault_node *>(tls_slot_value(*current));
}

/** @brief Overwrites the single, program-wide TLS slot's head. */
inline void fault_root_set_head(fault_node *head) noexcept {
  auto stored = fault_root_slot::set(static_cast<void *>(head));
  RELOCO_ASSERT(stored.has_value(), "reloco::fault_injection: TLS slot unavailable");
}

#endif // RELOCO_FAULT_INJECTION_UNLIMITED_TLS

} // namespace detail

#endif // RELOCO_ENABLE_FAULT_INJECTION


/**
 * @class fault_injector
 * @brief Caller-owned scoped control block that arms one fault point
 * (`Tag`) on the calling thread for its own lifetime.
 *
 * @details
 * Stackable: constructing a `fault_injector<Tag, Args...>` while another
 * instance is already active on this thread nests it on top, in strict
 * LIFO order, exactly like `scope_guard`. By default (the shared-stack
 * design; see the file-level docs' "Storage" section), that "another
 * instance" may be for the same `Tag` or any other one -- all
 * `fault_injector`s on a thread share one stack, so *all* of them, not
 * just same-`Tag` ones, must be destroyed in strict reverse-construction
 * order (asserted in debug builds; ordinary nested scopes already
 * guarantee it). Under `RELOCO_FAULT_INJECTION_UNLIMITED_TLS`, each `Tag`
 * has its own independent stack instead, so only same-`Tag` nesting order
 * is constrained.
 *
 * Never copyable or movable: its identity (its own address, linked into
 * its stack) is load-bearing, so it must stay put for its entire
 * lifetime -- construct it directly where it is meant to live
 * (typically a plain stack local at the top of a test or a scoped block
 * within one), never as a temporary or a relocated/returned value.
 *
 * @tparam Tag Caller-defined tag type identifying this fault point.
 * @tparam Args Reference-bound argument types the hook receives; must
 * exactly match every `RELOCO_FAULT_POINT_ARGS(Tag, ...)` call site for
 * this `Tag` (see the file-level docs' "`Tag` <-> `Args...` contract").
 */
template <typename Tag, typename... Args>
class fault_injector
#if defined(RELOCO_ENABLE_FAULT_INJECTION) && !RELOCO_FAULT_INJECTION_UNLIMITED_TLS
    : public detail::fault_node
#endif
{
public:
  /** @brief Non-owning hook signature: mutates its arguments by reference. */
  using hook_type = function_ref<void(Args &...)>;

  /**
   * @brief Arms this fault point on the calling thread for @p hook's
   * lifetime, nesting on top of whatever else is currently active for
   * this `Tag` (or, under the shared-stack design, for any `Tag`) on
   * this thread.
   * @param hook Closure invoked with references to the fault point's
   * arguments every time `RELOCO_FAULT_POINT_ARGS(Tag, ...)` is reached
   * on this thread while this guard is alive. Must outlive this
   * `fault_injector` -- pass a named local (e.g. `auto hook = [&](...)
   * {...};`) declared before it, not an inline temporary lambda with
   * captures, exactly like any other `function_ref`-taking reloco API.
   */
  explicit fault_injector(hook_type hook RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : hook_(hook) {
#if defined(RELOCO_ENABLE_FAULT_INJECTION)
#if RELOCO_FAULT_INJECTION_UNLIMITED_TLS
    auto current = detail::fault_slot<Tag>::get();
    RELOCO_ASSERT(current.has_value(), "reloco::fault_injection: TLS slot unavailable");
    prev_ = detail::tls_slot_value(*current);
    auto stored = detail::fault_slot<Tag>::set(static_cast<void *>(this));
    RELOCO_ASSERT(stored.has_value(), "reloco::fault_injection: TLS slot unavailable");
#else
    this->sig = type_id_of<detail::fault_signature<Tag, Args...>>();
    this->prev = detail::fault_root_head();
    detail::fault_root_set_head(this);
#endif
#endif
  }

  fault_injector(const fault_injector &) = delete;
  fault_injector &operator=(const fault_injector &) = delete;
  fault_injector(fault_injector &&) = delete;
  fault_injector &operator=(fault_injector &&) = delete;

  /** @brief Disarms this fault point, restoring whatever this thread had
   * active for this `Tag` (or, under the shared-stack design, for any
   * `Tag`) before this instance was constructed. */
  ~fault_injector() noexcept {
#if defined(RELOCO_ENABLE_FAULT_INJECTION)
#if RELOCO_FAULT_INJECTION_UNLIMITED_TLS
    auto restored = detail::fault_slot<Tag>::set(prev_);
    RELOCO_ASSERT(restored.has_value(), "reloco::fault_injection: TLS slot unavailable");
#else
    RELOCO_ASSERT(detail::fault_root_head() == this,
                  "reloco::fault_injector destroyed out of order -- every fault_injector on a "
                  "thread, across all Tags, must be destroyed in strict reverse-construction order");
    detail::fault_root_set_head(this->prev);
#endif
#endif
  }

  /** @brief Runs the hook directly, exactly as
   * `RELOCO_FAULT_POINT_ARGS(Tag, args...)` would if this were the
   * active instance for `Tag`. Exposed for direct, macro-free use (e.g.
   * from generic test helpers). */
  void invoke(Args &...args) const noexcept { hook_(args...); }

private:
  hook_type hook_;
#if defined(RELOCO_ENABLE_FAULT_INJECTION) && RELOCO_FAULT_INJECTION_UNLIMITED_TLS
  void *prev_{nullptr};
#endif
};

namespace detail {

/** @brief Extracts a callable's own parameter types (by value, reference
 * stripped) from its call operator, so `make_fault_injector` can deduce
 * `fault_injector<Tag, Args...>`'s `Args...` straight from a hook lambda
 * instead of the caller spelling them out a second time. Supports plain
 * function pointers and non-generic lambdas/function objects, both
 * `noexcept`-qualified and not (a `noexcept` call operator is a distinct
 * type since C++17, so each qualifier combination needs its own
 * specialization) -- a generic lambda's `operator()` is itself a
 * template and has no single address to take, so it is deliberately not
 * supported here.
 *
 * `bind<Target, Prefix...>` generalizes the same deduced `Args...` to any
 * other class template `Target<Prefix..., Args...>` (e.g. `Prefix...`
 * empty for `fault_injector` itself, or some wrapper's own leading
 * template parameters) -- this is what lets a separate header build
 * further hook-wrapping utilities (fire-once, conditional, ...) on top of
 * this same deduction without needing to duplicate or otherwise touch it. */
template <typename Callable>
struct fault_hook_signature : fault_hook_signature<decltype(&Callable::operator())> {};

#define RELOCO_DETAIL_FAULT_HOOK_SIGNATURE(qualifiers)                                                               \
  template <typename C, typename R, typename... A> struct fault_hook_signature<R (C::*)(A...) qualifiers> {          \
    template <typename Tag> using injector_type = fault_injector<Tag, std::remove_reference_t<A>...>;                \
    template <template <typename...> class Target, typename... Prefix>                                              \
    using bind = Target<Prefix..., std::remove_reference_t<A>...>;                                                  \
  }

RELOCO_DETAIL_FAULT_HOOK_SIGNATURE();
RELOCO_DETAIL_FAULT_HOOK_SIGNATURE(const);
RELOCO_DETAIL_FAULT_HOOK_SIGNATURE(noexcept);
RELOCO_DETAIL_FAULT_HOOK_SIGNATURE(const noexcept);

#undef RELOCO_DETAIL_FAULT_HOOK_SIGNATURE

template <typename R, typename... A> struct fault_hook_signature<R (*)(A...)> {
  template <typename Tag> using injector_type = fault_injector<Tag, std::remove_reference_t<A>...>;
  template <template <typename...> class Target, typename... Prefix>
  using bind = Target<Prefix..., std::remove_reference_t<A>...>;
};

template <typename R, typename... A> struct fault_hook_signature<R (*)(A...) noexcept> {
  template <typename Tag> using injector_type = fault_injector<Tag, std::remove_reference_t<A>...>;
  template <template <typename...> class Target, typename... Prefix>
  using bind = Target<Prefix..., std::remove_reference_t<A>...>;
};

/** @brief Constructs `fault_injector<Tag, Args...>`, deducing `Args...`
 * from @p hook's own call signature -- powers `RELOCO_FAULT_INJECTOR`.
 * Never itself gated by `RELOCO_ENABLE_FAULT_INJECTION`: `fault_injector`
 * exists (and is cheaply constructible/destructible) either way, exactly
 * like using its constructor directly. */
template <typename Tag, typename Hook>
[[nodiscard]] inline auto make_fault_injector(Hook &hook RELOCO_LIFETIMEBOUND) noexcept {
  return typename fault_hook_signature<Hook>::template injector_type<Tag>(hook);
}

} // namespace detail

#if defined(RELOCO_ENABLE_FAULT_INJECTION)

namespace detail {

/** @brief Returns the currently active `fault_injector<Tag, Args...>` on
 * the calling thread, or `nullptr` if none is armed. Under the default
 * shared-stack design, walks the single shared, per-thread stack looking
 * for the first (innermost) entry whose recorded signature matches
 * `Tag`/`Args...` -- `O(number of currently active fault_injectors on
 * this thread)`. Under `RELOCO_FAULT_INJECTION_UNLIMITED_TLS`, simply
 * reads `Tag`'s own private slot -- `O(1)`. */
template <typename Tag, typename... Args>
[[nodiscard]] inline fault_injector<Tag, Args...> *fault_active() noexcept {
#if RELOCO_FAULT_INJECTION_UNLIMITED_TLS
  auto current = fault_slot<Tag>::get();
  RELOCO_ASSERT(current.has_value(), "reloco::fault_injection: TLS slot unavailable");
  return static_cast<fault_injector<Tag, Args...> *>(tls_slot_value(*current));
#else
  const type_id want = type_id_of<fault_signature<Tag, Args...>>();
  for (fault_node *node = fault_root_head(); node != nullptr; node = node->prev) {
    if (node->sig == want)
      return static_cast<fault_injector<Tag, Args...> *>(node);
  }
  return nullptr;
#endif
}

template <typename Tag, typename... Args> inline void fault_trigger(Args &...args) noexcept {
  if (auto *active = fault_active<Tag, Args...>())
    active->invoke(args...);
}

} // namespace detail

#endif // RELOCO_ENABLE_FAULT_INJECTION

/**
 * @brief `true` if a `fault_injector<Tag, Args...>` is currently armed on
 * the calling thread; always `false` when `RELOCO_ENABLE_FAULT_INJECTION`
 * is not defined. Useful to skip preparing an expensive fault-point
 * argument unless a test has actually armed it.
 */
template <typename Tag, typename... Args> [[nodiscard]] inline bool fault_armed() noexcept {
#if defined(RELOCO_ENABLE_FAULT_INJECTION)
  return detail::fault_active<Tag, Args...>() != nullptr;
#else
  return false;
#endif
}

} // namespace reloco

/**
 * @def RELOCO_FAULT_POINT(Tag)
 * @brief Marks an argument-less injectable fault point. No-op unless
 * `RELOCO_ENABLE_FAULT_INJECTION` is defined, in which case it invokes
 * the hook of whichever `reloco::fault_injector<Tag>` (if any) is
 * currently armed for `Tag` on the calling thread.
 */
/**
 * @def RELOCO_FAULT_POINT_ARGS(Tag, ...)
 * @brief Marks an injectable fault point exposing one or more local
 * variables (passed by name) to the armed hook, by reference. No-op
 * unless `RELOCO_ENABLE_FAULT_INJECTION` is defined; when disabled, the
 * arguments are never evaluated at all (matching `RELOCO_ASSERT`'s own
 * disabled-mode convention) -- pass only side-effect-free lvalue
 * expressions (ordinarily just bare variable names).
 */
#if defined(RELOCO_ENABLE_FAULT_INJECTION)
#define RELOCO_FAULT_POINT(Tag) ::reloco::detail::fault_trigger<Tag>()
#define RELOCO_FAULT_POINT_ARGS(Tag, ...) ::reloco::detail::fault_trigger<Tag>(__VA_ARGS__)
#else
#define RELOCO_FAULT_POINT(Tag) ((void)0)
#define RELOCO_FAULT_POINT_ARGS(Tag, ...) ((void)0)
#endif

/**
 * @def RELOCO_FAULT_TAG(name)
 * @brief Declares @p name as an ordinary, otherwise-unused fault-point
 * `Tag` type (`struct name {};`), matching the convention every other
 * reloco `Tag` template parameter already uses. Purely a naming
 * convenience -- an equivalent hand-written `struct` works identically.
 */
#define RELOCO_FAULT_TAG(name) struct name {}

/**
 * @def RELOCO_FAULT_INJECTOR(var, Tag, ...)
 * @brief Declares a hook local (`var##_hook`) followed by a
 * `reloco::fault_injector<Tag, Args...>` local named @p var that arms
 * `Tag` with it, deducing `Args...` automatically from the hook's own
 * call signature (see `detail::make_fault_injector`) -- the common case
 * of "one hook, one scoped guard, right here" in a single line, with no
 * risk of the dangling-temporary pitfall a hand-written inline lambda
 * argument would otherwise invite (the hook is always a named local,
 * declared immediately before @p var, so it necessarily outlives it).
 * @param var Name of the resulting `fault_injector` local; the hook
 * itself becomes `var##_hook`, declared just before it.
 * @param Tag Caller-defined tag type identifying the fault point (see
 * `RELOCO_FAULT_TAG`).
 * @param ... The hook: any expression producing a callable (ordinarily a
 * lambda literal) whose call operator is not a template (a generic
 * lambda's parameter types cannot be deduced this way -- construct a
 * `fault_injector<Tag, Args...>` directly instead in that case).
 * @code
 * RELOCO_FAULT_TAG(commit_race_point);
 * // ...
 * RELOCO_FAULT_INJECTOR(fi, commit_race_point, [](std::uint64_t &w) { w = 0xDEADBEEF; });
 * // `fi` is a reloco::fault_injector<commit_race_point, std::uint64_t>, already armed.
 * @endcode
 */
#define RELOCO_FAULT_INJECTOR(var, Tag, ...)                                                                         \
  auto var##_hook = __VA_ARGS__;                                                                                     \
  auto var = ::reloco::detail::make_fault_injector<Tag>(var##_hook)
