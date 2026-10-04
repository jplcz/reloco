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
 * - `try_insert(node)` -> `result<void>`: fails with
 *   `error::already_exists` if an equivalent key is already present.
 *   `RELOCO_ASSERT`s that @p node is not already linked into *this* or any
 *   other `intrusive_rbtree` -- inserting an already-linked node would
 *   silently corrupt whichever tree it was already threaded into.
 * - `try_find(key)`/`contains(key)` -> lookup, `O(log n)` walk from the
 *   root, same asymptotic cost as `tree_set`/`tree_map`.
 * - `try_remove(key)` -> `result<void>`: walks to find the node, then
 *   unlinks+rebalances it -- fails with `error::not_found` if absent.
 * - `remove(node)`: unlinks+rebalances a node reference already in hand
 *   (no re-walk needed to *locate* it, unlike `try_remove(key)`).
 * - `begin()`/`end()`/`iterator_to(node)`/`erase(iterator)`: in-order
 *   (ascending `Compare` order) iteration -- the surface
 *   `extract_if_iterator`/`isolated_node_tx` (`intrusive_iteration.hpp`)
 *   needs from a `Container`.
 * - `try_first()`/`try_last()`/`try_pop_first()`/`try_pop_last()`: Rust
 *   `BTreeSet`-flavored smallest/greatest accessors, matching
 *   `detail::tree_base`'s own (see `tree-containers.md`).
 * - `clear()`: unlinks every node (leaving each one's own storage otherwise
 *   untouched -- there is nothing to destroy, `T` is never copied/moved/
 *   destroyed by this file at all).
 *
 * Unlike every other reloco container, `intrusive_rbtree` has no
 * `try_clone`: cloning would require deciding where the clone's nodes live,
 * exactly the decision this whole file exists to leave to the caller (same
 * rationale as `intrusive_hash_table`).
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
 * @brief Bidirectional, in-order iterator over an `intrusive_rbtree<T,
 * Hook, ...>`. Wraps a single `T *` (`nullptr` for `end()`) plus the tree's
 * root (carried along purely so `operator--` on an `end()` iterator can
 * find the rightmost node, exactly like `detail::tree_const_iterator`).
 * Templated only on @p T and @p Hook (not the tree's `KeyOf`/`Compare`), so
 * `intrusive_rbtree::iterator`/`const_iterator` are both instantiations of
 * this one class (@p IsConst selects which); `iterator` converts to
 * `const_iterator` implicitly, matching every other reloco container's
 * mutable/const iterator pair.
 */
template <typename T, intrusive_rbtree_hook<T> T::*Hook, bool IsConst> class intrusive_rbtree_iterator {
public:
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const T *, T *>;
  using reference = std::conditional_t<IsConst, const T &, T &>;

  constexpr intrusive_rbtree_iterator() noexcept = default;

  constexpr intrusive_rbtree_iterator(T *node, T *root) noexcept : m_node(node), m_root(root) {}

  // Mutable -> const conversion only, same direction every other reloco
  // iterator/const_iterator pair allows.
  template <bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
  constexpr intrusive_rbtree_iterator(const intrusive_rbtree_iterator<T, Hook, WasConst> &other) noexcept
      : m_node(other.node()), m_root(other.root()) {}

  [[nodiscard]] reference operator*() const noexcept { return *m_node; }
  [[nodiscard]] pointer operator->() const noexcept { return m_node; }

  intrusive_rbtree_iterator &operator++() noexcept {
    RELOCO_ASSERT(m_node != nullptr, "reloco::intrusive_rbtree_iterator: incrementing an end() iterator");
    m_node = successor(m_node);
    return *this;
  }
  intrusive_rbtree_iterator operator++(int) noexcept {
    auto tmp = *this;
    ++*this;
    return tmp;
  }
  intrusive_rbtree_iterator &operator--() noexcept {
    m_node = m_node != nullptr ? predecessor(m_node) : rightmost(m_root);
    return *this;
  }
  intrusive_rbtree_iterator operator--(int) noexcept {
    auto tmp = *this;
    --*this;
    return tmp;
  }

  [[nodiscard]] friend bool operator==(const intrusive_rbtree_iterator &lhs,
                                       const intrusive_rbtree_iterator &rhs) noexcept {
    return lhs.m_node == rhs.m_node;
  }
  [[nodiscard]] friend bool operator!=(const intrusive_rbtree_iterator &lhs,
                                       const intrusive_rbtree_iterator &rhs) noexcept {
    return !(lhs == rhs);
  }

  // Exposed only so the mutable -> const converting constructor above (a
  // different instantiation of this same template) can read another
  // iterator's state; not meant for general use.
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
 * `intrusive_rbtree_hook<T>` member @p T embeds; @p KeyOf extracts a
 * node's key (`KeyOf{}(const T &) -> const key_type &`, exactly like
 * `intrusive_hash_table`'s own `KeyOf`); @p Compare orders two keys,
 * defaulting to `std::less<key_type>`.
 */
template <typename T, intrusive_rbtree_hook<T> T::*Hook, typename KeyOf,
          typename Compare = std::less<std::decay_t<decltype(KeyOf{}(std::declval<const T &>()))>>>
class RELOCO_POINTER intrusive_rbtree {
public:
  using value_type = T;
  using key_type = std::decay_t<decltype(KeyOf{}(std::declval<const T &>()))>;
  using size_type = std::size_t;
  using iterator = intrusive_rbtree_iterator<T, Hook, false>;
  using const_iterator = intrusive_rbtree_iterator<T, Hook, true>;

  constexpr intrusive_rbtree() noexcept = default;

  intrusive_rbtree(const intrusive_rbtree &) = delete;
  intrusive_rbtree &operator=(const intrusive_rbtree &) = delete;

  intrusive_rbtree(intrusive_rbtree &&other) noexcept
      : root_(std::exchange(other.root_, nullptr)), size_(std::exchange(other.size_, size_type{0})) {}

  intrusive_rbtree &operator=(intrusive_rbtree &&other) noexcept {
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

  /** @brief Builds an iterator pointing at @p node, currently linked into *this* tree. */
  [[nodiscard]] iterator iterator_to(T &node) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(hook_of(node).is_linked(), "reloco::intrusive_rbtree::iterator_to: node is not linked");
    return iterator(&node, root_);
  }
  [[nodiscard]] const_iterator iterator_to(const T &node) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(hook_of(const_cast<T &>(node)).is_linked(),
                  "reloco::intrusive_rbtree::iterator_to: node is not linked");
    return const_iterator(const_cast<T *>(&node), root_);
  }
  iterator iterator_to(T &node) && = delete;
  const_iterator iterator_to(const T &node) const && = delete;

  /**
   * @brief Unlinks the node at @p pos and returns an iterator to the node
   * that followed it, matching `std::set::erase`/`boost::intrusive::set::
   * erase` -- what lets `extract_if_iterator` advance its own traversal
   * cursor directly off of this return value instead of needing a separate
   * lookahead step.
   */
  iterator erase(iterator pos) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos != end(), "reloco::intrusive_rbtree::erase: called with end() iterator");
    T &node = *pos;
    iterator next = pos;
    ++next;
    unlink(node);
    return iterator(next.node(), root_);
  }
  iterator erase(iterator pos) && = delete;

  /**
   * @brief Links @p node in, keyed by `KeyOf{}(node)`. Fails with
   * `error::already_exists` if an equivalent key is already present.
   * `RELOCO_ASSERT`s @p node is not already linked (into *this* or any
   * other `intrusive_rbtree`) -- see the file-level doc comment.
   */
  [[nodiscard]] result<void> try_insert(T &node) & noexcept {
    RELOCO_ASSERT(!hook_of(node).is_linked(), "reloco::intrusive_rbtree::try_insert: node is already linked into a tree");
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
    hook.red = true;
    hook.linked = true;
    if (parent == nullptr)
      root_ = &node;
    else if (went_left)
      hook_of(*parent).left = &node;
    else
      hook_of(*parent).right = &node;

    insert_fixup(&node);
    ++size_;
    return {};
  }

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
   * @brief Walks to find @p key, then unlinks it. Fails with
   * `error::not_found` if absent. Prefer `remove(node)` when the caller
   * already holds a reference to the node being removed (e.g. it just
   * found it via `try_find`) to skip the redundant walk.
   */
  template <typename K> [[nodiscard]] result<void> try_remove(const K &key) & noexcept {
    T *found = find_ptr(key);
    if (found == nullptr)
      return unexpected(error::not_found);
    unlink(*found);
    return {};
  }

  /**
   * @brief Removes @p node, already known to be linked into *this* tree
   * (no re-walk needed to locate it). `RELOCO_ASSERT`s @p node is
   * currently linked.
   */
  void remove(T &node) & noexcept {
    RELOCO_ASSERT(hook_of(node).is_linked(), "reloco::intrusive_rbtree::remove: node is not linked");
    unlink(node);
  }
  void remove(T &&node) & noexcept = delete;

  /** @brief Reference to the smallest element, without removing it. Fails with `error::container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_first() & noexcept {
    T *node = leftmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    return std::ref(*node);
  }
  [[nodiscard]] result<std::reference_wrapper<const T>> try_first() const & noexcept {
    const T *node = leftmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    return std::cref(*node);
  }

  /** @brief Reference to the greatest element, without removing it. Fails with `error::container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_last() & noexcept {
    T *node = rightmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    return std::ref(*node);
  }
  [[nodiscard]] result<std::reference_wrapper<const T>> try_last() const & noexcept {
    const T *node = rightmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    return std::cref(*node);
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
   * @brief Unlinks every node (their own storage is otherwise untouched --
   * `T` is never constructed/destroyed by this file). `O(n)`, iterative
   * (no recursion, so a degenerate/huge tree cannot blow the call stack):
   * repeatedly rotate the left child up and detach the now-left-child-free
   * root, the same teardown shape `detail::tree_base`'s `bst_clear` uses --
   * colors/rebalancing are irrelevant since the whole tree is being
   * discarded.
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
      hook.red = false;
      hook.linked = false;
      node = next;
    }
    root_ = nullptr;
    size_ = 0;
  }

private:
  static intrusive_rbtree_hook<T> &hook_of(T &node) noexcept { return node.*Hook; }

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

  // ---- CLRS red-black fixup, null-aware (no sentinel node) ----

  [[nodiscard]] static bool is_red(const T *node) noexcept { return node != nullptr && hook_of(*const_cast<T *>(node)).red; }

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
    hook_of(*root_).red = false;
  }

  // Replaces the subtree rooted at @p target with @p replacement (CLRS
  // "transplant"); @p replacement may be `nullptr`.
  void transplant(T *target, T *replacement) noexcept {
    T *parent = hook_of(*target).parent;
    if (parent == nullptr)
      root_ = replacement;
    else if (target == hook_of(*parent).left)
      hook_of(*parent).left = replacement;
    else
      hook_of(*parent).right = replacement;
    if (replacement != nullptr)
      hook_of(*replacement).parent = parent;
  }

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
      y = leftmost(hook_of(*z).right);
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
    --size_;
  }

  void delete_fixup(T *x, T *x_parent) noexcept {
    while (x != root_ && !is_red(x)) {
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
          x = root_;
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
          x = root_;
          x_parent = nullptr;
        }
      }
    }
    if (x != nullptr)
      hook_of(*x).red = false;
  }

  T *root_ = nullptr;
  size_type size_ = 0;
};

} // namespace reloco
