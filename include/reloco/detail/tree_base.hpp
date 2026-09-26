// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file tree_base.hpp
 * @brief `tree_base<T, Compare, KeyOf>`: a basic, unbalanced, node-based
 * binary search tree engine, allocator-managed only (no inline/embedded
 * storage variant -- every node is one `allocator_ref`-owned allocation via
 * `node_base`, unlike `vector_base.hpp`'s `unowned`/`inline`/`sso`/`outline`
 * storage policies).
 *
 * Node memory layout and allocation are fully type-erased, reused as-is
 * from `node_base.hpp` (single `[header][payload]` allocation per node) and
 * `type_metadata.hpp` (`metadata_for<T>`, for the payload's runtime size/
 * alignment). The tree *algorithm* itself -- comparison-driven insert/find/
 * erase, in-order traversal -- is **not** further type-erased: like
 * `flat_container_base.hpp` (see that file's doc comment and the "is it
 * worth writing a type erased flat container base?" analysis it resulted
 * from), `Compare`/`KeyOf` are ordinary template parameters here, not
 * function pointers behind a `void*` boundary, so every comparison on the
 * `O(log n)` insert/find/erase hot path can still be inlined by the
 * compiler. `tree_base<T, Compare, KeyOf>` is therefore a template
 * instantiated per `(T, Compare, KeyOf)`, exactly like
 * `flat_container_base<Storage, Compare, KeyOf>`.
 *
 * Deletion uses the classic CLRS "transplant" technique: a node being
 * erased with two children is not "replaced" by copying its in-order
 * successor's *value* into it (which would require `T` to be move- or
 * copy-assignable, a requirement reloco's other containers don't otherwise
 * impose) -- instead the successor node itself is relinked into the erased
 * node's position, and only the erased node's payload is destroyed. No `T`
 * value ever moves once constructed; only `node_header` pointers do.
 *
 * This is deliberately a *plain, unbalanced* BST: worst case (e.g.
 * inserting already-sorted input) degenerates to a linked list with
 * O(n) find/insert/erase. `node_header::aux` (see `node_base.hpp`) is
 * reserved precisely so a balancing policy (red-black colour, AVL balance
 * factor, ...) can be layered on top of this same node layout later
 * without changing it again; this file does not implement one yet.
 */

#include "../construction_helpers.hpp"
#include "../default_allocator.hpp"
#include "../error.hpp"
#include "../expected.hpp"
#include "../lifetime.hpp"
#include "flat_container_base.hpp"
#include "node_base.hpp"
#include "type_operations.hpp"

#include <functional>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>

namespace reloco::detail {

// ---------------------------------------------------------------------------
// Structural BST navigation/relinking, shared by any future balancing
// policy: these only ever follow `parent`/`left`/`right` links, never touch
// a payload. Small enough (a handful of lines each) that out-of-lining them
// into a `.ipp` -- like the larger, allocator/`type_operations`-aware
// `bst_unlink`/`bst_clear` below -- would only add an extra call/return
// across translation units for no real code-size benefit.
// ---------------------------------------------------------------------------

[[nodiscard]] inline const node_header *bst_leftmost(const node_header *node) noexcept {
  if (!node)
    return nullptr;
  while (node->left)
    node = node->left;
  return node;
}

[[nodiscard]] inline node_header *bst_leftmost(node_header *node) noexcept {
  return const_cast<node_header *>(bst_leftmost(static_cast<const node_header *>(node)));
}

[[nodiscard]] inline const node_header *bst_rightmost(const node_header *node) noexcept {
  if (!node)
    return nullptr;
  while (node->right)
    node = node->right;
  return node;
}

[[nodiscard]] inline node_header *bst_rightmost(node_header *node) noexcept {
  return const_cast<node_header *>(bst_rightmost(static_cast<const node_header *>(node)));
}

[[nodiscard]] inline const node_header *bst_successor(const node_header *node) noexcept {
  if (!node)
    return nullptr;
  if (node->right)
    return bst_leftmost(node->right);
  const node_header *parent = node->parent;
  while (parent && node == parent->right) {
    node = parent;
    parent = parent->parent;
  }
  return parent;
}

[[nodiscard]] inline node_header *bst_successor(node_header *node) noexcept {
  return const_cast<node_header *>(bst_successor(static_cast<const node_header *>(node)));
}

[[nodiscard]] inline const node_header *bst_predecessor(const node_header *node) noexcept {
  if (!node)
    return nullptr;
  if (node->left)
    return bst_rightmost(node->left);
  const node_header *parent = node->parent;
  while (parent && node == parent->left) {
    node = parent;
    parent = parent->parent;
  }
  return parent;
}

[[nodiscard]] inline node_header *bst_predecessor(node_header *node) noexcept {
  return const_cast<node_header *>(bst_predecessor(static_cast<const node_header *>(node)));
}

/// @brief Replaces the subtree rooted at @p target (a child of @p root, or
/// @p root itself) with the subtree rooted at @p replacement (which may be
/// `nullptr`) -- CLRS's "transplant". Updates @p root itself when @p target
/// had no parent. Only relinks `parent`/`left`/`right`; never touches a
/// payload, so it is entirely `T`-independent.
inline void bst_transplant(node_header *&root, node_header *target, node_header *replacement) noexcept {
  if (!target->parent)
    root = replacement;
  else if (target == target->parent->left)
    target->parent->left = replacement;
  else
    target->parent->right = replacement;
  if (replacement)
    replacement->parent = target->parent;
}

/**
 * @brief Removes @p node from the tree rooted at @p root (CLRS deletion),
 * updating @p root if @p node was it. @p node is left fully unlinked but
 * otherwise untouched -- the caller still owns destroying its payload and
 * freeing it (see `bst_destroy_node`). Declared here, defined in
 * `tree_base.ipp`: this is genuine branchy logic (three deletion cases),
 * unlike the handful of one-line navigation helpers above.
 */
RELOCO_API void bst_unlink(node_header *&root, node_header *node) noexcept;

/**
 * @brief Destroys the payload at @p node (via @p ops's `destroy_one`, a
 * no-op when `nullptr`) and frees @p node's storage via `node_base`.
 * Declared here, defined in `tree_base.ipp`, since it is the one place
 * `tree_base<T, Compare, KeyOf>` needs `type_operations` at all -- routed
 * through the erased `destroy_one` rather than a compile-time `if
 * constexpr` specifically so this function itself stays non-template,
 * compiled once for the whole program instead of once per `(T, Compare,
 * KeyOf)`.
 */
RELOCO_API void bst_destroy_node(allocator_ref alloc, const type_metadata &type, const type_operations &ops,
                                 node_header *node) noexcept;

/**
 * @brief Destroys every node reachable from @p root and sets @p root to
 * `nullptr`, via the same allocation-free "rotate the left child up, then
 * delete the now-left-child-free root" iterative teardown used by
 * `bst_unlink`'s callers to avoid unbounded recursion depth on a
 * degenerate (linked-list-shaped) tree. Declared here, defined in
 * `tree_base.ipp`.
 */
RELOCO_API void bst_clear(node_header *&root, allocator_ref alloc, const type_metadata &type,
                          const type_operations &ops) noexcept;

/**
 * @brief Bidirectional, read-only in-order iterator over a `tree_base<T,
 * Compare, KeyOf>`. Wraps a single `const node_header *`; incrementing/
 * decrementing walks to the in-order successor/predecessor via the
 * structural helpers above.
 */
template <typename T> class tree_const_iterator {
public:
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = const T *;
  using reference = const T &;

  constexpr tree_const_iterator() noexcept = default;

  /**
   * @brief Constructs an iterator positioned at @p node (`nullptr` for
   * `end()`). @p root is the tree's root, carried along purely so
   * `operator--` on an `end()` iterator can find the rightmost node
   * without a predecessor to walk up from -- `node_header` itself has no
   * way back to the tree once you have walked off its far edge.
   */
  constexpr tree_const_iterator(const node_header *node, const node_header *root) noexcept : node_(node), root_(root) {}

  [[nodiscard]] reference operator*() const noexcept {
    return *std::launder(static_cast<const T *>(node_base::payload_of(node_, metadata_for<T>)));
  }
  [[nodiscard]] pointer operator->() const noexcept { return &**this; }

  tree_const_iterator &operator++() noexcept {
    node_ = bst_successor(node_);
    return *this;
  }
  tree_const_iterator operator++(int) noexcept {
    auto tmp = *this;
    ++*this;
    return tmp;
  }
  tree_const_iterator &operator--() noexcept {
    node_ = node_ ? bst_predecessor(node_) : bst_rightmost(root_);
    return *this;
  }
  tree_const_iterator operator--(int) noexcept {
    auto tmp = *this;
    --*this;
    return tmp;
  }

  [[nodiscard]] friend bool operator==(const tree_const_iterator &lhs, const tree_const_iterator &rhs) noexcept {
    return lhs.node_ == rhs.node_;
  }
  [[nodiscard]] friend bool operator!=(const tree_const_iterator &lhs, const tree_const_iterator &rhs) noexcept {
    return !(lhs == rhs);
  }

private:
  const node_header *node_ = nullptr;
  const node_header *root_ = nullptr;
};

/**
 * @brief A basic, unbalanced, allocator-managed binary search tree over
 * `T`, keyed by `KeyOf{}(value)` and ordered by `Compare`. See the
 * file-level comment for the layout/erasure/deletion design.
 */
template <typename T, typename Compare, typename KeyOf> class RELOCO_EXPORT tree_base {
  using header = node_header;

public:
  using value_type = T;
  using key_type = std::decay_t<decltype(KeyOf{}(std::declval<const value_type &>()))>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using const_reference = const value_type &;
  using const_pointer = const value_type *;
  using const_iterator = tree_const_iterator<value_type>;

  constexpr tree_base() noexcept = default;
  constexpr explicit tree_base(allocator_ref alloc) noexcept : alloc_(alloc) {}

  ~tree_base() { clear(); }

  tree_base(const tree_base &) = delete;
  tree_base &operator=(const tree_base &) = delete;

  tree_base(tree_base &&other) noexcept
      : root_(std::exchange(other.root_, nullptr)), size_(std::exchange(other.size_, 0)), alloc_(other.alloc_) {}

  tree_base &operator=(tree_base &&other) noexcept {
    if (this != &other) {
      clear();
      root_ = std::exchange(other.root_, nullptr);
      size_ = std::exchange(other.size_, 0);
      alloc_ = other.alloc_;
    }
    return *this;
  }

  [[nodiscard]] static result<tree_base> try_allocate(allocator_ref alloc) noexcept { return tree_base(alloc); }
  [[nodiscard]] static result<tree_base> try_create() noexcept { return try_allocate(default_allocator()); }

  /**
   * @brief Performs a deep copy using a specific allocator. Reinserts every
   * element in ascending order rather than replicating the source's exact
   * node layout, so the clone's shape only matches the source's by
   * coincidence -- both are valid BSTs over the same elements either way.
   */
  [[nodiscard]] result<tree_base> try_clone(allocator_ref alloc) const noexcept {
    auto cloned = try_allocate(alloc);
    if (!cloned)
      return unexpected(cloned.error());
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

  [[nodiscard]] result<tree_base> try_clone() const noexcept { return try_clone(alloc_); }

  [[nodiscard]] allocator_ref get_allocator() const noexcept { return alloc_; }

  [[nodiscard]] size_type size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  void clear() noexcept {
    bst_clear(root_, alloc_, metadata_for<T>, *get_type_operations_for<T>());
    size_ = 0;
  }

  /**
   * @brief Inserts @p value in its sorted position, keyed by
   * `KeyOf{}(value)`. Fails with `error::already_exists` if an element with
   * an equivalent key is already present, or with whatever fallible
   * construction of `T` itself can fail with (allocation failure, a
   * `try_construct` tier reporting its own error, ...).
   *
   * Takes @p value by value for the same reason as
   * `flat_container_base::try_insert` (see that file): a by-value sink
   * parameter accepts both lvalues (one copy) and rvalues (moved in with
   * no extra copy) uniformly.
   */
  [[nodiscard]] result<std::reference_wrapper<value_type>>
  try_insert(value_type value) & noexcept RELOCO_LIFETIMEBOUND {
    const KeyOf key_of{};
    const key_type &key = key_of(value);

    header *parent = nullptr;
    header *current = root_;
    bool insert_left = false;
    while (current) {
      const key_type &current_key = key_of(value_of(current));
      if (comp_(key, current_key)) {
        parent = current;
        current = current->left;
        insert_left = true;
      } else if (comp_(current_key, key)) {
        parent = current;
        current = current->right;
        insert_left = false;
      } else {
        return unexpected(error::already_exists);
      }
    }

    auto node_res = node_base::try_allocate_node(alloc_, metadata_for<T>);
    if (!node_res)
      return unexpected(node_res.error());
    header *node = *node_res;

    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    auto ctor_res = construction_helpers::try_construct<T>(
        alloc_, static_cast<T *>(node_base::payload_of(node, metadata_for<T>)), std::move(value));
    RELOCO_END_UNSAFE_BUFFER_USAGE
    if (!ctor_res) {
      node_base::deallocate_node(alloc_, node, metadata_for<T>);
      return unexpected(ctor_res.error());
    }

    node->parent = parent;
    if (!parent)
      root_ = node;
    else if (insert_left)
      parent->left = node;
    else
      parent->right = node;
    ++size_;

    return std::ref(value_of(node));
  }

  template <typename Key> [[nodiscard]] bool contains(const Key &key) const noexcept {
    return find_node(key) != nullptr;
  }

  template <typename Key>
  [[nodiscard]] result<std::reference_wrapper<const value_type>>
  try_find(const Key &key) const & noexcept RELOCO_LIFETIMEBOUND {
    header *node = find_node(key);
    if (!node)
      return unexpected(error::not_found);
    return std::cref(value_of(node));
  }

  template <typename Key> [[nodiscard]] result<void> try_remove(const Key &key) & noexcept {
    header *node = find_node(key);
    if (!node)
      return unexpected(error::not_found);
    bst_unlink(root_, node);
    bst_destroy_node(alloc_, metadata_for<T>, *get_type_operations_for<T>(), node);
    --size_;
    return {};
  }

  /**
   * @brief Rust `BTreeSet::first`/`BTreeMap::first_key_value` equivalent:
   * a reference to the smallest element by `Compare`, without removing it.
   * Fails with `error::container_empty` if the tree is empty.
   */
  [[nodiscard]] result<std::reference_wrapper<const value_type>> try_first() const & noexcept RELOCO_LIFETIMEBOUND {
    header *node = bst_leftmost(root_);
    if (!node)
      return unexpected(error::container_empty);
    return std::cref(value_of(node));
  }

  /**
   * @brief Rust `BTreeSet::last`/`BTreeMap::last_key_value` equivalent: a
   * reference to the greatest element by `Compare`, without removing it.
   * Fails with `error::container_empty` if the tree is empty.
   */
  [[nodiscard]] result<std::reference_wrapper<const value_type>> try_last() const & noexcept RELOCO_LIFETIMEBOUND {
    header *node = bst_rightmost(root_);
    if (!node)
      return unexpected(error::container_empty);
    return std::cref(value_of(node));
  }

  auto try_first() const && = delete;
  auto try_last() const && = delete;

  /**
   * @brief Rust `BTreeSet::pop_first`/`BTreeMap::pop_first` equivalent:
   * removes and returns the smallest element by `Compare`. Fails with
   * `error::container_empty` if the tree is empty. Moves the value out
   * before the node is unlinked/destroyed, so only `T`'s move constructor
   * (never move- or copy-assignment) is required, consistent with
   * `try_remove`'s CLRS "transplant" deletion never reassigning `T`.
   */
  [[nodiscard]] result<value_type> try_pop_first() & noexcept {
    header *node = bst_leftmost(root_);
    if (!node)
      return unexpected(error::container_empty);
    return pop_node(node);
  }

  /**
   * @brief Rust `BTreeSet::pop_last`/`BTreeMap::pop_last` equivalent:
   * removes and returns the greatest element by `Compare`. Fails with
   * `error::container_empty` if the tree is empty.
   */
  [[nodiscard]] result<value_type> try_pop_last() & noexcept {
    header *node = bst_rightmost(root_);
    if (!node)
      return unexpected(error::container_empty);
    return pop_node(node);
  }

  [[nodiscard]] const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_iterator(bst_leftmost(root_), root_);
  }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return const_iterator(nullptr, root_); }
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
   * @brief Rust `BTreeSet::retain`/`BTreeMap::retain` equivalent: keeps
   * only the elements for which @p pred(value) returns `true`, destroying
   * and unlinking every other node in a single in-order walk. @p pred is
   * invoked with `const value_type &`; capture the next in-order node
   * before a possible removal, since removing a node invalidates its own
   * `parent`/`left`/`right` links but never those of nodes visited later.
   */
  template <typename Pred> void retain(Pred &&pred) & noexcept {
    header *node = bst_leftmost(root_);
    while (node) {
      header *next = bst_successor(node);
      if (!pred(std::as_const(value_of(node)))) {
        bst_unlink(root_, node);
        bst_destroy_node(alloc_, metadata_for<T>, *get_type_operations_for<T>(), node);
        --size_;
      }
      node = next;
    }
  }

  /**
   * @brief Rust `BTreeSet::append`/`BTreeMap::append` equivalent: moves
   * every element out of @p other into `*this`, leaving @p other empty.
   * On a key collision, the element already in `*this` is replaced by
   * @p other's (matching Rust's "self's value is overwritten by other's"
   * semantics). Never allocates or constructs a new `T`: each moved
   * element's existing node allocation is reused as-is, relinked directly
   * into `*this`'s tree, so this only touches `node_header` pointers (and,
   * on a collision, destroys/deallocates the one node being replaced).
   */
  void append(tree_base &other) & noexcept {
    header *node = bst_leftmost(other.root_);
    while (node) {
      header *next = bst_successor(node);
      bst_unlink(other.root_, node);
      --other.size_;
      insert_node(node);
      node = next;
    }
  }

  /**
   * @brief Rust `BTreeSet::is_subset` equivalent: `true` if every element
   * of `*this` (compared by key, via `Compare`) is also present in
   * @p other. `O(size() + other.size())` via an in-order merge walk of
   * both trees.
   */
  [[nodiscard]] bool is_subset(const tree_base &other) const noexcept {
    const KeyOf key_of{};
    auto it = begin();
    auto other_it = other.begin();
    while (it != end()) {
      while (other_it != other.end() && comp_(key_of(*other_it), key_of(*it)))
        ++other_it;
      if (other_it == other.end() || comp_(key_of(*it), key_of(*other_it)))
        return false;
      ++it;
      ++other_it;
    }
    return true;
  }

  /**
   * @brief Rust `BTreeSet::is_superset` equivalent: `true` if every
   * element of @p other is also present in `*this` (i.e. `other.is_subset(
   * *this)`).
   */
  [[nodiscard]] bool is_superset(const tree_base &other) const noexcept { return other.is_subset(*this); }

  /**
   * @brief Rust `BTreeSet::is_disjoint` equivalent: `true` if `*this` and
   * @p other share no keys. `O(size() + other.size())` via an in-order
   * merge walk of both trees.
   */
  [[nodiscard]] bool is_disjoint(const tree_base &other) const noexcept {
    const KeyOf key_of{};
    auto it = begin();
    auto other_it = other.begin();
    while (it != end() && other_it != other.end()) {
      if (comp_(key_of(*it), key_of(*other_it)))
        ++it;
      else if (comp_(key_of(*other_it), key_of(*it)))
        ++other_it;
      else
        return false;
    }
    return true;
  }

private:
  [[nodiscard]] static T &value_of(header *node) noexcept {
    return *std::launder(static_cast<T *>(node_base::payload_of(node, metadata_for<T>)));
  }
  [[nodiscard]] static const T &value_of(const header *node) noexcept {
    return *std::launder(static_cast<const T *>(node_base::payload_of(node, metadata_for<T>)));
  }

  [[nodiscard]] value_type pop_node(header *node) noexcept {
    value_type value(std::move(value_of(node)));
    bst_unlink(root_, node);
    bst_destroy_node(alloc_, metadata_for<T>, *get_type_operations_for<T>(), node);
    --size_;
    return value;
  }

  /**
   * @brief Structurally inserts an already-constructed, currently
   * unlinked @p node (its payload already holds a valid `T`, taken from
   * some other tree) into `*this` at its sorted position. On a key
   * collision, the existing node is unlinked/destroyed first and the
   * walk restarts from the root -- since the colliding key is gone, that
   * retry is guaranteed to find no further collision. Used by `append`
   * to move nodes between trees without reallocating or reconstructing
   * `T`.
   */
  void insert_node(header *node) noexcept {
    const KeyOf key_of{};
    const key_type &key = key_of(value_of(node));
    header *parent = nullptr;
    header *current = root_;
    bool insert_left = false;
    while (current) {
      const key_type &current_key = key_of(value_of(current));
      if (comp_(key, current_key)) {
        parent = current;
        current = current->left;
        insert_left = true;
      } else if (comp_(current_key, key)) {
        parent = current;
        current = current->right;
        insert_left = false;
      } else {
        bst_unlink(root_, current);
        bst_destroy_node(alloc_, metadata_for<T>, *get_type_operations_for<T>(), current);
        --size_;
        insert_node(node);
        return;
      }
    }

    node->parent = parent;
    node->left = nullptr;
    node->right = nullptr;
    if (!parent)
      root_ = node;
    else if (insert_left)
      parent->left = node;
    else
      parent->right = node;
    ++size_;
  }

  template <typename Key> [[nodiscard]] header *find_node(const Key &key) const noexcept {
    const KeyOf key_of{};
    header *current = root_;
    while (current) {
      const key_type &current_key = key_of(value_of(current));
      if (comp_(key, current_key))
        current = current->left;
      else if (comp_(current_key, key))
        current = current->right;
      else
        return current;
    }
    return nullptr;
  }

  header *root_ = nullptr;
  size_type size_ = 0;
  allocator_ref alloc_ = default_allocator();
  Compare comp_{};
};

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "tree_base.ipp"
#endif

} // namespace reloco::detail
