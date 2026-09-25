// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file sso_flat_map.hpp
 * @brief Sorted, unique-key map backed directly by `sso_vector<std::pair<Key,
 * Mapped>, InlineCapacity>`.
 *
 * `sso_flat_map<Key, Mapped, InlineCapacity, Compare>` is `flat_map<Key,
 * Mapped, Compare>`'s small-size-optimized counterpart, exactly the way
 * `sso_flat_set<T, InlineCapacity, Compare>` (see `sso_flat_set.hpp`)
 * relates to `flat_set<T, Compare>`: a thin derived class of
 * `detail::flat_container_base<sso_vector<std::pair<Key, Mapped>,
 * InlineCapacity>, Compare, detail::pair_key_of>` (see
 * `detail/flat_container_base.hpp`). Up to `InlineCapacity` entries stay
 * inline with no allocation at all; insertion past that transparently
 * promotes to the heap, exactly like `sso_vector` itself, so -- unlike
 * `inline_flat_map<Key, Mapped, Capacity, Compare>` -- `try_insert` never
 * fails with `error::capacity_exceeded`.
 *
 * This file adds the `sso_vector`-specific factory wrappers
 * (`try_allocate`/`try_create`), the `Key`/`Mapped`-split convenience
 * methods (`try_insert(key, mapped)`, `try_at(key)`), and re-wraps the
 * base's `try_clone` results back into `sso_flat_map` itself -- the exact
 * same shape as `flat_map<Key, Mapped, Compare>` (see `flat_map.hpp`).
 *
 * Like `flat_map`, `sso_flat_map` deliberately has no `operator[]`: `try_at`/
 * `try_insert(key, mapped)` are the fallible replacements.
 */

#include "detail/flat_container_base.hpp"
#include "relocatable_std.hpp"
#include "sso_vector.hpp"

#include <utility>

namespace reloco {

template <typename Key, typename Mapped, std::size_t InlineCapacity, typename Compare = std::less<Key>>
class RELOCO_OWNER sso_flat_map : public detail::flat_container_base<sso_vector<std::pair<Key, Mapped>, InlineCapacity>,
                                                                     Compare, detail::pair_key_of> {
  using base =
      detail::flat_container_base<sso_vector<std::pair<Key, Mapped>, InlineCapacity>, Compare, detail::pair_key_of>;

public:
  using key_type = Key;
  using mapped_type = Mapped;

  using base::base;
  using base::try_insert;
  using typename base::size_type;
  using typename base::value_type;

  [[nodiscard]] static result<sso_flat_map> try_allocate(allocator_ref alloc, size_type initial_cap = 0) noexcept {
    auto res = base::try_allocate(alloc, initial_cap);
    if (!res)
      return unexpected(res.error());
    return sso_flat_map(std::move(*res));
  }

  [[nodiscard]] static result<sso_flat_map> try_create(size_type initial_cap = 0) noexcept {
    return try_allocate(default_allocator(), initial_cap);
  }

  /**
   * @brief Performs a deep copy of the map using a specific allocator.
   */
  [[nodiscard]] result<sso_flat_map> try_clone(allocator_ref alloc) const noexcept {
    auto res = base::try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    return sso_flat_map(std::move(*res));
  }

  [[nodiscard]] result<sso_flat_map> try_clone() const noexcept {
    auto res = base::try_clone();
    if (!res)
      return unexpected(res.error());
    return sso_flat_map(std::move(*res));
  }

  /**
   * @brief Inserts (@p key, @p mapped) in sorted-by-key position. Fails
   * with `error::already_exists` if @p key is already present, returning a
   * reference to the newly inserted mapped value on success.
   */
  [[nodiscard]] result<std::reference_wrapper<mapped_type>>
  try_insert(key_type key, mapped_type mapped) & noexcept RELOCO_LIFETIMEBOUND {
    auto res = base::try_insert(value_type(std::move(key), std::move(mapped)));
    if (!res)
      return unexpected(res.error());
    return std::ref(res->get().second);
  }

  /**
   * @brief Looks up @p key, returning a mutable reference to its mapped
   * value on success or `error::not_found` otherwise.
   */
  template <typename K>
  [[nodiscard]] result<std::reference_wrapper<mapped_type>> try_at(const K &key) & noexcept RELOCO_LIFETIMEBOUND {
    auto found = base::try_find(key);
    if (!found)
      return unexpected(found.error());
    // base::try_find only ever hands back const references (see
    // flat_map's/collection_view_traits's is_mutable = false rationale:
    // the *key* half of the pair must stay immutable to preserve sort
    // order). Mutating just the mapped half cannot break that invariant,
    // so this narrowly re-adds mutability the same way container_ref_traits
    // does.
    auto &mutable_pair = const_cast<value_type &>(found->get());
    return std::ref(mutable_pair.second);
  }

  /**
   * @brief Looks up @p key, returning a read-only reference to its mapped
   * value on success or `error::not_found` otherwise.
   */
  template <typename K>
  [[nodiscard]] result<std::reference_wrapper<const mapped_type>>
  try_at(const K &key) const & noexcept RELOCO_LIFETIMEBOUND {
    auto found = base::try_find(key);
    if (!found)
      return unexpected(found.error());
    return std::cref(found->get().second);
  }

  /**
   * @brief Rust `HashMap::entry(key).or_insert(default_value)`
   * equivalent: returns a reference to the existing mapped value for
   * @p key, or inserts @p default_value and returns a reference to that.
   * Only fails if the insertion itself fails (e.g.
   * `error::allocation_failed`/`error::capacity_exceeded`, depending on
   * the concrete map type); an existing key is never overwritten.
   */
  [[nodiscard]] result<std::reference_wrapper<mapped_type>>
  try_entry_or_insert(key_type key, mapped_type default_value) & noexcept RELOCO_LIFETIMEBOUND {
    auto found = try_at(key);
    if (found)
      return found;
    return try_insert(std::move(key), std::move(default_value));
  }

  /**
   * @brief Rust `HashMap::entry(key).or_insert_with(f)` equivalent: like
   * `try_entry_or_insert`, but only invokes @p default_factory (and
   * constructs the mapped value) when @p key is actually absent.
   */
  template <typename F>
  [[nodiscard]] result<std::reference_wrapper<mapped_type>>
  try_entry_or_insert_with(key_type key, F &&default_factory) & noexcept RELOCO_LIFETIMEBOUND {
    auto found = try_at(key);
    if (found)
      return found;
    return try_insert(std::move(key), default_factory());
  }

  /**
   * @brief Rust `HashMap::entry(key).and_modify(f)` equivalent: invokes
   * @p modify with a mutable reference to the mapped value for @p key if
   * present, returning that reference; otherwise fails with
   * `error::not_found` without inserting anything. Combine with
   * `try_entry_or_insert`/`try_entry_or_insert_with` to replicate Rust's
   * `.and_modify(f).or_insert(v)` chain:
   * `auto slot = m.try_entry_and_modify(k, f); if (!slot) slot =
   * m.try_entry_or_insert(k, v);`
   */
  template <typename K, typename F>
  [[nodiscard]] result<std::reference_wrapper<mapped_type>>
  try_entry_and_modify(const K &key, F &&modify) & noexcept RELOCO_LIFETIMEBOUND {
    auto found = try_at(key);
    if (found)
      modify(found->get());
    return found;
  }

private:
  explicit sso_flat_map(base &&b) noexcept : base(std::move(b)) {}
};

/**
 * @brief Adapts `reloco::sso_flat_map<Key, Mapped, InlineCapacity,
 * Compare>` for `mutable_container_ref`. Identical in shape to
 * `flat_map`'s adapter (see `flat_map.hpp`).
 */
template <typename Key, typename Mapped, std::size_t InlineCapacity, typename Compare>
struct container_ref_traits<sso_flat_map<Key, Mapped, InlineCapacity, Compare>> {
  static constexpr bool is_associative = true;

  using element_type = Mapped;
  using key_type = Key;
  using container_type = sso_flat_map<Key, Mapped, InlineCapacity, Compare>;

  static std::size_t size(const container_type &c) noexcept { return c.size(); }

  static bool empty(const container_type &c) noexcept { return c.empty(); }

  static void clear(container_type &c) noexcept { c.clear(); }

  static result<void> try_insert_at(container_type &c, key_type &&key, element_type &&value) noexcept {
    auto res = c.try_insert(std::move(key), std::move(value));
    if (!res) {
      return unexpected(res.error());
    }
    return {};
  }

  static result<void> try_erase(container_type &c, const key_type &key) noexcept { return c.try_remove(key); }

  static element_type *find(container_type &c, const key_type &key) noexcept {
    auto found_res = c.try_at(key);
    if (!found_res) {
      return nullptr;
    }
    return &found_res->get();
  }

  static void for_each(container_type &c, void *visitor_ctx,
                       void (*visit)(void *, const key_type &, element_type &) noexcept) noexcept {
    c.for_each([visitor_ctx, visit](const typename container_type::value_type &entry) noexcept {
      visit(visitor_ctx, entry.first, const_cast<element_type &>(entry.second));
    });
  }
};

/**
 * @brief Adapts `reloco::sso_flat_map<Key, Mapped, InlineCapacity,
 * Compare>` for the collection views.
 *
 * Exposed strictly as a read-only collection of `(Key, Mapped)` pairs
 * (`is_mutable = false`), for the same reason as `flat_map`: mutating an
 * element in place could break the sort-by-key invariant.
 */
template <typename Key, typename Mapped, std::size_t InlineCapacity, typename Compare>
struct collection_view_traits<sso_flat_map<Key, Mapped, InlineCapacity, Compare>> {
  using element_type = typename sso_flat_map<Key, Mapped, InlineCapacity, Compare>::value_type;

  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = false;

  [[nodiscard]] static std::size_t size(const sso_flat_map<Key, Mapped, InlineCapacity, Compare> &c) noexcept {
    return c.size();
  }

  [[nodiscard]] static bool empty(const sso_flat_map<Key, Mapped, InlineCapacity, Compare> &c) noexcept {
    return c.empty();
  }

  [[nodiscard]] static const element_type &at(const sso_flat_map<Key, Mapped, InlineCapacity, Compare> &c,
                                              std::size_t index) noexcept {
    RELOCO_ASSERT(index < c.size(), "Index out of bounds");
    return c.begin()[index];
  }

  [[nodiscard]] static const element_type *data(const sso_flat_map<Key, Mapped, InlineCapacity, Compare> &c) noexcept {
    return c.begin();
  }
};

/**
 * @brief `sso_flat_map<Key, Mapped, InlineCapacity, Compare>` is trivially
 * relocatable exactly when... never: it wraps an `sso_vector<std::pair<Key,
 * Mapped>, InlineCapacity>`, which is itself never trivially relocatable
 * (see `sso_vector.hpp`), so `sso_flat_map` can't be either.
 */
template <typename Key, typename Mapped, std::size_t InlineCapacity, typename Compare>
struct is_trivially_relocatable<sso_flat_map<Key, Mapped, InlineCapacity, Compare>> : std::false_type {};

} // namespace reloco
