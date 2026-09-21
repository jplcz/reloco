// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file concepts.hpp
 * @brief Compile-time detection traits (and, under C++20, matching
 * `concept`s) for reloco's fallible construction/cloning conventions.
 *
 * Ported from reloco_legacy's `concepts.hpp`. Two things differ from the
 * legacy shape, both to match reloco's own conventions:
 *
 * - Legacy checked for a single, fixed `result<T>` (`expected<T, error>`
 *   with one global `error` enum). reloco keeps exactly that requirement --
 *   every fallible operation, everywhere in reloco, returns
 *   `reloco::result<T>` (or `result<void>`); no per-feature error enum is
 *   permitted -- so these traits check for that fixed return type rather
 *   than accepting an arbitrary `E`.
 * - Legacy took a `fallible_allocator &` (a virtual base). reloco has no
 *   virtual allocator interface; these traits take a `reloco::allocator_ref`
 *   (see `allocator.hpp`) by value instead, matching every other consumer
 *   of the type-erased allocator handle.
 */

#include "allocator.hpp"
#include "error.hpp"
#include "expected.hpp"

#include <type_traits>
#include <utility>

namespace reloco {

namespace detail {

// Detects a `reloco::result<T>` (`expected<T, error>`) returned by an
// operation, including the `result<void>` specialization (T == void).
template <typename T, typename Actual> inline constexpr bool is_result_of_v = std::is_same_v<Actual, result<T>>;

template <typename Void, typename T, typename... Args> struct has_try_create_impl : std::false_type {};

template <typename T, typename... Args>
struct has_try_create_impl<std::void_t<decltype(T::try_create(std::declval<Args>()...))>, T, Args...>
    : std::bool_constant<is_result_of_v<T, decltype(T::try_create(std::declval<Args>()...))>> {};

template <typename Void, typename T, typename... Args> struct has_try_allocate_impl : std::false_type {};

template <typename T, typename... Args>
struct has_try_allocate_impl<
    std::void_t<decltype(T::try_allocate(std::declval<allocator_ref>(), std::declval<Args>()...))>, T, Args...>
    : std::bool_constant<
          is_result_of_v<T, decltype(T::try_allocate(std::declval<allocator_ref>(), std::declval<Args>()...))>> {};

template <typename Void, typename T, typename... Args> struct has_try_construct_impl : std::false_type {};

template <typename T, typename... Args>
struct has_try_construct_impl<std::void_t<decltype(std::declval<T *>()->try_construct(std::declval<Args>()...))>, T,
                              Args...>
    : std::bool_constant<is_result_of_v<void, decltype(std::declval<T *>()->try_construct(std::declval<Args>()...))>> {
};

template <typename T, typename = void> struct has_try_clone_allocator_aware_impl : std::false_type {};

template <typename T>
struct has_try_clone_allocator_aware_impl<
    T, std::void_t<decltype(std::declval<const T &>().try_clone(std::declval<allocator_ref>()))>>
    : std::bool_constant<
          is_result_of_v<T, decltype(std::declval<const T &>().try_clone(std::declval<allocator_ref>()))>> {};

template <typename T, typename = void> struct has_try_clone_self_contained_impl : std::false_type {};

template <typename T>
struct has_try_clone_self_contained_impl<T, std::void_t<decltype(std::declval<const T &>().try_clone())>>
    : std::bool_constant<is_result_of_v<T, decltype(std::declval<const T &>().try_clone())>> {};

template <typename T, typename = void> struct has_try_clone_at_impl : std::false_type {};

template <typename T>
struct has_try_clone_at_impl<T, std::void_t<decltype(T::try_clone_at(std::declval<allocator_ref>(), std::declval<T *>(),
                                                                     std::declval<const T &>()))>>
    : std::bool_constant<
          is_result_of_v<void, decltype(T::try_clone_at(std::declval<allocator_ref>(), std::declval<T *>(),
                                                        std::declval<const T &>()))>> {};

} // namespace detail

/**
 * @brief Detects a static factory method using the process-wide default
 * allocator.
 *
 * `has_try_create_v<T, Args...>` marks types that can be instantiated
 * without an explicit allocator, typically by delegating to
 * `reloco::default_allocator()`. Satisfied by any
 * `T::try_create(Args...) -> reloco::result<T>`.
 */
template <typename T, typename... Args>
inline constexpr bool has_try_create_v = detail::has_try_create_impl<void, T, Args...>::value;

/**
 * @brief Detects a static factory method with an explicit allocator.
 *
 * `has_try_allocate_v<T, Args...>` is the preferred pattern where memory
 * residency must be strictly controlled by the caller. Satisfied by any
 * `T::try_allocate(reloco::allocator_ref, Args...) -> reloco::result<T>`.
 */
template <typename T, typename... Args>
inline constexpr bool has_try_allocate_v = detail::has_try_allocate_impl<void, T, Args...>::value;

/**
 * @brief Detects two-phase, in-place fallible construction.
 *
 * `has_try_construct_v<T, Args...>` enables zero-copy initialization: the
 * object is first placement-newed into a "shell" state (requiring a
 * noexcept default constructor), then this method performs fallible logic
 * (e.g., resource acquisition). Satisfied by any
 * `storage->try_construct(Args...) -> reloco::result<void>`, where
 * `storage` is a `T *`.
 *
 * @note If try_construct fails, the caller is responsible for invoking the
 * destructor on the shell object before reclaiming the raw memory.
 */
template <typename T, typename... Args>
inline constexpr bool has_try_construct_v = detail::has_try_construct_impl<void, T, Args...>::value;

/**
 * @brief Detects a fallible clone operation with an explicit allocator.
 *
 * Satisfied by `source.try_clone(reloco::allocator_ref) ->
 * reloco::result<T>` — the allocator-aware shape of `has_try_clone_v`,
 * useful on its own when a caller needs to distinguish the two shapes (e.g.
 * `construction_helpers`).
 */
template <typename T>
inline constexpr bool has_try_clone_allocator_aware_v = detail::has_try_clone_allocator_aware_impl<T>::value;

/**
 * @brief Detects a self-contained fallible clone operation.
 *
 * Satisfied by `source.try_clone() -> reloco::result<T>` — the
 * self-contained shape of `has_try_clone_v`.
 */
template <typename T>
inline constexpr bool has_try_clone_self_contained_v = detail::has_try_clone_self_contained_impl<T>::value;

/**
 * @brief Detects a fallible clone operation, allocator-aware or
 * self-contained.
 *
 * `has_try_clone_v<T>` is satisfied by either
 * `source.try_clone(reloco::allocator_ref) -> reloco::result<T>`
 * (allocator-aware, e.g. containers) or
 * `source.try_clone() -> reloco::result<T>` (self-contained, e.g.
 * simple objects).
 */
template <typename T>
inline constexpr bool has_try_clone_v = has_try_clone_allocator_aware_v<T> || has_try_clone_self_contained_v<T>;

/**
 * @brief Detects an optimized in-place clone.
 *
 * `has_try_clone_at_v<T>` is satisfied by
 * `T::try_clone_at(reloco::allocator_ref, T *storage, const T &source) ->
 * reloco::result<void>`: an allocator for nested resources, a raw pointer
 * to uninitialized destination storage, and a reference to the source
 * object to be cloned.
 */
template <typename T> inline constexpr bool has_try_clone_at_v = detail::has_try_clone_at_impl<T>::value;

#if RELOCO_CXX20

template <typename T, typename... Args>
concept has_try_create = has_try_create_v<T, Args...>;

template <typename T, typename... Args>
concept has_try_allocate = has_try_allocate_v<T, Args...>;

template <typename T, typename... Args>
concept has_try_construct = has_try_construct_v<T, Args...>;

template <typename T>
concept has_try_clone_allocator_aware = has_try_clone_allocator_aware_v<T>;

template <typename T>
concept has_try_clone_self_contained = has_try_clone_self_contained_v<T>;

template <typename T>
concept has_try_clone = has_try_clone_v<T>;

template <typename T>
concept has_try_clone_at = has_try_clone_at_v<T>;

#endif // RELOCO_CXX20

} // namespace reloco
