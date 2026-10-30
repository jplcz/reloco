// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file rbtree_base.hpp
 * @brief `rbtree_base<T, Compare, KeyOf>`: the balanced (red-black),
 * allocator-owned node tree engine behind `reloco::set`/`reloco::map`.
 *
 * It is `tree_base` (see `tree_base.hpp`) instantiated with `rb_policy`
 * instead of the default `unbalanced_policy`: the node layout, allocation,
 * comparison-driven search, iteration, cloning, `append`/`retain`/
 * set-comparison algorithms are all shared. The policy only adds the two
 * things a balanced tree needs: recolouring/rotating after a leaf insert
 * (`rb_insert_fixup`) and the red-black deletion (`rb_unlink`). The colour
 * lives in `node_header::aux` (0 = red, so a freshly constructed node is
 * red; 1 = black), the byte `node_base` reserves for exactly this purpose.
 *
 * Like `tree_base`'s unbalanced deletion, `rb_unlink` never copies or
 * assigns a payload: when the erased node has two children its in-order
 * successor *node* is relinked into its place, so `T` needs no assignment
 * operators and references to other elements stay valid.
 *
 * Guarantees: `O(log n)` worst-case insert/erase/find; tree height is at
 * most `2 * log2(n + 1)`.
 */

#include "tree_base.hpp"

RELOCO_BEGIN_UNSAFE_CODE

namespace reloco::detail {

/// Small structural helpers kept inline (header-visible) so the compiler can fold them into the hot insert/erase
/// paths instead of paying a cross-translation-unit call for each rotation or colour test.
namespace rb_detail {

inline constexpr unsigned char red = 0;
inline constexpr unsigned char black = 1;

[[nodiscard]] inline bool is_red(const node_header *node) noexcept { return node && node->aux() == red; }
[[nodiscard]] inline bool is_black(const node_header *node) noexcept { return !node || node->aux() == black; }

inline void replace_child(node_header *&root, node_header *parent, node_header *old_child,
                          node_header *new_child) noexcept {
  if (!parent)
    root = new_child;
  else if (parent->left == old_child)
    parent->left = new_child;
  else
    parent->right = new_child;
}

inline void rotate_left(node_header *&root, node_header *x) noexcept {
  node_header *y = x->right;
  x->right = y->left;
  if (y->left)
    y->left->set_parent(x);
  y->set_parent(x->parent());
  replace_child(root, x->parent(), x, y);
  y->left = x;
  x->set_parent(y);
}

inline void rotate_right(node_header *&root, node_header *x) noexcept {
  node_header *y = x->left;
  x->left = y->right;
  if (y->right)
    y->right->set_parent(x);
  y->set_parent(x->parent());
  replace_child(root, x->parent(), x, y);
  y->right = x;
  x->set_parent(y);
}

} // namespace rb_detail

/**
 * @brief Restores the red-black invariants after @p node was linked as a
 * leaf: colours it red, then recolours/rotates up the path to @p root.
 * Defined in `rbtree_base.ipp`.
 */
RELOCO_API void rb_insert_fixup(node_header *&root, node_header *node) noexcept;

/**
 * @brief Red-black deletion: unlinks @p node from the tree rooted at
 * @p root and rebalances. @p node is left unlinked but otherwise untouched,
 * so the caller still destroys its payload and frees it. Defined in
 * `rbtree_base.ipp`.
 */
RELOCO_API void rb_unlink(node_header *&root, node_header *node) noexcept;

/// @brief `tree_base` balancing policy that keeps the tree red-black.
struct rb_policy {
  // Fast paths are inline; only the genuinely branchy rebalancing is out of line.
  static void after_insert(node_header *&root, node_header *node) noexcept {
    if (!node->parent()) {
      node->set_aux(rb_detail::black);
    } else if (rb_detail::is_black(node->parent())) {
      node->set_aux(rb_detail::red); // a red leaf under a black parent breaks no invariant
    } else {
      rb_insert_fixup(root, node);
    }
  }

  static void unlink(node_header *&root, node_header *node) noexcept {
    if (rb_detail::is_red(node) && !node->left && !node->right) {
      // Removing a red leaf never changes any black height.
      rb_detail::replace_child(root, node->parent(), node, nullptr);
      return;
    }
    rb_unlink(root, node);
  }
};

template <typename T, typename Compare, typename KeyOf> using rbtree_base = tree_base<T, Compare, KeyOf, rb_policy>;

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "rbtree_base.ipp"
#endif

} // namespace reloco::detail

RELOCO_END_UNSAFE_CODE
