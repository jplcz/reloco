// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file flat_hash_map.hpp
 * @brief Unique-key map backed by an open-addressing, vector-of-
 * `optional<std::pair<Key, Mapped>>` hash table over `std::pair<Key,
 * Mapped>` (see `detail/flat_hash_base.hpp`).
 *
 * `flat_hash_map<Key, Mapped, Hash, KeyEqual>` is a thin derived class of
 * `detail::flat_hash_base<std::pair<Key, Mapped>, Hash, KeyEqual,
 * detail::pair_key_of>` (see `detail/flat_hash_base.hpp`): the base already
 * implements `contains`/`try_find`/`try_remove`/`try_take`/iteration/
 * cloning/`retain`, keyed by `detail::pair_key_of` (`value.first`) since a
 * map's key is only part of each stored element. This file adds the
 * `Key`/`Mapped`-split convenience methods (`try_insert(key, mapped)`,
 * `try_at(key)`), the Rust `HashMap::entry`-flavored API, and re-wraps the
 * base's `try_clone` results back into `flat_hash_map` itself -- exactly
 * like `tree_map` does over `tree_base`.
 *
 * Like `tree_map`/`flat_map`, `flat_hash_map` deliberately has no
 * `operator[]`: it would need to silently default-construct and insert a
 * missing key on demand, which reloco's fallible-everywhere design does
 * not permit. `try_at(key)`/`try_insert(key, mapped)` are the two fallible
 * replacements.
 *
 * Unlike `tree_map` (ordered by `Compare`, node-based storage),
 * `flat_hash_map` has *no* ordering guarantee -- iteration order depends on
 * hash values and insertion/removal history, exactly like Rust's
 * `HashMap` -- but offers O(1) average-case lookup/insert/remove instead
 * of O(log n), and a single contiguous backing allocation instead of one
 * allocation per entry. It therefore has no `try_first_key_value`/
 * `try_pop_first`/`append` (those rely on the tree's ascending-key
 * ordering), but does expose `try_remove_entry` (Rust
 * `HashMap::remove_entry` equivalent, mirroring `flat_hash_base::try_take`
 * at the engine level).
 */

#include "container_ref.hpp"
#include "default_allocator.hpp"
#include "detail/flat_hash_base.hpp"
#include "relocatable.hpp"

#include <functional>
#include <type_traits>
#include <utility>

namespace reloco {

template <typename Key, typename Mapped, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class RELOCO_OWNER flat_hash_map
    : public detail::flat_hash_base<std::pair<Key, Mapped>, Hash, KeyEqual, detail::pair_key_of> {
  using base = detail::flat_hash_base<std::pair<Key, Mapped>, Hash, KeyEqual, detail::pair_key_of>;

public:
  using key_type = Key;
  using mapped_type = Mapped;

  using base::base;
  using typename base::size_type;
  using typename base::value_type;

  [[nodiscard]] static result<flat_hash_map> try_allocate(allocator_ref alloc) noexcept {
    auto res = base::try_allocate(alloc);
    if (!res)
      return unexpected(res.error());
    return flat_hash_map(std::move(*res));
  }

  [[nodiscard]] static result<flat_hash_map> try_create() noexcept { return try_allocate(default_allocator()); }

  /**
   * @brief Performs a deep copy of the map using a specific allocator.
   */
  [[nodiscard]] result<flat_hash_map> try_clone(allocator_ref alloc) const noexcept {
    auto res = base::try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    return flat_hash_map(std::move(*res));
  }

  [[nodiscard]] result<flat_hash_map> try_clone() const noexcept {
    auto res = base::try_clone();
    if (!res)
      return unexpected(res.error());
    return flat_hash_map(std::move(*res));
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
    // of the pair must stay untouched -- mutating it out from under the
    // table's own hash-derived slot would silently corrupt the invariant
    // that a key's slot is reachable by hashing that same key). Mutating
    // just the mapped half is safe, exactly like flat_map/tree_map's
    // identical const_cast.
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
   * @brief Rust `HashMap::remove_entry` equivalent: removes and returns
   * the (key, mapped value) pair for @p key, rather than just discarding
   * it like `try_remove`. Fails with `error::not_found` if @p key is
   * absent.
   */
  template <typename K> [[nodiscard]] result<value_type> try_remove_entry(const K &key) & noexcept {
    return base::try_take(key);
  }

  /**
   * @brief Rust `HashMap::retain` equivalent: keeps only the entries for
   * which @p pred(key, mapped) returns `true`. @p pred receives a mutable
   * reference to the mapped value (mutating it is safe, exactly like
   * `try_at`'s mutable overload) but only a `const key_type &` for the key
   * itself.
   */
  template <typename Pred> void retain(Pred &&pred) & noexcept {
    base::retain([&pred](const value_type &entry) noexcept {
      return pred(entry.first, const_cast<mapped_type &>(entry.second));
    });
  }

private:
  explicit flat_hash_map(base &&b) noexcept : base(std::move(b)) {}
};

/**
 * @brief Adapts `reloco::flat_hash_map<Key, Mapped, Hash, KeyEqual>` for
 * `mutable_container_ref`, exactly like `container_ref_traits<tree_map<Key,
 * Mapped, Compare>>`.
 */
template <typename Key, typename Mapped, typename Hash, typename KeyEqual>
struct container_ref_traits<flat_hash_map<Key, Mapped, Hash, KeyEqual>> {
  static constexpr bool is_associative = true;

  using element_type = Mapped;
  using key_type = Key;
  using container_type = flat_hash_map<Key, Mapped, Hash, KeyEqual>;

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
 * @brief `flat_hash_map<Key, Mapped, Hash, KeyEqual>` is trivially
 * relocatable exactly when both `Hash` and `KeyEqual` are -- same
 * rationale as `flat_hash_set`.
 */
template <typename Key, typename Mapped, typename Hash, typename KeyEqual>
struct is_trivially_relocatable<flat_hash_map<Key, Mapped, Hash, KeyEqual>>
    : std::conjunction<is_trivially_relocatable<Hash>, is_trivially_relocatable<KeyEqual>> {};

} // namespace reloco
