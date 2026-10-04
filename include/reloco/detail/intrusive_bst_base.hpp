// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file intrusive_bst_base.hpp
 * @brief `detail::intrusive_bst_iterator<T, Hook, IsConst>` and
 * `detail::intrusive_bst_base<Derived, T, Hook, KeyOf, Compare>`: the
 * balancing-agnostic machinery shared by `intrusive_rbtree.hpp` and
 * `intrusive_splay_tree.hpp` (CRTP base, not a public-facing container).
 *
 * Both files embed the same `parent`/`left`/`right`-linked binary search
 * tree shape and only disagree about *how* they stay balanced (red-black
 * fixups vs. splaying); this file factors out everything that doesn't care
 * about that distinction: the bidirectional in-order iterator, left/right
 * rotation (a pure pointer-rewiring operation, identical either way),
 * plain (non-restructuring) key lookup/bound queries, and the
 * iteration/dispose surface (`begin`/`end`/`erase`/`*_and_dispose`/
 * `clear`) `intrusive_iteration.hpp` and Boost.Intrusive-flavored callers
 * both need. @p Derived (the concrete `intrusive_rbtree`/
 * `intrusive_splay_tree`) supplies only the balancing-specific `unlink`
 * (CLRS delete-fixup vs. splay-then-join) and `reset_hook` (per-node hook
 * fields to clear during `clear_and_dispose`'s teardown, e.g. the
 * red-black hook's `red` bit) via a CRTP `static_cast`, and must `friend`
 * this template so it may call them.
 *
 * **Boost.Intrusive-flavored additions**: `lower_bound`/`upper_bound`/
 * `bounded_range` (ordered range queries), `erase(first, last)`, and
 * `erase_and_dispose`/`remove_and_dispose`/`try_remove_and_dispose`/
 * `clear_and_dispose` (removal that also invokes a caller-supplied
 * `Disposer` on each removed node -- `boost::intrusive::set`'s own
 * naming/shape) all live here too, since none of them care about the
 * balancing strategy either: a bound query is just a walk, and disposal
 * is just "unlink/teardown, then call `disposer(node)` instead of
 * leaving the node's storage untouched". `Disposer` is any `void(T
 * &)`-callable (a lambda, function pointer, or functor) -- typically
 * something that returns @p T's storage to whatever pool/slab it came
 * from, or runs its destructor if the caller used placement-new over the
 * hook.
 *
 * **Splicing**: `splice`/`splice_replace`/`splice_discard` move a
 * `[first, last)` range of nodes out of a *different* tree of this same
 * concrete type into *this*, resolving key conflicts three different
 * ways (`std::map::merge`'s "leave the conflicting source node behind",
 * or either side of a replace -- see each method's own doc comment).
 * Every node still has to be walked one at a time (`find_ptr` against
 * *this*, then `unlink` from @p source, then `try_insert` into *this*)
 * since there is no `O(1)` "just relink this subtree" shortcut once key
 * conflicts are possible: the moved nodes have to be re-inserted
 * one-by-one (via @p Derived's own `try_insert`, so red-black rebalancing
 * or splaying still happens exactly as if the caller had called
 * `try_insert` directly) rather than spliced in as an intact subtree.
 */

#include "../error.hpp"
#include "../expected.hpp"
#include "../lifetime.hpp"
#include "assert.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace reloco::detail {

/**
 * @brief Bidirectional, in-order iterator shared by `intrusive_rbtree` and
 * `intrusive_splay_tree`. Wraps a single `T *` (`nullptr` for `end()`) plus
 * the tree's root (carried along purely so `operator--` on an `end()`
 * iterator can find the rightmost node). Templated on @p Hook (a pointer
 * to whichever hook type @p T embeds -- `intrusive_rbtree_hook<T>` or
 * `intrusive_splay_tree_hook<T>`, both expose the same `parent`/`left`/
 * `right` member names this class relies on) rather than on the owning
 * tree's `KeyOf`/`Compare`, so `iterator`/`const_iterator` are both
 * instantiations of this one class (@p IsConst selects which); `iterator`
 * converts to `const_iterator` implicitly, matching every other reloco
 * container's mutable/const iterator pair.
 */
template <typename T, auto Hook, bool IsConst> class intrusive_bst_iterator {
public:
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const T *, T *>;
  using reference = std::conditional_t<IsConst, const T &, T &>;

  constexpr intrusive_bst_iterator() noexcept = default;

  constexpr intrusive_bst_iterator(T *node, T *root) noexcept : m_node(node), m_root(root) {}

  // Mutable -> const conversion only, same direction every other reloco
  // iterator/const_iterator pair allows.
  template <bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
  constexpr intrusive_bst_iterator(const intrusive_bst_iterator<T, Hook, WasConst> &other) noexcept
      : m_node(other.node()), m_root(other.root()) {}

  [[nodiscard]] reference operator*() const noexcept { return *m_node; }
  [[nodiscard]] pointer operator->() const noexcept { return m_node; }

  intrusive_bst_iterator &operator++() noexcept {
    RELOCO_ASSERT(m_node != nullptr, "reloco::detail::intrusive_bst_iterator: incrementing an end() iterator");
    m_node = successor(m_node);
    return *this;
  }
  intrusive_bst_iterator operator++(int) noexcept {
    auto tmp = *this;
    ++*this;
    return tmp;
  }
  intrusive_bst_iterator &operator--() noexcept {
    m_node = m_node != nullptr ? predecessor(m_node) : rightmost(m_root);
    return *this;
  }
  intrusive_bst_iterator operator--(int) noexcept {
    auto tmp = *this;
    --*this;
    return tmp;
  }

  [[nodiscard]] friend bool operator==(const intrusive_bst_iterator &lhs, const intrusive_bst_iterator &rhs) noexcept {
    return lhs.m_node == rhs.m_node;
  }
  [[nodiscard]] friend bool operator!=(const intrusive_bst_iterator &lhs, const intrusive_bst_iterator &rhs) noexcept {
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
 * @brief CRTP base factoring out everything `intrusive_rbtree`/
 * `intrusive_splay_tree` don't disagree about -- see the file-level doc
 * comment. @p Derived must `friend class intrusive_bst_base<Derived, T,
 * Hook, KeyOf, Compare>;` and provide:
 *
 * - `void unlink(T &node) noexcept`: unlinks @p node (already known
 *   linked into *this* tree), rebalancing however @p Derived likes.
 * - `void reset_hook(T &node) noexcept`: clears any hook fields beyond
 *   `parent`/`left`/`right`/`linked` (which this base already resets)
 *   that @p Derived's hook adds -- e.g. `intrusive_rbtree_hook::red`.
 *
 * Every accessor that returns a reference/pointer/iterator into the tree
 * is blocked on rvalue `*this` (`const &&`/`&&` deleted), matching the
 * library-wide rvalue-safety convention: the tree is a non-owning
 * `RELOCO_POINTER` view, so handing back something `RELOCO_LIFETIMEBOUND`
 * to it from a temporary would dangle immediately.
 */
template <typename Derived, typename T, auto Hook, typename KeyOf, typename Compare> class intrusive_bst_base {
public:
  using value_type = T;
  using key_type = std::decay_t<decltype(KeyOf{}(std::declval<const T &>()))>;
  using size_type = std::size_t;
  using iterator = intrusive_bst_iterator<T, Hook, false>;
  using const_iterator = intrusive_bst_iterator<T, Hook, true>;

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
    RELOCO_ASSERT(hook_of(node).is_linked(), "reloco::detail::intrusive_bst_base::iterator_to: node is not linked");
    return iterator(&node, root_);
  }
  [[nodiscard]] const_iterator iterator_to(const T &node) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(hook_of(const_cast<T &>(node)).is_linked(),
                  "reloco::detail::intrusive_bst_base::iterator_to: node is not linked");
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
    RELOCO_ASSERT(pos != end(), "reloco::detail::intrusive_bst_base::erase: called with end() iterator");
    T &node = *pos;
    iterator next = pos;
    ++next;
    derived().unlink(node);
    return iterator(next.node(), root_);
  }
  iterator erase(iterator pos) && = delete;

  /**
   * @brief Unlinks every node in `[first, last)` and returns @p last
   * (re-anchored to the possibly-changed root), matching
   * `std::set::erase(first, last)`/`boost::intrusive::set::erase(first,
   * last)`. Each node is re-looked-up as "the current leftmost remaining
   * node" one at a time rather than captured up front, so this is safe
   * even though every `unlink` can restructure the tree (rebalance or,
   * for a splay tree, splay) out from under a cached `next` pointer --
   * unlike single-element `erase`, which can safely cache `++pos` first
   * because only the single erased node's links change.
   */
  iterator erase(iterator first, iterator last) & noexcept RELOCO_LIFETIMEBOUND {
    while (first != last) {
      T &node = *first;
      ++first;
      derived().unlink(node);
    }
    return iterator(last.node(), root_);
  }
  iterator erase(iterator first, iterator last) && = delete;

  /** @brief Plain walk, never restructures. */
  template <typename K> [[nodiscard]] bool contains(const K &key) const & noexcept { return find_ptr(key) != nullptr; }
  template <typename K> bool contains(const K &key) const && = delete;

  /**
   * @brief Plain walk, never restructures. Fails with
   * `error::not_found` if @p key is absent. Returns a mutable `T &` even
   * though this overload is `const`-qualified: the tree doesn't own its
   * nodes, so a `const` *tree* reference says nothing about whether the
   * caller may mutate the nodes it already has pointers/references to.
   */
  template <typename K> [[nodiscard]] result<std::reference_wrapper<T>> try_find(const K &key) const & noexcept {
    T *found = find_ptr(key);
    if (found == nullptr)
      return unexpected(error::not_found);
    return std::ref(*found);
  }
  template <typename K> result<std::reference_wrapper<T>> try_find(const K &key) const && = delete;

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
    derived().unlink(*found);
    return {};
  }

  /**
   * @brief Removes @p node, already known to be linked into *this* tree
   * (no re-walk needed to locate it). `RELOCO_ASSERT`s @p node is
   * currently linked.
   */
  void remove(T &node) & noexcept {
    RELOCO_ASSERT(hook_of(node).is_linked(), "reloco::detail::intrusive_bst_base::remove: node is not linked");
    derived().unlink(node);
  }
  void remove(T &&node) & noexcept = delete;

  /** @brief Reference to the smallest element, without removing it. Fails with `error::container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_first() const & noexcept {
    T *node = leftmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    return std::ref(*node);
  }
  result<std::reference_wrapper<T>> try_first() const && = delete;

  /** @brief Reference to the greatest element, without removing it. Fails with `error::container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_last() const & noexcept {
    T *node = rightmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    return std::ref(*node);
  }
  result<std::reference_wrapper<T>> try_last() const && = delete;

  /** @brief Unlinks and returns the smallest element. Fails with `error::container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_pop_first() & noexcept {
    T *node = leftmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    derived().unlink(*node);
    return std::ref(*node);
  }

  /** @brief Unlinks and returns the greatest element. Fails with `error::container_empty`. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_pop_last() & noexcept {
    T *node = rightmost(root_);
    if (node == nullptr)
      return unexpected(error::container_empty);
    derived().unlink(*node);
    return std::ref(*node);
  }

  /**
   * @brief First element whose key is not less than @p key (the
   * `std::set::lower_bound`/`boost::intrusive::set::lower_bound`
   * convention), or `end()` if every element's key is less than @p key.
   * Plain walk, never restructures.
   */
  template <typename K> [[nodiscard]] iterator lower_bound(const K &key) & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(lower_bound_ptr(key), root_);
  }
  template <typename K>
  [[nodiscard]] const_iterator lower_bound(const K &key) const & noexcept RELOCO_LIFETIMEBOUND {
    return const_iterator(lower_bound_ptr(key), root_);
  }
  template <typename K> iterator lower_bound(const K &key) && = delete;
  template <typename K> const_iterator lower_bound(const K &key) const && = delete;

  /**
   * @brief First element whose key is greater than @p key (the
   * `std::set::upper_bound`/`boost::intrusive::set::upper_bound`
   * convention), or `end()` if no element's key is greater than @p key.
   * Plain walk, never restructures.
   */
  template <typename K> [[nodiscard]] iterator upper_bound(const K &key) & noexcept RELOCO_LIFETIMEBOUND {
    return iterator(upper_bound_ptr(key), root_);
  }
  template <typename K>
  [[nodiscard]] const_iterator upper_bound(const K &key) const & noexcept RELOCO_LIFETIMEBOUND {
    return const_iterator(upper_bound_ptr(key), root_);
  }
  template <typename K> iterator upper_bound(const K &key) && = delete;
  template <typename K> const_iterator upper_bound(const K &key) const && = delete;

  /**
   * @brief The `[lower_bound(lo), upper_bound(hi)]`-or-similar sub-range
   * of elements between @p lo and @p hi, matching
   * `boost::intrusive::set::bounded_range`: @p left_closed/@p
   * right_closed select whether @p lo/@p hi themselves are included
   * (`true`, the default for @p left_closed) or excluded (`false`, the
   * default for @p right_closed) -- i.e. `[lo, hi)` by default, the same
   * half-open convention `std::set::equal_range`-over-a-range callers
   * expect. Returns `{end(), end()}` (an empty range) if @p lo compares
   * greater than @p hi under `Compare`. Plain walks, never restructures.
   */
  template <typename K>
  [[nodiscard]] std::pair<iterator, iterator> bounded_range(const K &lo, const K &hi, bool left_closed = true,
                                                             bool right_closed = false) & noexcept {
    const Compare less{};
    if (less(hi, lo))
      return {end(), end()};
    T *first = left_closed ? lower_bound_ptr(lo) : upper_bound_ptr(lo);
    T *last = right_closed ? upper_bound_ptr(hi) : lower_bound_ptr(hi);
    return {iterator(first, root_), iterator(last, root_)};
  }
  template <typename K>
  [[nodiscard]] std::pair<const_iterator, const_iterator> bounded_range(const K &lo, const K &hi,
                                                                         bool left_closed = true,
                                                                         bool right_closed = false) const & noexcept {
    const Compare less{};
    if (less(hi, lo))
      return {end(), end()};
    T *first = left_closed ? lower_bound_ptr(lo) : upper_bound_ptr(lo);
    T *last = right_closed ? upper_bound_ptr(hi) : lower_bound_ptr(hi);
    return {const_iterator(first, root_), const_iterator(last, root_)};
  }
  template <typename K>
  std::pair<iterator, iterator> bounded_range(const K &lo, const K &hi, bool left_closed, bool right_closed) && =
      delete;
  template <typename K>
  std::pair<const_iterator, const_iterator> bounded_range(const K &lo, const K &hi, bool left_closed,
                                                            bool right_closed) const && = delete;

  /**
   * @brief Unlinks the node at @p pos, then invokes @p disposer on it,
   * matching `boost::intrusive::set::erase_and_dispose`. @p disposer is
   * any `void(T &)`-callable -- typically something that returns the
   * node's storage to whatever pool/slab it came from.
   */
  template <typename Disposer> iterator erase_and_dispose(iterator pos, Disposer disposer) & noexcept {
    RELOCO_ASSERT(pos != end(), "reloco::detail::intrusive_bst_base::erase_and_dispose: called with end() iterator");
    T &node = *pos;
    iterator next = pos;
    ++next;
    derived().unlink(node);
    disposer(node);
    return iterator(next.node(), root_);
  }
  template <typename Disposer> iterator erase_and_dispose(iterator pos, Disposer disposer) && = delete;

  /**
   * @brief Unlinks every node in `[first, last)`, invoking @p disposer on
   * each one, and returns @p last (re-anchored to the possibly-changed
   * root) -- matching `boost::intrusive::set::erase_and_dispose(first,
   * last, disposer)`. See the non-disposing `erase(first, last)`'s doc
   * comment for why re-walking one node at a time (rather than capturing
   * the whole range up front) is required here.
   */
  template <typename Disposer>
  iterator erase_and_dispose(iterator first, iterator last, Disposer disposer) & noexcept {
    while (first != last) {
      T &node = *first;
      ++first;
      derived().unlink(node);
      disposer(node);
    }
    return iterator(last.node(), root_);
  }
  template <typename Disposer> iterator erase_and_dispose(iterator first, iterator last, Disposer disposer) && =
      delete;

  /**
   * @brief Removes @p node, already known to be linked into *this* tree,
   * then invokes @p disposer on it, matching
   * `boost::intrusive::set::erase_and_dispose` (called by node reference
   * rather than iterator -- see `remove`). `RELOCO_ASSERT`s @p node is
   * currently linked.
   */
  template <typename Disposer> void remove_and_dispose(T &node, Disposer disposer) & noexcept {
    RELOCO_ASSERT(hook_of(node).is_linked(),
                  "reloco::detail::intrusive_bst_base::remove_and_dispose: node is not linked");
    derived().unlink(node);
    disposer(node);
  }
  template <typename Disposer> void remove_and_dispose(T &&node, Disposer disposer) & noexcept = delete;

  /**
   * @brief Walks to find @p key, unlinks it, then invokes @p disposer on
   * it. Fails with `error::not_found` if absent.
   */
  template <typename K, typename Disposer>
  [[nodiscard]] result<void> try_remove_and_dispose(const K &key, Disposer disposer) & noexcept {
    T *found = find_ptr(key);
    if (found == nullptr)
      return unexpected(error::not_found);
    derived().unlink(*found);
    disposer(*found);
    return {};
  }

  /**
   * @brief Unlinks every node, invoking @p disposer on each one instead of
   * leaving its storage untouched. `O(n)`, iterative (no recursion, so a
   * degenerate/huge tree cannot blow the call stack): repeatedly rotate
   * the left child up and detach the now-left-child-free root, the same
   * teardown shape `detail::tree_base`'s `bst_clear` uses -- balance is
   * irrelevant since the whole tree is being discarded. @p Derived's
   * `reset_hook` clears any balance-specific hook field (e.g. the
   * red-black hook's `red` bit) this generic teardown doesn't know about.
   */
  template <typename Disposer> void clear_and_dispose(Disposer disposer) noexcept {
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
      derived().reset_hook(*node);
      disposer(*node);
      node = next;
    }
    root_ = nullptr;
    size_ = 0;
  }

  /**
   * @brief Unlinks every node (their own storage is otherwise untouched --
   * `T` is never constructed/destroyed by this file). See
   * `clear_and_dispose`.
   */
  void clear() noexcept {
    clear_and_dispose([](T &) noexcept {});
  }

  /**
   * @brief Moves every node in `[first, last)` of @p source (a *different*
   * tree of this same concrete type) into *this*, matching
   * `std::map::merge`'s conflict handling: a source node whose key
   * already exists in *this* is left exactly where it is, still linked
   * into @p source -- so after this call @p source holds only the
   * conflicting leftovers (if any) plus whatever outside `[first, last)`.
   * `RELOCO_ASSERT`s @p source is not *this* (splicing a tree into itself
   * is nonsensical: every node would "conflict" with itself). Returns the
   * number of nodes actually moved.
   *
   * @p first/@p last must be from @p source, not *this* -- both trees
   * share the same `iterator` type (same @p Derived), so nothing stops a
   * caller from passing *this*'s own iterators by mistake; nothing here
   * can detect that misuse, so get it right.
   */
  size_type splice(Derived &source, iterator first, iterator last) & noexcept {
    RELOCO_ASSERT(&source != static_cast<Derived *>(this),
                  "reloco::detail::intrusive_bst_base::splice: source is the same tree as the splice target");
    const KeyOf keyOf{};
    size_type moved = 0;
    while (first != last) {
      T &node = *first;
      ++first;
      if (find_ptr(keyOf(node)) != nullptr)
        continue; // conflict: retain node in source, untouched
      source.derived().unlink(node);
      [[maybe_unused]] result<void> inserted = derived().try_insert(node);
      RELOCO_ASSERT(inserted.has_value(),
                    "reloco::detail::intrusive_bst_base::splice: spurious try_insert failure after a successful "
                    "find_ptr-based conflict check");
      ++moved;
    }
    return moved;
  }
  size_type splice(Derived &source, iterator first, iterator last) && = delete;

  /** @brief `splice(source, source.begin(), source.end())` -- moves every node of @p source into *this*. */
  size_type splice(Derived &source) & noexcept { return splice(source, source.begin(), source.end()); }
  size_type splice(Derived &source) && = delete;

  /**
   * @brief Moves every node in `[first, last)` of @p source into *this*,
   * same as `splice`, except a key conflict is resolved the other way:
   * the *existing* node in *this* is unlinked and passed to @p disposer,
   * then the @p source node takes its place. Every node in `[first,
   * last)` ends up moved into *this* (none are left behind in @p source),
   * unlike plain `splice`. `RELOCO_ASSERT`s @p source is not *this*.
   */
  template <typename Disposer>
  size_type splice_replace(Derived &source, iterator first, iterator last, Disposer disposer) & noexcept {
    RELOCO_ASSERT(
        &source != static_cast<Derived *>(this),
        "reloco::detail::intrusive_bst_base::splice_replace: source is the same tree as the splice target");
    const KeyOf keyOf{};
    size_type moved = 0;
    while (first != last) {
      T &node = *first;
      ++first;
      T *existing = find_ptr(keyOf(node));
      if (existing != nullptr) {
        derived().unlink(*existing);
        disposer(*existing);
      }
      source.derived().unlink(node);
      [[maybe_unused]] result<void> inserted = derived().try_insert(node);
      RELOCO_ASSERT(inserted.has_value(), "reloco::detail::intrusive_bst_base::splice_replace: spurious "
                                           "try_insert failure after evicting any conflicting node");
      ++moved;
    }
    return moved;
  }
  template <typename Disposer>
  size_type splice_replace(Derived &source, iterator first, iterator last, Disposer disposer) && = delete;

  /** @brief `splice_replace(source, source.begin(), source.end(), disposer)` -- see above. */
  template <typename Disposer> size_type splice_replace(Derived &source, Disposer disposer) & noexcept {
    return splice_replace(source, source.begin(), source.end(), std::move(disposer));
  }
  template <typename Disposer> size_type splice_replace(Derived &source, Disposer disposer) && = delete;

  /**
   * @brief Moves every non-conflicting node in `[first, last)` of
   * @p source into *this*; a conflicting source node (its key already
   * present in *this*) is instead unlinked from @p source and passed to
   * @p disposer, leaving the existing node already in *this* untouched --
   * the mirror image of `splice_replace`. Every node in `[first, last)`
   * leaves @p source either way (moved into *this*, or disposed).
   * `RELOCO_ASSERT`s @p source is not *this*.
   */
  template <typename Disposer>
  size_type splice_discard(Derived &source, iterator first, iterator last, Disposer disposer) & noexcept {
    RELOCO_ASSERT(
        &source != static_cast<Derived *>(this),
        "reloco::detail::intrusive_bst_base::splice_discard: source is the same tree as the splice target");
    const KeyOf keyOf{};
    size_type moved = 0;
    while (first != last) {
      T &node = *first;
      ++first;
      if (find_ptr(keyOf(node)) != nullptr) {
        source.derived().unlink(node);
        disposer(node);
        continue;
      }
      source.derived().unlink(node);
      [[maybe_unused]] result<void> inserted = derived().try_insert(node);
      RELOCO_ASSERT(inserted.has_value(), "reloco::detail::intrusive_bst_base::splice_discard: spurious "
                                           "try_insert failure after a successful find_ptr-based conflict check");
      ++moved;
    }
    return moved;
  }
  template <typename Disposer>
  size_type splice_discard(Derived &source, iterator first, iterator last, Disposer disposer) && = delete;

  /** @brief `splice_discard(source, source.begin(), source.end(), disposer)` -- see above. */
  template <typename Disposer> size_type splice_discard(Derived &source, Disposer disposer) & noexcept {
    return splice_discard(source, source.begin(), source.end(), std::move(disposer));
  }
  template <typename Disposer> size_type splice_discard(Derived &source, Disposer disposer) && = delete;

protected:
  constexpr intrusive_bst_base() noexcept = default;

  intrusive_bst_base(const intrusive_bst_base &) = delete;
  intrusive_bst_base &operator=(const intrusive_bst_base &) = delete;

  intrusive_bst_base(intrusive_bst_base &&other) noexcept
      : root_(std::exchange(other.root_, nullptr)), size_(std::exchange(other.size_, size_type{0})) {}

  intrusive_bst_base &operator=(intrusive_bst_base &&other) noexcept {
    if (this != &other) {
      root_ = std::exchange(other.root_, nullptr);
      size_ = std::exchange(other.size_, size_type{0});
    }
    return *this;
  }

  static auto &hook_of(T &node) noexcept { return node.*Hook; }

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

  template <typename K> [[nodiscard]] T *lower_bound_ptr(const K &key) const noexcept {
    const Compare less{};
    const KeyOf keyOf{};
    T *cur = root_;
    T *result_node = nullptr;
    while (cur != nullptr) {
      if (!less(keyOf(*cur), key)) { // keyOf(*cur) >= key
        result_node = cur;
        cur = hook_of(*cur).left;
      } else {
        cur = hook_of(*cur).right;
      }
    }
    return result_node;
  }

  template <typename K> [[nodiscard]] T *upper_bound_ptr(const K &key) const noexcept {
    const Compare less{};
    const KeyOf keyOf{};
    T *cur = root_;
    T *result_node = nullptr;
    while (cur != nullptr) {
      if (less(key, keyOf(*cur))) { // key < keyOf(*cur)
        result_node = cur;
        cur = hook_of(*cur).left;
      } else {
        cur = hook_of(*cur).right;
      }
    }
    return result_node;
  }

  T *root_ = nullptr;
  size_type size_ = 0;

private:
  [[nodiscard]] Derived &derived() noexcept { return static_cast<Derived &>(*this); }
  [[nodiscard]] const Derived &derived() const noexcept { return static_cast<const Derived &>(*this); }
};

} // namespace reloco::detail
