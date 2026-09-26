// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file lru_cache.hpp
 * @brief `lru_cache<Key, Mapped, Hash, KeyEqual>`, matching Rust's
 * de-facto `lru` crate's `LruCache<K, V>`: a fixed-capacity, unique-key
 * map that evicts its least-recently-used entry once full, with O(1)
 * average-case `try_put`/`try_get`/`try_peek`/`try_remove`.
 *
 * Built from two already-existing reloco containers rather than new
 * low-level plumbing (same "compose, don't reinvent" approach as
 * `lazy_lock.hpp` over `once_lock.hpp`):
 *
 * - `vector<optional<node>>` (see `vector.hpp`), a fixed-size (reserved to
 *   `capacity()` once at `try_create` and never grown past it), *stable*-
 *   index slab of entries -- each holding one (`Key`, `Mapped`) pair plus
 *   `prev`/`next` indices threading every live entry into one intrusive
 *   doubly-linked list, most-recently-used at the head, least-recently-
 *   used at the tail. "Stable-index" is the operative requirement here:
 *   unlike `flat_hash_base`'s own backward-shift deletion (which
 *   *relocates* surviving elements into the gap a removal leaves behind,
 *   see `detail/flat_hash_base.hpp`), an index into this slab must never
 *   change out from under a live entry, or the intrusive list's `prev`/
 *   `next` links would silently point at the wrong slot. Removal here
 *   therefore never shuffles other entries -- it only clears the freed
 *   slot and pushes its index onto a small free-list (`vector<size_type>`,
 *   also reserved to `capacity()` once, so reclaiming a slot can never
 *   itself fail with `error::allocation_failed`).
 * - `flat_hash_map<Key, size_type, Hash, KeyEqual>` (see
 *   `flat_hash_map.hpp`), giving O(1) average-case `Key -> slab index`
 *   lookup exactly like `lru`'s own internal `HashMap<K, NonNull<...>>`.
 *
 * Because the slab's intrusive list needs a `Key` to remove the evicted
 * tail entry's `flat_hash_map` entry, and `flat_hash_map` needs its own
 * owned `Key` for hashing/lookup, a *new* key handed to `try_put` is
 * stored twice -- once, moved, into the slab node; once, copied, into the
 * `flat_hash_map` index. This is the one place `lru_cache` asks more of
 * `Key` than the rest of reloco's containers: **`Key` must be copy-
 * constructible** (`Mapped` need not be -- it is only ever moved).
 * Rust's `lru` crate avoids this by storing only *one* `K` per entry and
 * pointing the hash map at it with a raw, GC-tied-to-the-entry pointer
 * (`KeyRef<K>`), which is exactly the kind of aliased-pointer trick
 * reloco's index-based safety model deliberately avoids (see
 * `docs/hardened-containers.md`) -- the extra `Key` copy is the price of
 * side-stepping it. Updating an *existing* key's value (the common case
 * once a working set stabilizes) never copies `Key` again.
 *
 * - `try_put(key, value)` -> `result<void>`: inserts @p key/@p value, or
 *   overwrites @p value and promotes @p key to most-recently-used if
 *   already present. Evicts the current least-recently-used entry first
 *   if the cache is already at `capacity()` and @p key is not already
 *   present, matching Rust's `LruCache::put` (this reloco port always
 *   drops the evicted entry rather than handing it back -- unlike
 *   `LruCache::push`, which returns the evicted `(K, V)` pair).
 * - `try_get(key)` -> `result<std::reference_wrapper<Mapped>>`: looks up
 *   @p key and promotes it to most-recently-used, matching
 *   `LruCache::get_mut` (this port has no separate immutable `get`,
 *   `try_get`'s reference is always mutable -- `try_peek` below is the
 *   promotion-free option, plain reloco `const`-overload-on-reference
 *   would only differ in mutability, not promotion, so is not useful
 *   here).
 * - `try_peek(key)` -> `result<std::reference_wrapper<const Mapped>>`:
 *   looks up @p key *without* promoting it, matching `LruCache::peek`.
 * - `try_peek_mut(key)` -> `result<std::reference_wrapper<Mapped>>`: like
 *   `try_peek`, but mutable, matching `LruCache::peek_mut`.
 * - `contains(key)` -> `bool`: never promotes, matching
 *   `LruCache::contains`.
 * - `try_remove(key)` -> `result<Mapped>`: removes and returns @p key's
 *   value, matching `LruCache::pop`.
 * - `clear()`: drops every entry, matching `LruCache::clear`.
 * - `size()`/`capacity()`/`empty()`/`is_full()`: matching
 *   `LruCache::len()`/`::cap()`/`::is_empty()`, plus a convenience
 *   `is_full()` reloco's own containers don't otherwise need (a fixed
 *   capacity that is never grown is unique to `lru_cache` among reloco's
 *   map-shaped containers).
 * - `begin()`/`end()`: a `const_iterator` walking every live entry from
 *   most- to least-recently-used (`operator*` returns `std::pair<const
 *   Key &, const Mapped &>`), matching `LruCache::iter()`; iterating
 *   never itself promotes anything, same as `peek`.
 *
 * Unlike `flat_hash_map`, capacity is fixed at `try_create(capacity,
 * alloc)` and never grows -- `try_put` on an already-full cache always
 * evicts instead of allocating, so (after the one-time `try_create`
 * allocation) every subsequent `try_put`/`try_get`/`try_peek`/
 * `try_remove` call is itself allocation-free, matching the constant-
 * memory-footprint use case an LRU cache exists for in the first place.
 *
 * @code
 * auto cache_res = reloco::lru_cache<int, reloco::string>::try_create(2);
 * if (!cache_res)
 *   return;
 * auto &cache = *cache_res;
 * std::ignore = cache.try_put(1, reloco::string("one"));
 * std::ignore = cache.try_put(2, reloco::string("two"));
 * std::ignore = cache.try_get(1);           // promotes 1 -- 2 is now LRU
 * std::ignore = cache.try_put(3, reloco::string("three")); // evicts 2
 * assert(!cache.contains(2));
 * assert(cache.contains(1) && cache.contains(3));
 * @endcode
 */

#include "construction_helpers.hpp"
#include "container_ref.hpp"
#include "default_allocator.hpp"
#include "error.hpp"
#include "flat_hash_map.hpp"
#include "optional.hpp"
#include "relocatable.hpp"
#include "vector.hpp"

#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace reloco {

template <typename Key, typename Mapped, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class RELOCO_OWNER lru_cache {
  static_assert(std::is_copy_constructible_v<Key>,
                "lru_cache requires Key to be copy-constructible: a newly inserted key is stored once "
                "(moved) in the intrusive list entry and once (copied) as the flat_hash_map index's own "
                "key -- see this file's top-of-file doc comment for why.");

public:
  using key_type = Key;
  using mapped_type = Mapped;
  using size_type = std::size_t;

  static constexpr size_type npos = static_cast<size_type>(-1);

private:
  struct node {
    Key key;
    Mapped value;
    size_type prev = npos;
    size_type next = npos;

    node(Key k, Mapped v) noexcept : key(std::move(k)), value(std::move(v)) {}
  };

public:
  /**
   * @brief `const_iterator` walking every live entry from most- to
   * least-recently-used, matching `LruCache::iter()`. Never promotes
   * anything, same as `try_peek`.
   */
  class const_iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::pair<const Key &, const Mapped &>;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = value_type;

    constexpr const_iterator() noexcept = default;

    [[nodiscard]] value_type operator*() const noexcept {
      const node &n = nodes_->operator[](idx_).value();
      return value_type(n.key, n.value);
    }

    const_iterator &operator++() noexcept {
      idx_ = nodes_->operator[](idx_).value().next;
      return *this;
    }
    const_iterator operator++(int) noexcept {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    [[nodiscard]] friend bool operator==(const const_iterator &lhs, const const_iterator &rhs) noexcept {
      return lhs.idx_ == rhs.idx_;
    }
    [[nodiscard]] friend bool operator!=(const const_iterator &lhs, const const_iterator &rhs) noexcept {
      return !(lhs == rhs);
    }

  private:
    friend class lru_cache;
    const_iterator(const vector<optional<node>> *nodes, size_type idx) noexcept : nodes_(nodes), idx_(idx) {}

    const vector<optional<node>> *nodes_ = nullptr;
    size_type idx_ = npos;
  };

  constexpr lru_cache() noexcept = default;

  lru_cache(const lru_cache &) = delete;
  lru_cache &operator=(const lru_cache &) = delete;
  lru_cache(lru_cache &&) noexcept = default;
  lru_cache &operator=(lru_cache &&) noexcept = default;

  /**
   * @brief Allocates a cache holding at most @p capacity entries. Fails
   * with `error::invalid_argument` if @p capacity is `0`, or with
   * whatever the allocator itself fails with.
   */
  [[nodiscard]] static result<lru_cache> try_create(size_type capacity,
                                                     allocator_ref alloc = default_allocator()) noexcept {
    if (capacity == 0)
      return unexpected(error::invalid_argument);

    lru_cache cache;
    cache.capacity_ = capacity;

    auto nodes_res = vector<optional<node>>::try_allocate(alloc, capacity);
    if (!nodes_res)
      return unexpected(nodes_res.error());
    cache.nodes_ = std::move(*nodes_res);
    if (auto resize_res = cache.nodes_.try_resize(capacity); !resize_res)
      return unexpected(resize_res.error());

    auto free_res = vector<size_type>::try_allocate(alloc, capacity);
    if (!free_res)
      return unexpected(free_res.error());
    cache.free_ = std::move(*free_res);
    for (size_type i = capacity; i-- > 0;)
      std::ignore = cache.free_.try_push_back(i);

    auto index_res = flat_hash_map<Key, size_type, Hash, KeyEqual>::try_allocate(alloc);
    if (!index_res)
      return unexpected(index_res.error());
    cache.index_ = std::move(*index_res);
    if (auto reserve_res = cache.index_.try_reserve(capacity); !reserve_res)
      return unexpected(reserve_res.error());

    return cache;
  }

  /**
   * @brief Performs a deep copy using a specific allocator. Entries are
   * re-inserted from least- to most-recently-used, so the clone's order
   * matches the source's exactly, unlike `flat_hash_map::try_clone`
   * (which makes no such promise, since plain hash-table order was never
   * meaningful in the first place).
   */
  [[nodiscard]] result<lru_cache> try_clone(allocator_ref alloc) const noexcept {
    auto cloned = try_create(capacity_, alloc);
    if (!cloned)
      return unexpected(cloned.error());
    for (size_type idx = tail_; idx != npos; idx = nodes_[idx].value().prev) {
      const node &n = nodes_[idx].value();
      auto key_copy = construction_helpers::try_clone(alloc, n.key);
      if (!key_copy)
        return unexpected(key_copy.error());
      auto value_copy = construction_helpers::try_clone(alloc, n.value);
      if (!value_copy)
        return unexpected(value_copy.error());
      auto put_res = cloned->try_put(std::move(*key_copy), std::move(*value_copy));
      if (!put_res)
        return unexpected(put_res.error());
    }
    return std::move(*cloned);
  }

  [[nodiscard]] result<lru_cache> try_clone() const noexcept { return try_clone(nodes_.get_allocator()); }

  [[nodiscard]] allocator_ref get_allocator() const noexcept { return nodes_.get_allocator(); }

  [[nodiscard]] size_type size() const noexcept { return index_.size(); }
  [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }
  [[nodiscard]] bool is_full() const noexcept { return size() == capacity_; }

  /**
   * @brief Inserts @p key/@p value, or overwrites @p value and promotes
   * @p key to most-recently-used if already present. Evicts the current
   * least-recently-used entry first if the cache is already at
   * `capacity()` and @p key is not already present. Fails with
   * `error::capacity_exceeded` only when `capacity() == 0` (nothing to
   * evict); otherwise infallible once `try_create` itself has succeeded.
   */
  [[nodiscard]] result<void> try_put(Key key, Mapped value) & noexcept {
    if (auto existing = index_.try_at(key)) {
      size_type idx = existing->get();
      nodes_[idx].value().value = std::move(value);
      list_touch(idx);
      return {};
    }

    size_type idx;
    if (!free_.empty()) {
      idx = free_.back();
      std::ignore = free_.try_pop_back();
    } else {
      if (tail_ == npos)
        return unexpected(error::capacity_exceeded);
      idx = tail_;
      std::ignore = index_.try_remove(nodes_[idx].value().key);
      list_unlink(idx);
      nodes_[idx].reset();
    }

    Key key_for_index = key;
    nodes_[idx].emplace(std::move(key), std::move(value));
    list_push_front(idx);

    auto insert_res = index_.try_insert(std::move(key_for_index), idx);
    if (!insert_res) {
      // Unreachable in practice (the key was just confirmed absent above),
      // but stay fallible and unwind the node rather than leave a
      // list-linked entry with no index pointing at it.
      list_unlink(idx);
      nodes_[idx].reset();
      std::ignore = free_.try_push_back(idx);
      return unexpected(insert_res.error());
    }
    return {};
  }

  /**
   * @brief Looks up @p key and promotes it to most-recently-used,
   * returning a mutable reference to its value. Fails with
   * `error::not_found` if absent.
   */
  template <typename K>
  [[nodiscard]] result<std::reference_wrapper<Mapped>> try_get(const K &key) & noexcept {
    auto found = index_.try_at(key);
    if (!found)
      return unexpected(found.error());
    size_type idx = found->get();
    list_touch(idx);
    return std::ref(nodes_[idx].value().value);
  }

  /**
   * @brief Looks up @p key without promoting it, returning a read-only
   * reference to its value. Fails with `error::not_found` if absent.
   */
  template <typename K>
  [[nodiscard]] result<std::reference_wrapper<const Mapped>> try_peek(const K &key) const & noexcept {
    auto found = index_.try_at(key);
    if (!found)
      return unexpected(found.error());
    return std::cref(nodes_[found->get()].value().value);
  }

  /**
   * @brief Like `try_peek`, but returns a mutable reference without
   * promoting @p key.
   */
  template <typename K>
  [[nodiscard]] result<std::reference_wrapper<Mapped>> try_peek_mut(const K &key) & noexcept {
    auto found = index_.try_at(key);
    if (!found)
      return unexpected(found.error());
    return std::ref(nodes_[found->get()].value().value);
  }

  /**
   * @brief Returns whether @p key is present, without promoting it.
   */
  template <typename K> [[nodiscard]] bool contains(const K &key) const noexcept { return index_.contains(key); }

  /**
   * @brief Removes and returns @p key's value. Fails with
   * `error::not_found` if absent.
   */
  template <typename K> [[nodiscard]] result<Mapped> try_remove(const K &key) & noexcept {
    auto found = index_.try_remove_entry(key);
    if (!found)
      return unexpected(found.error());
    size_type idx = found->second;
    list_unlink(idx);
    Mapped value(std::move(nodes_[idx].value().value));
    nodes_[idx].reset();
    std::ignore = free_.try_push_back(idx);
    return value;
  }

  /** @brief Drops every entry. `capacity()` is unchanged. */
  void clear() noexcept {
    for (size_type idx = head_; idx != npos;) {
      size_type next = nodes_[idx].value().next;
      nodes_[idx].reset();
      idx = next;
    }
    index_.clear();
    free_.clear();
    for (size_type i = capacity_; i-- > 0;)
      std::ignore = free_.try_push_back(i);
    head_ = npos;
    tail_ = npos;
  }

  /** @brief Most- to least-recently-used iteration. Never promotes. */
  [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(&nodes_, head_); }
  [[nodiscard]] const_iterator end() const noexcept { return const_iterator(&nodes_, npos); }

private:
  void list_unlink(size_type idx) noexcept {
    node &n = nodes_[idx].value();
    if (n.prev != npos)
      nodes_[n.prev].value().next = n.next;
    else
      head_ = n.next;
    if (n.next != npos)
      nodes_[n.next].value().prev = n.prev;
    else
      tail_ = n.prev;
    n.prev = npos;
    n.next = npos;
  }

  void list_push_front(size_type idx) noexcept {
    node &n = nodes_[idx].value();
    n.prev = npos;
    n.next = head_;
    if (head_ != npos)
      nodes_[head_].value().prev = idx;
    head_ = idx;
    if (tail_ == npos)
      tail_ = idx;
  }

  void list_touch(size_type idx) noexcept {
    if (head_ == idx)
      return;
    list_unlink(idx);
    list_push_front(idx);
  }

  vector<optional<node>> nodes_;
  vector<size_type> free_;
  flat_hash_map<Key, size_type, Hash, KeyEqual> index_;
  size_type head_ = npos;
  size_type tail_ = npos;
  size_type capacity_ = 0;
};

/**
 * @brief `lru_cache<Key, Mapped, Hash, KeyEqual>` is trivially relocatable
 * exactly when `Hash` and `KeyEqual` are -- same rationale as
 * `flat_hash_map`: every `Key`/`Mapped` instance lives inside one of
 * `nodes_`/`index_`'s own heap-owned backing arrays, which are themselves
 * unconditionally relocatable regardless of what they store.
 */
template <typename Key, typename Mapped, typename Hash, typename KeyEqual>
struct is_trivially_relocatable<lru_cache<Key, Mapped, Hash, KeyEqual>>
    : std::conjunction<is_trivially_relocatable<Hash>, is_trivially_relocatable<KeyEqual>> {};

} // namespace reloco
