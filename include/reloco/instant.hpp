// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file instant.hpp
 * @brief `reloco::instant`, matching Rust's `std::time::Instant`: an
 * opaque, monotonically non-decreasing point in time, measured against
 * `reloco::duration` (see `duration.hpp`) rather than
 * `std::chrono::time_point<Clock>` -- the same integer-only,
 * `<chrono>`-free rationale `duration.hpp` documents applies here too:
 * some `<chrono>` implementations use floating-point intermediates for
 * cross-`Period` arithmetic, and `<chrono>` itself may not be available
 * on a freestanding target at all.
 *
 * Like Rust's `Instant`, a value returned by `instant::now()` carries no
 * defined epoch or meaning by itself -- only the *difference* between two
 * `instant` values (`duration_since`/`elapsed`/subtraction) is meaningful.
 * `instant` is therefore not for wall-clock/calendar timekeeping (there is
 * no `reloco::system_time` counterpart yet); it exists to measure elapsed
 * time and compute timeouts/deadlines against a clock that (ideally) never
 * runs backward.
 *
 * `instant::now()`'s actual clock reading is a customization point,
 * exactly like `duration_converter<T>` (`duration.hpp`) or
 * `allocator_traits<Tag>` (`allocator.hpp`, see `docs/extending.md`): an
 * empty tag type selects an `instant_clock_traits<Tag>` specialization
 * providing `static duration now() noexcept`. `RELOCO_INSTANT_CLOCK_TAG`
 * selects which tag `instant::now()` actually calls through to:
 *
 * - **Default, when `<time.h>`'s `clock_gettime` is available**:
 *   `reloco::posix_clock_tag`, backed by `clock_gettime(3)` against
 *   `CLOCK_MONOTONIC` when the platform's pthread implementation supports
 *   selecting it (POSIX's optional "Clock Selection" feature -- notably
 *   absent on Darwin/macOS), falling back to `CLOCK_REALTIME` otherwise.
 *   `RELOCO_MUTEX_NO_MONOTONIC_CLOCK` (see `reloco_config.hpp`) forces
 *   `CLOCK_REALTIME` here too, matching `mutex.hpp`'s own
 *   `condition_variable::wait_for` deadline clock -- the two headers
 *   share the same opt-out macro and detection logic (duplicated rather
 *   than shared via an include, so `instant.hpp` does not have to depend
 *   on `mutex.hpp`) so `instant::now()` and a timed condition-variable
 *   wait agree on which clock "now" means.
 * - **Not available, and no `RELOCO_INSTANT_CLOCK_TAG` defined**:
 *   `instant::now()`/`instant::elapsed()` are not declared at all --
 *   every other `instant` member (`duration_since`, arithmetic,
 *   comparisons, ...) still works on values obtained some other way (a
 *   caller-tracked `instant` is still a perfectly ordinary, comparable
 *   value type).
 * - **Custom**: define `RELOCO_INSTANT_CLOCK_TAG` to your own empty tag
 *   type and specialize `reloco::instant_clock_traits` for it, for any
 *   clock source `clock_gettime` cannot reach -- an RTOS tick counter, a
 *   memory-mapped hardware timer, or a kernel API with its own notion of
 *   "now" as a platform-specific fixed-point type, e.g. **FreeBSD kernel**
 *   `sbinuptime()` (a 64-bit `32.32` fixed-point seconds count, the same
 *   `sbintime_t` representation `duration.hpp`'s own file-level doc
 *   comment shows converting a `duration` *to*) or `binuptime()` (a
 *   `struct bintime`, 64-bit seconds plus a 64-bit binary fraction of a
 *   second):
 *
 * @code
 * // reloco_user_config.hpp
 * #define RELOCO_INSTANT_CLOCK_TAG my_freebsd_kernel_clock_tag
 *
 * // my_freebsd_kernel_clock.hpp -- included normally elsewhere, e.g. from
 * // a source file, before any use of reloco::instant::now(). Documentation
 * // /reference only: never compiled or exercised by this repository
 * // (which targets hosted userspace, not the FreeBSD kernel proper) --
 * // review and adapt before relying on it.
 * #include <sys/param.h>
 * #include <sys/systm.h>
 * #include <sys/time.h>
 *
 * struct my_freebsd_kernel_clock_tag {};
 *
 * template <> struct reloco::instant_clock_traits<my_freebsd_kernel_clock_tag> {
 *   // sbinuptime() is monotonic (uptime, not wall-clock) and already a
 *   // 32.32 fixed-point seconds count, so converting it to a `duration`
 *   // is the same fixed-point math `duration_converter<sbintime_t>`
 *   // does, just in the opposite direction.
 *   static reloco::duration now() noexcept {
 *     sbintime_t sbt = sbinuptime();
 *     auto secs = static_cast<std::uint64_t>(sbt >> 32);
 *     auto frac = static_cast<std::uint64_t>(static_cast<std::uint64_t>(sbt) & 0xffff'ffffULL);
 *     return reloco::duration::from_secs(secs) +
 *            reloco::duration::from_nanos((frac * 1'000'000'000ULL) >> 32);
 *   }
 * };
 * @endcode
 */

#include "detail/compat.hpp"
#include "duration.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "int_ops.hpp"

#include <cstdint>

#if RELOCO_HAS_INCLUDE(<time.h>)
#include <time.h>
#define RELOCO_DETAIL_INSTANT_HAS_POSIX_CLOCK 1
#else
#define RELOCO_DETAIL_INSTANT_HAS_POSIX_CLOCK 0
#endif

#if RELOCO_DETAIL_INSTANT_HAS_POSIX_CLOCK
// Mirrors mutex.hpp's own CLOCK_MONOTONIC detection/opt-out (see this
// file's own doc comment above for why it is duplicated rather than
// shared via an include).
#if defined(RELOCO_MUTEX_NO_MONOTONIC_CLOCK)
#define RELOCO_DETAIL_INSTANT_MONOTONIC_CLOCK 0
#elif defined(__APPLE__) || defined(__MACH__)
#define RELOCO_DETAIL_INSTANT_MONOTONIC_CLOCK 0
#elif defined(_POSIX_CLOCK_SELECTION) && _POSIX_CLOCK_SELECTION >= 0 && defined(CLOCK_MONOTONIC)
#define RELOCO_DETAIL_INSTANT_MONOTONIC_CLOCK 1
#else
#define RELOCO_DETAIL_INSTANT_MONOTONIC_CLOCK 0
#endif

#if RELOCO_DETAIL_INSTANT_MONOTONIC_CLOCK
#define RELOCO_DETAIL_INSTANT_CLOCKID CLOCK_MONOTONIC
#else
#define RELOCO_DETAIL_INSTANT_CLOCKID CLOCK_REALTIME
#endif
#endif // RELOCO_DETAIL_INSTANT_HAS_POSIX_CLOCK

// `expected<T, E>` (see `expected.hpp`) is only a literal type once
// C++20's relaxed constexpr rules let its destructor itself be
// `constexpr`. Every function below that returns (or locally holds) a
// `result<T>` uses `RELOCO_CONSTEXPR20` (`detail/compat.hpp`) instead of
// a plain `constexpr`, rather than unconditionally marking it
// `constexpr` and relying on it silently never being constant-evaluated:
// at least one mainstream compiler (Clang) hard-errors, rather than just
// warning, on a `constexpr` function whose return type can never satisfy
// a constant expression in the current standard.

namespace reloco {

/**
 * @brief Customization point supplying `instant::now()`'s underlying
 * clock reading, keyed by an empty tag type (the same tag + `*_traits<Tag>`
 * provider shape `docs/extending.md` documents, minus the type-erased
 * `*_ref` handle -- there is exactly one clock source selected at compile
 * time, so no runtime dispatch is needed). No generic definition --
 * specialize with a `static duration now() noexcept` for any clock source
 * you need. See the file-level documentation above.
 */
template <typename Tag> struct instant_clock_traits;

#if RELOCO_DETAIL_INSTANT_HAS_POSIX_CLOCK

/** @brief Tag selecting the built-in POSIX `clock_gettime` clock source.
 * Only defined when `<time.h>` is available. */
struct posix_clock_tag {};

/** @brief Built-in clock source: `clock_gettime(3)`. See the file-level
 * documentation above for which clock ID this actually reads. */
template <> struct instant_clock_traits<posix_clock_tag> {
  [[nodiscard]] static duration now() noexcept {
    struct timespec ts{};
    clock_gettime(RELOCO_DETAIL_INSTANT_CLOCKID, &ts);
    return duration::from_secs(static_cast<std::uint64_t>(ts.tv_sec)) +
           duration::from_nanos(static_cast<std::uint64_t>(ts.tv_nsec));
  }
};

#endif // RELOCO_DETAIL_INSTANT_HAS_POSIX_CLOCK

namespace detail {

/** @brief `lhs - rhs` as a `duration`, or `error::invalid_argument` if
 * `rhs` is later than `lhs` -- `duration`'s unsigned representation
 * cannot express a negative span, matching why `duration` itself has no
 * plain `operator-`. Built on `int_ops.hpp`'s `checked_sub<std::uint32_t>`
 * to detect the one place this subtraction can actually borrow (the
 * sub-second remainder); once `lhs >= rhs` holds (checked below, via
 * `duration`'s own total order, seconds compared before nanoseconds),
 * `lhs.as_secs() >= rhs.as_secs()` is already guaranteed, so the whole-
 * second subtraction never needs its own overflow check. */
[[nodiscard]] inline RELOCO_CONSTEXPR20 result<duration> checked_duration_diff(const duration &lhs,
                                                                               const duration &rhs) noexcept {
  if (lhs < rhs)
    return unexpected(error::invalid_argument);

  if (auto nanos = checked_sub<std::uint32_t>(lhs.subsec_nanos(), rhs.subsec_nanos()))
    return duration::from_secs(lhs.as_secs() - rhs.as_secs()) + duration::from_nanos(*nanos);

  // subsec_nanos() borrow: lhs.as_secs() must be strictly greater than
  // rhs.as_secs() here (lhs >= rhs overall, but its sub-second remainder
  // is smaller), so both subtractions below are safe.
  return duration::from_secs(lhs.as_secs() - rhs.as_secs() - 1) +
         duration::from_nanos(static_cast<std::uint64_t>(lhs.subsec_nanos()) + duration::nanos_per_sec -
                              rhs.subsec_nanos());
}

} // namespace detail

} // namespace reloco

// Selects which instant_clock_traits<Tag> specialization instant::now()
// calls through to. Defaults to the built-in POSIX backend when
// available; a freestanding/kernel target with no clock_gettime (or one
// that wants a different clock source) must define this to its own tag
// type and specialize reloco::instant_clock_traits for it -- see this
// file's own doc comment above for a worked FreeBSD kernel example.
#if !defined(RELOCO_INSTANT_CLOCK_TAG)
#if RELOCO_DETAIL_INSTANT_HAS_POSIX_CLOCK
#define RELOCO_INSTANT_CLOCK_TAG ::reloco::posix_clock_tag
#define RELOCO_DETAIL_INSTANT_HAS_NOW 1
#else
#define RELOCO_DETAIL_INSTANT_HAS_NOW 0
#endif
#else
#define RELOCO_DETAIL_INSTANT_HAS_NOW 1
#endif

namespace reloco {

/**
 * @brief An opaque, monotonically non-decreasing point in time, matching
 * Rust's `std::time::Instant`. See the file-level documentation above.
 */
class instant {
public:
  /** @brief The default-constructed "zero" instant -- not `now()`, and
   * meaningful only as a base to compare/measure other `instant` values
   * against (matching a default-constructed `duration`'s own "zero span"
   * role). */
  constexpr instant() noexcept = default;

#if RELOCO_DETAIL_INSTANT_HAS_NOW
  /** @brief The current time, per `instant_clock_traits<
   * RELOCO_INSTANT_CLOCK_TAG>::now()`. See the file-level documentation
   * above for backend selection. */
  [[nodiscard]] static instant now() noexcept { return instant(instant_clock_traits<RELOCO_INSTANT_CLOCK_TAG>::now()); }
#endif

  /**
   * @brief The elapsed time from @p earlier to `*this`, matching Rust's
   * `Instant::duration_since`. Currently **saturates to zero** rather
   * than reporting an error if `earlier` is actually later than `*this`
   * (e.g. a non-monotonic clock source), matching current Rust behavior
   * -- see `checked_duration_since` to detect that case instead.
   */
  [[nodiscard]] RELOCO_CONSTEXPR20 duration duration_since(const instant &earlier) const noexcept {
    auto diff = detail::checked_duration_diff(since_epoch_, earlier.since_epoch_);
    return diff ? *diff : duration();
  }

  /** @brief Alias for `duration_since`, matching Rust's own API surface
   * (which keeps both names even though they currently behave
   * identically). */
  [[nodiscard]] RELOCO_CONSTEXPR20 duration saturating_duration_since(const instant &earlier) const noexcept {
    return duration_since(earlier);
  }

  /**
   * @brief Same as `duration_since`, but fails with
   * `error::invalid_argument` instead of saturating if @p earlier is
   * later than `*this`, matching Rust's `checked_duration_since() ->
   * Option<Duration>`.
   */
  [[nodiscard]] RELOCO_CONSTEXPR20 result<duration> checked_duration_since(const instant &earlier) const noexcept {
    return detail::checked_duration_diff(since_epoch_, earlier.since_epoch_);
  }

#if RELOCO_DETAIL_INSTANT_HAS_NOW
  /** @brief `instant::now().duration_since(*this)`, matching Rust's
   * `Instant::elapsed()`. */
  [[nodiscard]] duration elapsed() const noexcept { return now().duration_since(*this); }
#endif

  /**
   * @brief `*this + rhs`, matching Rust's `Instant::checked_add(Duration)
   * -> Option<Instant>`. Always succeeds today (`duration`'s own
   * `operator+` has no overflow detection, see `duration.hpp`); kept
   * fallible to match Rust's signature and stay forward-compatible if
   * `duration` grows overflow detection later.
   */
  [[nodiscard]] RELOCO_CONSTEXPR20 result<instant> checked_add(duration rhs) const noexcept {
    return instant(since_epoch_ + rhs);
  }

  /**
   * @brief `*this - rhs`, matching Rust's `Instant::checked_sub(Duration)
   * -> Option<Instant>`. Fails with `error::invalid_argument` if @p rhs
   * is greater than the time already elapsed since this `instant`'s own
   * epoch (an underflow `duration`'s unsigned representation cannot
   * express).
   */
  [[nodiscard]] RELOCO_CONSTEXPR20 result<instant> checked_sub(duration rhs) const noexcept {
    auto diff = detail::checked_duration_diff(since_epoch_, rhs);
    if (!diff)
      return unexpected(diff.error());
    return instant(*diff);
  }

  /** @brief Matches Rust's `Add<Duration> for Instant`. Saturates rather
   * than overflowing/panicking if the result would be out of range --
   * see `checked_add` to detect that case instead. */
  [[nodiscard]] friend constexpr instant operator+(const instant &lhs, duration rhs) noexcept {
    return instant(lhs.since_epoch_ + rhs);
  }

  /** @brief Matches Rust's `Sub<Duration> for Instant`. Saturates to the
   * "zero" instant rather than underflowing/panicking if @p rhs is
   * greater than @p lhs's own time since epoch -- see `checked_sub` to
   * detect that case instead. */
  [[nodiscard]] friend RELOCO_CONSTEXPR20 instant operator-(const instant &lhs, duration rhs) noexcept {
    auto diff = detail::checked_duration_diff(lhs.since_epoch_, rhs);
    return instant(diff ? *diff : duration());
  }

  /** @brief Matches Rust's `Sub<Instant> for Instant` (`-> Duration`).
   * Equivalent to `lhs.duration_since(rhs)`. */
  [[nodiscard]] friend RELOCO_CONSTEXPR20 duration operator-(const instant &lhs, const instant &rhs) noexcept {
    return lhs.duration_since(rhs);
  }

  [[nodiscard]] friend constexpr bool operator==(const instant &lhs, const instant &rhs) noexcept {
    return lhs.since_epoch_ == rhs.since_epoch_;
  }

  [[nodiscard]] friend constexpr bool operator!=(const instant &lhs, const instant &rhs) noexcept {
    return !(lhs == rhs);
  }

  [[nodiscard]] friend constexpr bool operator<(const instant &lhs, const instant &rhs) noexcept {
    return lhs.since_epoch_ < rhs.since_epoch_;
  }

  [[nodiscard]] friend constexpr bool operator>(const instant &lhs, const instant &rhs) noexcept { return rhs < lhs; }

  [[nodiscard]] friend constexpr bool operator<=(const instant &lhs, const instant &rhs) noexcept {
    return !(rhs < lhs);
  }

  [[nodiscard]] friend constexpr bool operator>=(const instant &lhs, const instant &rhs) noexcept {
    return !(lhs < rhs);
  }

private:
  explicit constexpr instant(duration since_epoch) noexcept : since_epoch_(since_epoch) {}

  duration since_epoch_{};
};

} // namespace reloco
