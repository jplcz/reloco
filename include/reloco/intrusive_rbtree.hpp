// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file intrusive_rbtree.hpp
 * @brief `intrusive_rbtree<T, Hook, KeyOf, Compare>`: an ordered, unique-key
 * red-black tree that never allocates -- matching Linux's `struct rb_node`/
 * `<linux/rbtree.h>` or Boost.Intrusive's `set`, the ordered-container
 * counterpart to `intrusive_hash_table.hpp` (unordered).
 *
 * Every other reloco ordered container (`tree_set`/`tree_map`,
 * `detail::tree_base`) *owns* the storage backing each element -- inserting
 * copies or moves a value in, and the container's own allocator gives it
 * back on removal/destruction. That is the wrong shape for code that must
 * run before an allocator subsystem is even up (early kernel boot, an
 * interrupt handler, a `RELOCO_TLS_MODEL_OS`/`RELOCO_MUTEX_BACKEND_CUSTOM`
 * port's own internals -- see `detail/porting/README.md`):
 * `intrusive_rbtree` never allocates *at all*, at any point, because it
 * never owns a single byte of node storage. Nodes are ordinary caller-owned
 * objects (static, stack, a `slab`/pool the caller manages some other way)
 * that embed an `intrusive_rbtree_hook<T>` as a named member; the tree
 * itself holds nothing but a root pointer and a running element count --
 * inserting/removing a node only ever rewrites a handful of existing
 * pointers plus a one-bit color flip per rotation step.
 *
 * **Hook**: `struct my_node { reloco::intrusive_rbtree_hook<my_node> hook;
 * ... };` -- a named member, not a CRTP base, so one object can carry
 * independent hooks for several different trees (exactly like
 * Boost.Intrusive's member-hook style; Linux's embedded `struct rb_node`
 * inside the owning struct is the same idea). The tree is templated on a
 * pointer to that member (`&my_node::hook`), used purely at compile time to
 * get from a `T &` to its hook and back -- there is no `offsetof`/
 * reinterpret-cast trick anywhere in this file.
 *
 * **Balancing**: classic CLRS red-black (`parent`/`left`/`right` links plus
 * one `red` bool per node, no sentinel "nil" node -- `nullptr` is treated as
 * black throughout, the same null-aware style Linux's `rbtree.c` and
 * FreeBSD's rank-balanced `RB_*` macros (`sys/sys/tree.h`) use instead of a
 * shared mutable sentinel object, which a header-only, user-owned-node
 * design like this one has no good place to keep anyway). Worst-case tree
 * height stays `O(log n)` (`<= 2*log2(n+1)`) by construction, unlike
 * `detail::tree_base`'s deliberately unbalanced BST.
 *
 * **Key extraction**: a `KeyOf` functor template parameter (`KeyOf{}(const
 * T &) -> const key_type &`), exactly like `detail::flat_hash_base`'s and
 * `intrusive_hash_table`'s own `KeyOf` -- a template parameter, not a
 * type-erased callback, so every `Compare` call on the `O(log n)` hot path
 * still inlines. `Compare` defaults to `std::less<key_type>`.
 *
 * Everything that doesn't care about red-black balancing specifically --
 * iteration, `lower_bound`/`upper_bound`/`bounded_range`, plain
 * (non-restructuring) lookup, and the `*_and_dispose`/`clear`
 * Boost.Intrusive-flavored removal surface -- lives in the shared CRTP
 * base `detail::intrusive_bst_base` (see `detail/intrusive_bst_base.hpp`);
 * this file only adds what's genuinely red-black-specific:
 *
 * - `try_insert(node)` -> `result<void>`: fails with
 *   `error::already_exists` if an equivalent key is already present.
 *   `RELOCO_ASSERT`s that @p node is not already linked into *this* or any
 *   other `intrusive_rbtree` -- inserting an already-linked node would
 *   silently corrupt whichever tree it was already threaded into. Runs
 *   the CLRS insert-fixup afterwards to restore the red-black invariants.
 * - `try_remove(key)`/`remove(node)`/`try_pop_first()`/`try_pop_last()`/
 *   `erase(iterator)`/`*_and_dispose`: all unlink via the CLRS
 *   delete-fixup (private `unlink`), restoring the red-black invariants.
 *
 * See `detail::intrusive_bst_base`'s file-level doc comment for
 * `try_find(key)`/`contains(key)`/`try_first()`/`try_last()`/
 * `lower_bound`/`upper_bound`/`bounded_range` (all plain `O(log n)` walks,
 * never restructuring) and `clear()`/`clear_and_dispose` (iterative, no
 * recursion).
 *
 * Unlike every other reloco container, `intrusive_rbtree` has no
 * `try_clone`: cloning would require deciding where the clone's nodes live,
 * exactly the decision this whole file exists to leave to the caller (same
 * rationale as `intrusive_hash_table`).
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
 * this file's doc comment) to be usable with `intrusive_rbtree`.
 * `parent`/`left`/`right` are plain BST links; `red` is the one-bit color
 * CLRS red-black fixup needs. No sentinel "nil" node is used anywhere --
 * `nullptr` always means "black, no child/parent here".
 */
template <typename T> struct intrusive_rbtree_hook {
  T *parent = nullptr;
  T *left = nullptr;
  T *right = nullptr;
  bool red = false;
  bool linked = false;

  /** @brief Whether this hook is currently threaded into some tree. */
  [[nodiscard]] constexpr bool is_linked() const noexcept { return linked; }
};

/**
 * @brief See the file-level doc comment. @p Hook is a pointer to the
 * `intrusive_rbtree_hook<T>` member @p T embeds; @p KeyOf extracts a
 * node's key (`KeyOf{}(const T &) -> const key_type &`, exactly like
 * `intrusive_hash_table`'s own `KeyOf`); @p Compare orders two keys,
 * defaulting to `std::less<key_type>`.
 */
template <typename T, intrusive_rbtree_hook<T> T::*Hook, typename KeyOf,
          typename Compare = std::less<std::decay_t<decltype(KeyOf{}(std::declval<const T &>()))>>>
class RELOCO_POINTER intrusive_rbtree
    : public detail::intrusive_bst_base<intrusive_rbtree<T, Hook, KeyOf, Compare>, T, Hook, KeyOf, Compare> {
  using base_type = detail::intrusive_bst_base<intrusive_rbtree<T, Hook, KeyOf, Compare>, T, Hook, KeyOf, Compare>;
  friend base_type;

public:
  using typename base_type::const_iterator;
  using typename base_type::iterator;
  using typename base_type::key_type;
  using typename base_type::size_type;
  using typename base_type::value_type;

  constexpr intrusive_rbtree() noexcept = default;

  intrusive_rbtree(const intrusive_rbtree &) = delete;
  intrusive_rbtree &operator=(const intrusive_rbtree &) = delete;

  intrusive_rbtree(intrusive_rbtree &&other) noexcept = default;
  intrusive_rbtree &operator=(intrusive_rbtree &&other) noexcept = default;

  /**
   * @brief Links @p node in, keyed by `KeyOf{}(node)`. Fails with
   * `error::already_exists` if an equivalent key is already present.
   * `RELOCO_ASSERT`s @p node is not already linked (into *this* or any
   * other `intrusive_rbtree`) -- see the file-level doc comment.
   */
  [[nodiscard]] result<void> try_insert(T &node) & noexcept {
    RELOCO_ASSERT(!hook_of(node).is_linked(),
                  "reloco::intrusive_rbtree::try_insert: node is already linked into a tree");
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
    hook.red = true;
    hook.linked = true;
    if (parent == nullptr)
      this->root_ = &node;
    else if (went_left)
      hook_of(*parent).left = &node;
    else
      hook_of(*parent).right = &node;

    insert_fixup(&node);
    ++this->size_;
    return {};
  }

  result<void> try_insert(T &&node) & noexcept = delete;

private:
  using base_type::hook_of;
  using base_type::rotate_left;
  using base_type::rotate_right;

  // Called by `detail::intrusive_bst_base` (a friend) -- see this class's
  // `friend base_type;` declaration above.
  void unlink(T &node) noexcept {
    T *z = &node;
    T *y = z;
    bool y_original_red = hook_of(*y).red;
    T *x = nullptr;
    T *x_parent = nullptr;

    if (hook_of(*z).left == nullptr) {
      x = hook_of(*z).right;
      x_parent = hook_of(*z).parent;
      transplant(z, x);
    } else if (hook_of(*z).right == nullptr) {
      x = hook_of(*z).left;
      x_parent = hook_of(*z).parent;
      transplant(z, x);
    } else {
      y = this->leftmost(hook_of(*z).right);
      y_original_red = hook_of(*y).red;
      x = hook_of(*y).right;
      if (hook_of(*y).parent == z) {
        x_parent = y;
        if (x != nullptr)
          hook_of(*x).parent = y;
      } else {
        x_parent = hook_of(*y).parent;
        transplant(y, hook_of(*y).right);
        hook_of(*y).right = hook_of(*z).right;
        hook_of(*hook_of(*y).right).parent = y;
      }
      transplant(z, y);
      hook_of(*y).left = hook_of(*z).left;
      hook_of(*hook_of(*y).left).parent = y;
      hook_of(*y).red = hook_of(*z).red;
    }

    if (!y_original_red)
      delete_fixup(x, x_parent);

    auto &hz = hook_of(*z);
    hz.parent = nullptr;
    hz.left = nullptr;
    hz.right = nullptr;
    hz.red = false;
    hz.linked = false;
    --this->size_;
  }

  // Called by `detail::intrusive_bst_base` (a friend) during
  // `clear_and_dispose`'s generic teardown -- it already resets
  // `parent`/`right`/`linked` itself, this only needs to handle the
  // red-black-specific `red` bit.
  static void reset_hook(T &node) noexcept { hook_of(node).red = false; }

  // ---- CLRS red-black fixup, null-aware (no sentinel node) ----

  [[nodiscard]] static bool is_red(const T *node) noexcept {
    return node != nullptr && hook_of(*const_cast<T *>(node)).red;
  }

  void insert_fixup(T *z) noexcept {
    while (hook_of(*z).parent != nullptr && is_red(hook_of(*z).parent)) {
      T *parent = hook_of(*z).parent;
      T *grandparent = hook_of(*parent).parent; // non-null: a red node is never the root
      if (parent == hook_of(*grandparent).left) {
        T *uncle = hook_of(*grandparent).right;
        if (is_red(uncle)) {
          hook_of(*parent).red = false;
          hook_of(*uncle).red = false;
          hook_of(*grandparent).red = true;
          z = grandparent;
        } else {
          if (z == hook_of(*parent).right) {
            z = parent;
            rotate_left(z);
            parent = hook_of(*z).parent;
          }
          hook_of(*parent).red = false;
          hook_of(*grandparent).red = true;
          rotate_right(grandparent);
        }
      } else {
        T *uncle = hook_of(*grandparent).left;
        if (is_red(uncle)) {
          hook_of(*parent).red = false;
          hook_of(*uncle).red = false;
          hook_of(*grandparent).red = true;
          z = grandparent;
        } else {
          if (z == hook_of(*parent).left) {
            z = parent;
            rotate_right(z);
            parent = hook_of(*z).parent;
          }
          hook_of(*parent).red = false;
          hook_of(*grandparent).red = true;
          rotate_left(grandparent);
        }
      }
    }
    hook_of(*this->root_).red = false;
  }

  // Replaces the subtree rooted at @p target with @p replacement (CLRS
  // "transplant"); @p replacement may be `nullptr`.
  void transplant(T *target, T *replacement) noexcept {
    T *parent = hook_of(*target).parent;
    if (parent == nullptr)
      this->root_ = replacement;
    else if (target == hook_of(*parent).left)
      hook_of(*parent).left = replacement;
    else
      hook_of(*parent).right = replacement;
    if (replacement != nullptr)
      hook_of(*replacement).parent = parent;
  }

  void delete_fixup(T *x, T *x_parent) noexcept {
    while (x != this->root_ && !is_red(x)) {
      RELOCO_ASSERT(x_parent != nullptr, "reloco::intrusive_rbtree: delete_fixup lost track of parent");
      if (x == hook_of(*x_parent).left) {
        T *sibling = hook_of(*x_parent).right;
        if (is_red(sibling)) {
          hook_of(*sibling).red = false;
          hook_of(*x_parent).red = true;
          rotate_left(x_parent);
          sibling = hook_of(*x_parent).right;
        }
        if (!is_red(hook_of(*sibling).left) && !is_red(hook_of(*sibling).right)) {
          hook_of(*sibling).red = true;
          x = x_parent;
          x_parent = hook_of(*x).parent;
        } else {
          if (!is_red(hook_of(*sibling).right)) {
            if (hook_of(*sibling).left != nullptr)
              hook_of(*hook_of(*sibling).left).red = false;
            hook_of(*sibling).red = true;
            rotate_right(sibling);
            sibling = hook_of(*x_parent).right;
          }
          hook_of(*sibling).red = hook_of(*x_parent).red;
          hook_of(*x_parent).red = false;
          if (hook_of(*sibling).right != nullptr)
            hook_of(*hook_of(*sibling).right).red = false;
          rotate_left(x_parent);
          x = this->root_;
          x_parent = nullptr;
        }
      } else {
        T *sibling = hook_of(*x_parent).left;
        if (is_red(sibling)) {
          hook_of(*sibling).red = false;
          hook_of(*x_parent).red = true;
          rotate_right(x_parent);
          sibling = hook_of(*x_parent).left;
        }
        if (!is_red(hook_of(*sibling).right) && !is_red(hook_of(*sibling).left)) {
          hook_of(*sibling).red = true;
          x = x_parent;
          x_parent = hook_of(*x).parent;
        } else {
          if (!is_red(hook_of(*sibling).left)) {
            if (hook_of(*sibling).right != nullptr)
              hook_of(*hook_of(*sibling).right).red = false;
            hook_of(*sibling).red = true;
            rotate_left(sibling);
            sibling = hook_of(*x_parent).left;
          }
          hook_of(*sibling).red = hook_of(*x_parent).red;
          hook_of(*x_parent).red = false;
          if (hook_of(*sibling).left != nullptr)
            hook_of(*hook_of(*sibling).left).red = false;
          rotate_right(x_parent);
          x = this->root_;
          x_parent = nullptr;
        }
      }
    }
    if (x != nullptr)
      hook_of(*x).red = false;
  }
};

} // namespace reloco
