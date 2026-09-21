// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file flat_set.hpp
 * @brief Sorted, unique-element set backed directly by `vector<T>`.
 *
 * `flat_set<T, Compare>` is a thin derived class of
 * `detail::flat_container_base<vector<T>, Compare, detail::identity_key_of>`
 * (see `detail/flat_container_base.hpp`): the base already implements
 * `try_insert`/`contains`/`try_find`/`try_remove`/iteration/cloning against
 * any `Storage` that looks like `vector<T>`/`inline_vector<T, Capacity>`,
 * keyed by `identity_key_of` since a set's element *is* its own key. This
 * file only adds the `vector<T>`-specific factory wrappers
 * (`try_allocate`/`try_create`) and re-wraps the base's `try_clone`
 * results back into `flat_set` itself.
 */

#include "detail/flat_container_base.hpp"
#include "vector.hpp"

namespace reloco {

template <typename T, typename Compare = std::less<T>>
class RELOCO_OWNER flat_set : public detail::flat_container_base<vector<T>, Compare, detail::identity_key_of> {
  using base = detail::flat_container_base<vector<T>, Compare, detail::identity_key_of>;

public:
  using base::base;
  using typename base::size_type;

  [[nodiscard]] static result<flat_set> try_allocate(allocator_ref alloc, size_type initial_cap = 0) noexcept {
    auto res = base::try_allocate(alloc, initial_cap);
    if (!res)
      return unexpected(res.error());
    return flat_set(std::move(*res));
  }

  [[nodiscard]] static result<flat_set> try_create(size_type initial_cap = 0) noexcept {
    return try_allocate(default_allocator(), initial_cap);
  }

  /**
   * @brief Performs a deep copy of the set using a specific allocator.
   */
  [[nodiscard]] result<flat_set> try_clone(allocator_ref alloc) const noexcept {
    auto res = base::try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    return flat_set(std::move(*res));
  }

  [[nodiscard]] result<flat_set> try_clone() const noexcept {
    auto res = base::try_clone();
    if (!res)
      return unexpected(res.error());
    return flat_set(std::move(*res));
  }

private:
  explicit flat_set(base &&b) noexcept : base(std::move(b)) {}
};

/**
 * @brief Adapts `reloco::flat_set<T, Compare>` for `mutable_container_ref`.
 *
 * Configured as an associative container where `key_type` and `element_type`
 * are both `T`. Insertion uses `try_insert` (which preserves sorted order
 * and handles uniqueness), erasure looks up and removes by key, and `find`
 * returns a non-const pointer to the stored element for mutation (or
 * traversal via `for_each`).
 */
template <typename T, typename Compare> struct container_ref_traits<flat_set<T, Compare>> {
  static constexpr bool is_associative = true;

  using element_type = T;
  using key_type = T;
  using container_type = flat_set<T, Compare>;

  static std::size_t size(const container_type &c) noexcept { return c.size(); }

  static bool empty(const container_type &c) noexcept { return c.empty(); }

  static void clear(container_type &c) noexcept { c.clear(); }

  static result<void> try_insert_at(container_type &c, key_type &&key, element_type &&value) noexcept {
    // For a flat_set, key and value are the same. We ensure key uniqueness
    // and insert the value in sorted order.
    if (key != value) {
      return unexpected(error::invalid_argument);
    }
    auto res = c.try_insert(std::move(value));
    if (!res) {
      return unexpected(res.error());
    }
    return {};
  }

  static result<void> try_erase(container_type &c, const key_type &key) noexcept {
    // If flat_set provides a try_erase(key) method, delegate to it.
    // Assuming flat_set handles key-based removal:
    return c.try_remove(key);
  }

  static element_type *find(container_type &c, const key_type &key) noexcept {
    // flat_set's try_find returns result<std::reference_wrapper<const T>>.
    // For associative container refs, we need a mutable T*. We can safely
    // cast away constness if we obtain a mutable iterator/pointer from the underlying vector,
    // or provide a mutable find method on flat_set.
    // Assuming a mutable pointer lookup or direct data access helper exists:
    auto found_res = c.try_find(key);
    if (!found_res) {
      return nullptr;
    }
    // Unconsting the reference safely since the container ref allows value mutation
    return const_cast<element_type *>(&found_res->get());
  }

  static void for_each(container_type &c, void *visitor_ctx,
                       void (*visit)(void *, const key_type &, element_type &) noexcept) noexcept {
    // Traverse the flat_set and visit each element (where key == value)
    c.for_each([visitor_ctx, visit](const T &val) noexcept {
      // flat_set elements are sorted keys, so key and value point to `val`
      visit(visitor_ctx, val, const_cast<T &>(val));
    });
  }
};

/**
 * @brief Adapts `reloco::flat_set<T, Compare>` for the collection views.
 *
 * `flat_set` is exposed strictly as a read-only collection (`is_mutable = false`).
 * This prevents `mutable_collection_view` from bypassing the sorting invariant
 * and modifying the keys in-place, while allowing full O(1) read access for
 * formatting and inspection.
 */
template <typename T, typename Compare> struct collection_view_traits<flat_set<T, Compare>> {
  using element_type = T;

  // Backed by a contiguous vector, so random access and raw data pointers are O(1)
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;

  // CRITICAL: Prevent mutation of sorted keys
  static constexpr bool is_mutable = false;

  [[nodiscard]] static std::size_t size(const flat_set<T, Compare> &c) noexcept { return c.size(); }

  [[nodiscard]] static bool empty(const flat_set<T, Compare> &c) noexcept { return c.empty(); }

  [[nodiscard]] static const T &at(const flat_set<T, Compare> &c, std::size_t index) noexcept {
    RELOCO_ASSERT(index < c.size(), "Index out of bounds");
    return c.begin()[index];
  }

  [[nodiscard]] static const T *data(const flat_set<T, Compare> &c) noexcept { return c.begin(); }
};

/**
 * @brief `flat_set<T, Compare>` is trivially relocatable exactly when
 * `Compare` is: it wraps a `vector<T>`, which is always relocatable
 * regardless of `T`, so only the (usually stateless, trivially copyable)
 * comparator can prevent that.
 */
template <typename T, typename Compare>
struct is_trivially_relocatable<flat_set<T, Compare>> : is_trivially_relocatable<Compare> {};

} // namespace reloco
