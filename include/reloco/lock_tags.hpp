// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file lock_tags.hpp @brief Disambiguation tag types shared by
 * `unique_lock.hpp` and `shared_lock.hpp`, matching `std::defer_lock_t`/
 * `std::try_to_lock_t`/`std::adopt_lock_t` (and the `std::defer_lock`/
 * `std::try_to_lock`/`std::adopt_lock` constants) in name and meaning, but
 * declared independently in `reloco::` rather than reused from `<mutex>`
 * -- `unique_lock`/`shared_lock` themselves must not pull in `<mutex>` at
 * all (see those files), and these tags are the only piece of that
 * surface either one actually needs.
 */

namespace reloco {

/** @brief Tag requesting that a lock wrapper's constructor not acquire
 * the mutex at all, leaving it to a later explicit `lock()`/`try_lock()`
 * call. Matches `std::defer_lock_t`. */
struct defer_lock_t {
  explicit defer_lock_t() = default;
};

/** @brief Tag requesting that a lock wrapper's constructor attempt the
 * acquisition non-blockingly (as if via `try_lock()`) instead of
 * blocking. Matches `std::try_to_lock_t`. */
struct try_to_lock_t {
  explicit try_to_lock_t() = default;
};

/** @brief Tag asserting that the calling thread already holds the mutex
 * (e.g. acquired directly, or adopted from another lock wrapper via
 * `release()`), so the lock wrapper's constructor should record ownership
 * without calling `lock()`/`try_lock()` again. Matches `std::adopt_lock_t`.
 */
struct adopt_lock_t {
  explicit adopt_lock_t() = default;
};

inline constexpr defer_lock_t defer_lock{};
inline constexpr try_to_lock_t try_to_lock{};
inline constexpr adopt_lock_t adopt_lock{};

} // namespace reloco
