// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file inline_flat_map.hpp
 * @brief Sorted, unique-key, fixed-capacity map backed directly by
 * `inline_vector<std::pair<Key, Mapped>, Capacity>`.
 *
 * `inline_flat_map<Key, Mapped, Capacity, Compare>` is `flat_map<Key,
 * Mapped, Compare>`'s allocation-free counterpart: a thin derived class of
 * `detail::flat_container_base<inline_vector<std::pair<Key, Mapped>,
 * Capacity>, Compare, detail::pair_key_of>` (see
 * `detail/flat_container_base.hpp` and `inline_vector.hpp`). As with
 * `inline_flat_set`, there is no allocator-taking factory to forward to
 * (`Capacity` itself is the reserved capacity), so only a default
 * constructor is available; `try_insert`/`try_insert_at` fail with
 * `error::capacity_exceeded` once `size() == Capacity`.
 *
 * Like `flat_map`, `inline_flat_map` has no `operator[]` -- see
 * `flat_map.hpp` for why -- and provides the same `try_insert(key,
 * mapped)`/`try_at(key)` fallible replacements, plus a `flat_map`-upgrade
 * path (`try_to_flat_map`, mirroring `inline_vector::try_to_vector`) for
 * callers that reach the fixed `Capacity` but need to keep growing.
 */

#include "detail/flat_container_base.hpp"
#include "flat_map.hpp"
#include "inline_vector.hpp"

#include <utility>

namespace reloco {

template <typename Key, typename Mapped, std::size_t Capacity, typename Compare = std::less<Key>>
class RELOCO_OWNER inline_flat_map : public detail::flat_container_base<inline_vector<std::pair<Key, Mapped>, Capacity>,
                                                                        Compare, detail::pair_key_of> {
  using base =
      detail::flat_container_base<inline_vector<std::pair<Key, Mapped>, Capacity>, Compare, detail::pair_key_of>;

public:
  using key_type = Key;
  using mapped_type = Mapped;

  using base::base;
  using base::try_insert;
  using typename base::value_type;

  /**
   * @brief Performs a deep copy of the map using a specific allocator for
   * any nested fallible-allocation `Key`/`Mapped` (`inline_flat_map`
   * itself never allocates).
   */
  [[nodiscard]] result<inline_flat_map> try_clone(allocator_ref alloc) const noexcept {
    auto res = base::try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    return inline_flat_map(std::move(*res));
  }

  /**
   * @brief Performs a deep copy using the process-wide default allocator
   * (see `default_allocator()`) for any nested fallible-allocation
   * `Key`/`Mapped`.
   */
  [[nodiscard]] result<inline_flat_map> try_clone() const noexcept {
    auto res = base::try_clone();
    if (!res)
      return unexpected(res.error());
    return inline_flat_map(std::move(*res));
  }

  // ---- upgrading to a heap-backed flat_map<Key, Mapped, Compare> ----

  /**
   * @brief Clones every (key, mapped) entry into a newly heap-allocated
   * `flat_map<Key, Mapped, Compare>`, leaving `*this` untouched. Useful
   * when the fixed `Capacity` has been reached (or is about to be) but the
   * caller still wants a growable map.
   */
  [[nodiscard]] result<flat_map<Key, Mapped, Compare>> try_to_flat_map(allocator_ref alloc) const & noexcept {
    auto map_res = flat_map<Key, Mapped, Compare>::try_allocate(alloc, base::size());
    if (!map_res)
      return unexpected(map_res.error());
    flat_map<Key, Mapped, Compare> map = std::move(*map_res);
    bool failed = false;
    error first_error{};
    base::for_each([&](const value_type &entry) noexcept {
      if (failed)
        return;
      auto clone_res = construction_helpers::try_clone<value_type>(alloc, entry);
      if (!clone_res) {
        failed = true;
        first_error = clone_res.error();
        return;
      }
      auto ins_res = map.try_insert(std::move(clone_res->first), std::move(clone_res->second));
      if (!ins_res) {
        failed = true;
        first_error = ins_res.error();
      }
    });
    if (failed)
      return unexpected(first_error);
    return map;
  }

  /**
   * @brief Same as `try_to_flat_map(allocator_ref)`, using the
   * process-wide default allocator (see `default_allocator()`).
   */
  [[nodiscard]] result<flat_map<Key, Mapped, Compare>> try_to_flat_map() const & noexcept {
    return try_to_flat_map(default_allocator());
  }

  /**
   * @brief Moves every (key, mapped) entry out into a newly
   * heap-allocated `flat_map<Key, Mapped, Compare>`, consuming `*this`
   * (which is left empty regardless of success or failure).
   */
  [[nodiscard]] result<flat_map<Key, Mapped, Compare>> try_to_flat_map(allocator_ref alloc) && noexcept {
    auto map_res = flat_map<Key, Mapped, Compare>::try_allocate(alloc, base::size());
    if (!map_res) {
      base::clear();
      return unexpected(map_res.error());
    }
    flat_map<Key, Mapped, Compare> map = std::move(*map_res);
    bool failed = false;
    error first_error{};
    base::for_each([&](const value_type &entry) noexcept {
      if (failed)
        return;
      auto &mutable_entry = const_cast<value_type &>(entry);
      auto ins_res = map.try_insert(std::move(mutable_entry.first), std::move(mutable_entry.second));
      if (!ins_res) {
        failed = true;
        first_error = ins_res.error();
      }
    });
    base::clear();
    if (failed)
      return unexpected(first_error);
    return map;
  }

  /**
   * @brief Same as `try_to_flat_map(allocator_ref) &&`, using the
   * process-wide default allocator (see `default_allocator()`).
   */
  [[nodiscard]] result<flat_map<Key, Mapped, Compare>> try_to_flat_map() && noexcept {
    return std::move(*this).try_to_flat_map(default_allocator());
  }

  /**
   * @brief Inserts (@p key, @p mapped) in sorted-by-key position. Fails
   * with `error::already_exists` if @p key is already present, or
   * `error::capacity_exceeded` if the map is full; returns a reference to
   * the newly inserted mapped value on success.
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

private:
  explicit inline_flat_map(base &&b) noexcept : base(std::move(b)) {}
};

/**
 * @brief Adapts `reloco::inline_flat_map<Key, Mapped, Capacity, Compare>`
 * for `mutable_container_ref`. Identical in shape to `flat_map`'s adapter
 * (see `flat_map.hpp`).
 */
template <typename Key, typename Mapped, std::size_t Capacity, typename Compare>
struct container_ref_traits<inline_flat_map<Key, Mapped, Capacity, Compare>> {
  static constexpr bool is_associative = true;

  using element_type = Mapped;
  using key_type = Key;
  using container_type = inline_flat_map<Key, Mapped, Capacity, Compare>;

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
 * @brief Adapts `reloco::inline_flat_map<Key, Mapped, Capacity, Compare>`
 * for the collection views. Exposed strictly as a read-only collection of
 * `(Key, Mapped)` pairs (`is_mutable = false`), same rationale as
 * `flat_map`.
 */
template <typename Key, typename Mapped, std::size_t Capacity, typename Compare>
struct collection_view_traits<inline_flat_map<Key, Mapped, Capacity, Compare>> {
  using element_type = typename inline_flat_map<Key, Mapped, Capacity, Compare>::value_type;

  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = false;

  [[nodiscard]] static std::size_t size(const inline_flat_map<Key, Mapped, Capacity, Compare> &c) noexcept {
    return c.size();
  }

  [[nodiscard]] static bool empty(const inline_flat_map<Key, Mapped, Capacity, Compare> &c) noexcept {
    return c.empty();
  }

  [[nodiscard]] static const element_type &at(const inline_flat_map<Key, Mapped, Capacity, Compare> &c,
                                              std::size_t index) noexcept {
    RELOCO_ASSERT(index < c.size(), "Index out of bounds");
    return c.begin()[index];
  }

  [[nodiscard]] static const element_type *data(const inline_flat_map<Key, Mapped, Capacity, Compare> &c) noexcept {
    return c.begin();
  }
};

/**
 * @brief `inline_flat_map<Key, Mapped, Capacity, Compare>` is trivially
 * relocatable exactly when `Key`, `Mapped`, and `Compare` all are: it wraps
 * an `inline_vector<std::pair<Key, Mapped>, Capacity>`, whose own
 * relocatability already depends on its element type (see
 * `inline_vector.hpp`).
 */
template <typename Key, typename Mapped, std::size_t Capacity, typename Compare>
struct is_trivially_relocatable<inline_flat_map<Key, Mapped, Capacity, Compare>>
    : std::bool_constant<is_trivially_relocatable_v<Key> && is_trivially_relocatable_v<Mapped> &&
                         is_trivially_relocatable_v<Compare>> {};

} // namespace reloco
