// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file intrusive_hash_table.hpp
 * @brief `intrusive_hash_table<T, Hook, KeyOf, Hash, KeyEqual>`: a
 * unique-key hash table that never allocates -- matching Linux's
 * `hlist_head`/`hlist_node` (`<linux/list.h>`) or Boost.Intrusive's
 * `unordered_set`, rather than any reloco container seen so far.
 *
 * Every other reloco map/set (`flat_hash_map`, `tree_map`, ...) *owns*
 * the storage backing each element -- inserting copies or moves a value
 * in, and the container's own allocator gives it back on removal/
 * destruction. That is the wrong shape for code that must run before an
 * allocator subsystem is even up (early kernel boot, an interrupt
 * handler, a `RELOCO_TLS_MODEL_OS`/`RELOCO_MUTEX_BACKEND_CUSTOM` port's
 * own internals -- see `detail/porting/README.md`): `intrusive_hash_table`
 * never allocates *at all*, at any point, because it never owns a single
 * byte of node or bucket storage. Nodes are ordinary caller-owned
 * objects (static, stack, a `slab`/pool the caller manages some other
 * way) that embed an `intrusive_hash_hook<T>` as a named member; the
 * bucket array is a `span<T *>` over caller-owned memory too (a `static
 * T *buckets[N];`, in the simplest bare-metal case). The table itself
 * holds nothing but that `span` and a running element count -- inserting/
 * removing a node only ever rewrites a handful of existing pointers.
 *
 * **Hook**: `struct my_node { reloco::intrusive_hash_hook<my_node> hook; ...
 * };` -- a named member, not a CRTP base, so one object can carry
 * independent hooks for several different tables (exactly like
 * Boost.Intrusive's member-hook style; Linux's embedded `struct
 * hlist_node` inside the owning struct is the same idea). The table is
 * templated on a pointer to that member (`&my_node::hook`), used purely
 * at compile time to get from a `T &` to its hook and back -- there is no
 * `offsetof`/reinterpret-cast trick anywhere in this file.
 *
 * **Chaining**: doubly-linked (`next`/`pprev`, matching `hlist_node`
 * exactly, including the "`pprev` is a pointer *to the pointer that
 * points at this node*" trick -- either a bucket slot or another node's
 * `next` field, uniformly, since both have type `T *`). This is what lets
 * `remove(node)` unlink in O(1) given only a node reference already in
 * hand, with no hashing or bucket-chain walk -- unlike `try_remove(key)`,
 * which still has to hash+walk to *find* the node in the first place.
 *
 * **Key extraction**: a `KeyOf` functor template parameter (`KeyOf{}(const
 * T &) -> const key_type &`), exactly like `detail::flat_hash_base`'s
 * `KeyOf` -- see that file's doc comment for the same rationale (a
 * template parameter, not a type-erased callback, so every hash/equality
 * comparison on the hot path still inlines).
 *
 * **Growing (`rehash`)**: since the bucket array is caller-owned, growing
 * it is a two-step, caller-driven protocol rather than something
 * `try_insert` ever does on its own (contrast `flat_hash_base::grow_to`,
 * which reallocates internally under the hood): the caller allocates a
 * *new*, larger `span<T *>` however it likes -- critically, **without**
 * holding whatever lock guards concurrent access to the table -- then
 * calls `rehash(new_buckets)`, which walks every currently linked node
 * and re-threads it into the new array, and only *that* call needs to
 * happen while holding the lock:
 *
 * @code
 * // Some other thread may still be doing try_find/contains right up
 * // until the guard below is taken.
 * auto new_storage = allocate_bucket_array(old_bucket_count * 2); // slow;
 *                                                                  // deliberately outside the lock
 * {
 *   auto guard = table_mutex.lock(); // held only for the O(n) relink below
 *   std::ignore = table.rehash(reloco::span<my_node *>(new_storage, new_count));
 * } // old bucket array (now unreferenced) may be freed by the caller here
 * @endcode
 *
 * `rehash` itself never allocates either -- it only ever rewrites `next`/
 * `pprev` pointers and the new array's slots, exactly like `try_insert`/
 * `remove` do. Nothing in this file ever calls an allocator.
 *
 * - `try_create(buckets)` -> `result<intrusive_hash_table>`: adopts
 *   @p buckets (zeroing every slot first), failing with
 *   `error::invalid_argument` if it is empty.
 * - `try_insert(node)` -> `result<void>`: fails with
 *   `error::already_exists` if an equivalent key is already present.
 *   `RELOCO_ASSERT`s that @p node is not already linked into *this* or
 *   any other `intrusive_hash_table` -- inserting an already-linked node
 *   would silently corrupt whichever table it was already threaded into.
 * - `try_find(key)`/`contains(key)` -> lookup, hashing+walking one bucket
 *   chain, same asymptotic cost as `flat_hash_map`.
 * - `try_remove(key)` -> `result<void>`: hashes+walks to find the node,
 *   then unlinks it -- fails with `error::not_found` if absent.
 * - `remove(node)`: O(1) unlink given a node reference already in hand
 *   (see "Chaining" above). `RELOCO_ASSERT`s @p node is currently linked.
 * - `rehash(new_buckets)` -> `result<void>`: see "Growing" above.
 * - `clear()`: unlinks every node (leaving each one's own storage
 *   otherwise untouched -- there is nothing to destroy, `T` is never
 *   copied/moved/destroyed by this file at all) and zeroes every bucket.
 *
 * Unlike every other reloco container, `intrusive_hash_table` has no
 * `try_clone`: cloning would require deciding where the clone's nodes
 * live, which is exactly the decision this whole file exists to leave to
 * the caller.
 */

#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "span.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief The embedded hook a node type must carry (as a named member, see
 * this file's doc comment) to be usable with `intrusive_hash_table`.
 * `next`/`pprev` follow Linux's `struct hlist_node` layout exactly: `next`
 * points at the next node in this hook's bucket chain (or `nullptr` at
 * the chain's end); `pprev` points at *whichever pointer currently
 * points at this node* -- either a bucket array slot or another node's
 * own `next` field, both `T *` either way -- which is what lets `unlink`
 * (see `intrusive_hash_table::remove`) rewrite that one pointer directly
 * instead of having to walk the chain to find this node's predecessor.
 */
template <typename T> struct intrusive_hash_hook {
  T *next = nullptr;
  T **pprev = nullptr;

  /** @brief Whether this hook is currently threaded into some table. */
  [[nodiscard]] constexpr bool is_linked() const noexcept { return pprev != nullptr; }
};

/**
 * @brief See the file-level doc comment. @p Hook is a pointer to the
 * `intrusive_hash_hook<T>` member @p T embeds; @p KeyOf extracts a node's
 * key (`KeyOf{}(const T &) -> const key_type &`, exactly like
 * `detail::flat_hash_base`'s own `KeyOf`).
 */
template <typename T, intrusive_hash_hook<T> T::*Hook, typename KeyOf,
          typename Hash = std::hash<std::decay_t<decltype(KeyOf{}(std::declval<const T &>()))>>,
          typename KeyEqual = std::equal_to<std::decay_t<decltype(KeyOf{}(std::declval<const T &>()))>>>
class RELOCO_POINTER intrusive_hash_table {
public:
  using value_type = T;
  using key_type = std::decay_t<decltype(KeyOf{}(std::declval<const T &>()))>;
  using size_type = std::size_t;

  constexpr intrusive_hash_table() noexcept = default;

  intrusive_hash_table(const intrusive_hash_table &) = delete;
  intrusive_hash_table &operator=(const intrusive_hash_table &) = delete;

  intrusive_hash_table(intrusive_hash_table &&other) noexcept
      : buckets_(std::exchange(other.buckets_, span<T *>())), size_(std::exchange(other.size_, size_type{0})) {}

  intrusive_hash_table &operator=(intrusive_hash_table &&other) noexcept {
    if (this != &other) {
      buckets_ = std::exchange(other.buckets_, span<T *>());
      size_ = std::exchange(other.size_, size_type{0});
    }
    return *this;
  }

  /**
   * @brief Adopts @p buckets as this table's bucket array, zeroing every
   * slot first. Fails with `error::invalid_argument` if @p buckets is
   * empty. Never allocates -- @p buckets is caller-owned memory (a
   * `static`/stack array, or anything else the caller manages) that must
   * outlive the table.
   */
  [[nodiscard]] static result<intrusive_hash_table> try_create(span<T *> buckets) noexcept {
    if (buckets.empty())
      return unexpected(error::invalid_argument);
    for (auto &slot : buckets)
      slot = nullptr;
    intrusive_hash_table table;
    table.buckets_ = buckets;
    return table;
  }

  [[nodiscard]] size_type size() const & noexcept { return size_; }
  [[nodiscard]] bool empty() const & noexcept { return size_ == 0; }
  [[nodiscard]] size_type bucket_count() const & noexcept { return buckets_.size(); }
  /**
   * @brief Current load as parts-per-thousand, e.g. `1500` for an
   * average chain length of `1.5`. Integer-only, like every other reloco
   * diagnostic accessor (see `detail/flat_hash_base.hpp`'s
   * `load_factor_permille`) -- reloco uses no floating point anywhere,
   * since kernel/bare-metal code (this file's whole reason to exist)
   * frequently cannot use the FPU at all without extra save/restore
   * ceremony.
   */
  [[nodiscard]] size_type load_factor_permille() const & noexcept {
    return buckets_.empty() ? 0 : (size_ * 1000) / buckets_.size();
  }

  // A table over a temporary -- `table_type::try_create(buckets).value().size()` and
  // the like -- is always a caller bug: `intrusive_hash_table` is a `RELOCO_POINTER`
  // view over caller-owned storage, meant to be kept as a named, addressable
  // variable, not chained off a prvalue. See `rvalue_safety.hpp`'s
  // `RELOCO_BLOCK_RVALUE_ACCESS` for the same convention applied to reloco's
  // owning containers.
  size_type size() const && = delete;
  bool empty() const && = delete;
  size_type bucket_count() const && = delete;
  size_type load_factor_permille() const && = delete;


  /**
   * @brief Links @p node in, keyed by `KeyOf{}(node)`. Fails with
   * `error::already_exists` if an equivalent key is already present.
   * `RELOCO_ASSERT`s @p node is not already linked (into *this* or any
   * other `intrusive_hash_table`) -- see the file-level doc comment.
   */
  [[nodiscard]] result<void> try_insert(T &node) & noexcept {
    RELOCO_ASSERT(!hook_of(node).is_linked(),
                  "reloco::intrusive_hash_table::try_insert: node is already linked into a table");
    const key_type &key = KeyOf{}(node);
    if (find_ptr(key) != nullptr)
      return unexpected(error::already_exists);
    link_at(index_for(key), node);
    ++size_;
    return {};
  }

  // Inserting a temporary node would leave the table holding a pointer into
  // storage that no longer exists past the end of this full expression --
  // `intrusive_hash_table` never owns/extends a node's lifetime, so this must
  // be a hard compile error rather than a silently-dangling `try_insert`.
  result<void> try_insert(T &&node) & noexcept = delete;

  template <typename K> [[nodiscard]] bool contains(const K &key) const & noexcept { return find_ptr(key) != nullptr; }
  template <typename K> bool contains(const K &key) const && = delete;

  /** @brief Fails with `error::not_found` if @p key is absent. */
  template <typename K> [[nodiscard]] result<std::reference_wrapper<T>> try_find(const K &key) & noexcept {
    T *found = find_ptr(key);
    if (found == nullptr)
      return unexpected(error::not_found);
    return std::ref(*found);
  }

  /** @brief Fails with `error::not_found` if @p key is absent. */
  template <typename K>
  [[nodiscard]] result<std::reference_wrapper<const T>> try_find(const K &key) const & noexcept {
    const T *found = find_ptr(key);
    if (found == nullptr)
      return unexpected(error::not_found);
    return std::cref(*found);
  }

  template <typename K> result<std::reference_wrapper<const T>> try_find(const K &key) const && = delete;

  /**
   * @brief Hashes+walks to find @p key, then unlinks it. Fails with
   * `error::not_found` if absent. Prefer `remove(node)` -- O(1), no
   * hashing/walking -- when the caller already holds a reference to the
   * node being removed (e.g. it just found it via `try_find`).
   */
  template <typename K> [[nodiscard]] result<void> try_remove(const K &key) & noexcept {
    T *found = find_ptr(key);
    if (found == nullptr)
      return unexpected(error::not_found);
    unlink(*found);
    return {};
  }

  /**
   * @brief Removes @p node in O(1) via its own `pprev` link -- no
   * hashing or bucket-chain walk, matching Linux's `hlist_del`.
   * `RELOCO_ASSERT`s @p node is currently linked into *this* table.
   */
  void remove(T &node) & noexcept {
    RELOCO_ASSERT(hook_of(node).is_linked(), "reloco::intrusive_hash_table::remove: node is not linked");
    unlink(node);
  }

  // Same rationale as `try_insert(T &&)` above -- a temporary node cannot
  // meaningfully be "currently linked" for `remove` to unlink.
  void remove(T &&node) & noexcept = delete;

  /**
   * @brief Re-threads every currently linked node into @p new_buckets
   * (recomputing each one's chain from `Hash{}(key) %
   * new_buckets.size()`), zeroing @p new_buckets first, then adopts it
   * as this table's storage. Fails with `error::invalid_argument` if
   * @p new_buckets is empty. Never allocates -- see the file-level doc
   * comment's "Growing" section for the concurrency pattern this exists
   * for (allocate @p new_buckets outside whatever lock guards this
   * table, then call `rehash` while holding it).
   */
  [[nodiscard]] result<void> rehash(span<T *> new_buckets) & noexcept {
    if (new_buckets.empty())
      return unexpected(error::invalid_argument);
    for (auto &slot : new_buckets)
      slot = nullptr;

    span<T *> old_buckets = buckets_;
    buckets_ = new_buckets;
    for (size_type i = 0; i < old_buckets.size(); ++i) {
      T *cur = old_buckets[i];
      while (cur != nullptr) {
        T *next = hook_of(*cur).next;
        hook_of(*cur).next = nullptr;
        hook_of(*cur).pprev = nullptr;
        link_at(index_for(KeyOf{}(*cur)), *cur);
        cur = next;
      }
    }
    return {};
  }

  /**
   * @brief Unlinks every node (their own storage is otherwise untouched
   * -- `T` is never constructed/destroyed by this file) and zeroes every
   * bucket. `bucket_count()` is unchanged.
   */
  void clear() noexcept {
    for (size_type i = 0; i < buckets_.size(); ++i) {
      T *cur = buckets_[i];
      while (cur != nullptr) {
        T *next = hook_of(*cur).next;
        hook_of(*cur).next = nullptr;
        hook_of(*cur).pprev = nullptr;
        cur = next;
      }
      buckets_[i] = nullptr;
    }
    size_ = 0;
  }

private:
  static intrusive_hash_hook<T> &hook_of(T &node) noexcept { return node.*Hook; }

  [[nodiscard]] size_type index_for(const key_type &key) const noexcept { return Hash{}(key) % buckets_.size(); }

  void link_at(size_type idx, T &node) noexcept {
    auto &hook = hook_of(node);
    T *first = buckets_[idx];
    hook.next = first;
    if (first != nullptr)
      hook_of(*first).pprev = &hook.next;
    buckets_[idx] = &node;
    hook.pprev = &buckets_[idx];
  }

  void unlink(T &node) noexcept {
    auto &hook = hook_of(node);
    T *next = hook.next;
    T **pprev = hook.pprev;
    *pprev = next;
    if (next != nullptr)
      hook_of(*next).pprev = pprev;
    hook.next = nullptr;
    hook.pprev = nullptr;
    --size_;
  }

  template <typename K> [[nodiscard]] T *find_ptr(const K &key) const noexcept {
    if (buckets_.empty())
      return nullptr;
    size_type idx = Hash{}(key) % buckets_.size();
    for (T *cur = buckets_[idx]; cur != nullptr; cur = hook_of(*cur).next) {
      if (KeyEqual{}(KeyOf{}(*cur), key))
        return cur;
    }
    return nullptr;
  }

  span<T *> buckets_;
  size_type size_ = 0;
};

} // namespace reloco
