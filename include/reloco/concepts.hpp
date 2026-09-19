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
 *   with one global `error` enum). reloco uses a scoped, per-feature error
 *   enum for every fallible type, so these traits instead accept *any*
 *   `reloco::expected<T, E>` return, whatever `E` the implementing type
 *   chooses.
 * - Legacy took a `fallible_allocator &` (a virtual base). reloco has no
 *   virtual allocator interface; these traits take a `reloco::allocator_ref`
 *   (see `allocator.hpp`) by value instead, matching every other consumer
 *   of the type-erased allocator handle.
 */

#include "allocator.hpp"
#include "expected.hpp"

#include <type_traits>
#include <utility>

namespace reloco {

namespace detail {

// Detects a `reloco::expected<T, E>` (for any E) returned by an operation,
// including the `expected<void, E>` specialization (T == void).
template <typename T, typename Expected>
struct is_expected_of : std::false_type {};

template <typename T, typename E>
struct is_expected_of<T, expected<T, E>> : std::true_type {};

template <typename T, typename Expected>
inline constexpr bool is_expected_of_v = is_expected_of<T, Expected>::value;

template <typename Void, typename T, typename... Args>
struct has_try_create_impl : std::false_type {};

template <typename T, typename... Args>
struct has_try_create_impl<std::void_t<decltype(T::try_create(std::declval<Args>()...))>, T, Args...>
    : std::bool_constant<is_expected_of_v<T, decltype(T::try_create(std::declval<Args>()...))>> {};

template <typename Void, typename T, typename... Args>
struct has_try_allocate_impl : std::false_type {};

template <typename T, typename... Args>
struct has_try_allocate_impl<
    std::void_t<decltype(T::try_allocate(std::declval<allocator_ref>(), std::declval<Args>()...))>, T, Args...>
    : std::bool_constant<is_expected_of_v<
          T, decltype(T::try_allocate(std::declval<allocator_ref>(), std::declval<Args>()...))>> {};

template <typename Void, typename T, typename... Args>
struct has_try_construct_impl : std::false_type {};

template <typename T, typename... Args>
struct has_try_construct_impl<
    std::void_t<decltype(std::declval<T *>()->try_construct(std::declval<Args>()...))>, T, Args...>
    : std::bool_constant<is_expected_of_v<
          void, decltype(std::declval<T *>()->try_construct(std::declval<Args>()...))>> {};

template <typename T, typename = void> struct has_try_clone_allocator_aware_impl : std::false_type {};

template <typename T>
struct has_try_clone_allocator_aware_impl<
    T, std::void_t<decltype(std::declval<const T &>().try_clone(std::declval<allocator_ref>()))>>
    : std::bool_constant<is_expected_of_v<
          T, decltype(std::declval<const T &>().try_clone(std::declval<allocator_ref>()))>> {};

template <typename T, typename = void> struct has_try_clone_self_contained_impl : std::false_type {};

template <typename T>
struct has_try_clone_self_contained_impl<T, std::void_t<decltype(std::declval<const T &>().try_clone())>>
    : std::bool_constant<is_expected_of_v<T, decltype(std::declval<const T &>().try_clone())>> {};

template <typename T, typename = void> struct has_try_clone_at_impl : std::false_type {};

template <typename T>
struct has_try_clone_at_impl<
    T, std::void_t<decltype(T::try_clone_at(std::declval<allocator_ref>(), std::declval<T *>(),
                                            std::declval<const T &>()))>>
    : std::bool_constant<is_expected_of_v<
          void, decltype(T::try_clone_at(std::declval<allocator_ref>(), std::declval<T *>(),
                                          std::declval<const T &>()))>> {};

} // namespace detail

/**
 * @brief Detects a static factory method using the process-wide default
 * allocator.
 *
 * `has_try_create_v<T, Args...>` marks types that can be instantiated
 * without an explicit allocator, typically by delegating to
 * `reloco::default_allocator()`. Satisfied by any
 * `T::try_create(Args...) -> reloco::expected<T, E>`, for any `E`.
 */
template <typename T, typename... Args>
inline constexpr bool has_try_create_v = detail::has_try_create_impl<void, T, Args...>::value;

/**
 * @brief Detects a static factory method with an explicit allocator.
 *
 * `has_try_allocate_v<T, Args...>` is the preferred pattern where memory
 * residency must be strictly controlled by the caller. Satisfied by any
 * `T::try_allocate(reloco::allocator_ref, Args...) -> reloco::expected<T, E>`,
 * for any `E`.
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
 * `storage->try_construct(Args...) -> reloco::expected<void, E>`, for any
 * `E`, where `storage` is a `T *`.
 *
 * @note If try_construct fails, the caller is responsible for invoking the
 * destructor on the shell object before reclaiming the raw memory.
 */
template <typename T, typename... Args>
inline constexpr bool has_try_construct_v = detail::has_try_construct_impl<void, T, Args...>::value;

/**
 * @brief Detects a fallible clone operation, allocator-aware or
 * self-contained.
 *
 * `has_try_clone_v<T>` is satisfied by either
 * `source.try_clone(reloco::allocator_ref) -> reloco::expected<T, E>`
 * (allocator-aware, e.g. containers) or
 * `source.try_clone() -> reloco::expected<T, E>` (self-contained, e.g.
 * simple objects), for any `E`.
 */
template <typename T>
inline constexpr bool has_try_clone_v =
    detail::has_try_clone_allocator_aware_impl<T>::value || detail::has_try_clone_self_contained_impl<T>::value;

/**
 * @brief Detects an optimized in-place clone.
 *
 * `has_try_clone_at_v<T>` is satisfied by
 * `T::try_clone_at(reloco::allocator_ref, T *storage, const T &source) ->
 * reloco::expected<void, E>`, for any `E`: an allocator for nested
 * resources, a raw pointer to uninitialized destination storage, and a
 * reference to the source object to be cloned.
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
concept has_try_clone = has_try_clone_v<T>;

template <typename T>
concept has_try_clone_at = has_try_clone_at_v<T>;

#endif // RELOCO_CXX20

} // namespace reloco
