// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file inline_flat_set.hpp
 * @brief Sorted, unique-element, fixed-capacity set backed directly by
 * `inline_vector<T, Capacity>`.
 *
 * `inline_flat_set<T, Capacity, Compare>` is `flat_set<T, Compare>`'s
 * allocation-free counterpart: a thin derived class of
 * `detail::flat_container_base<inline_vector<T, Capacity>, Compare,
 * detail::identity_key_of>` (see `detail/flat_container_base.hpp` and
 * `inline_vector.hpp`). Since `inline_vector<T, Capacity>` has no
 * allocator-taking factory to forward to, the base's
 * `try_allocate`/`try_create`/`get_allocator` are all SFINAE'd away for
 * this `Storage`, leaving only a default constructor -- there is no
 * "explicit allocator" or "reserve a capacity" step, `Capacity` itself
 * *is* the reserved capacity. `try_insert` fails with
 * `error::capacity_exceeded` once `size() == Capacity`, exactly like
 * `inline_vector::try_push_back`/`try_insert_at`.
 *
 * This file only adds the re-wrapping of the base's `try_clone` results
 * back into `inline_flat_set` itself, plus a `flat_set`-upgrade path
 * (`try_to_flat_set`, mirroring `inline_vector::try_to_vector`) for callers
 * that reach the fixed `Capacity` but need to keep growing; everything
 * else is inherited as-is.
 */

#include "detail/flat_container_base.hpp"
#include "flat_set.hpp"
#include "inline_vector.hpp"

namespace reloco {

template <typename T, std::size_t Capacity, typename Compare = std::less<T>>
class RELOCO_OWNER inline_flat_set
    : public detail::flat_container_base<inline_vector<T, Capacity>, Compare, detail::identity_key_of> {
  using base = detail::flat_container_base<inline_vector<T, Capacity>, Compare, detail::identity_key_of>;

public:
  using base::base;

  /**
   * @brief Performs a deep copy of the set using a specific allocator for
   * any nested fallible-allocation `T` (`inline_flat_set` itself never
   * allocates).
   */
  [[nodiscard]] result<inline_flat_set> try_clone(allocator_ref alloc) const noexcept {
    auto res = base::try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    return inline_flat_set(std::move(*res));
  }

  /**
   * @brief Performs a deep copy using the process-wide default allocator
   * (see `default_allocator()`) for any nested fallible-allocation `T`.
   */
  [[nodiscard]] result<inline_flat_set> try_clone() const noexcept {
    auto res = base::try_clone();
    if (!res)
      return unexpected(res.error());
    return inline_flat_set(std::move(*res));
  }

  // ---- upgrading to a heap-backed flat_set<T, Compare> ----

  /**
   * @brief Clones every element into a newly heap-allocated `flat_set<T,
   * Compare>`, leaving `*this` untouched. Useful when the fixed `Capacity`
   * has been reached (or is about to be) but the caller still wants a
   * growable set.
   */
  [[nodiscard]] result<flat_set<T, Compare>> try_to_flat_set(allocator_ref alloc) const & noexcept {
    auto set_res = flat_set<T, Compare>::try_allocate(alloc, base::size());
    if (!set_res)
      return unexpected(set_res.error());
    flat_set<T, Compare> set = std::move(*set_res);
    bool failed = false;
    error first_error{};
    base::for_each([&](const T &elem) noexcept {
      if (failed)
        return;
      auto clone_res = construction_helpers::try_clone<T>(alloc, elem);
      if (!clone_res) {
        failed = true;
        first_error = clone_res.error();
        return;
      }
      auto ins_res = set.try_insert(std::move(*clone_res));
      if (!ins_res) {
        failed = true;
        first_error = ins_res.error();
      }
    });
    if (failed)
      return unexpected(first_error);
    return set;
  }

  /**
   * @brief Same as `try_to_flat_set(allocator_ref)`, using the
   * process-wide default allocator (see `default_allocator()`).
   */
  [[nodiscard]] result<flat_set<T, Compare>> try_to_flat_set() const & noexcept {
    return try_to_flat_set(default_allocator());
  }

  /**
   * @brief Moves every element out into a newly heap-allocated `flat_set<T,
   * Compare>`, consuming `*this` (which is left empty regardless of
   * success or failure).
   */
  [[nodiscard]] result<flat_set<T, Compare>> try_to_flat_set(allocator_ref alloc) && noexcept {
    auto set_res = flat_set<T, Compare>::try_allocate(alloc, base::size());
    if (!set_res) {
      base::clear();
      return unexpected(set_res.error());
    }
    flat_set<T, Compare> set = std::move(*set_res);
    bool failed = false;
    error first_error{};
    base::for_each([&](const T &elem) noexcept {
      if (failed)
        return;
      auto ins_res = set.try_insert(std::move(const_cast<T &>(elem)));
      if (!ins_res) {
        failed = true;
        first_error = ins_res.error();
      }
    });
    base::clear();
    if (failed)
      return unexpected(first_error);
    return set;
  }

  /**
   * @brief Same as `try_to_flat_set(allocator_ref) &&`, using the
   * process-wide default allocator (see `default_allocator()`).
   */
  [[nodiscard]] result<flat_set<T, Compare>> try_to_flat_set() && noexcept {
    return std::move(*this).try_to_flat_set(default_allocator());
  }

private:
  explicit inline_flat_set(base &&b) noexcept : base(std::move(b)) {}
};

/**
 * @brief Adapts `reloco::inline_flat_set<T, Capacity, Compare>` for
 * `mutable_container_ref`. Identical in shape to `flat_set`'s adapter (see
 * `flat_set.hpp`): `key_type`/`element_type` are both `T`.
 */
template <typename T, std::size_t Capacity, typename Compare>
struct container_ref_traits<inline_flat_set<T, Capacity, Compare>> {
  static constexpr bool is_associative = true;

  using element_type = T;
  using key_type = T;
  using container_type = inline_flat_set<T, Capacity, Compare>;

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
 * @brief Adapts `reloco::inline_flat_set<T, Capacity, Compare>` for the
 * collection views. Exposed strictly as a read-only collection
 * (`is_mutable = false`), same rationale as `flat_set`.
 */
template <typename T, std::size_t Capacity, typename Compare>
struct collection_view_traits<inline_flat_set<T, Capacity, Compare>> {
  using element_type = T;

  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = false;

  [[nodiscard]] static std::size_t size(const inline_flat_set<T, Capacity, Compare> &c) noexcept { return c.size(); }

  [[nodiscard]] static bool empty(const inline_flat_set<T, Capacity, Compare> &c) noexcept { return c.empty(); }

  [[nodiscard]] static const T &at(const inline_flat_set<T, Capacity, Compare> &c, std::size_t index) noexcept {
    RELOCO_ASSERT(index < c.size(), "Index out of bounds");
    return c.begin()[index];
  }

  [[nodiscard]] static const T *data(const inline_flat_set<T, Capacity, Compare> &c) noexcept { return c.begin(); }
};

/**
 * @brief `inline_flat_set<T, Capacity, Compare>` is trivially relocatable
 * exactly when both `T` and `Compare` are: it wraps an `inline_vector<T,
 * Capacity>`, whose own relocatability already depends on `T` (see
 * `inline_vector.hpp`).
 */
template <typename T, std::size_t Capacity, typename Compare>
struct is_trivially_relocatable<inline_flat_set<T, Capacity, Compare>>
    : std::bool_constant<is_trivially_relocatable_v<T> && is_trivially_relocatable_v<Compare>> {};

} // namespace reloco
