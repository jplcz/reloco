// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file tree_map.hpp
 * @brief Unique-key map backed by a basic, unbalanced, allocator-owned
 * binary search tree over `std::pair<Key, Mapped>`.
 *
 * `tree_map<Key, Mapped, Compare>` is a thin derived class of
 * `detail::tree_base<std::pair<Key, Mapped>, Compare, detail::pair_key_of>`
 * (see `detail/tree_base.hpp`): the base already implements
 * `contains`/`try_find`/`try_remove`/iteration/cloning, keyed by
 * `detail::pair_key_of` (`value.first`) since a map's key is only part of
 * each stored element. This file adds the `Key`/`Mapped`-split convenience
 * methods (`try_insert(key, mapped)`, `try_at(key)`) and re-wraps the
 * base's `try_clone` results back into `tree_map` itself -- exactly like
 * `flat_map` does over `flat_container_base`.
 *
 * Like `flat_map`, `tree_map` deliberately has no `operator[]`: it would
 * need to silently default-construct and insert a missing key on demand,
 * which reloco's fallible-everywhere design does not permit.
 * `try_at(key)`/`try_insert(key, mapped)` are the two fallible
 * replacements. Unlike `flat_map` (backed by a sorted `vector<
 * std::pair<Key, Mapped>>`), inserting into a `tree_map` never invalidates
 * references/pointers to other entries -- each lives in its own node-based
 * allocation (see `detail/node_base.hpp`), not a contiguous, reallocatable
 * buffer.
 *
 * Because the tree is always kept in ascending `Compare` order over
 * `Key`, `tree_map` also exposes Rust `BTreeMap`-flavored
 * `try_first_key_value`/`try_last_key_value`/`try_pop_first`/
 * `try_pop_last`, none of which have a `flat_map` counterpart today. It
 * also has `append` (moves every entry of another `tree_map` into `*this`
 * by relinking existing nodes, never reallocating) and `retain` (keeps
 * only entries matching a `(const Key &, Mapped &)` predicate).
 */

#include "container_ref.hpp"
#include "default_allocator.hpp"
#include "detail/tree_base.hpp"
#include "relocatable.hpp"

#include <utility>

namespace reloco {

template <typename Key, typename Mapped, typename Compare = std::less<Key>>
class RELOCO_OWNER tree_map : public detail::tree_base<std::pair<Key, Mapped>, Compare, detail::pair_key_of> {
  using base = detail::tree_base<std::pair<Key, Mapped>, Compare, detail::pair_key_of>;

public:
  using key_type = Key;
  using mapped_type = Mapped;

  using base::base;
  using typename base::size_type;
  using typename base::value_type;

  [[nodiscard]] static result<tree_map> try_allocate(allocator_ref alloc) noexcept {
    auto res = base::try_allocate(alloc);
    if (!res)
      return unexpected(res.error());
    return tree_map(std::move(*res));
  }

  [[nodiscard]] static result<tree_map> try_create() noexcept { return try_allocate(default_allocator()); }

  /**
   * @brief Performs a deep copy of the map using a specific allocator.
   */
  [[nodiscard]] result<tree_map> try_clone(allocator_ref alloc) const noexcept {
    auto res = base::try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    return tree_map(std::move(*res));
  }

  [[nodiscard]] result<tree_map> try_clone() const noexcept {
    auto res = base::try_clone();
    if (!res)
      return unexpected(res.error());
    return tree_map(std::move(*res));
  }

  /**
   * @brief Inserts (@p key, @p mapped) keyed by @p key. Fails with
   * `error::already_exists` if @p key is already present, returning a
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
    // base::try_find only ever hands back const references (the *key* half
    // of the pair must stay immutable so the tree's ordering invariant
    // holds). Mutating just the mapped half cannot break that invariant,
    // exactly like flat_map::try_at's identical const_cast.
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
   * `error::allocation_failed`); an existing key is never overwritten.
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
   * `error::not_found` without inserting anything.
   */
  template <typename K, typename F>
  [[nodiscard]] result<std::reference_wrapper<mapped_type>>
  try_entry_and_modify(const K &key, F &&modify) & noexcept RELOCO_LIFETIMEBOUND {
    auto found = try_at(key);
    if (found)
      modify(found->get());
    return found;
  }

  /**
   * @brief Rust `BTreeMap::first_key_value` equivalent: the (key, mapped
   * value) pair with the smallest key by `Compare`, without removing it.
   * Fails with `error::container_empty` if the map is empty.
   */
  [[nodiscard]] result<std::pair<std::reference_wrapper<const key_type>, std::reference_wrapper<const mapped_type>>>
  try_first_key_value() const & noexcept RELOCO_LIFETIMEBOUND {
    auto found = base::try_first();
    if (!found)
      return unexpected(found.error());
    const value_type &entry = found->get();
    return std::make_pair(std::cref(entry.first), std::cref(entry.second));
  }

  /**
   * @brief Rust `BTreeMap::last_key_value` equivalent: the (key, mapped
   * value) pair with the greatest key by `Compare`, without removing it.
   * Fails with `error::container_empty` if the map is empty.
   */
  [[nodiscard]] result<std::pair<std::reference_wrapper<const key_type>, std::reference_wrapper<const mapped_type>>>
  try_last_key_value() const & noexcept RELOCO_LIFETIMEBOUND {
    auto found = base::try_last();
    if (!found)
      return unexpected(found.error());
    const value_type &entry = found->get();
    return std::make_pair(std::cref(entry.first), std::cref(entry.second));
  }

  auto try_first_key_value() const && = delete;
  auto try_last_key_value() const && = delete;

  /**
   * @brief Rust `BTreeMap::pop_first` equivalent: removes and returns the
   * (key, mapped value) pair with the smallest key by `Compare`. Fails
   * with `error::container_empty` if the map is empty.
   */
  using base::try_pop_first;

  /**
   * @brief Rust `BTreeMap::pop_last` equivalent: removes and returns the
   * (key, mapped value) pair with the greatest key by `Compare`. Fails
   * with `error::container_empty` if the map is empty.
   */
  using base::try_pop_last;

  /**
   * @brief Rust `BTreeMap::append` equivalent: moves every entry out of
   * @p other into `*this`, leaving @p other empty. On a key collision, the
   * entry already in `*this` is replaced by @p other's. Never allocates or
   * constructs a new `(Key, Mapped)` pair -- each moved entry's existing
   * node allocation is reused as-is (see `tree_base::append`).
   */
  void append(tree_map &other) & noexcept { base::append(other); }

  /**
   * @brief Rust `BTreeMap::retain` equivalent: keeps only the entries for
   * which @p pred(key, mapped) returns `true`. @p pred receives a mutable
   * reference to the mapped value (mutating it cannot break the tree's key
   * ordering, exactly like `try_at`'s mutable overload) but only a
   * `const key_type &` for the key itself.
   */
  template <typename Pred> void retain(Pred &&pred) & noexcept {
    base::retain([&pred](const value_type &entry) noexcept {
      return pred(entry.first, const_cast<mapped_type &>(entry.second));
    });
  }

private:
  explicit tree_map(base &&b) noexcept : base(std::move(b)) {}
};

/**
 * @brief Adapts `reloco::tree_map<Key, Mapped, Compare>` for
 * `mutable_container_ref`, exactly like `container_ref_traits<flat_map<Key,
 * Mapped, Compare>>`.
 */
template <typename Key, typename Mapped, typename Compare> struct container_ref_traits<tree_map<Key, Mapped, Compare>> {
  static constexpr bool is_associative = true;

  using element_type = Mapped;
  using key_type = Key;
  using container_type = tree_map<Key, Mapped, Compare>;

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
 * @brief `tree_map<Key, Mapped, Compare>` is trivially relocatable exactly
 * when `Compare` is -- same rationale as `tree_set`/`flat_map`.
 */
template <typename Key, typename Mapped, typename Compare>
struct is_trivially_relocatable<tree_map<Key, Mapped, Compare>> : is_trivially_relocatable<Compare> {};

} // namespace reloco
