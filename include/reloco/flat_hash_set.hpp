// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file flat_hash_set.hpp
 * @brief Unique-element set backed by an open-addressing, vector-of-
 * `optional<T>` hash table (see `detail/flat_hash_base.hpp`).
 *
 * `flat_hash_set<T, Hash, KeyEqual>` is a thin derived class of
 * `detail::flat_hash_base<T, Hash, KeyEqual, detail::identity_key_of>`: the
 * base already implements `try_insert`/`contains`/`try_find`/`try_remove`/
 * `try_take`/iteration/cloning/`retain`/`is_subset`/`is_superset`/
 * `is_disjoint`, keyed by `identity_key_of` since a set's element *is* its
 * own key. This file only adds the factory wrappers (`try_allocate`/
 * `try_create`) and re-wraps the base's `try_clone` results back into
 * `flat_hash_set` itself -- exactly like `tree_set` does over `tree_base`.
 *
 * Unlike `tree_set`/`tree_map` (ordered by `Compare`, node-based storage),
 * `flat_hash_set` has *no* ordering guarantee at all -- iteration order
 * depends on hash values and insertion/removal history, exactly like Rust's
 * `HashSet` -- but offers O(1) average-case `try_insert`/`contains`/
 * `try_find`/`try_remove` instead of O(log n), and a single contiguous
 * `vector<optional<T>>` backing allocation instead of one allocation per
 * element. Prefer `flat_hash_set` over `tree_set` unless sorted iteration
 * or the `BTreeSet`-flavored `try_first`/`try_last`/`try_pop_first`/
 * `try_pop_last`/`append` API is actually needed.
 */

#include "container_ref.hpp"
#include "default_allocator.hpp"
#include "detail/flat_hash_base.hpp"
#include "relocatable.hpp"

#include <functional>
#include <type_traits>

namespace reloco {

template <typename T, typename Hash = std::hash<T>, typename KeyEqual = std::equal_to<T>>
class RELOCO_OWNER flat_hash_set : public detail::flat_hash_base<T, Hash, KeyEqual, detail::identity_key_of> {
  using base = detail::flat_hash_base<T, Hash, KeyEqual, detail::identity_key_of>;

public:
  using base::base;
  using base::is_disjoint;
  using base::is_subset;
  using base::is_superset;
  using base::retain;
  using base::try_insert;
  using typename base::size_type;
  using typename base::value_type;

  [[nodiscard]] static result<flat_hash_set> try_allocate(allocator_ref alloc) noexcept {
    auto res = base::try_allocate(alloc);
    if (!res)
      return unexpected(res.error());
    return flat_hash_set(std::move(*res));
  }

  [[nodiscard]] static result<flat_hash_set> try_create() noexcept { return try_allocate(default_allocator()); }

  /**
   * @brief Performs a deep copy of the set using a specific allocator.
   */
  [[nodiscard]] result<flat_hash_set> try_clone(allocator_ref alloc) const noexcept {
    auto res = base::try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    return flat_hash_set(std::move(*res));
  }

  [[nodiscard]] result<flat_hash_set> try_clone() const noexcept {
    auto res = base::try_clone();
    if (!res)
      return unexpected(res.error());
    return flat_hash_set(std::move(*res));
  }

private:
  explicit flat_hash_set(base &&b) noexcept : base(std::move(b)) {}
};

/**
 * @brief Adapts `reloco::flat_hash_set<T, Hash, KeyEqual>` for
 * `mutable_container_ref`, exactly like `container_ref_traits<tree_set<T,
 * Compare>>`.
 */
template <typename T, typename Hash, typename KeyEqual> struct container_ref_traits<flat_hash_set<T, Hash, KeyEqual>> {
  static constexpr bool is_associative = true;

  using element_type = T;
  using key_type = T;
  using container_type = flat_hash_set<T, Hash, KeyEqual>;

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
 * @brief `flat_hash_set<T, Hash, KeyEqual>` is trivially relocatable
 * exactly when both `Hash` and `KeyEqual` are: its own members (a
 * `vector<optional<T>>`, a size, a mask, and `Hash`/`KeyEqual` themselves)
 * are trivially relocatable regardless of `T` -- `vector<U>` is
 * unconditionally trivially relocatable for any `U`, see `vector.hpp`.
 */
template <typename T, typename Hash, typename KeyEqual>
struct is_trivially_relocatable<flat_hash_set<T, Hash, KeyEqual>>
    : std::conjunction<is_trivially_relocatable<Hash>, is_trivially_relocatable<KeyEqual>> {};

} // namespace reloco
