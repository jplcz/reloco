// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Out-of-line, non-template bodies for the free functions declared
// `RELOCO_API` in `tree_base.hpp`. Included directly into the
// `reloco::detail` namespace block, either by `tree_base.hpp` itself
// (ordinary header-only build) or by the one translation unit building a
// shared library with `RELOCO_SHARED_BUILD` -- see `reloco_extern.hpp`.

RELOCO_API void bst_unlink(node_header *&root, node_header *node) noexcept {
  if (!node->left) {
    bst_transplant(root, node, node->right);
  } else if (!node->right) {
    bst_transplant(root, node, node->left);
  } else {
    node_header *successor = bst_leftmost(node->right);
    if (successor->parent != node) {
      bst_transplant(root, successor, successor->right);
      successor->right = node->right;
      successor->right->parent = successor;
    }
    bst_transplant(root, node, successor);
    successor->left = node->left;
    successor->left->parent = successor;
  }
}

RELOCO_API void bst_destroy_node(allocator_ref alloc, const type_metadata &type, const type_operations &ops,
                                 node_header *node) noexcept {
  if (ops.destroy_one) {
    ops.destroy_one(type, node_base::payload_of(node, type));
  }
  node_base::deallocate_node(alloc, node, type);
}

RELOCO_API void bst_clear(node_header *&root, allocator_ref alloc, const type_metadata &type,
                          const type_operations &ops) noexcept {
  // Iterative teardown, no recursion/extra storage: repeatedly right-rotate
  // away the left child (parent links are not kept consistent -- the whole
  // tree is being discarded) until `root` has none, then destroy that
  // left-child-free node and descend into its former right child.
  while (root) {
    if (root->left) {
      node_header *left = root->left;
      root->left = left->right;
      left->right = root;
      root = left;
    } else {
      node_header *next = root->right;
      bst_destroy_node(alloc, type, ops, root);
      root = next;
    }
  }
}
