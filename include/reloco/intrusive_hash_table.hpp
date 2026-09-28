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
#include <iterator>
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
 * @brief Forward iterator over an `intrusive_hash_table<T, Hook, ...>`'s
 * linked nodes, walking bucket chains left-to-right and skipping empty
 * buckets -- iteration order is unspecified (depends on hash values and
 * insertion/removal history), exactly like `flat_hash_const_iterator`.
 * Templated only on @p T and @p Hook (not the table's `KeyOf`/`Hash`/
 * `KeyEqual`), so `intrusive_hash_table::iterator`/`const_iterator` are
 * both instantiations of this one class (@p IsConst selects which);
 * `iterator` converts to `const_iterator` implicitly, matching every
 * other reloco container's mutable/const iterator pair. The public
 * three-argument constructor exists for `intrusive_hash_table`'s own
 * `begin()`/`iterator_to()`/etc. to build one from a bucket span
 * (mirroring `flat_hash_const_iterator`'s own public raw-pointer
 * constructor) -- it is not meant to be called directly by other code.
 */
template <typename T, intrusive_hash_hook<T> T::*Hook, bool IsConst> class intrusive_hash_iterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const T *, T *>;
  using reference = std::conditional_t<IsConst, const T &, T &>;
  using size_type = std::size_t;

  constexpr intrusive_hash_iterator() noexcept = default;

  constexpr intrusive_hash_iterator(span<T *> buckets, size_type index, T *cur) noexcept
      : m_buckets(buckets), m_index(index), m_cur(cur) {}

  // Mutable -> const conversion only, same direction every other reloco
  // iterator/const_iterator pair allows.
  template <bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
  constexpr intrusive_hash_iterator(const intrusive_hash_iterator<T, Hook, WasConst> &other) noexcept
      : m_buckets(other.buckets()), m_index(other.index()), m_cur(other.node()) {}

  [[nodiscard]] reference operator*() const noexcept { return *m_cur; }
  [[nodiscard]] pointer operator->() const noexcept { return m_cur; }

  intrusive_hash_iterator &operator++() noexcept {
    advance();
    return *this;
  }
  intrusive_hash_iterator operator++(int) noexcept {
    auto tmp = *this;
    advance();
    return tmp;
  }

  [[nodiscard]] friend bool operator==(const intrusive_hash_iterator &lhs, const intrusive_hash_iterator &rhs) noexcept {
    return lhs.m_cur == rhs.m_cur;
  }
  [[nodiscard]] friend bool operator!=(const intrusive_hash_iterator &lhs, const intrusive_hash_iterator &rhs) noexcept {
    return !(lhs == rhs);
  }

  // Exposed only so the mutable -> const converting constructor above (a
  // different instantiation of this same template) can read another
  // iterator's state; not meant for general use.
  [[nodiscard]] constexpr span<T *> buckets() const noexcept { return m_buckets; }
  [[nodiscard]] constexpr size_type index() const noexcept { return m_index; }
  [[nodiscard]] constexpr T *node() const noexcept { return m_cur; }

private:
  void advance() noexcept {
    RELOCO_ASSERT(m_cur != nullptr, "reloco::intrusive_hash_iterator: incrementing an end() iterator");
    m_cur = (m_cur->*Hook).next;
    while (m_cur == nullptr && ++m_index < m_buckets.size())
      m_cur = m_buckets[m_index];
  }

  span<T *> m_buckets;
  size_type m_index = 0;
  T *m_cur = nullptr;
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
  using iterator = intrusive_hash_iterator<T, Hook, false>;
  using const_iterator = intrusive_hash_iterator<T, Hook, true>;

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

  /**
   * @brief Suggests a bucket count for the caller's *next* `rehash()`
   * ahead of inserting @p n more elements, clamped to the inclusive
   * `[min_buckets, max_buckets]` range the caller considers "sensible"
   * (e.g. the smallest/largest static bucket array their platform's
   * memory budget allows -- `intrusive_hash_table` cannot decide that on
   * its own, since it never allocates its own bucket storage). Purely
   * advisory: never touches the table, never allocates -- the caller
   * still decides whether/when to actually build a new `span<T *>` and
   * call `rehash()` with it (see the file-level doc comment's "Growing"
   * section).
   *
   * Targets an average chain length of @p elements_per_bucket (default
   * `1`, i.e. `load_factor_permille() == 1000`) at the projected
   * `size() + n`: `buckets == ceil((size() + n) / elements_per_bucket)`.
   * Raising @p elements_per_bucket trades bucket-array size for longer
   * chains -- e.g. if walking a 4-element chain is still cheap enough
   * for the caller's `try_find`/`contains` hot path, 100 projected
   * elements only need 25 buckets (`elements_per_bucket == 4`) rather
   * than 100 (`elements_per_bucket == 1`), a 4x smaller caller-owned
   * `span<T *>`. Computed with integer-only arithmetic throughout
   * (nothing here uses a `float`/`double`, same as every other reloco
   * diagnostic). Unlike `flat_hash_base::capacity_for`, the suggestion
   * is never rounded up to a power of two: `intrusive_hash_table`
   * indexes buckets with plain modulo (`index_for`), not a power-of-two
   * mask, so any bucket count is valid, and a tight-fitting count keeps
   * `rehash()`'s caller-owned storage no bigger than it needs to be.
   *
   * `RELOCO_ASSERT`s `min_buckets >= 1` (matching `rehash`/`try_create`'s
   * own rejection of an empty bucket span), `min_buckets <=
   * max_buckets`, and `elements_per_bucket >= 1`.
   */
  [[nodiscard]] size_type suggest_bucket_count_for_insert(size_type n, size_type min_buckets, size_type max_buckets,
                                                           size_type elements_per_bucket = 1) const & noexcept {
    RELOCO_ASSERT(min_buckets >= 1,
                  "reloco::intrusive_hash_table::suggest_bucket_count_for_insert: min_buckets must be >= 1");
    RELOCO_ASSERT(min_buckets <= max_buckets,
                  "reloco::intrusive_hash_table::suggest_bucket_count_for_insert: min_buckets must be <= max_buckets");
    RELOCO_ASSERT(elements_per_bucket >= 1,
                  "reloco::intrusive_hash_table::suggest_bucket_count_for_insert: elements_per_bucket must be >= 1");
    return clamp_bucket_count(buckets_for_elements(size_ + n, elements_per_bucket), min_buckets, max_buckets);
  }

  /**
   * @brief Same contract as `suggest_bucket_count_for_insert` (including
   * @p elements_per_bucket), but for @p n pending *removals* instead:
   * targets `ceil((size() - min(n, size())) / elements_per_bucket)`
   * (never underflowing past zero), clamped to `[min_buckets,
   * max_buckets]`.
   */
  [[nodiscard]] size_type suggest_bucket_count_for_remove(size_type n, size_type min_buckets, size_type max_buckets,
                                                           size_type elements_per_bucket = 1) const & noexcept {
    RELOCO_ASSERT(min_buckets >= 1,
                  "reloco::intrusive_hash_table::suggest_bucket_count_for_remove: min_buckets must be >= 1");
    RELOCO_ASSERT(min_buckets <= max_buckets,
                  "reloco::intrusive_hash_table::suggest_bucket_count_for_remove: min_buckets must be <= max_buckets");
    RELOCO_ASSERT(elements_per_bucket >= 1,
                  "reloco::intrusive_hash_table::suggest_bucket_count_for_remove: elements_per_bucket must be >= 1");
    size_type shrunk = n < size_ ? n : size_;
    return clamp_bucket_count(buckets_for_elements(size_ - shrunk, elements_per_bucket), min_buckets, max_buckets);
  }

  /**
   * @brief Forward iteration over every currently linked node, walking
   * bucket chains left-to-right and skipping empty buckets. Order is
   * unspecified (depends on hash values and insertion/removal history),
   * exactly like every other reloco hash container. This is what makes
   * `intrusive_hash_table` usable with `extract_if_iterator`/
   * `isolated_node_tx` (`intrusive_iteration.hpp`): `begin()`/`end()`/
   * `erase(iterator)` are exactly the surface that adaptor needs from a
   * `Container`, the same surface `boost::intrusive::list` already
   * provides.
   */
  [[nodiscard]] iterator begin() & noexcept RELOCO_LIFETIMEBOUND { return first_iterator(); }
  [[nodiscard]] iterator end() & noexcept RELOCO_LIFETIMEBOUND { return iterator(buckets_, buckets_.size(), nullptr); }
  [[nodiscard]] const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND { return first_iterator(); }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_iterator(buckets_, buckets_.size(), nullptr);
  }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return begin(); }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return end(); }

  /**
   * @brief Builds an iterator pointing at @p node, currently linked into
   * *this* table (re-hashes @p node's key to locate its bucket, same
   * cost as `try_find`/`remove` -- there is no cheaper O(1) way to
   * recover a bucket index from a bare node reference). `RELOCO_ASSERT`s
   * @p node is linked.
   */
  [[nodiscard]] iterator iterator_to(T &node) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(hook_of(node).is_linked(), "reloco::intrusive_hash_table::iterator_to: node is not linked");
    const KeyOf keyOf{};
    return iterator(buckets_, index_for(keyOf(node)), &node);
  }

  [[nodiscard]] const_iterator iterator_to(const T &node) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(hook_of(const_cast<T &>(node)).is_linked(),
                  "reloco::intrusive_hash_table::iterator_to: node is not linked");
    const KeyOf keyOf{};
    return const_iterator(buckets_, index_for(keyOf(node)), const_cast<T *>(&node));
  }

  /**
   * @brief Unlinks the node at @p pos (same O(1) unlink as `remove`) and
   * returns an iterator to the node that followed it, matching
   * `std::list::erase`/`boost::intrusive::list::erase` -- what lets
   * `extract_if_iterator` advance its own traversal cursor directly off
   * of this return value instead of needing a separate lookahead step.
   */
  iterator erase(iterator pos) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos != end(), "reloco::intrusive_hash_table::erase: called with end() iterator");
    T &node = *pos;
    iterator next = pos;
    ++next;
    unlink(node);
    return next;
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
  size_type suggest_bucket_count_for_insert(size_type n, size_type min_buckets, size_type max_buckets,
                                             size_type elements_per_bucket = 1) const && = delete;
  size_type suggest_bucket_count_for_remove(size_type n, size_type min_buckets, size_type max_buckets,
                                             size_type elements_per_bucket = 1) const && = delete;
  iterator begin() && = delete;
  iterator end() && = delete;
  const_iterator begin() const && = delete;
  const_iterator end() const && = delete;
  const_iterator cbegin() const && = delete;
  const_iterator cend() const && = delete;
  iterator iterator_to(T &node) && = delete;
  const_iterator iterator_to(const T &node) const && = delete;
  iterator erase(iterator pos) && = delete;

  /**
   * @brief Links @p node in, keyed by `KeyOf{}(node)`. Fails with
   * `error::already_exists` if an equivalent key is already present.
   * `RELOCO_ASSERT`s @p node is not already linked (into *this* or any
   * other `intrusive_hash_table`) -- see the file-level doc comment.
   */
  [[nodiscard]] result<void> try_insert(T &node) & noexcept {
    RELOCO_ASSERT(!hook_of(node).is_linked(),
                  "reloco::intrusive_hash_table::try_insert: node is already linked into a table");
    const KeyOf keyOf{};
    const key_type &key = keyOf(node);
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
  template <typename K> [[nodiscard]] result<std::reference_wrapper<const T>> try_find(const K &key) const & noexcept {
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
        const KeyOf keyOf{};
        T *next = hook_of(*cur).next;
        hook_of(*cur).next = nullptr;
        hook_of(*cur).pprev = nullptr;
        link_at(index_for(keyOf(*cur)), *cur);
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

  // Shared by `suggest_bucket_count_for_insert`/`_for_remove`: how many
  // buckets it takes to hold @p elements at no more than
  // @p elements_per_bucket per chain on average -- `ceil(elements /
  // elements_per_bucket)`, computed without overflow-prone floating-point
  // division (`(elements + elements_per_bucket - 1) / elements_per_bucket`
  // would risk overflowing the numerator; the equivalent `quotient +
  // (remainder != 0)` form below never does).
  [[nodiscard]] static size_type buckets_for_elements(size_type elements, size_type elements_per_bucket) noexcept {
    size_type quotient = elements / elements_per_bucket;
    size_type remainder = elements % elements_per_bucket;
    return remainder == 0 ? quotient : quotient + 1;
  }

  // Shared by `suggest_bucket_count_for_insert`/`_for_remove`: clamps a
  // target bucket count (an average-chain-length-of-1 projection) into
  // the caller-supplied "sensible" range.
  [[nodiscard]] static size_type clamp_bucket_count(size_type target, size_type min_buckets,
                                                     size_type max_buckets) noexcept {
    if (target < min_buckets)
      return min_buckets;
    if (target > max_buckets)
      return max_buckets;
    return target;
  }

  // Shared by both the mutable and const `begin()` overloads: finds the
  // first non-empty bucket (or `buckets_.size()`/`nullptr` if the table is
  // empty, matching `end()`).
  [[nodiscard]] iterator first_iterator() const noexcept {
    size_type idx = 0;
    while (idx < buckets_.size() && buckets_[idx] == nullptr)
      ++idx;
    return iterator(buckets_, idx, idx < buckets_.size() ? buckets_[idx] : nullptr);
  }

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
    const KeyOf keyOf{};
    const KeyEqual keyEqual{};
    for (T *cur = buckets_[idx]; cur != nullptr; cur = hook_of(*cur).next) {
      if (keyEqual(keyOf(*cur), key))
        return cur;
    }
    return nullptr;
  }

  span<T *> buckets_;
  size_type size_ = 0;
};

} // namespace reloco
