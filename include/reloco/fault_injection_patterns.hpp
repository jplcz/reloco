// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file fault_injection_patterns.hpp
 * @brief Convenience macros for `fault_injection.hpp`'s most common and a
 * few more advanced fault-point arming patterns.
 *
 * @details
 * `fault_injection.hpp` itself only provides the minimal, general
 * mechanism -- `fault_injector<Tag, Args...>`, `RELOCO_FAULT_TAG`, and
 * `RELOCO_FAULT_INJECTOR` -- deliberately kept small. This header instead
 * collects small, self-contained macros for patterns that come up
 * repeatedly once a test suite actually starts using it, all built
 * strictly on top of that public/`detail` API (nothing here needs, or
 * gets, any special access to `fault_injection.hpp`'s internals beyond
 * what it already exposes):
 *
 * - `RELOCO_FAULT_MUTATE(var, Tag, Type, ...)` / `RELOCO_FAULT_SET(var,
 *   Tag, Type, value)` -- arm a single-argument fault point to mutate (or
 *   unconditionally overwrite) the exposed value every time it fires;
 *   the single most common use of this whole framework ("simulate a
 *   concurrent CPU/ISR having changed this exact value").
 * - `RELOCO_FAULT_SPY(var, Tag, counter)` -- arm a fault point purely to
 *   count how many times it is reached, without touching any of its
 *   arguments; useful to assert *how often* a race window was hit,
 *   separately from what a mutating hook already asserts about its
 *   effect.
 * - `RELOCO_FAULT_FIRE_N(var, Tag, count, ...)` / `RELOCO_FAULT_FIRE_ONCE(
 *   var, Tag, ...)` -- arm a hook that only actually fires the first
 *   @p count times its fault point is reached (once for `FIRE_ONCE`),
 *   silently doing nothing on every later hit -- for simulating a fault
 *   that only strikes on a specific, otherwise-hard-to-target iteration
 *   of a loop under test.
 * - `RELOCO_FAULT_WHEN(var, Tag, pred, ...)` -- arm a hook that only
 *   fires when a caller-supplied predicate (itself receiving the fault
 *   point's own arguments, by reference, exactly like the hook) returns
 *   `true` -- for simulating a fault that depends on the current state
 *   of the arguments themselves (e.g. only corrupt an index once it
 *   passes a threshold).
 * - `RELOCO_FAULT_SKIP_N(var, Tag, skip, ...)` -- the mirror image of
 *   `RELOCO_FAULT_FIRE_N`: silently do nothing for the first @p skip
 *   hits, then fire on every hit after that, for as long as @p var
 *   stays alive -- for simulating a fault that only starts striking
 *   once some warm-up/steady-state point has been reached.
 * - `RELOCO_FAULT_NTH(var, Tag, n, ...)` -- fire only on the single,
 *   exact @p n'th (1-based) time the fault point is reached, and never
 *   before or after -- for targeting one specific iteration of a loop
 *   under test without having to hand-roll a counter.
 * - `RELOCO_FAULT_EVERY_N(var, Tag, n, ...)` -- fire periodically, once
 *   every @p n hits (the @p n'th, `2*n`'th, `3*n`'th, ...) -- for
 *   simulating an intermittent fault rather than a one-shot one.
 * - `RELOCO_FAULT_TOGGLE(var, Tag, Type)` / `RELOCO_FAULT_INCREMENT(var,
 *   Tag, Type, delta)` / `RELOCO_FAULT_XOR(var, Tag, Type, mask)` --
 *   trivial `RELOCO_FAULT_MUTATE` sugar for the most common bodies after
 *   an unconditional overwrite: flipping a `bool`-like value, nudging a
 *   counter/index by a fixed amount, or XOR-ing in a bit-flip/partial
 *   corruption mask, every time the fault point fires.
 *
 * Every macro here still respects `fault_injection.hpp`'s own
 * `RELOCO_ENABLE_FAULT_INJECTION` opt-in: with it undefined, the
 * `fault_injector` each of these macros ultimately constructs is the
 * same true no-op it always is, and the wrapper hook types below compile
 * and are exercised (harmlessly) either way, exactly like a
 * hand-written `RELOCO_FAULT_INJECTOR` call.
 *
 * @code
 * RELOCO_FAULT_TAG(commit_race_point);
 * // ...
 * TEST(Commit, SurvivesConcurrentIndexMutation) {
 *   RELOCO_FAULT_SET(fi, commit_race_point, std::uint64_t, 0xDEADBEEF);
 *   // ... call producer_commit() and assert on the resulting (mis)behavior ...
 * }
 *
 * TEST(Commit, OnlyFailsOnSecondAttempt) {
 *   RELOCO_FAULT_FIRE_N(fi, commit_race_point, 2, [](std::uint64_t &w) { w = 0xDEADBEEF; });
 *   // producer_commit() is called three times; only calls #1 and #2 observe the corruption.
 * }
 * @endcode
 *
 * @see [Fault injection](../docs/fault-injection.md) for the full guide.
 */

#pragma once

#include "fault_injection.hpp"

#include <cstddef>

namespace reloco::detail {

/**
 * @brief Wraps a hook so it only actually invokes it the first @p
 * max_fires times it is called, silently doing nothing afterwards.
 * Powers `RELOCO_FAULT_FIRE_N`/`RELOCO_FAULT_FIRE_ONCE`. `Args...` is
 * deduced from @p Hook via `fault_hook_signature::bind` (see
 * `fault_injection.hpp`), so this class's own `operator()` is a
 * concrete, non-template member function -- itself directly usable with
 * `make_fault_injector`.
 */
template <typename Hook, typename... Args> class fire_n_times_hook {
public:
  fire_n_times_hook(Hook &hook RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS, std::size_t max_fires) noexcept
      : hook_(hook), remaining_(max_fires) {}

  void operator()(Args &...args) noexcept {
    if (remaining_ > 0) {
      --remaining_;
      hook_(args...);
    }
  }

  /** @brief How many more times this hook will still fire. */
  [[nodiscard]] std::size_t remaining() const noexcept { return remaining_; }

private:
  Hook &hook_;
  std::size_t remaining_;
};

template <typename Hook>
using fire_n_times_hook_for = typename fault_hook_signature<Hook>::template bind<fire_n_times_hook, Hook>;

/**
 * @brief Wraps a hook so it only actually invokes it when @p pred --
 * called with the same arguments, by reference -- returns `true`.
 * Powers `RELOCO_FAULT_WHEN`. `Args...` is deduced from @p Hook (not @p
 * Pred), exactly like `fire_n_times_hook` above.
 */
template <typename Pred, typename Hook, typename... Args> class conditional_hook {
public:
  conditional_hook(Pred &pred RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS,
                   Hook &hook RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : pred_(pred), hook_(hook) {}

  void operator()(Args &...args) noexcept {
    if (pred_(args...))
      hook_(args...);
  }

private:
  Pred &pred_;
  Hook &hook_;
};

template <typename Pred, typename Hook>
using conditional_hook_for = typename fault_hook_signature<Hook>::template bind<conditional_hook, Pred, Hook>;

/**
 * @brief Wraps a hook so it silently does nothing for the first @p
 * skip_count calls, then invokes it on every call after that, for as
 * long as it is alive. Powers `RELOCO_FAULT_SKIP_N` -- the mirror image
 * of `fire_n_times_hook`.
 */
template <typename Hook, typename... Args> class skip_n_hook {
public:
  skip_n_hook(Hook &hook RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS, std::size_t skip_count) noexcept
      : hook_(hook), remaining_skip_(skip_count) {}

  void operator()(Args &...args) noexcept {
    if (remaining_skip_ > 0) {
      --remaining_skip_;
      return;
    }
    hook_(args...);
  }

  /** @brief How many more leading calls will still be skipped. */
  [[nodiscard]] std::size_t remaining_skip() const noexcept { return remaining_skip_; }

private:
  Hook &hook_;
  std::size_t remaining_skip_;
};

template <typename Hook> using skip_n_hook_for = typename fault_hook_signature<Hook>::template bind<skip_n_hook, Hook>;

/**
 * @brief Wraps a hook so it fires exactly once, on the single @p target
 * (1-based) call, and is a no-op on every other call before or after it.
 * Powers `RELOCO_FAULT_NTH`.
 */
template <typename Hook, typename... Args> class nth_call_hook {
public:
  nth_call_hook(Hook &hook RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS, std::size_t target) noexcept
      : hook_(hook), target_(target), count_(0) {}

  void operator()(Args &...args) noexcept {
    ++count_;
    if (count_ == target_)
      hook_(args...);
  }

  /** @brief How many calls have been observed so far. */
  [[nodiscard]] std::size_t call_count() const noexcept { return count_; }

private:
  Hook &hook_;
  std::size_t target_;
  std::size_t count_;
};

template <typename Hook>
using nth_call_hook_for = typename fault_hook_signature<Hook>::template bind<nth_call_hook, Hook>;

/**
 * @brief Wraps a hook so it fires periodically: on the @p period'th
 * call, the `2*period`'th, the `3*period`'th, and so on, and is a no-op
 * on every call in between. Powers `RELOCO_FAULT_EVERY_N`.
 */
template <typename Hook, typename... Args> class every_n_hook {
public:
  every_n_hook(Hook &hook RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS, std::size_t period) noexcept
      : hook_(hook), period_(period), count_(0) {}

  void operator()(Args &...args) noexcept {
    ++count_;
    if (period_ > 0 && count_ % period_ == 0)
      hook_(args...);
  }

  /** @brief How many calls have been observed so far. */
  [[nodiscard]] std::size_t call_count() const noexcept { return count_; }

private:
  Hook &hook_;
  std::size_t period_;
  std::size_t count_;
};

template <typename Hook>
using every_n_hook_for = typename fault_hook_signature<Hook>::template bind<every_n_hook, Hook>;

} // namespace reloco::detail

/**
 * @def RELOCO_FAULT_MUTATE(var, Tag, Type, ...)
 * @brief Arms @p Tag (a single-argument fault point of type @p Type)
 * with a hook whose body is @p ... , referring to the exposed value as
 * `reloco_fault_value`. The most direct way to say "mutate this exact
 * value right here" without writing out a lambda by hand. The hook
 * captures its enclosing scope by reference (`[&]`), so @p ... may also
 * refer to other locals already declared before this macro (e.g. a
 * runtime-computed mask or delta) -- safe because, like every hook this
 * framework's macros declare, it lives on the stack and is guaranteed to
 * go out of scope no later than everything it could have captured.
 * @code
 * RELOCO_FAULT_MUTATE(fi, my_point, int, reloco_fault_value += 1000);
 * @endcode
 */
#define RELOCO_FAULT_MUTATE(var, Tag, Type, ...)                                                                       \
  RELOCO_FAULT_INJECTOR(var, Tag, [&](Type &reloco_fault_value) { __VA_ARGS__; })

/**
 * @def RELOCO_FAULT_SET(var, Tag, Type, value)
 * @brief Arms @p Tag (a single-argument fault point of type @p Type)
 * with a hook that unconditionally overwrites the exposed value with @p
 * value every time it fires -- the single most common fault-injection
 * pattern ("simulate a concurrent CPU/ISR having changed this exact
 * value to X").
 * @code
 * RELOCO_FAULT_SET(fi, commit_race_point, std::uint64_t, 0xDEADBEEF);
 * @endcode
 */
#define RELOCO_FAULT_SET(var, Tag, Type, value) RELOCO_FAULT_MUTATE(var, Tag, Type, reloco_fault_value = (value))

/**
 * @def RELOCO_FAULT_SPY(var, Tag, counter)
 * @brief Arms @p Tag with a hook that increments @p counter (an
 * existing, caller-owned local of any integer type) every time it fires,
 * without touching any of the fault point's own arguments -- for
 * asserting *how many times* a race window was actually reached, as
 * opposed to (or alongside) mutating it.
 * @code
 * int hits = 0;
 * RELOCO_FAULT_SPY(fi, my_point, hits);
 * // ... exercise the code under test ...
 * EXPECT_EQ(3, hits);
 * @endcode
 */
#define RELOCO_FAULT_SPY(var, Tag, counter) RELOCO_FAULT_INJECTOR(var, Tag, [&] { ++(counter); })

/**
 * @def RELOCO_FAULT_FIRE_N(var, Tag, count, ...)
 * @brief Arms @p Tag with a hook (@p ..., ordinarily a lambda literal)
 * that only actually fires the first @p count times the fault point is
 * reached; every later hit while @p var is still alive is a silent
 * no-op. `var` is a `reloco::fault_injector<Tag, Args...>` as usual;
 * `var##_countdown.remaining()` reports how many firings are left.
 * @code
 * RELOCO_FAULT_FIRE_N(fi, commit_race_point, 2, [](std::uint64_t &w) { w = 0xDEADBEEF; });
 * // producer_commit() called three times: only the first two observe the corruption.
 * @endcode
 */
#define RELOCO_FAULT_FIRE_N(var, Tag, count, ...)                                                                      \
  auto var##_hook = __VA_ARGS__;                                                                                       \
  ::reloco::detail::fire_n_times_hook_for<decltype(var##_hook)> var##_countdown(var##_hook,                            \
                                                                                static_cast<std::size_t>(count));      \
  auto var = ::reloco::detail::make_fault_injector<Tag>(var##_countdown)

/**
 * @def RELOCO_FAULT_FIRE_ONCE(var, Tag, ...)
 * @brief Shorthand for `RELOCO_FAULT_FIRE_N(var, Tag, 1, ...)`: the hook
 * fires exactly once, the first time the fault point is reached, and is
 * a silent no-op on every subsequent hit while @p var is still alive.
 */
#define RELOCO_FAULT_FIRE_ONCE(var, Tag, ...) RELOCO_FAULT_FIRE_N(var, Tag, 1, __VA_ARGS__)

/**
 * @def RELOCO_FAULT_WHEN(var, Tag, pred, ...)
 * @brief Arms @p Tag with a hook (@p ...) that only actually fires when
 * @p pred -- a callable receiving the fault point's own arguments, by
 * reference, exactly like the hook itself -- returns `true`. For
 * simulating a fault that depends on the current state of the arguments
 * (e.g. only corrupt an index once it has already advanced far enough).
 * @code
 * RELOCO_FAULT_WHEN(fi, my_point, [](int &idx) { return idx > 100; },
 *                    [](int &idx) { idx = -1; });
 * @endcode
 */
#define RELOCO_FAULT_WHEN(var, Tag, pred, ...)                                                                         \
  auto var##_pred = (pred);                                                                                            \
  auto var##_hook = __VA_ARGS__;                                                                                       \
  ::reloco::detail::conditional_hook_for<decltype(var##_pred), decltype(var##_hook)> var##_cond(var##_pred,            \
                                                                                                var##_hook);           \
  auto var = ::reloco::detail::make_fault_injector<Tag>(var##_cond)

/**
 * @def RELOCO_FAULT_SKIP_N(var, Tag, skip, ...)
 * @brief Arms @p Tag with a hook (@p ...) that is a silent no-op for the
 * first @p skip times the fault point is reached, then fires on every
 * hit after that, for as long as @p var is alive -- the mirror image of
 * `RELOCO_FAULT_FIRE_N`. `var##_skip.remaining_skip()` reports how many
 * leading hits are still being skipped.
 * @code
 * // Only corrupt the value once the loop has already run 10 times.
 * RELOCO_FAULT_SKIP_N(fi, warm_up_point, 10, [](int &v) { v = -1; });
 * @endcode
 */
#define RELOCO_FAULT_SKIP_N(var, Tag, skip, ...)                                                                       \
  auto var##_hook = __VA_ARGS__;                                                                                       \
  ::reloco::detail::skip_n_hook_for<decltype(var##_hook)> var##_skip(var##_hook, static_cast<std::size_t>(skip));      \
  auto var = ::reloco::detail::make_fault_injector<Tag>(var##_skip)

/**
 * @def RELOCO_FAULT_NTH(var, Tag, n, ...)
 * @brief Arms @p Tag with a hook (@p ...) that fires exactly once, on
 * the single, exact @p n'th (1-based) time the fault point is reached,
 * and is a silent no-op on every other hit before or after it -- for
 * targeting one specific loop iteration without hand-rolling a counter.
 * `var##_nth.call_count()` reports how many hits have been observed so
 * far.
 * @code
 * // Only the 3rd retry attempt observes the corruption.
 * RELOCO_FAULT_NTH(fi, retry_point, 3, [](int &attempt) { attempt = -1; });
 * @endcode
 */
#define RELOCO_FAULT_NTH(var, Tag, n, ...)                                                                             \
  auto var##_hook = __VA_ARGS__;                                                                                       \
  ::reloco::detail::nth_call_hook_for<decltype(var##_hook)> var##_nth(var##_hook, static_cast<std::size_t>(n));        \
  auto var = ::reloco::detail::make_fault_injector<Tag>(var##_nth)

/**
 * @def RELOCO_FAULT_EVERY_N(var, Tag, n, ...)
 * @brief Arms @p Tag with a hook (@p ...) that fires periodically: on
 * the @p n'th hit, the `2*n`'th, the `3*n`'th, and so on, staying a
 * silent no-op on every hit in between -- for simulating an
 * intermittent fault rather than a one-shot one. `var##_period.
 * call_count()` reports how many hits have been observed so far.
 * @code
 * // Every 5th call observes the corruption; the rest do not.
 * RELOCO_FAULT_EVERY_N(fi, flaky_point, 5, [](int &v) { v = -1; });
 * @endcode
 */
#define RELOCO_FAULT_EVERY_N(var, Tag, n, ...)                                                                         \
  auto var##_hook = __VA_ARGS__;                                                                                       \
  ::reloco::detail::every_n_hook_for<decltype(var##_hook)> var##_period(var##_hook, static_cast<std::size_t>(n));      \
  auto var = ::reloco::detail::make_fault_injector<Tag>(var##_period)

/**
 * @def RELOCO_FAULT_TOGGLE(var, Tag, Type)
 * @brief Arms @p Tag (a single-argument fault point of type @p Type)
 * with a hook that flips the exposed value (`reloco_fault_value =
 * !reloco_fault_value`) every time it fires. `Type` must support unary
 * `!` and assignment from its result (e.g. `bool`, or any type
 * contextually convertible to/from `bool`).
 * @code
 * RELOCO_FAULT_TOGGLE(fi, retry_flag_point, bool);
 * @endcode
 */
#define RELOCO_FAULT_TOGGLE(var, Tag, Type)                                                                            \
  RELOCO_FAULT_MUTATE(var, Tag, Type, reloco_fault_value = !reloco_fault_value)

/**
 * @def RELOCO_FAULT_INCREMENT(var, Tag, Type, delta)
 * @brief Arms @p Tag (a single-argument fault point of type @p Type)
 * with a hook that nudges the exposed value by @p delta
 * (`reloco_fault_value += delta`) every time it fires -- for simulating
 * an off-by-@p-delta corruption of a counter or index, as opposed to
 * `RELOCO_FAULT_SET`'s unconditional overwrite.
 * @code
 * RELOCO_FAULT_INCREMENT(fi, index_point, std::size_t, -1);
 * @endcode
 */
#define RELOCO_FAULT_INCREMENT(var, Tag, Type, delta) RELOCO_FAULT_MUTATE(var, Tag, Type, reloco_fault_value += (delta))

/**
 * @def RELOCO_FAULT_XOR(var, Tag, Type, mask)
 * @brief Arms @p Tag (a single-argument fault point of type @p Type)
 * with a hook that XORs the exposed value with @p mask
 * (`reloco_fault_value ^= mask`) every time it fires -- for simulating a
 * bit-flip/partial corruption (e.g. a single stray write, a torn
 * multi-word update, or a specific bit pattern injected by a fuzzer)
 * rather than `RELOCO_FAULT_SET`'s full overwrite.
 * @code
 * // Flip only the top bit -- e.g. to probe a signedness/wraparound bug.
 * RELOCO_FAULT_XOR(fi, index_point, std::uint64_t, std::uint64_t{1} << 63);
 * @endcode
 */
#define RELOCO_FAULT_XOR(var, Tag, Type, mask) RELOCO_FAULT_MUTATE(var, Tag, Type, reloco_fault_value ^= (mask))
