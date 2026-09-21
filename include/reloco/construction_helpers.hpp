// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file construction_helpers.hpp
 * @brief `reloco::construction_helpers`, a compile-time dispatcher that
 * picks the most efficient way to construct or clone a type, based on
 * which of the `try_create`/`try_allocate`/`try_construct`/`try_clone`/
 * `try_clone_at` functions it implements (see `concepts.hpp` and
 * `docs/fallible-construction.md`).
 *
 * Ported from `reloco_legacy/include/reloco/construction_helpers.hpp`. Only
 * one thing differs from the legacy shape: legacy took a
 * `fallible_allocator &`; these take a `reloco::allocator_ref` (see
 * `allocator.hpp`) by value instead. Every function still returns a single,
 * fixed `reloco::result<T>` (or `result<void>`), exactly like legacy --
 * `concepts.hpp`'s `has_try_*_v` traits only recognize a `try_*` operation
 * that itself returns `reloco::result<...>`, so every tier already agrees
 * on the same error type and no per-instantiation deduction is needed.
 *
 * `try_construct` and `try_clone_at` placement-new directly into
 * caller-owned, uninitialized storage and manually destroy partially
 * constructed shells on failure -- raw memory management with no
 * bounds-tracked alternative, exactly like `allocator_ref`'s own operations
 * (see `docs/extending.md`). Like `allocator_ref`, both are marked
 * `RELOCO_UNSAFE_BUFFER_USAGE` and must be called from inside a
 * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE` block.
 * `try_allocate` and `try_clone` only ever return a newly constructed value
 * (never write through a caller-supplied pointer) and are not gated.
 */

#include "concepts.hpp"
#include "error.hpp"

#include <new>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

struct construction_helpers {
  /**
   * @brief High-level in-place constructor dispatcher for caller-owned
   * memory.
   *
   * Resolves the most efficient construction strategy at compile time:
   *
   * 1. **In-place fallible** (`has_try_construct_v<T, Args...>`):
   *    placement-news a `noexcept`-default-constructed shell and calls
   *    `storage->try_construct(args...)`. Zero-copy; the most efficient
   *    path.
   * 2. **Factory fallible, explicit allocator**
   *    (`has_try_allocate_v<T, Args...>`): calls `T::try_allocate(alloc,
   *    args...)` and move-constructs the result into `storage`.
   * 3. **Factory fallible, default allocator**
   *    (`has_try_create_v<T, Args...>`): calls `T::try_create(args...)` and
   *    move-constructs the result into `storage`.
   * 4. **Standard nothrow**: falls back to plain placement-new with the
   *    given arguments; `T` must be `std::is_nothrow_constructible_v<T,
   *    Args...>`.
   *
   * @note If any fallible tier fails, `storage` is left destroyed (the
   * shell's destructor has already run) rather than partially initialized.
   *
   * @param alloc The allocator to use for tier 2.
   * @param storage Uninitialized memory of size >= `sizeof(T)`.
   * @param args Arguments forwarded to whichever tier is selected.
   */
  template <typename T, typename... Args>
  RELOCO_UNSAFE_BUFFER_USAGE static result<void> try_construct(allocator_ref alloc, T *storage,
                                                               Args &&...args) noexcept {
    if constexpr (has_try_construct_v<T, Args...>) {
      static_assert(std::is_nothrow_default_constructible_v<T>,
                    "Two-phase construction requires a noexcept default constructor for the shell.");
      new (storage) T();
      auto res = storage->try_construct(std::forward<Args>(args)...);
      if (!res)
        storage->~T();
      return res;
    } else if constexpr (has_try_allocate_v<T, Args...>) {
      auto res = T::try_allocate(alloc, std::forward<Args>(args)...);
      if (!res)
        return unexpected(res.error());
      static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
      new (storage) T(std::move(*res));
      return {};
    } else if constexpr (has_try_create_v<T, Args...>) {
      auto res = T::try_create(std::forward<Args>(args)...);
      if (!res)
        return unexpected(res.error());
      static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
      new (storage) T(std::move(*res));
      return {};
    } else {
      static_assert(std::is_nothrow_constructible_v<T, Args...>,
                    "Type must be either fallible via try_construct, try_allocate, try_create, or nothrow "
                    "constructible.");
      new (storage) T(std::forward<Args>(args)...);
      return {};
    }
  }

  /**
   * @brief High-level factory that produces a value of type `T` fallibly.
   *
   * Resolves the best creation strategy at compile time, prioritizing
   * explicit allocation, then default creation, then two-phase stack
   * construction, and finally standard nothrow construction.
   *
   * @param alloc The allocator to provide for tiers that support
   * `has_try_allocate_v`.
   */
  template <typename T, typename... Args> static result<T> try_allocate(allocator_ref alloc, Args &&...args) noexcept {
    if constexpr (has_try_allocate_v<T, Args...>) {
      return T::try_allocate(alloc, std::forward<Args>(args)...);
    } else if constexpr (has_try_create_v<T, Args...>) {
      return T::try_create(std::forward<Args>(args)...);
    } else if constexpr (has_try_construct_v<T, Args...>) {
      static_assert(std::is_nothrow_default_constructible_v<T>,
                    "Two-phase construction requires a noexcept default constructor for the shell.");
      static_assert(std::is_nothrow_move_constructible_v<T>,
                    "reloco requires noexcept move-construction to return values safely.");
      T shell;
      auto res = shell.try_construct(std::forward<Args>(args)...);
      if (!res)
        return unexpected(res.error());
      return result<T>(std::move(shell));
    } else {
      static_assert(std::is_nothrow_constructible_v<T, Args...>,
                    "Type must be either fallible (try_construct/allocate/create) or standard nothrow "
                    "constructible.");
      return result<T>(T(std::forward<Args>(args)...));
    }
  }

  /**
   * @brief Performs a fallible deep-copy, resolving the best strategy.
   *
   * 1. **`try_clone(alloc)`** (`has_try_clone_allocator_aware_v<T>`):
   *    custom deep-copy with an explicit allocator.
   * 2. **`try_clone()`** (`has_try_clone_self_contained_v<T>`): custom,
   *    self-contained deep-copy.
   * 3. **`try_allocate(alloc, source)`**
   *    (`has_try_allocate_v<T, const T &>`): treats cloning as a new
   *    allocation from `source`.
   * 4. **`try_create(source)`** (`has_try_create_v<T, const T &>`): treats
   *    cloning as a new default-allocator creation from `source`.
   * 5. **Nothrow copy**: standard copy-construction; `T` must be
   *    `std::is_nothrow_copy_constructible_v<T>`.
   */
  template <typename T> static result<T> try_clone(allocator_ref alloc, const T &source) noexcept {
    if constexpr (has_try_clone_allocator_aware_v<T>) {
      return source.try_clone(alloc);
    } else if constexpr (has_try_clone_self_contained_v<T>) {
      return source.try_clone();
    } else if constexpr (has_try_allocate_v<T, const T &>) {
      return T::try_allocate(alloc, source);
    } else if constexpr (has_try_create_v<T, const T &>) {
      return T::try_create(source);
    } else {
      static_assert(std::is_nothrow_copy_constructible_v<T>,
                    "Type must implement try_clone or be nothrow copy constructible.");
      return result<T>(T(source));
    }
  }

  /**
   * @brief Performs a fallible deep-copy directly into uninitialized
   * storage.
   *
   * 1. **`T::try_clone_at(alloc, storage, source)`**
   *    (`has_try_clone_at_v<T>`): direct construction from `source` into
   *    `storage`.
   * 2. Otherwise, falls back to `try_clone` and move-constructs the result
   *    into `storage`.
   */
  template <typename T>
  RELOCO_UNSAFE_BUFFER_USAGE static result<void> try_clone_at(allocator_ref alloc, T *storage,
                                                              const T &source) noexcept {
    if constexpr (has_try_clone_at_v<T>) {
      return T::try_clone_at(alloc, storage, source);
    } else {
      static_assert(std::is_nothrow_move_constructible_v<T>,
                    "reloco requires noexcept move-construction for clone fallbacks.");
      auto res = try_clone<T>(alloc, source);
      if (!res)
        return unexpected(res.error());
      new (storage) T(std::move(*res));
      return {};
    }
  }
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
