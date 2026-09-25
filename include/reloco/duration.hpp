// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file duration.hpp
 * @brief `reloco::duration`, a Rust `std::time::Duration`-like,
 * integer-only time span, plus `duration_cast<T>`/`duration_converter<T>`
 * for converting it to a platform time type (`struct timespec`, `struct
 * timeval`, or a kernel-specific type like FreeBSD's `sbintime_t`).
 *
 * `std::chrono::duration<Rep, Period>` is deliberately not used for this,
 * for two reasons:
 *
 * - Some `<chrono>` implementations compute a cross-`Period` conversion
 *   (`duration_cast`, or simply constructing one duration from another
 *   with a different `Period`) through floating-point intermediate
 *   arithmetic when the ratio between the two periods is not exact --
 *   unusable in kernel/freestanding builds with no FPU, or with FPU use
 *   disabled/restricted (e.g. Linux kernel code, which never touches the
 *   FPU state on ordinary paths). `reloco::duration` stores a plain
 *   `(seconds: std::uint64_t, subsec_nanoseconds: std::uint32_t)` pair --
 *   the same representation Rust's own `Duration` uses -- and every
 *   conversion (`from_millis`, `as_micros`, ...) is plain integer
 *   multiply/divide/modulo, never floating point.
 * - `<chrono>` itself may not be available at all on a freestanding
 *   target; `reloco::duration` only needs `<cstdint>`.
 *
 * `duration_cast<T>(d)` converts to a platform/target time type `T` via
 * the `duration_converter<T>` customization point (the same shape as
 * `allocator_traits<Tag>`/`is_send<T>`/`alignment_of<T>`): specialize
 * `duration_converter<T>` with a `static T convert(duration) noexcept`
 * for any `T` you need. reloco itself provides built-in specializations
 * for `struct timespec` (guarded on `<time.h>`'s availability) and
 * `struct timeval` (guarded on `<sys/time.h>`'s availability) below. A
 * kernel target can add its own specialization for a fixed-point type
 * like FreeBSD's `sbintime_t` (a 64-bit `32.32` fixed-point seconds
 * count) in its own header:
 *
 * @code
 * template <> struct reloco::duration_converter<sbintime_t> {
 *   static constexpr sbintime_t convert(reloco::duration d) noexcept {
 *     return (static_cast<sbintime_t>(d.as_secs()) << 32) |
 *            static_cast<sbintime_t>((static_cast<std::uint64_t>(d.subsec_nanos()) << 32) / 1'000'000'000ULL);
 *   }
 * };
 * @endcode
 */

#include "detail/compat.hpp"

#include <cstdint>

#if RELOCO_HAS_INCLUDE(<time.h>)
#include <time.h>
#define RELOCO_DETAIL_DURATION_HAS_TIMESPEC 1
#else
#define RELOCO_DETAIL_DURATION_HAS_TIMESPEC 0
#endif

#if RELOCO_HAS_INCLUDE(<sys/time.h>)
#include <sys/time.h>
#define RELOCO_DETAIL_DURATION_HAS_TIMEVAL 1
#else
#define RELOCO_DETAIL_DURATION_HAS_TIMEVAL 0
#endif

namespace reloco {

/**
 * @brief An integer-only, non-negative time span, matching Rust's
 * `std::time::Duration`. See the file-level documentation above for why
 * `std::chrono::duration` is not used instead.
 */
class duration {
public:
  static constexpr std::uint64_t nanos_per_sec = 1'000'000'000ULL;

  constexpr duration() noexcept = default;

  [[nodiscard]] static constexpr duration from_secs(std::uint64_t secs) noexcept { return duration(secs, 0); }

  [[nodiscard]] static constexpr duration from_millis(std::uint64_t millis) noexcept {
    return duration(millis / 1'000ULL, static_cast<std::uint32_t>((millis % 1'000ULL) * 1'000'000ULL));
  }

  [[nodiscard]] static constexpr duration from_micros(std::uint64_t micros) noexcept {
    return duration(micros / 1'000'000ULL, static_cast<std::uint32_t>((micros % 1'000'000ULL) * 1'000ULL));
  }

  [[nodiscard]] static constexpr duration from_nanos(std::uint64_t nanos) noexcept {
    return duration(nanos / nanos_per_sec, static_cast<std::uint32_t>(nanos % nanos_per_sec));
  }

  [[nodiscard]] constexpr std::uint64_t as_secs() const noexcept { return secs_; }

  /** @brief The sub-second remainder, in nanoseconds: always `< 1'000'000'000`. */
  [[nodiscard]] constexpr std::uint32_t subsec_nanos() const noexcept { return nanos_; }

  [[nodiscard]] constexpr std::uint32_t subsec_micros() const noexcept { return nanos_ / 1'000U; }

  [[nodiscard]] constexpr std::uint32_t subsec_millis() const noexcept { return nanos_ / 1'000'000U; }

  [[nodiscard]] constexpr bool is_zero() const noexcept { return secs_ == 0 && nanos_ == 0; }

  [[nodiscard]] constexpr std::uint64_t as_millis() const noexcept {
    return secs_ * 1'000ULL + static_cast<std::uint64_t>(nanos_) / 1'000'000ULL;
  }

  [[nodiscard]] constexpr std::uint64_t as_micros() const noexcept {
    return secs_ * 1'000'000ULL + static_cast<std::uint64_t>(nanos_) / 1'000ULL;
  }

  /** @brief @warning Wraps (silently overflows) for a duration longer
   * than roughly 584 years -- reloco's fallible-first philosophy does not
   * extend here, matching Rust's own infallible (but wider, `u128`-based)
   * `Duration::as_nanos()`; reloco has no portable 128-bit integer to
   * match that width. Every reloco use of `duration` (`this_thread::
   * sleep_for`/`park_timeout`, ...) is a bounded, ordinary timeout, far
   * below this limit in practice. */
  [[nodiscard]] constexpr std::uint64_t as_nanos() const noexcept { return secs_ * nanos_per_sec + nanos_; }

  [[nodiscard]] friend constexpr bool operator==(const duration &lhs, const duration &rhs) noexcept {
    return lhs.secs_ == rhs.secs_ && lhs.nanos_ == rhs.nanos_;
  }

  [[nodiscard]] friend constexpr bool operator!=(const duration &lhs, const duration &rhs) noexcept {
    return !(lhs == rhs);
  }

  [[nodiscard]] friend constexpr bool operator<(const duration &lhs, const duration &rhs) noexcept {
    return lhs.secs_ != rhs.secs_ ? lhs.secs_ < rhs.secs_ : lhs.nanos_ < rhs.nanos_;
  }

  [[nodiscard]] friend constexpr bool operator>(const duration &lhs, const duration &rhs) noexcept { return rhs < lhs; }

  [[nodiscard]] friend constexpr bool operator<=(const duration &lhs, const duration &rhs) noexcept {
    return !(rhs < lhs);
  }

  [[nodiscard]] friend constexpr bool operator>=(const duration &lhs, const duration &rhs) noexcept {
    return !(lhs < rhs);
  }

  [[nodiscard]] friend constexpr duration operator+(const duration &lhs, const duration &rhs) noexcept {
    std::uint64_t secs = lhs.secs_ + rhs.secs_;
    std::uint32_t nanos = lhs.nanos_ + rhs.nanos_;
    if (nanos >= static_cast<std::uint32_t>(nanos_per_sec)) {
      nanos -= static_cast<std::uint32_t>(nanos_per_sec);
      ++secs;
    }
    return duration(secs, nanos);
  }

private:
  constexpr duration(std::uint64_t secs, std::uint32_t nanos) noexcept : secs_(secs), nanos_(nanos) {}

  std::uint64_t secs_ = 0;
  std::uint32_t nanos_ = 0;
};

/**
 * @brief Customization point converting a `duration` to a platform/target
 * time type `T`. No generic definition -- specialize with a
 * `static constexpr T convert(duration) noexcept` for any `T` you need
 * (see the file-level documentation above). `duration_cast<T>(d)` is the
 * public entry point.
 */
template <typename T> struct duration_converter;

/**
 * @brief Converts @p d to `T` via `duration_converter<T>::convert`. See
 * the file-level documentation above.
 */
template <typename T> [[nodiscard]] constexpr T duration_cast(duration d) noexcept {
  return duration_converter<T>::convert(d);
}

#if RELOCO_DETAIL_DURATION_HAS_TIMESPEC

/** @brief Built-in `duration` -> `struct timespec` conversion. */
template <> struct duration_converter<struct timespec> {
  [[nodiscard]] static constexpr struct timespec convert(duration d) noexcept {
    struct timespec ts {};
    ts.tv_sec = static_cast<decltype(ts.tv_sec)>(d.as_secs());
    ts.tv_nsec = static_cast<decltype(ts.tv_nsec)>(d.subsec_nanos());
    return ts;
  }
};

#endif // RELOCO_DETAIL_DURATION_HAS_TIMESPEC

#if RELOCO_DETAIL_DURATION_HAS_TIMEVAL

/** @brief Built-in `duration` -> `struct timeval` conversion (microsecond
 * resolution -- a sub-microsecond remainder is truncated, matching
 * `timeval`'s own resolution limit). */
template <> struct duration_converter<struct timeval> {
  [[nodiscard]] static constexpr struct timeval convert(duration d) noexcept {
    struct timeval tv {};
    tv.tv_sec = static_cast<decltype(tv.tv_sec)>(d.as_secs());
    tv.tv_usec = static_cast<decltype(tv.tv_usec)>(d.subsec_micros());
    return tv;
  }
};

#endif // RELOCO_DETAIL_DURATION_HAS_TIMEVAL

} // namespace reloco
