// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file container_ref_std.hpp
 * @brief `container_ref_traits` adapters for `std::vector`/`std::map`.
 *
 * Kept in a separate header from `container_ref.hpp` on purpose: these
 * adapters wrap standard containers whose insertion operations allocate
 * and may throw (`std::bad_alloc`, or anything thrown by `T`/`Key`/`Value`'s
 * copy/move constructors), which the rest of reloco deliberately avoids
 * pulling in. Only a user who explicitly `#include`s this header opts into
 * that behavior; `container_ref.hpp` alone never does.
 *
 * Every adapter function here wraps the underlying standard-library call in
 * `try`/`catch (...)`, converting any thrown exception into
 * `unexpected(error::allocation_failed)` so it never escapes past this
 * header's `noexcept` trait functions.
 */

#include "container_ref.hpp"
#include "error.hpp"
#include "expected.hpp"

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace reloco {

/**
 * @brief Adapts `std::vector<T>` as a sequence container for
 * @ref mutable_container_ref.
 *
 * `try_push_front`/`try_insert_at` fall back to `std::vector::insert`,
 * which is O(n) (`std::vector` has no dedicated front-insertion); this is
 * still correct, just not the fastest possible choice of container for
 * frequent front-insertion.
 */
template <typename T> struct container_ref_traits<std::vector<T>> {
  using element_type = T;
  static constexpr bool is_associative = false;

  static std::size_t size(const std::vector<T> &c) noexcept { return c.size(); }
  static bool empty(const std::vector<T> &c) noexcept { return c.empty(); }
  static void clear(std::vector<T> &c) noexcept { c.clear(); }
  static T &at(std::vector<T> &c, std::size_t index) noexcept { return c[index]; }

  static result<void> try_push_back(std::vector<T> &c, T value) noexcept {
    try {
      c.push_back(std::move(value));
      return {};
    } catch (...) {
      return unexpected(error::allocation_failed);
    }
  }

  static result<void> try_push_front(std::vector<T> &c, T value) noexcept {
    try {
      c.insert(c.begin(), std::move(value));
      return {};
    } catch (...) {
      return unexpected(error::allocation_failed);
    }
  }

  static result<void> try_insert_at(std::vector<T> &c, std::size_t index, T value) noexcept {
    if (index > c.size())
      return unexpected(error::out_of_bounds);
    try {
      c.insert(c.begin() + static_cast<typename std::vector<T>::difference_type>(index), std::move(value));
      return {};
    } catch (...) {
      return unexpected(error::allocation_failed);
    }
  }

  static result<void> try_erase_at(std::vector<T> &c, std::size_t index) noexcept {
    if (index >= c.size())
      return unexpected(error::out_of_bounds);
    c.erase(c.begin() + static_cast<typename std::vector<T>::difference_type>(index));
    return {};
  }
};

/**
 * @brief Adapts `std::map<Key, Value>` as an associative container for
 * @ref mutable_container_ref.
 *
 * `try_insert_at` never overwrites an existing key: it fails with
 * `error::already_exists` instead, matching "insert", not "insert-or-
 * assign", semantics.
 */
template <typename Key, typename Value> struct container_ref_traits<std::map<Key, Value>> {
  using element_type = Value;
  using key_type = Key;
  static constexpr bool is_associative = true;

  static std::size_t size(const std::map<Key, Value> &c) noexcept { return c.size(); }
  static bool empty(const std::map<Key, Value> &c) noexcept { return c.empty(); }
  static void clear(std::map<Key, Value> &c) noexcept { c.clear(); }

  static Value *find(std::map<Key, Value> &c, const Key &key) noexcept {
    auto it = c.find(key);
    return it == c.end() ? nullptr : &it->second;
  }

  static void for_each(std::map<Key, Value> &c, void *visitor_ctx,
                        void (*visit)(void *, const Key &, Value &) noexcept) noexcept {
    for (auto &[key, value] : c)
      visit(visitor_ctx, key, value);
  }

  static result<void> try_insert_at(std::map<Key, Value> &c, Key key, Value value) noexcept {
    try {
      auto [it, inserted] = c.emplace(std::move(key), std::move(value));
      (void)it;
      if (!inserted)
        return unexpected(error::already_exists);
      return {};
    } catch (...) {
      return unexpected(error::allocation_failed);
    }
  }

  static result<void> try_erase(std::map<Key, Value> &c, const Key &key) noexcept {
    if (c.erase(key) == 0)
      return unexpected(error::not_found);
    return {};
  }
};

} // namespace reloco
