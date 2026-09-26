// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file flat_hash_base.hpp
 * @brief `flat_hash_base<T, Hash, KeyEqual, KeyOf>`: an open-addressing,
 * linear-probing hash table backed by a single `vector<optional<T>>` --
 * the "flat" (contiguous, vector-backed) counterpart to `tree_base`'s
 * node-based storage, exactly like `flat_set`/`flat_map` are the
 * contiguous counterpart to `tree_set`/`tree_map`, only unordered.
 *
 * Storage is deliberately built on two already-existing, already-tested
 * reloco types rather than new low-level plumbing (unlike `tree_base`,
 * which needed its own `node_base` allocation/layout engine):
 * `vector<optional<T>>` is the one contiguous backing array (growable,
 * heap-allocated, unconditionally relocatable regardless of `T` -- see
 * `vector.hpp`), and `optional<T>` gives each slot a built-in "empty" state
 * without requiring `T` to be default-constructible. Because insertion
 * always hands `try_insert` an already-fully-constructed `value_type`
 * (a by-value sink parameter, same convention as every other reloco
 * container's `try_insert`), placing it into a slot is an ordinary
 * `optional<T>::emplace(std::move(value))` -- a ordinary, non-fallible
 * move-construction -- rather than routing through
 * `construction_helpers::try_construct` (which exists to let a single
 * function support several different *construction argument* protocols;
 * here there is only ever one already-built `T` to move in).
 *
 * Deletion uses backward-shift deletion (no tombstones): removing a slot
 * immediately walks its probe sequence forward, moving any element whose
 * probe sequence would otherwise skip over the freed slot back into it, one
 * position at a time, until an already-empty slot is reached. This keeps
 * every element within `contains`/`try_find`'s probe reach without ever
 * needing to special-case "deleted" slots during lookup (see
 * `erase_slot`'s doc comment for the exact invariant).
 *
 * Growth is a classic power-of-two doubling with a fixed maximum load
 * factor: `capacity()` is always `0` or a power of two, `size() + 1` is
 * kept `<= capacity() * max_load_factor_numerator / max_load_factor_denominator`
 * (`7/8`, chosen to keep probe
 * sequences short without wasting more than 1/8th of the backing array),
 * and growing rehashes every live element into a freshly allocated,
 * larger `vector<optional<T>>` in one pass.
 *
 * Like `tree_base` (see that file's doc comment and the "is it worth
 * writing a type-erased flat container base?" analysis it links to),
 * `Hash`/`KeyEqual`/`KeyOf` are ordinary template parameters, not function
 * pointers behind a `void*` boundary: every hash/equality comparison on
 * the hot `try_insert`/`try_find`/`try_remove` path can still be inlined,
 * and `Hash`/`KeyEqual` are almost always small, stateless types.
 *
 * Unlike `tree_base`, though, `flat_hash_base` has no `.ipp` file at all:
 * `tree_base` could move its two heaviest routines (`bst_unlink`'s CLRS
 * deletion, `bst_clear`'s iterative teardown) out of the header because
 * neither one ever calls `Compare` -- they only relink/destroy
 * `node_header` pointers, which is exactly as non-template as
 * `node_base`'s own layout math. `grow_to` (rehashing every live element
 * into a larger array) and `erase_slot` (the backward-shift walk above)
 * have no such Compare-free structural core to peel off: both call
 * `Hash{}(key_of(value))` on every single element they touch, since a
 * slot's rehash target (`grow_to`) and a probe sequence's reachability
 * test (`erase_slot`) are only knowable by actually hashing that
 * element's key. Erasing either routine to a single, `T`-independent
 * `.ipp` definition would mean routing every one of those hash calls
 * through a `void*`-erased function pointer -- on the same amortized-O(1)
 * hot path this whole design exists to keep inlined -- which would cost
 * more than the (typically small) template-instantiation duplication it
 * would save, especially since `Hash`/`KeyEqual` are almost always small
 * stateless types with only a handful of distinct instantiations in any
 * one program.
 */

#include "../construction_helpers.hpp"
#include "../default_allocator.hpp"
#include "../error.hpp"
#include "../expected.hpp"
#include "../lifetime.hpp"
#include "../optional.hpp"
#include "../vector.hpp"
#include "flat_container_base.hpp"

#include <cstddef>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

namespace reloco::detail {

/**
 * @brief Forward, read-only iterator over a `flat_hash_base<T, Hash,
 * KeyEqual, KeyOf>`'s backing slot array, skipping empty slots. Iteration
 * order matches slot order in the backing array, which depends on hash
 * values and insertion/removal history -- exactly as unspecified as Rust
 * `HashMap`/`HashSet` iteration order, and for the same reason.
 */
template <typename T> class flat_hash_const_iterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = const T *;
  using reference = const T &;

  constexpr flat_hash_const_iterator() noexcept = default;

  flat_hash_const_iterator(const optional<T> *slot, const optional<T> *end) noexcept : slot_(slot), end_(end) {
    skip_empty();
  }

  [[nodiscard]] reference operator*() const noexcept { return **slot_; }
  [[nodiscard]] pointer operator->() const noexcept { return &**this; }

  flat_hash_const_iterator &operator++() noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    ++slot_;
    RELOCO_END_UNSAFE_BUFFER_USAGE
    skip_empty();
    return *this;
  }
  flat_hash_const_iterator operator++(int) noexcept {
    auto tmp = *this;
    ++*this;
    return tmp;
  }

  [[nodiscard]] friend bool operator==(const flat_hash_const_iterator &lhs,
                                       const flat_hash_const_iterator &rhs) noexcept {
    return lhs.slot_ == rhs.slot_;
  }
  [[nodiscard]] friend bool operator!=(const flat_hash_const_iterator &lhs,
                                       const flat_hash_const_iterator &rhs) noexcept {
    return !(lhs == rhs);
  }

private:
  void skip_empty() noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    while (slot_ != end_ && !slot_->has_value())
      ++slot_;
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  const optional<T> *slot_ = nullptr;
  const optional<T> *end_ = nullptr;
};

/**
 * @brief An open-addressing, linear-probing, allocator-backed hash table
 * over `T`, keyed by `KeyOf{}(value)`, hashed by `Hash`, compared by
 * `KeyEqual`. See the file-level comment for the storage/deletion/growth
 * design.
 */
template <typename T, typename Hash, typename KeyEqual, typename KeyOf> class RELOCO_EXPORT flat_hash_base {
public:
  using value_type = T;
  using key_type = std::decay_t<decltype(KeyOf{}(std::declval<const value_type &>()))>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using const_reference = const value_type &;
  using const_pointer = const value_type *;
  using const_iterator = flat_hash_const_iterator<value_type>;

  /// @brief Never let the table get more than 7/8ths full: keeps average
  /// probe length short without the backing array wasting much space.
  static constexpr size_type max_load_factor_numerator = 7;
  static constexpr size_type max_load_factor_denominator = 8;
  /// @brief Smallest non-zero capacity a table grows to; always a power
  /// of two, like every larger capacity.
  static constexpr size_type min_capacity = 8;

  constexpr explicit flat_hash_base(allocator_ref alloc = default_allocator()) noexcept : slots_(alloc) {}

  flat_hash_base(const flat_hash_base &) = delete;
  flat_hash_base &operator=(const flat_hash_base &) = delete;

  flat_hash_base(flat_hash_base &&other) noexcept
      : slots_(std::move(other.slots_)), size_(std::exchange(other.size_, 0)), mask_(std::exchange(other.mask_, 0)) {}

  flat_hash_base &operator=(flat_hash_base &&other) noexcept {
    if (this != &other) {
      slots_ = std::move(other.slots_);
      size_ = std::exchange(other.size_, 0);
      mask_ = std::exchange(other.mask_, 0);
    }
    return *this;
  }

  [[nodiscard]] static result<flat_hash_base> try_allocate(allocator_ref alloc) noexcept {
    return flat_hash_base(alloc);
  }
  [[nodiscard]] static result<flat_hash_base> try_create() noexcept { return try_allocate(default_allocator()); }

  /**
   * @brief Performs a deep copy using a specific allocator. Reinserts
   * every element rather than replicating the source's exact slot
   * layout, so the clone's slot order only matches the source's by
   * coincidence -- both are valid tables over the same elements either
   * way, and rehashing on clone means the clone never inherits a
   * degenerate probe-length history from the source.
   */
  [[nodiscard]] result<flat_hash_base> try_clone(allocator_ref alloc) const noexcept {
    auto cloned = try_allocate(alloc);
    if (!cloned)
      return unexpected(cloned.error());
    auto reserve_res = cloned->try_reserve(size_);
    if (!reserve_res)
      return unexpected(reserve_res.error());
    for (const auto &value : *this) {
      auto value_copy = construction_helpers::try_clone(alloc, value);
      if (!value_copy)
        return unexpected(value_copy.error());
      auto insert_res = cloned->try_insert(std::move(*value_copy));
      if (!insert_res)
        return unexpected(insert_res.error());
    }
    return std::move(*cloned);
  }

  [[nodiscard]] result<flat_hash_base> try_clone() const noexcept { return try_clone(slots_.get_allocator()); }

  [[nodiscard]] allocator_ref get_allocator() const noexcept { return slots_.get_allocator(); }

  [[nodiscard]] size_type size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] size_type capacity() const noexcept { return slots_.size(); }
  /**
   * @brief Current load as parts-per-thousand (e.g. `875` for exactly
   * `7/8`) -- an integer, never a `float`/`double`: reloco avoids
   * floating point everywhere (kernel/bare-metal code frequently cannot
   * use the FPU at all without extra save/restore ceremony), including
   * in diagnostic-only accessors like this one. `size_ * 1000` cannot
   * itself overflow before `capacity()` would, since `size_ <=
   * capacity()` always holds.
   */
  [[nodiscard]] size_type load_factor_permille() const noexcept {
    return capacity() == 0 ? 0 : (size_ * 1000) / capacity();
  }

  void clear() noexcept {
    for (auto &slot : slots_)
      slot.reset();
    size_ = 0;
  }

  /**
   * @brief Ensures room for at least @p additional more elements without
   * growing again, rounding up to the next power-of-two capacity that
   * keeps `size() + additional` within the max load factor. A no-op if the
   * current capacity already suffices.
   */
  [[nodiscard]] result<void> try_reserve(size_type additional) & noexcept {
    if (additional == 0)
      return {};
    size_type required = size_ + additional;
    size_type target = capacity_for(required);
    if (target <= capacity())
      return {};
    return grow_to(target);
  }

  /**
   * @brief Inserts @p value, keyed by `KeyOf{}(value)`. Fails with
   * `error::already_exists` if an element with an equivalent key is
   * already present, or with whatever `try_reserve` fails with
   * (allocation failure) if growing the table is needed first.
   *
   * Takes @p value by value for the same reason as
   * `flat_container_base::try_insert`/`tree_base::try_insert`: a by-value
   * sink parameter accepts both lvalues (one copy) and rvalues (moved in
   * with no extra copy) uniformly.
   */
  [[nodiscard]] result<std::reference_wrapper<value_type>>
  try_insert(value_type value) & noexcept RELOCO_LIFETIMEBOUND {
    const KeyOf key_of{};
    const key_type &key = key_of(value);
    if (find_slot(key) != npos)
      return unexpected(error::already_exists);

    auto reserve_res = try_reserve(1);
    if (!reserve_res)
      return unexpected(reserve_res.error());

    size_type index = probe_for_insert(key);
    T &placed = slots_[index].emplace(std::move(value));
    ++size_;
    return std::ref(placed);
  }

  template <typename Key> [[nodiscard]] bool contains(const Key &key) const noexcept { return find_slot(key) != npos; }

  template <typename Key>
  [[nodiscard]] result<std::reference_wrapper<const value_type>>
  try_find(const Key &key) const & noexcept RELOCO_LIFETIMEBOUND {
    size_type index = find_slot(key);
    if (index == npos)
      return unexpected(error::not_found);
    return std::cref(*slots_[index]);
  }

  template <typename Key> [[nodiscard]] result<void> try_remove(const Key &key) & noexcept {
    size_type index = find_slot(key);
    if (index == npos)
      return unexpected(error::not_found);
    erase_slot(index);
    return {};
  }

  /**
   * @brief Rust `HashSet::take`/`HashMap::remove_entry` equivalent:
   * removes and returns the element keyed by @p key, rather than just
   * discarding it like `try_remove`. Fails with `error::not_found` if
   * @p key is absent.
   */
  template <typename Key> [[nodiscard]] result<value_type> try_take(const Key &key) & noexcept {
    size_type index = find_slot(key);
    if (index == npos)
      return unexpected(error::not_found);
    value_type value(std::move(*slots_[index]));
    erase_slot(index);
    return value;
  }

  [[nodiscard]] const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND {
    if (capacity() == 0)
      return const_iterator();
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return const_iterator(slots_.data(), slots_.data() + slots_.size());
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND {
    if (capacity() == 0)
      return const_iterator();
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return const_iterator(slots_.data() + slots_.size(), slots_.data() + slots_.size());
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return begin(); }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return end(); }

  auto begin() const && = delete;
  auto end() const && = delete;
  auto cbegin() const && = delete;
  auto cend() const && = delete;

  template <typename Fn> void for_each(Fn &&fn) const {
    for (const auto &value : *this) {
      fn(value);
    }
  }

  /**
   * @brief Rust `HashSet::retain`/`HashMap::retain` equivalent: keeps
   * only the elements for which @p pred(value) returns `true`. Since
   * removal is backward-shift (no tombstones), the walk below always
   * re-examines the slot an earlier removal may have just shifted a live
   * element into, rather than a plain forward-only index walk, so no live
   * element is ever skipped.
   */
  template <typename Pred> void retain(Pred &&pred) & noexcept {
    if (capacity() == 0)
      return;
    for (size_type index = 0; index < capacity();) {
      if (slots_[index].has_value() && !pred(std::as_const(*slots_[index]))) {
        erase_slot(index);
        // A backward-shifted element may now occupy `index`; re-examine
        // it instead of advancing.
        continue;
      }
      ++index;
    }
  }

  /**
   * @brief Rust `HashSet::is_subset` equivalent: `true` if every element
   * of `*this` (compared by key) is also present in @p other. `O(size())`
   * hash lookups into @p other.
   */
  [[nodiscard]] bool is_subset(const flat_hash_base &other) const noexcept {
    const KeyOf key_of{};
    for (const auto &value : *this) {
      if (!other.contains(key_of(value)))
        return false;
    }
    return true;
  }

  /**
   * @brief Rust `HashSet::is_superset` equivalent: `true` if every
   * element of @p other is also present in `*this` (i.e. `other.is_subset(
   * *this)`).
   */
  [[nodiscard]] bool is_superset(const flat_hash_base &other) const noexcept { return other.is_subset(*this); }

  /**
   * @brief Rust `HashSet::is_disjoint` equivalent: `true` if `*this` and
   * @p other share no keys. `O(min(size(), other.size()))` hash lookups,
   * probing whichever of the two tables is smaller into the larger one.
   */
  [[nodiscard]] bool is_disjoint(const flat_hash_base &other) const noexcept {
    const flat_hash_base &smaller = size() <= other.size() ? *this : other;
    const flat_hash_base &larger = size() <= other.size() ? other : *this;
    const KeyOf key_of{};
    for (const auto &value : smaller) {
      if (larger.contains(key_of(value)))
        return false;
    }
    return true;
  }

private:
  static constexpr size_type npos = static_cast<size_type>(-1);

  /// @brief `capacity()` slots kept occupied at the max load factor (`7/8`),
  /// computed with integer arithmetic (never floating point) so growth
  /// decisions stay exact at every scale: `min_capacity` and every later
  /// capacity are powers of two and therefore already multiples of 8, so
  /// `cap / 8 * 7` never loses precision the way a float computation
  /// could for very large `cap`.
  [[nodiscard]] static constexpr size_type threshold_for(size_type cap) noexcept { return cap / 8 * 7; }

  [[nodiscard]] static constexpr size_type capacity_for(size_type required_size) noexcept {
    size_type cap = min_capacity;
    while (required_size > threshold_for(cap))
      cap *= 2;
    return cap;
  }

  template <typename Key> [[nodiscard]] size_type find_slot(const Key &key) const noexcept {
    if (capacity() == 0)
      return npos;
    const KeyOf key_of{};
    size_type index = hash_(key) & mask_;
    for (size_type probes = 0; probes <= mask_; ++probes) {
      if (!slots_[index].has_value())
        return npos;
      if (eq_(key_of(*slots_[index]), key))
        return index;
      index = (index + 1) & mask_;
    }
    return npos;
  }

  [[nodiscard]] size_type probe_for_insert(const key_type &key) const noexcept {
    size_type index = hash_(key) & mask_;
    while (slots_[index].has_value())
      index = (index + 1) & mask_;
    return index;
  }

  /**
   * @brief Allocates a fresh `capacity`-sized slot array and rehashes
   * every currently-live element into it (guaranteed to succeed without
   * growing again, since @p capacity was already sized to fit `size_`
   * within the max load factor), then swaps it in. The only fallible step
   * is the initial allocation itself.
   */
  [[nodiscard]] result<void> grow_to(size_type new_capacity) & noexcept {
    auto new_slots_res = vector<optional<T>>::try_allocate(get_allocator());
    if (!new_slots_res)
      return unexpected(new_slots_res.error());
    auto new_slots = std::move(*new_slots_res);
    auto resize_res = new_slots.try_resize(new_capacity);
    if (!resize_res)
      return unexpected(resize_res.error());

    const KeyOf key_of{};
    size_type new_mask = new_capacity - 1;
    for (auto &slot : slots_) {
      if (!slot.has_value())
        continue;
      size_type index = hash_(key_of(*slot)) & new_mask;
      while (new_slots[index].has_value())
        index = (index + 1) & new_mask;
      new_slots[index].emplace(std::move(*slot));
    }

    slots_ = std::move(new_slots);
    mask_ = new_mask;
    return {};
  }

  /**
   * @brief Removes the element at @p index (backward-shift deletion, no
   * tombstones): clears the slot, then walks its probe sequence forward,
   * relocating each subsequent occupied slot back into the growing hole
   * whenever its own ideal slot does not lie strictly between the hole
   * and its current position (in probe order) -- i.e. whenever leaving it
   * in place would make it unreachable from its own ideal slot once the
   * hole is gone. This is the standard "deletion without tombstones"
   * algorithm for linear-probed open addressing.
   */
  void erase_slot(size_type hole) noexcept {
    const KeyOf key_of{};
    slots_[hole].reset();
    --size_;
    size_type scan = hole;
    for (;;) {
      scan = (scan + 1) & mask_;
      if (!slots_[scan].has_value())
        break;
      size_type ideal = hash_(key_of(*slots_[scan])) & mask_;
      bool reachable_without_moving =
          (hole <= scan) ? (hole < ideal && ideal <= scan) : (hole < ideal || ideal <= scan);
      if (reachable_without_moving)
        continue;
      slots_[hole] = std::move(slots_[scan]);
      slots_[scan].reset();
      hole = scan;
    }
  }

  vector<optional<T>> slots_{};
  size_type size_ = 0;
  size_type mask_ = 0;
  Hash hash_{};
  KeyEqual eq_{};
};

} // namespace reloco::detail
