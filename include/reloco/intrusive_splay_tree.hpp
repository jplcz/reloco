// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file intrusive_splay_tree.hpp
 * @brief `intrusive_splay_tree<T, Hook, KeyOf, Compare>`: an ordered,
 * unique-key, self-adjusting binary search tree that never allocates --
 * the splay-tree counterpart to `intrusive_rbtree.hpp` (worst-case
 * `O(log n)`) and `intrusive_hash_table.hpp` (unordered), matching
 * FreeBSD's `SPLAY_*` macros (`sys/sys/tree.h`) or Boost.Intrusive's
 * `splaytree`/`splay_set`.
 *
 * Like every other `intrusive_*` container in reloco, nodes are ordinary
 * caller-owned objects (static, stack, a `slab`/pool the caller manages
 * some other way) that embed an `intrusive_splay_tree_hook<T>` as a named
 * member; the tree itself holds nothing but a root pointer and a running
 * element count. See `intrusive_rbtree.hpp`'s file-level doc comment for
 * the full "why intrusive, never allocates" rationale, which applies here
 * identically -- this file only documents what is specific to splaying.
 *
 * **Self-adjustment, not worst-case balance**: a splay tree keeps no
 * per-node balance metadata at all (`intrusive_splay_tree_hook<T>` has only
 * `parent`/`left`/`right`, no color/balance-factor byte) -- instead, every
 * access (`try_find`, `try_insert`, `remove`) *splays* the accessed node up
 * to the root via a sequence of "zig"/"zig-zig"/"zig-zag" rotations (the
 * classic Sleator-Tarjan bottom-up splay). This gives an amortized
 * `O(log n)` bound across any sequence of `m` operations (the Balance
 * Theorem), with a single worst-case access still possibly `O(n)` -- unlike
 * `intrusive_rbtree`'s strict per-operation `O(log n)` worst case -- in
 * exchange for working-set locality: repeatedly accessed nodes migrate
 * toward the root and stay cheap to reach again, which a strictly balanced
 * tree does not do. Whether that trade-off is worth it depends entirely on
 * the caller's access pattern; see `tree-containers.md`'s
 * `tree_set`/`tree_map` vs. `flat_set`/`flat_map` comparison for the same
 * kind of trade-off discussion applied to a different pair of containers.
 *
 * **Mutating lookups**: because splaying restructures the tree on every
 * successful (or unsuccessful, via semi-splaying the last node visited)
 * search, the non-`const` (`&`-qualified) `try_find`/`try_first`/
 * `try_last`/`remove` splay, unlike every other reloco associative
 * container's `try_find`. Each of `contains`/`try_find`/`try_first`/
 * `try_last` also has a `const &`-qualified overload that is a plain,
 * non-restructuring walk instead (no splay) -- for callers that only hold
 * a `const intrusive_splay_tree &`, or that would rather not pay a
 * rotation cost just to peek. Overload resolution picks the splaying
 * version automatically for a non-`const` tree and the peek version for a
 * `const` one; call through a `const` reference/pointer explicitly to get
 * the peek behavior on an otherwise-mutable tree. These `const &`
 * overloads still return a mutable `T &` (via `result<std::
 * reference_wrapper<T>>`, the same as the non-`const` overloads) rather
 * than `const T &`: the tree doesn't own its nodes, so `const`-qualifying
 * the *tree* says nothing about whether the caller may mutate the nodes
 * it already has pointers/references to -- returning `const T &` here
 * would only force every caller into a `const_cast`.
 *
 * **Key extraction**: a `KeyOf` functor template parameter, exactly like
 * `intrusive_rbtree`'s own `KeyOf`; `Compare` defaults to
 * `std::less<key_type>`.
 *
 * - `try_insert(node)` -> `result<void>`: fails with
 *   `error::already_exists` if an equivalent key is already present,
 *   otherwise splays the newly inserted node to the root.
 * - `try_find(key)` -> `result<std::reference_wrapper<T>>`: splays the
 *   found node to the root; fails with `error::not_found` if absent (still
 *   splaying the last node visited along the failed search path). The
 *   `const &`-qualified overload never splays, but still returns
 *   `result<std::reference_wrapper<T>>` (see above).
 * - `contains(key)` -> `bool`: plain walk, never splays.
 * - `try_remove(key)`/`remove(node)`: splays the target to the root, then
 *   joins its left/right subtrees (splaying the left subtree's maximum to
 *   its own root first, then attaching the right subtree under it) --
 *   the standard splay-tree deletion-by-join.
 * - `begin()`/`end()`/`iterator_to(node)`/`erase(iterator)`: ascending
 *   `Compare`-order iteration, the surface `extract_if_iterator`/
 *   `isolated_node_tx` (`intrusive_iteration.hpp`) needs from a
 *   `Container`. Plain `++`/`--` never splays (it only follows existing
 *   links), so iterating the whole tree does not itself cause any
 *   restructuring -- only `try_find`/`try_insert`/`remove`/`erase` do.
 *   `end()` iterators obtained before an intervening splay cache a
 *   now-stale root pointer (only used for `operator--`); do not mix
 *   mutation with a held `end()` iterator from before it.
 * - `try_first()`/`try_last()`/`try_pop_first()`/`try_pop_last()`: Rust
 *   `BTreeSet`-flavored smallest/greatest accessors, matching
 *   `intrusive_rbtree`'s own. The non-`const` `try_first`/`try_last` also
 *   splay the found node to the root, same as `try_find`; the `const &`
 *   overloads are a non-restructuring peek instead, like `contains`.
 * - `clear()`: unlinks every node (leaving each one's own storage otherwise
 *   untouched); `O(n)`, iterative, no recursion -- same left-rotate-away
 *   teardown shape as `intrusive_rbtree::clear`/`detail::tree_base`'s
 *   `bst_clear`.
 *
 * Unlike every other reloco container, `intrusive_splay_tree` has no
 * `try_clone`: cloning would require deciding where the clone's nodes
 * live, exactly the decision this whole file exists to leave to the caller
 * (same rationale as `intrusive_hash_table`/`intrusive_rbtree`).
 */

#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief The embedded hook a node type must carry (as a named member, see
 * this file's doc comment) to be usable with `intrusive_splay_tree`. Just
 * `parent`/`left`/`right` BST links -- no color/balance-factor byte, unlike
 * `intrusive_rbtree_hook`, since a splay tree carries no per-node balancing
 * metadata at all.
 */
template <typename T> struct intrusive_splay_tree_hook {
  T *parent = nullptr;
  T *left = nullptr;
  T *right = nullptr;
  bool linked = false;

  /** @brief Whether this hook is currently threaded into some tree. */
  [[nodiscard]] constexpr bool is_linked() const noexcept { return linked; }
};

/**
 * @brief Bidirectional, in-order iterator over an `intrusive_splay_tree<T,
 * Hook, ...>`. See `intrusive_rbtree_iterator`'s doc comment -- same shape,
 * same `T *` + cached-root-for-`operator--` design, differing only in
 * which hook type it walks.
 */
template <typename T, intrusive_splay_tree_hook<T> T::*Hook, bool IsConst> class intrusive_splay_tree_iterator {
public:
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const T *, T *>;
  using reference = std::conditional_t<IsConst, const T &, T &>;

  constexpr intrusive_splay_tree_iterator() noexcept = default;

  constexpr intrusive_splay_tree_iterator(T *node, T *root) noexcept : m_node(node), m_root(root) {}

  template <bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
  constexpr intrusive_splay_tree_iterator(const intrusive_splay_tree_iterator<T, Hook, WasConst> &other) noexcept
      : m_node(other.node()), m_root(other.root()) {}

  [[nodiscard]] reference operator*() const noexcept { return *m_node; }
  [[nodiscard]] pointer operator->() const noexcept { return m_node; }

  intrusive_splay_tree_iterator &operator++() noexcept {
    RELOCO_ASSERT(m_node != nullptr, "reloco::intrusive_splay_tree_iterator: incrementing an end() iterator");
    m_node = successor(m_node);
    return *this;
  }
  intrusive_splay_tree_iterator operator++(int) noexcept {
    auto tmp = *this;
    ++*this;
    return tmp;
  }
  intrusive_splay_tree_iterator &operator--() noexcept {
    m_node = m_node != nullptr ? predecessor(m_node) : rightmost(m_root);
    return *this;
  }
  intrusive_splay_tree_iterator operator--(int) noexcept {
    auto tmp = *this;
    --*this;
    return tmp;
  }

  [[nodiscard]] friend bool operator==(const intrusive_splay_tree_iterator &lhs,
                                       const intrusive_splay_tree_iterator &rhs) noexcept {
    return lhs.m_node == rhs.m_node;
  }
  [[nodiscard]] friend bool operator!=(const intrusive_splay_tree_iterator &lhs,
                                       const intrusive_splay_tree_iterator &rhs) noexcept {
    return !(lhs == rhs);
  }

  [[nodiscard]] constexpr T *node() const noexcept { return m_node; }
  [[nodiscard]] constexpr T *root() const noexcept { return m_root; }

private:
  [[nodiscard]] static T *leftmost(T *node) noexcept {
    if (node == nullptr)
      return nullptr;
    while ((node->*Hook).left != nullptr)
      node = (node->*Hook).left;
    return node;
  }
  [[nodiscard]] static T *rightmost(T *node) noexcept {
    if (node == nullptr)
      return nullptr;
    while ((node->*Hook).right != nullptr)
      node = (node->*Hook).right;
    return node;
  }
  [[nodiscard]] static T *successor(T *node) noexcept {
    if ((node->*Hook).right != nullptr)
      return leftmost((node->*Hook).right);
    T *parent = (node->*Hook).parent;
    while (parent != nullptr && node == (parent->*Hook).right) {
      node = parent;
      parent = (parent->*Hook).parent;
    }
    return parent;
  }
  [[nodiscard]] static T *predecessor(T *node) noexcept {
    if ((node->*Hook).left != nullptr)
      return rightmost((node->*Hook).left);
    T *parent = (node->*Hook).parent;
    while (parent != nullptr && node == (parent->*Hook).left) {
      node = parent;
      parent = (parent->*Hook).parent;
    }
    return parent;
  }

  T *m_node = nullptr;
  T *m_root = nullptr;
};

/**
 * @brief See the file-level doc comment. @p Hook is a pointer to the
 * `intrusive_splay_tree_hook<T>` member @p T embeds; @p KeyOf extracts a
 * node's key; @p Compare orders two keys, defaulting to
 * `std::less<key_type>`.
 */
template <typename T, intrusive_splay_tree_hook<T> T::*Hook, typename KeyOf,
          typename Compare = std::less<std::decay_t<decltype(KeyOf{}(std::declval<const T &>()))>>>
class RELOCO_POINTER intrusive_splay_tree {
public:
  using value_type = T;
  using key_type = std::decay_t<decltype(KeyOf{}(std::declval<const T &>()))>;
  using size_type = std::size_t;
  using iterator = intrusive_splay_tree_iterator<T, Hook, false>;
  using const_iterator = intrusive_splay_tree_iterator<T, Hook, true>;

  constexpr intrusive_splay_tree() noexcept = default;

  intrusive_splay_tree(const intrusive_splay_tree &) = delete;
  intrusive_splay_tree &operator=(const intrusive_splay_tree &) = delete;

  intrusive_splay_tree(intrusive_splay_tree &&other) noexcept
      : root_(std::exchange(other.root_, nullptr)), size_(std::exchange(other.size_, size_type{0})) {}

  intrusive_splay_tree &operator=(intrusive_splay_tree &&other) noexcept {
    if (this != &other) {
      root_ = std::exchange(other.root_, nullptr);
      size_ = std::exchange(other.size_, size_type{0});
    }
    return *this;
  }

  [[nodiscard]] size_type size() const & noexcept { return size_; }
  [[nodiscard]] bool empty() const & noexcept { return size_ == 0; }
  size_type size() const && = delete;
  bool empty() const && = delete;

  [[nodiscard]] iterator begin() & noexcept RELOCO_LIFETIMEBOUND { return iterator(leftmost(root_), root_); }
  [[nodiscard]] iterator end() & noexcept RELOCO_LIFETIMEBOUND { return iterator(nullptr, root_); }
  [[nodiscard]] const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_iterator(leftmost(root_), root_);
  }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return const_iterator(nullptr, root_); }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return begin(); }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return end(); }
  iterator begin() && = delete;
  iterator end() && = delete;
  const_iterator begin() const && = delete;
  const_iterator end() const && = delete;
  const_iterator cbegin() const && = delete;
  const_iterator cend() const && = delete;

  /** @brief Builds an iterator pointing at @p node, currently linked into *this* tree. Does not splay. */
  [[nodiscard]] iterator iterator_to(T &node) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(hook_of(node).is_linked(), "reloco::intrusive_splay_tree::iterator_to: node is not linked");
    return iterator(&node, root_);
  }
  [[nodiscard]] const_iterator iterator_to(const T &node) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(hook_of(const_cast<T &>(node)).is_linked(),
                  "reloco::intrusive_splay_tree::iterator_to: node is not linked");
    return const_iterator(const_cast<T *>(&node), root_);
  }
  iterator iterator_to(T &node) && = delete;
  const_iterator iterator_to(const T &node) const && = delete;

  /**
   * @brief Unlinks the node at @p pos (splaying+joining, see `remove`) and
   * returns an iterator to the node that followed it, matching
   * `std::set::erase`/`boost::intrusive::set::erase`.
   */
  iterator erase(iterator pos) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos != end(), "reloco::intrusive_splay_tree::erase: called with end() iterator");
    T &node = *pos;
    iterator next = pos;
    ++next;
    unlink(node);
    return iterator(next.node(), root_);
  }
  iterator erase(iterator pos) && = delete;

  /**
   * @brief Links @p node in, keyed by `KeyOf{}(node)`, then splays it to
   * the root. Fails with `error::already_exists` if an equivalent key is
   * already present. `RELOCO_ASSERT`s @p node is not already linked.
   */
  [[nodiscard]] result<void> try_insert(T &node) & noexcept {
    RELOCO_ASSERT(!hook_of(node).is_linked(),
                  "reloco::intrusive_splay_tree::try_insert: node is already linked into a tree");
    const KeyOf keyOf{};
    const Compare less{};
    const key_type &key = keyOf(node);

    T *parent = nullptr;
    T *cur = root_;
    bool went_left = false;
    while (cur != nullptr) {
      parent = cur;
      if (less(key, keyOf(*cur))) {
        cur = hook_of(*cur).left;
        went_left = true;
      } else if (less(keyOf(*cur), key)) {
        cur = hook_of(*cur).right;
        went_left = false;
      } else {
        return unexpected(error::already_exists);
      }
    }

    auto &hook = hook_of(node);
    hook.parent = parent;
    hook.left = nullptr;
    hook.right = nullptr;
    hook.linked = true;
    if (parent == nullptr)
      root_ = &node;
    else if (went_left)
      hook_of(*parent).left = &node;
    else
      hook_of(*parent).right = &node;

    ++size_;
    splay(&node);
    return {};
  }

  result<void> try_insert(T &&node) & noexcept = delete;

  /** @brief Plain walk, never splays/restructures -- see the file-level doc comment. */
  template <typename K> [[nodiscard]] bool contains(const K &key) const & noexcept { return find_ptr(key) != nullptr; }
  template <typename K> bool contains(const K &key) const && = delete;

  /**
   * @brief Fails with `error::not_found` if @p key is absent. Splays the
   * found node (or, on a miss, the last node visited along the search
   * path) to the root -- see the file-level doc comment.
   */
  template <typename K> [[nodiscard]] result<std::reference_wrapper<T>> try_find(const K &key) & noexcept {
    T *found = find_and_splay(key);
    if (found == nullptr)
      return unexpected(error::not_found);
    return std::ref(*found);
  }
  template <typename K> result<std::reference_wrapper<T>> try_find(const K &key) && = delete;

  /**
   * @brief `const`, non-restructuring peek: plain walk, never splays --
   * unlike the non-`const` `try_find` above, so callers that only have a
   * `const intrusive_splay_tree &` (or want to guarantee no restructuring)
   * can still look a key up. Fails with `error::not_found` if absent.
   * Returns a mutable `T &` even though the overload is `const`-qualified:
   * the tree doesn't own its nodes (same as `intrusive_hash_table`'s own
   * `remove(node)` taking a mutable node reference regardless of how it
   * was found), so a `const` *tree* reference says nothing about whether
   * the caller may mutate the nodes it holds pointers to -- returning
   * `const T &` here would just force every caller into a `const_cast`.
   */
  template <typename K> [[nodiscard]] result<std::reference_wrapper<T>> try_find(const K &key) const & noexcept {
    T *found = find_ptr(key);
    if (found == nullptr)
      return unexpected(error::not_found);
    return std::ref(*found);
  }
  template <typename K> result<std::reference_wrapper<T>> try_find(const K &key) const && = delete;

  /**
   * @brief Walks to find @p key (splaying it to the root), then unlinks
   * it. Fails with `error::not_found` if absent.
   */
  template <typename K> [[nodiscard]] result<void> try_remove(const K &key) & noexcept {
    T *found = find_and_splay(key);
    if (found == nullptr)
      return unexpected(error::not_found);
    unlink(*found);
    return {};
  }

  /**
   * @brief Removes @p node, already known to be linked into *this* tree:
   * splays it to the root, then joins its left/right subtrees.
   * `RELOCO_ASSERT`s @p node is currently linked.
   */
  void remove(T &node) & noexcept {
    RELOCO_ASSERT(hook_of(node).is_linked(), "reloco::intrusive_splay_tree::remove: node is not linked");
    unlink(node);
  }
  void remove(T &&node) & noexcept = delete;

  /** @brief Reference to the smallest element, splaying it to the root. Fails with `error::container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_first() & noexcept {
    T *node = leftmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    splay(node);
    return std::ref(*node);
  }

  /**
   * @brief `const`, non-restructuring peek at the smallest element --
   * unlike the non-`const` `try_first` above, this never splays. Returns
   * a mutable `T &` despite being `const`-qualified -- see `try_find`'s
   * `const &` overload's doc comment for why. Fails with
   * `error::container_empty`.
   */
  [[nodiscard]] result<std::reference_wrapper<T>> try_first() const & noexcept {
    T *node = leftmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    return std::ref(*node);
  }

  /** @brief Reference to the greatest element, splaying it to the root. Fails with `error::container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_last() & noexcept {
    T *node = rightmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    splay(node);
    return std::ref(*node);
  }

  /**
   * @brief `const`, non-restructuring peek at the greatest element --
   * unlike the non-`const` `try_last` above, this never splays. Returns
   * a mutable `T &` despite being `const`-qualified -- see `try_find`'s
   * `const &` overload's doc comment for why. Fails with
   * `error::container_empty`.
   */
  [[nodiscard]] result<std::reference_wrapper<T>> try_last() const & noexcept {
    T *node = rightmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    return std::ref(*node);
  }

  /** @brief Unlinks and returns the smallest element. Fails with `error::container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_pop_first() & noexcept {
    T *node = leftmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    unlink(*node);
    return std::ref(*node);
  }

  /** @brief Unlinks and returns the greatest element. Fails with `error::container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_pop_last() & noexcept {
    T *node = rightmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    unlink(*node);
    return std::ref(*node);
  }

  /**
   * @brief Unlinks every node (their own storage is otherwise untouched).
   * `O(n)`, iterative (no recursion) -- same left-rotate-away teardown
   * shape as `intrusive_rbtree::clear`/`detail::tree_base`'s `bst_clear`.
   */
  void clear() noexcept {
    T *node = root_;
    while (node != nullptr) {
      auto &hook = hook_of(*node);
      if (hook.left != nullptr) {
        T *left = hook.left;
        hook.left = hook_of(*left).right;
        if (hook.left != nullptr)
          hook_of(*hook.left).parent = node;
        hook_of(*left).right = node;
        hook_of(*left).parent = nullptr;
        node = left;
        continue;
      }
      T *next = hook.right;
      hook.parent = nullptr;
      hook.right = nullptr;
      hook.linked = false;
      node = next;
    }
    root_ = nullptr;
    size_ = 0;
  }

private:
  static intrusive_splay_tree_hook<T> &hook_of(T &node) noexcept { return node.*Hook; }

  [[nodiscard]] static T *leftmost(T *node) noexcept {
    if (node == nullptr)
      return nullptr;
    while (hook_of(*node).left != nullptr)
      node = hook_of(*node).left;
    return node;
  }
  [[nodiscard]] static T *rightmost(T *node) noexcept {
    if (node == nullptr)
      return nullptr;
    while (hook_of(*node).right != nullptr)
      node = hook_of(*node).right;
    return node;
  }

  template <typename K> [[nodiscard]] T *find_and_splay(const K &key) noexcept {
    const Compare less{};
    const KeyOf keyOf{};
    T *cur = root_;
    T *last = nullptr;
    while (cur != nullptr) {
      last = cur;
      if (less(key, keyOf(*cur))) {
        cur = hook_of(*cur).left;
      } else if (less(keyOf(*cur), key)) {
        cur = hook_of(*cur).right;
      } else {
        splay(cur);
        return cur;
      }
    }
    if (last != nullptr)
      splay(last);
    return nullptr;
  }

  /** @brief Plain walk, never splays -- backs `contains`/the `const` peek overloads. */
  template <typename K> [[nodiscard]] T *find_ptr(const K &key) const noexcept {
    const Compare less{};
    const KeyOf keyOf{};
    T *cur = root_;
    while (cur != nullptr) {
      if (less(key, keyOf(*cur)))
        cur = hook_of(*cur).left;
      else if (less(keyOf(*cur), key))
        cur = hook_of(*cur).right;
      else
        return cur;
    }
    return nullptr;
  }

  void rotate_left(T *x) noexcept {
    auto &hx = hook_of(*x);
    T *y = hx.right;
    auto &hy = hook_of(*y);
    hx.right = hy.left;
    if (hy.left != nullptr)
      hook_of(*hy.left).parent = x;
    hy.parent = hx.parent;
    if (hx.parent == nullptr)
      root_ = y;
    else if (x == hook_of(*hx.parent).left)
      hook_of(*hx.parent).left = y;
    else
      hook_of(*hx.parent).right = y;
    hy.left = x;
    hx.parent = y;
  }

  void rotate_right(T *x) noexcept {
    auto &hx = hook_of(*x);
    T *y = hx.left;
    auto &hy = hook_of(*y);
    hx.left = hy.right;
    if (hy.right != nullptr)
      hook_of(*hy.right).parent = x;
    hy.parent = hx.parent;
    if (hx.parent == nullptr)
      root_ = y;
    else if (x == hook_of(*hx.parent).right)
      hook_of(*hx.parent).right = y;
    else
      hook_of(*hx.parent).left = y;
    hy.right = x;
    hx.parent = y;
  }

  /**
   * @brief Classic Sleator-Tarjan bottom-up splay: repeatedly applies a
   * zig (one rotation, @p x's parent is the root), zig-zig (two
   * same-direction rotations, @p x and its parent are both left or both
   * right children), or zig-zag (two opposite-direction rotations) step
   * until @p x is the root. Assumes @p x is non-null and already linked
   * into *this* tree.
   */
  void splay(T *x) noexcept {
    while (hook_of(*x).parent != nullptr) {
      T *parent = hook_of(*x).parent;
      T *grandparent = hook_of(*parent).parent;
      if (grandparent == nullptr) {
        if (x == hook_of(*parent).left)
          rotate_right(parent);
        else
          rotate_left(parent);
      } else if (x == hook_of(*parent).left && parent == hook_of(*grandparent).left) {
        rotate_right(grandparent);
        rotate_right(parent);
      } else if (x == hook_of(*parent).right && parent == hook_of(*grandparent).right) {
        rotate_left(grandparent);
        rotate_left(parent);
      } else if (x == hook_of(*parent).right && parent == hook_of(*grandparent).left) {
        rotate_left(parent);
        rotate_right(grandparent);
      } else {
        rotate_right(parent);
        rotate_left(grandparent);
      }
    }
    root_ = x;
  }

  /**
   * @brief Joins subtrees @p left and @p right (every key in @p left less
   * than every key in @p right) into one, returning the new root. Splays
   * @p left's maximum to its own root first (so it ends up with no right
   * child), then attaches @p right directly under it -- the standard
   * splay-tree join used by deletion. Either may be `nullptr`.
   */
  [[nodiscard]] T *join(T *left, T *right) noexcept {
    if (left == nullptr)
      return right;
    if (right == nullptr)
      return left;
    hook_of(*left).parent = nullptr;
    root_ = left; // so splay()'s loop/root bookkeeping stays confined to this detached subtree
    T *new_root = rightmost(left);
    splay(new_root);
    hook_of(*new_root).right = right;
    hook_of(*right).parent = new_root;
    return new_root;
  }

  void unlink(T &node) noexcept {
    splay(&node);
    T *left = hook_of(node).left;
    T *right = hook_of(node).right;
    if (left != nullptr)
      hook_of(*left).parent = nullptr;
    if (right != nullptr)
      hook_of(*right).parent = nullptr;
    root_ = join(left, right);
    if (root_ != nullptr)
      hook_of(*root_).parent = nullptr;

    auto &hz = hook_of(node);
    hz.parent = nullptr;
    hz.left = nullptr;
    hz.right = nullptr;
    hz.linked = false;
    --size_;
  }

  T *root_ = nullptr;
  size_type size_ = 0;
};

} // namespace reloco
