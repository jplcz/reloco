// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file sso_flat_set.hpp
 * @brief Sorted, unique-element set backed directly by `sso_vector<T,
 * InlineCapacity>`.
 *
 * `sso_flat_set<T, InlineCapacity, Compare>` is `flat_set<T, Compare>`'s
 * small-size-optimized counterpart, exactly the way `sso_vector<T,
 * InlineCapacity>` (see `sso_vector.hpp`) relates to `vector<T>`: a thin
 * derived class of `detail::flat_container_base<sso_vector<T,
 * InlineCapacity>, Compare, detail::identity_key_of>` (see
 * `detail/flat_container_base.hpp`). `sso_vector<T, InlineCapacity>`
 * already implements the exact same `try_allocate`/`try_create`/
 * `get_allocator`/`try_insert_at`/`try_erase_at`/`try_clone`/iteration
 * surface as `vector<T>`, so it plugs into `flat_container_base` with no
 * changes to the base at all -- the only difference from `flat_set<T,
 * Compare>` is which `Storage` is named. Up to `InlineCapacity` elements
 * stay inline with no allocation at all; insertion past that transparently
 * promotes to the heap, exactly like `sso_vector` itself, so -- unlike
 * `inline_flat_set<T, Capacity, Compare>` -- `try_insert` never fails with
 * `error::capacity_exceeded`.
 *
 * This file only adds the `sso_vector<T, InlineCapacity>`-specific factory
 * wrappers (`try_allocate`/`try_create`) and re-wraps the base's
 * `try_clone` results back into `sso_flat_set` itself.
 */

#include "detail/flat_container_base.hpp"
#include "sso_vector.hpp"

namespace reloco {

template <typename T, std::size_t InlineCapacity, typename Compare = std::less<T>>
class RELOCO_OWNER sso_flat_set
    : public detail::flat_container_base<sso_vector<T, InlineCapacity>, Compare, detail::identity_key_of> {
  using base = detail::flat_container_base<sso_vector<T, InlineCapacity>, Compare, detail::identity_key_of>;

public:
  using base::base;
  using typename base::size_type;

  [[nodiscard]] static result<sso_flat_set> try_allocate(allocator_ref alloc, size_type initial_cap = 0) noexcept {
    auto res = base::try_allocate(alloc, initial_cap);
    if (!res)
      return unexpected(res.error());
    return sso_flat_set(std::move(*res));
  }

  [[nodiscard]] static result<sso_flat_set> try_create(size_type initial_cap = 0) noexcept {
    return try_allocate(default_allocator(), initial_cap);
  }

  /**
   * @brief Performs a deep copy of the set using a specific allocator.
   */
  [[nodiscard]] result<sso_flat_set> try_clone(allocator_ref alloc) const noexcept {
    auto res = base::try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    return sso_flat_set(std::move(*res));
  }

  [[nodiscard]] result<sso_flat_set> try_clone() const noexcept {
    auto res = base::try_clone();
    if (!res)
      return unexpected(res.error());
    return sso_flat_set(std::move(*res));
  }

private:
  explicit sso_flat_set(base &&b) noexcept : base(std::move(b)) {}
};

/**
 * @brief Adapts `reloco::sso_flat_set<T, InlineCapacity, Compare>` for
 * `mutable_container_ref`. Identical in shape to `flat_set`'s adapter (see
 * `flat_set.hpp`): `key_type`/`element_type` are both `T`.
 */
template <typename T, std::size_t InlineCapacity, typename Compare>
struct container_ref_traits<sso_flat_set<T, InlineCapacity, Compare>> {
  static constexpr bool is_associative = true;

  using element_type = T;
  using key_type = T;
  using container_type = sso_flat_set<T, InlineCapacity, Compare>;

  static std::size_t size(const container_type &c) noexcept { return c.size(); }

  static bool empty(const container_type &c) noexcept { return c.empty(); }

  static void clear(container_type &c) noexcept { c.clear(); }

  static result<void> try_insert_at(container_type &c, key_type &&key, element_type &&value) noexcept {
    // For a set, key and value are the same. We ensure key uniqueness and
    // insert the value in sorted order.
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
 * @brief Adapts `reloco::sso_flat_set<T, InlineCapacity, Compare>` for the
 * collection views. Exposed strictly as a read-only collection
 * (`is_mutable = false`), same rationale as `flat_set`.
 */
template <typename T, std::size_t InlineCapacity, typename Compare>
struct collection_view_traits<sso_flat_set<T, InlineCapacity, Compare>> {
  using element_type = T;

  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = false;

  [[nodiscard]] static std::size_t size(const sso_flat_set<T, InlineCapacity, Compare> &c) noexcept { return c.size(); }

  [[nodiscard]] static bool empty(const sso_flat_set<T, InlineCapacity, Compare> &c) noexcept { return c.empty(); }

  [[nodiscard]] static const T &at(const sso_flat_set<T, InlineCapacity, Compare> &c, std::size_t index) noexcept {
    RELOCO_ASSERT(index < c.size(), "Index out of bounds");
    return c.begin()[index];
  }

  [[nodiscard]] static const T *data(const sso_flat_set<T, InlineCapacity, Compare> &c) noexcept { return c.begin(); }
};

/**
 * @brief `sso_flat_set<T, InlineCapacity, Compare>` is trivially
 * relocatable exactly when... never: it wraps an `sso_vector<T,
 * InlineCapacity>`, which is itself never trivially relocatable regardless
 * of `T` (see `sso_vector.hpp`), so `sso_flat_set` can't be either.
 */
template <typename T, std::size_t InlineCapacity, typename Compare>
struct is_trivially_relocatable<sso_flat_set<T, InlineCapacity, Compare>> : std::false_type {};

} // namespace reloco
