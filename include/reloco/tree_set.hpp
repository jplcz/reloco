// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file tree_set.hpp
 * @brief Unique-element set backed by a basic, unbalanced, allocator-owned
 * binary search tree.
 *
 * `tree_set<T, Compare>` is a thin derived class of `detail::tree_base<T,
 * Compare, detail::identity_key_of>` (see `detail/tree_base.hpp`): the base
 * already implements `try_insert`/`contains`/`try_find`/`try_remove`/
 * iteration/cloning, keyed by `identity_key_of` since a set's element *is*
 * its own key. This file only adds the factory wrappers
 * (`try_allocate`/`try_create`) and re-wraps the base's `try_clone` results
 * back into `tree_set` itself -- exactly like `flat_set` does over
 * `flat_container_base`.
 *
 * Unlike `flat_set<T, Compare>` (backed by a sorted `vector<T>`), `tree_set`
 * has no `capacity()`/`reserve()` concept and no random-access
 * `operator[]`/`data()`: every element lives in its own node-based
 * allocation (see `detail/node_base.hpp`), so there is nothing to
 * pre-reserve and no contiguous buffer to index into or point at. Insertion
 * never invalidates existing references/pointers to other elements (unlike
 * `flat_set`, where inserting can reallocate the whole backing `vector<T>`)
 * -- only `try_remove`ing the specific element you are pointing at does.
 */

#include "container_ref.hpp"
#include "default_allocator.hpp"
#include "detail/tree_base.hpp"
#include "relocatable.hpp"

namespace reloco {

template <typename T, typename Compare = std::less<T>>
class RELOCO_OWNER tree_set : public detail::tree_base<T, Compare, detail::identity_key_of> {
  using base = detail::tree_base<T, Compare, detail::identity_key_of>;

public:
  using base::base;
  using base::try_insert;
  using typename base::size_type;
  using typename base::value_type;

  [[nodiscard]] static result<tree_set> try_allocate(allocator_ref alloc) noexcept {
    auto res = base::try_allocate(alloc);
    if (!res)
      return unexpected(res.error());
    return tree_set(std::move(*res));
  }

  [[nodiscard]] static result<tree_set> try_create() noexcept { return try_allocate(default_allocator()); }

  /**
   * @brief Performs a deep copy of the set using a specific allocator.
   */
  [[nodiscard]] result<tree_set> try_clone(allocator_ref alloc) const noexcept {
    auto res = base::try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    return tree_set(std::move(*res));
  }

  [[nodiscard]] result<tree_set> try_clone() const noexcept {
    auto res = base::try_clone();
    if (!res)
      return unexpected(res.error());
    return tree_set(std::move(*res));
  }

private:
  explicit tree_set(base &&b) noexcept : base(std::move(b)) {}
};

/**
 * @brief Adapts `reloco::tree_set<T, Compare>` for `mutable_container_ref`.
 *
 * Configured as an associative container where `key_type` and
 * `element_type` are both `T`, exactly like `container_ref_traits<flat_set<
 * T, Compare>>`.
 */
template <typename T, typename Compare> struct container_ref_traits<tree_set<T, Compare>> {
  static constexpr bool is_associative = true;

  using element_type = T;
  using key_type = T;
  using container_type = tree_set<T, Compare>;

  static std::size_t size(const container_type &c) noexcept { return c.size(); }

  static bool empty(const container_type &c) noexcept { return c.empty(); }

  static void clear(container_type &c) noexcept { c.clear(); }

  static result<void> try_insert_at(container_type &c, key_type &&key, element_type &&value) noexcept {
    if (key != value) {
      return unexpected(error::invalid_argument);
    }
    auto res = c.try_insert(std::move(value));
    if (!res) {
      return unexpected(res.error());
    }
    return {};
  }

  static result<void> try_erase(container_type &c, const key_type &key) noexcept { return c.try_remove(key); }

  static element_type *find(container_type &c, const key_type &key) noexcept {
    auto found_res = c.try_find(key);
    if (!found_res) {
      return nullptr;
    }
    return const_cast<element_type *>(&found_res->get());
  }

  static void for_each(container_type &c, void *visitor_ctx,
                       void (*visit)(void *, const key_type &, element_type &) noexcept) noexcept {
    c.for_each([visitor_ctx, visit](const T &val) noexcept { visit(visitor_ctx, val, const_cast<T &>(val)); });
  }
};

/**
 * @brief `tree_set<T, Compare>` is trivially relocatable exactly when
 * `Compare` is: its own members (a node pointer, a size, an `allocator_ref`,
 * and `Compare` itself) never point into `*this`, so relocating its bytes
 * is safe regardless of `T` -- `T` values live in separately allocated
 * nodes that a byte-copy of the `tree_set` itself never touches.
 */
template <typename T, typename Compare>
struct is_trivially_relocatable<tree_set<T, Compare>> : is_trivially_relocatable<Compare> {};

} // namespace reloco
