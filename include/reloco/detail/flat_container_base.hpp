// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file flat_container_base.hpp
 * @brief Shared implementation for reloco's sorted, contiguous associative
 * containers (`flat_set`, `flat_map`, `inline_flat_set`, `inline_flat_map`).
 *
 * `flat_container_base<Storage, Compare, KeyOf>` factors out everything a
 * sorted-vector-backed associative container needs -- `try_insert`,
 * `contains`, `try_find`, `try_remove`, iteration, cloning -- so that
 * `flat_set<T, Compare>` and `flat_map<Key, Mapped, Compare>` (and their
 * fixed-capacity `inline_flat_set`/`inline_flat_map` counterparts) are thin
 * derived classes that only need to plug in:
 *
 * - `Storage`: the backing contiguous container, either `vector<T>`
 *   (heap-allocated, growable) or `inline_vector<T, Capacity>`
 *   (allocation-free, fixed-capacity) -- both already implement the exact
 *   same `try_insert_at`/`try_erase_at`/`try_clone`/`begin`/`end` surface
 *   (see `vector.hpp`/`inline_vector.hpp`), so the base needs no
 *   `if constexpr` branching to use either one as `Storage`.
 * - `Compare`: an ordering over `key_type`, exactly like
 *   `std::map`/`std::set`'s comparator.
 * - `KeyOf`: a stateless functor extracting `key_type` from `value_type`
 *   (`identity_key_of` for sets, where the element *is* the key;
 *   `pair_key_of` for maps, where `value_type` is `std::pair<Key, Mapped>`
 *   and the key is `.first`).
 *
 * Only two container-level operations are gated by whether `Storage`
 * supports them at all: `try_allocate`/`try_create` (present only for
 * `vector<T>`-backed containers, since `inline_vector<T, Capacity>` has no
 * allocator-taking factory to forward to) and `get_allocator()` (present
 * only when `Storage` exposes one). Everything else -- `try_clone`,
 * `size`/`capacity`/`empty`, mutation, lookup, iteration -- is available
 * unconditionally, because both `Storage` candidates already implement it
 * identically.
 *
 * The base is deliberately *not* a public reloco type: it is an
 * implementation detail (`reloco::detail`) that only concrete containers
 * derive from, exactly like `detail::has_try_clone_at_impl` and friends in
 * `concepts.hpp` are internal detection machinery rather than public API.
 */

#include "../concepts.hpp"
#include "../construction_helpers.hpp"
#include "../default_allocator.hpp"
#include "../error.hpp"
#include "../expected.hpp"
#include "../lifetime.hpp"
#include "../rvalue_safety.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

namespace reloco::detail {

/**
 * @brief Key-extraction functor for set-like flat containers, where the
 * stored element *is* the key (used by `flat_set`/`inline_flat_set`).
 */
struct identity_key_of {
  template <typename T> constexpr const T &operator()(const T &value) const noexcept { return value; }
};

/**
 * @brief Key-extraction functor for map-like flat containers storing
 * `std::pair<Key, Mapped>` elements (used by `flat_map`/`inline_flat_map`).
 */
struct pair_key_of {
  template <typename Pair> constexpr const auto &operator()(const Pair &value) const noexcept { return value.first; }
};

/**
 * @brief Shared base for `flat_set`/`flat_map`/`inline_flat_set`/
 * `inline_flat_map`: a sorted, unique-key sequence backed directly by
 * `Storage` (a `vector<T>` or `inline_vector<T, Capacity>`), searched with
 * `std::lower_bound` rather than a node-based tree.
 */
template <typename Storage, typename Compare, typename KeyOf> class flat_container_base {
public:
  using storage_type = Storage;
  using value_type = typename Storage::value_type;
  using key_type = std::decay_t<decltype(KeyOf{}(std::declval<const value_type &>()))>;
  using size_type = typename Storage::size_type;
  using difference_type = typename Storage::difference_type;
  using reference = typename Storage::reference;
  using const_reference = typename Storage::const_reference;
  using pointer = typename Storage::pointer;
  using const_pointer = typename Storage::const_pointer;
  using iterator = typename Storage::iterator;
  using const_iterator = typename Storage::const_iterator;

  RELOCO_BLOCK_RVALUE_ACCESS(value_type);

  constexpr flat_container_base() noexcept = default;

  /**
   * @brief Constructs with an explicit allocator. Only participates in
   * overload resolution when `Storage` itself accepts one (i.e. `vector<T>`,
   * not `inline_vector<T, Capacity>`).
   */
  template <typename S = Storage, typename = std::enable_if_t<std::is_constructible_v<S, allocator_ref>>>
  constexpr explicit flat_container_base(allocator_ref alloc) noexcept : data_(alloc) {}

  /**
   * @brief Builds, optionally reserving capacity, using an explicit
   * allocator. Only available when `Storage::try_allocate` exists.
   */
  template <typename S = Storage, typename = std::enable_if_t<has_try_allocate_v<S, size_type>>>
  [[nodiscard]] static result<flat_container_base> try_allocate(allocator_ref alloc,
                                                                size_type initial_cap = 0) noexcept {
    auto storage_res = Storage::try_allocate(alloc, initial_cap);
    if (!storage_res)
      return unexpected(storage_res.error());
    return flat_container_base(std::move(*storage_res));
  }

  /**
   * @brief Builds, optionally reserving capacity, using
   * `default_allocator()`. Only available when `Storage::try_create`
   * exists.
   */
  template <typename S = Storage, typename = std::enable_if_t<has_try_create_v<S, size_type>>>
  [[nodiscard]] static result<flat_container_base> try_create(size_type initial_cap = 0) noexcept {
    return try_allocate(default_allocator(), initial_cap);
  }

  /**
   * @brief Performs a deep copy using a specific allocator.
   */
  [[nodiscard]] result<flat_container_base> try_clone(allocator_ref alloc) const noexcept {
    auto cloned_data = data_.try_clone(alloc);
    if (!cloned_data)
      return unexpected(cloned_data.error());
    return flat_container_base(std::move(*cloned_data));
  }

  /**
   * @brief Performs a deep copy, delegating to `Storage::try_clone()` for
   * the allocator choice (the storage's own bound allocator for
   * `vector<T>`, `default_allocator()` for `inline_vector<T, Capacity>`).
   */
  [[nodiscard]] result<flat_container_base> try_clone() const noexcept {
    auto cloned_data = data_.try_clone();
    if (!cloned_data)
      return unexpected(cloned_data.error());
    return flat_container_base(std::move(*cloned_data));
  }

  /**
   * @brief Returns the bound allocator. Only available when `Storage`
   * itself exposes one (i.e. `vector<T>`, not `inline_vector<T, Capacity>`).
   */
  template <typename S = Storage>
  [[nodiscard]] auto get_allocator() const noexcept -> decltype(std::declval<const S &>().get_allocator()) {
    return data_.get_allocator();
  }

  [[nodiscard]] size_type size() const noexcept { return data_.size(); }
  [[nodiscard]] size_type capacity() const noexcept { return data_.capacity(); }
  [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

  void clear() noexcept { data_.clear(); }

  /**
   * @brief Inserts @p value in sorted position, keyed by `KeyOf{}(value)`.
   * Fails with `error::already_exists` if an element with an equivalent key
   * is already present, or with whatever `Storage::try_insert_at` itself
   * can fail with (e.g. `error::capacity_exceeded` for a full
   * `inline_vector`-backed container).
   */
  [[nodiscard]] result<std::reference_wrapper<value_type>>
  try_insert(value_type &&value) & noexcept RELOCO_LIFETIMEBOUND {
    const key_type &key = KeyOf{}(value);
    auto it = find_pos(key);
    if (it != data_.end() && !comp_(key, KeyOf{}(*it))) {
      return unexpected(error::already_exists);
    }
    const auto index = static_cast<size_type>(std::distance(data_.begin(), it));
    return data_.try_insert_at(index, std::move(value));
  }

  template <typename Key> [[nodiscard]] bool contains(const Key &key) const noexcept {
    auto it = find_pos(key);
    return it != data_.end() && !comp_(key, KeyOf{}(*it));
  }

  template <typename Key>
  [[nodiscard]] result<std::reference_wrapper<const value_type>>
  try_find(const Key &key) const & noexcept RELOCO_LIFETIMEBOUND {
    auto it = find_pos(key);
    if (it != data_.end() && !comp_(key, KeyOf{}(*it))) {
      return std::cref(*it);
    }
    return unexpected(error::not_found);
  }

  template <typename Key> [[nodiscard]] result<void> try_remove(const Key &key) & noexcept {
    auto it = find_pos(key);
    if (it == data_.end() || comp_(key, KeyOf{}(*it))) {
      return unexpected(error::not_found);
    }
    const auto index = static_cast<size_type>(std::distance(data_.begin(), it));
    auto res = data_.try_erase_at(index);
    if (!res) {
      return unexpected(res.error());
    }
    return {};
  }

  [[nodiscard]] const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND { return data_.begin(); }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return data_.end(); }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return data_.cbegin(); }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return data_.cend(); }

  template <typename Fn> void for_each(Fn &&fn) const {
    for (size_type i = 0; i < data_.size(); ++i) {
      fn(data_[i]);
    }
  }

protected:
  /**
   * @brief Adopts an already-built `Storage` (e.g. the result of
   * `Storage::try_allocate`/`try_clone`). Used internally by this class and
   * by derived classes implementing their own factories.
   */
  explicit flat_container_base(Storage &&storage) noexcept : data_(std::move(storage)) {}

private:
  template <typename Key> [[nodiscard]] auto find_pos(const Key &key) const noexcept {
    return std::lower_bound(
        data_.begin(), data_.end(), key,
        [this](const value_type &elem, const Key &target) noexcept { return comp_(KeyOf{}(elem), target); });
  }

  template <typename Key> [[nodiscard]] auto find_pos(const Key &key) noexcept {
    return std::lower_bound(
        data_.begin(), data_.end(), key,
        [this](const value_type &elem, const Key &target) noexcept { return comp_(KeyOf{}(elem), target); });
  }

  Storage data_;
  Compare comp_;
};

} // namespace reloco::detail
