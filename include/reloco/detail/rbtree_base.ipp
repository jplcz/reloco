// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Out-of-line, non-template bodies for the functions declared `RELOCO_API`
// in `rbtree_base.hpp`. Included into the `reloco::detail` namespace block
// by `rbtree_base.hpp` (header-only build) or by the one translation unit
// building a shared library -- see `reloco_extern.hpp`.

// Unsafe by design: red-black rebalancing relinks raw node pointers; reached only through the checked public API.
RELOCO_BEGIN_UNSAFE_CODE

// Colour/rotation helpers (`rb_detail`) live inline in `rbtree_base.hpp`.

RELOCO_API void rb_insert_fixup(node_header *&root, node_header *node) noexcept {
  using namespace rb_detail;
  node->set_aux(red);
  while (node != root && is_red(node->parent())) {
    // A red parent is never the root, so the grandparent exists.
    node_header *grand = node->parent()->parent();
    if (node->parent() == grand->left) {
      node_header *uncle = grand->right;
      if (is_red(uncle)) {
        node->parent()->set_aux(black);
        uncle->set_aux(black);
        grand->set_aux(red);
        node = grand;
      } else {
        if (node == node->parent()->right) {
          node = node->parent();
          rotate_left(root, node);
        }
        node->parent()->set_aux(black);
        grand->set_aux(red);
        rotate_right(root, grand);
      }
    } else {
      node_header *uncle = grand->left;
      if (is_red(uncle)) {
        node->parent()->set_aux(black);
        uncle->set_aux(black);
        grand->set_aux(red);
        node = grand;
      } else {
        if (node == node->parent()->left) {
          node = node->parent();
          rotate_right(root, node);
        }
        node->parent()->set_aux(black);
        grand->set_aux(red);
        rotate_left(root, grand);
      }
    }
  }
  root->set_aux(black);
}

RELOCO_API void rb_unlink(node_header *&root, node_header *node) noexcept {
  using namespace rb_detail;
  node_header *removed = node; // node whose position/colour is physically removed
  node_header *child = nullptr;
  node_header *child_parent = nullptr;
  unsigned char removed_colour = node->aux();

  if (!node->left) {
    child = node->right;
    child_parent = node->parent();
    replace_child(root, node->parent(), node, child);
    if (child)
      child->set_parent(node->parent());
  } else if (!node->right) {
    child = node->left;
    child_parent = node->parent();
    replace_child(root, node->parent(), node, child);
    child->set_parent(node->parent());
  } else {
    removed = bst_leftmost(node->right);
    removed_colour = removed->aux();
    child = removed->right;
    if (removed->parent() == node) {
      child_parent = removed;
    } else {
      child_parent = removed->parent();
      if (child)
        child->set_parent(removed->parent());
      removed->parent()->left = child;
      removed->right = node->right;
      removed->right->set_parent(removed);
    }
    replace_child(root, node->parent(), node, removed);
    removed->set_parent(node->parent());
    removed->left = node->left;
    removed->left->set_parent(removed);
    removed->set_aux(node->aux());
  }

  if (removed_colour == red)
    return;

  // A black node left the tree: the path through `child` is one black short.
  while (child != root && is_black(child)) {
    if (child == child_parent->left) {
      node_header *sibling = child_parent->right;
      if (is_red(sibling)) {
        sibling->set_aux(black);
        child_parent->set_aux(red);
        rotate_left(root, child_parent);
        sibling = child_parent->right;
      }
      if (is_black(sibling->left) && is_black(sibling->right)) {
        sibling->set_aux(red);
        child = child_parent;
        child_parent = child->parent();
      } else {
        if (is_black(sibling->right)) {
          sibling->left->set_aux(black);
          sibling->set_aux(red);
          rotate_right(root, sibling);
          sibling = child_parent->right;
        }
        sibling->set_aux(child_parent->aux());
        child_parent->set_aux(black);
        sibling->right->set_aux(black);
        rotate_left(root, child_parent);
        child = root;
        break;
      }
    } else {
      node_header *sibling = child_parent->left;
      if (is_red(sibling)) {
        sibling->set_aux(black);
        child_parent->set_aux(red);
        rotate_right(root, child_parent);
        sibling = child_parent->left;
      }
      if (is_black(sibling->left) && is_black(sibling->right)) {
        sibling->set_aux(red);
        child = child_parent;
        child_parent = child->parent();
      } else {
        if (is_black(sibling->left)) {
          sibling->right->set_aux(black);
          sibling->set_aux(red);
          rotate_left(root, sibling);
          sibling = child_parent->left;
        }
        sibling->set_aux(child_parent->aux());
        child_parent->set_aux(black);
        sibling->left->set_aux(black);
        rotate_right(root, child_parent);
        child = root;
        break;
      }
    }
  }
  if (child)
    child->set_aux(black);
}

RELOCO_END_UNSAFE_CODE
