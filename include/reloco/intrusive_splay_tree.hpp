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
 * `try_last` also has a `const &`-qualified overload (inherited from the
 * shared `detail::intrusive_bst_base`) that is a plain, non-restructuring
 * walk instead (no splay) -- for callers that only hold a `const
 * intrusive_splay_tree &`, or that would rather not pay a rotation cost
 * just to peek. Overload resolution picks the splaying version
 * automatically for a non-`const` tree and the peek version for a `const`
 * one; call through a `const` reference/pointer explicitly to get the peek
 * behavior on an otherwise-mutable tree. These `const &` overloads still
 * return a mutable `T &` (via `result<std::reference_wrapper<T>>`, the
 * same as the non-`const` overloads) rather than `const T &`: the tree
 * doesn't own its nodes, so `const`-qualifying the *tree* says nothing
 * about whether the caller may mutate the nodes it already has
 * pointers/references to -- returning `const T &` here would only force
 * every caller into a `const_cast`.
 *
 * **Key extraction**: a `KeyOf` functor template parameter, exactly like
 * `intrusive_rbtree`'s own `KeyOf`; `Compare` defaults to
 * `std::less<key_type>`.
 *
 * Everything that doesn't care about splaying specifically -- iteration,
 * `lower_bound`/`upper_bound`/`bounded_range`, the plain (non-splaying)
 * `const &`-qualified peek overloads, and the `*_and_dispose`/`clear`
 * Boost.Intrusive-flavored removal surface -- lives in the shared CRTP
 * base `detail::intrusive_bst_base` (see `detail/intrusive_bst_base.hpp`).
 * This file adds what's genuinely splaying-specific on top:
 *
 * - `try_insert(node)` -> `result<void>`: fails with
 *   `error::already_exists` if an equivalent key is already present,
 *   otherwise splays the newly inserted node to the root.
 * - `try_find(key)` -> `result<std::reference_wrapper<T>>`: splays the
 *   found node to the root; fails with `error::not_found` if absent (still
 *   splaying the last node visited along the failed search path).
 * - `try_remove(key)`/`remove(node)`: splays the target to the root, then
 *   joins its left/right subtrees (splaying the left subtree's maximum to
 *   its own root first, then attaching the right subtree under it) --
 *   the standard splay-tree deletion-by-join. `try_pop_first()`/
 *   `try_pop_last()`/`erase(iterator)`/`*_and_dispose` share this same
 *   splay-then-join unlink, inherited unchanged from the base.
 * - `try_first()`/`try_last()`: the non-`const` overloads additionally
 *   splay the found node to the root, same as `try_find`.
 *
 * See `detail::intrusive_bst_base`'s file-level doc comment for
 * `lower_bound`/`upper_bound`/`bounded_range` (plain walks, never
 * splaying) and `clear()`/`clear_and_dispose` (iterative, no recursion).
 *
 * Unlike every other reloco container, `intrusive_splay_tree` has no
 * `try_clone`: cloning would require deciding where the clone's nodes
 * live, exactly the decision this whole file exists to leave to the caller
 * (same rationale as `intrusive_hash_table`/`intrusive_rbtree`).
 */

#include "detail/assert.hpp"
#include "detail/intrusive_bst_base.hpp"
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
 * @brief See the file-level doc comment. @p Hook is a pointer to the
 * `intrusive_splay_tree_hook<T>` member @p T embeds; @p KeyOf extracts a
 * node's key; @p Compare orders two keys, defaulting to
 * `std::less<key_type>`.
 */
template <typename T, intrusive_splay_tree_hook<T> T::*Hook, typename KeyOf,
          typename Compare = std::less<std::decay_t<decltype(KeyOf{}(std::declval<const T &>()))>>>
class RELOCO_POINTER intrusive_splay_tree
    : public detail::intrusive_bst_base<intrusive_splay_tree<T, Hook, KeyOf, Compare>, T, Hook, KeyOf, Compare> {
  using base_type = detail::intrusive_bst_base<intrusive_splay_tree<T, Hook, KeyOf, Compare>, T, Hook, KeyOf, Compare>;
  friend base_type;

public:
  using typename base_type::const_iterator;
  using typename base_type::iterator;
  using typename base_type::key_type;
  using typename base_type::size_type;
  using typename base_type::value_type;

  // The base's plain, non-splaying peek overloads; the splaying overloads
  // this class adds below (same signature modulo cv/ref-qualification)
  // live alongside them, not in place of them.
  using base_type::try_find;
  using base_type::try_first;
  using base_type::try_last;

  constexpr intrusive_splay_tree() noexcept = default;

  intrusive_splay_tree(const intrusive_splay_tree &) = delete;
  intrusive_splay_tree &operator=(const intrusive_splay_tree &) = delete;

  intrusive_splay_tree(intrusive_splay_tree &&other) noexcept = default;
  intrusive_splay_tree &operator=(intrusive_splay_tree &&other) noexcept = default;

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
    T *cur = this->root_;
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
      this->root_ = &node;
    else if (went_left)
      hook_of(*parent).left = &node;
    else
      hook_of(*parent).right = &node;

    ++this->size_;
    splay(&node);
    return {};
  }

  result<void> try_insert(T &&node) & noexcept = delete;

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
    T *node = this->leftmost(this->root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    splay(node);
    return std::ref(*node);
  }

  /** @brief Reference to the greatest element, splaying it to the root. Fails with `error::container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_last() & noexcept {
    T *node = this->rightmost(this->root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    splay(node);
    return std::ref(*node);
  }

private:
  using base_type::hook_of;
  using base_type::rotate_left;
  using base_type::rotate_right;

  template <typename K> [[nodiscard]] T *find_and_splay(const K &key) noexcept {
    const Compare less{};
    const KeyOf keyOf{};
    T *cur = this->root_;
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
    this->root_ = x;
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
    this->root_ = left; // so splay()'s loop/root bookkeeping stays confined to this detached subtree
    T *new_root = this->rightmost(left);
    splay(new_root);
    hook_of(*new_root).right = right;
    hook_of(*right).parent = new_root;
    return new_root;
  }

  // Called by `detail::intrusive_bst_base` (a friend) -- see this class's
  // `friend base_type;` declaration above.
  void unlink(T &node) noexcept {
    splay(&node);
    T *left = hook_of(node).left;
    T *right = hook_of(node).right;
    if (left != nullptr)
      hook_of(*left).parent = nullptr;
    if (right != nullptr)
      hook_of(*right).parent = nullptr;
    this->root_ = join(left, right);
    if (this->root_ != nullptr)
      hook_of(*this->root_).parent = nullptr;

    auto &hz = hook_of(node);
    hz.parent = nullptr;
    hz.left = nullptr;
    hz.right = nullptr;
    hz.linked = false;
    --this->size_;
  }

  // Called by `detail::intrusive_bst_base` (a friend) during
  // `clear_and_dispose`'s generic teardown; `intrusive_splay_tree_hook`
  // has no balance-specific field beyond what the base already resets, so
  // there is nothing extra to do here.
  static void reset_hook(T &) noexcept {}
};

} // namespace reloco
