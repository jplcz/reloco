// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file lock_location_traits.hpp
 * @brief SFINAE detection traits shared by `unique_lock.hpp`/
 * `shared_lock.hpp`: whether a wrapped `MutexT`/`SharedMutexT` provides a
 * `call_location_ref`-aware overload of `lock()`/`try_lock()`/`unlock()`
 * (exclusive) or `lock_shared()`/`try_lock_shared()`/`unlock_shared()`
 * (shared) alongside its plain, location-less one.
 *
 * Most `reloco` mutex backends (`mutex`, `recursive_mutex`,
 * `shared_mutex`, a custom `RELOCO_MUTEX_BACKEND_CUSTOM`/
 * `RELOCO_SPIN_LOCK_BACKEND_CUSTOM` port, ...) only ever provide the
 * plain overload -- `unique_lock<MutexT>`/`shared_lock<SharedMutexT>`
 * must keep working against exactly those, unchanged, which is why this
 * is detected rather than required. A mutex type that *does* want its
 * acquisitions/releases attributed to a caller location (e.g. for a
 * contention/deadlock diagnostic log) adds the `call_location_ref`
 * overload itself; `unique_lock`/`shared_lock` then forward whatever
 * location their own caller supplied into it automatically, with no
 * change needed at the `unique_lock`/`shared_lock` call site either way.
 */

#include "../call_location.hpp"

#include <type_traits>
#include <utility>

namespace reloco {
namespace detail {

template <typename T, typename = void> struct has_location_lock : std::false_type {};
template <typename T>
struct has_location_lock<T, std::void_t<decltype(std::declval<T &>().lock(std::declval<call_location_ref>()))>>
    : std::true_type {};

template <typename T, typename = void> struct has_location_try_lock : std::false_type {};
template <typename T>
struct has_location_try_lock<T, std::void_t<decltype(std::declval<T &>().try_lock(std::declval<call_location_ref>()))>>
    : std::true_type {};

template <typename T, typename = void> struct has_location_unlock : std::false_type {};
template <typename T>
struct has_location_unlock<T, std::void_t<decltype(std::declval<T &>().unlock(std::declval<call_location_ref>()))>>
    : std::true_type {};

template <typename T, typename = void> struct has_location_lock_shared : std::false_type {};
template <typename T>
struct has_location_lock_shared<
    T, std::void_t<decltype(std::declval<T &>().lock_shared(std::declval<call_location_ref>()))>> : std::true_type {};

template <typename T, typename = void> struct has_location_try_lock_shared : std::false_type {};
template <typename T>
struct has_location_try_lock_shared<
    T, std::void_t<decltype(std::declval<T &>().try_lock_shared(std::declval<call_location_ref>()))>>
    : std::true_type {};

template <typename T, typename = void> struct has_location_unlock_shared : std::false_type {};
template <typename T>
struct has_location_unlock_shared<
    T, std::void_t<decltype(std::declval<T &>().unlock_shared(std::declval<call_location_ref>()))>> : std::true_type {
};

} // namespace detail
} // namespace reloco
