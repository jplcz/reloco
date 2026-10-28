// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file detail/owning_list_base.hpp
 * @brief Shared engine of the owning linked containers `tailq`, `stailq`, `slist`, `list` and `circular_list`.
 *
 * Every owning container allocates one `list_node<T, Link>` per element through an `allocator_ref` and delegates
 * all linking to the matching intrusive `c_*` container. A *policy* class selects the intrusive container, its hook
 * layout and which operations are available in O(1); see `docs/linked-containers.md` for the full picture.
 */

#include "../allocator.hpp"
#include "../construction_helpers.hpp"
#include "../default_allocator.hpp"
#include "../error.hpp"
#include "../expected.hpp"
#include "../lifetime.hpp"
#include "../rvalue_safety.hpp"
#include "assert.hpp"
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace reloco::detail {

/**
 * @brief One heap node: the intrusive hook plus uninitialized storage for the element.
 *
 * The element is constructed in `storage` by the owning container (fallibly, through `construction_helpers`), so a
 * failed construction never leaves a half-built `T` behind.
 */
template <typename T, template <typename> class Link> struct list_node {
  // Element first, so a node's address equals its element's address.
  // Raw storage on purpose: the element is constructed later through the fallible construction protocol.
  // NOLINTNEXTLINE(reloco-no-c-array)
  alignas(T) std::byte storage[sizeof(T)];
  Link<list_node> link;

  list_node() noexcept : link() {}

  [[nodiscard]] T *value_ptr() & noexcept RELOCO_LIFETIMEBOUND {
    // The storage is suitably aligned and sized for T by construction.
    // NOLINTNEXTLINE(reloco-banned-construct)
    return reinterpret_cast<T *>(storage);
  }

  [[nodiscard]] T &value() & noexcept RELOCO_LIFETIMEBOUND { return *std::launder(value_ptr()); }

  [[nodiscard]] T &value() const && = delete;
  [[nodiscard]] T *value_ptr() const && = delete;

  [[nodiscard]] const T &value() const & noexcept RELOCO_LIFETIMEBOUND {
    // NOLINTNEXTLINE(reloco-banned-construct)
    return *std::launder(reinterpret_cast<const T *>(storage));
  }
};

/** @brief Iterator over element values, wrapping an intrusive iterator that yields `list_node`s. */
template <typename T, typename Inner, bool IsConst> class RELOCO_POINTER list_iterator {
public:
  using iterator_category = typename Inner::iterator_category;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const T *, T *>;
  using reference = std::conditional_t<IsConst, const T &, T &>;

  constexpr list_iterator() noexcept = default;
  explicit constexpr list_iterator(Inner inner) noexcept : inner_(inner) {}

  template <typename OtherInner, bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
  // iterator -> const_iterator conversion, like every standard container.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr list_iterator(const list_iterator<T, OtherInner, WasConst> &other) noexcept : inner_(other.inner()) {}

  // Dereferencing `end()` is a precondition violation, exactly as for the standard containers.
  RELOCO_CHECKED [[nodiscard]] reference operator*() const noexcept { return inner_->value(); }
  RELOCO_CHECKED [[nodiscard]] pointer operator->() const noexcept { return std::addressof(inner_->value()); }

  list_iterator &operator++() & noexcept {
    ++inner_;
    return *this;
  }

  list_iterator operator++(int) noexcept {
    auto tmp = *this;
    ++inner_;
    return tmp;
  }

  list_iterator &operator--() & noexcept {
    --inner_;
    return *this;
  }

  list_iterator operator--(int) noexcept {
    auto tmp = *this;
    --inner_;
    return tmp;
  }

  [[nodiscard]] friend bool operator==(const list_iterator &lhs, const list_iterator &rhs) noexcept {
    return lhs.inner_ == rhs.inner_;
  }

  [[nodiscard]] friend bool operator!=(const list_iterator &lhs, const list_iterator &rhs) noexcept {
    return !(lhs == rhs);
  }

  [[nodiscard]] constexpr const Inner &inner() const noexcept { return inner_; }

private:
  Inner inner_{};
};

/**
 * @brief Engine shared by all owning linked containers.
 *
 * @tparam Derived The concrete container (CRTP), used as the return type of the factories and of `try_clone`.
 * @tparam T Element type.
 * @tparam Policy Selects the intrusive container and capabilities:
 *   - `link<Node>`: the hook member type;
 *   - `queue<Node>`: the intrusive container over `Node` (hook member `link`);
 *   - `make_iterator(Node &)`: builds an intrusive iterator positioned at a node;
 *   - `has_back`, `has_push_back`: O(1) access/append at the tail;
 *   - `doubly_linked`: O(1) removal and insertion before an arbitrary position.
 */
template <typename Derived, typename T, typename Policy> class RELOCO_OWNER owning_list_base {
  using node_type = list_node<T, Policy::template link>;
  using queue_type = typename Policy::template queue<node_type>;

public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T &;
  using const_reference = const T &;
  using pointer = T *;
  using const_pointer = const T *;
  using iterator = list_iterator<T, typename queue_type::iterator, false>;
  using const_iterator = list_iterator<T, typename queue_type::const_iterator, true>;

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  /** @brief Creates an empty container whose nodes are allocated from @p alloc. Does not allocate. */
  explicit owning_list_base(allocator_ref alloc = default_allocator()) noexcept : alloc_(alloc) {}

  owning_list_base(const owning_list_base &) = delete;
  owning_list_base &operator=(const owning_list_base &) = delete;

  owning_list_base(owning_list_base &&other) noexcept
      : queue_(std::move(other.queue_)), alloc_(other.alloc_), size_(std::exchange(other.size_, 0)) {}

  /** @brief Destroys the current elements, then adopts @p other's nodes together with its allocator. */
  owning_list_base &operator=(owning_list_base &&other) noexcept {
    if (this != &other) {
      clear();
      queue_ = std::move(other.queue_);
      alloc_ = other.alloc_;
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }

  ~owning_list_base() { clear(); }

  // ---- factories (fallible-construction protocol) ----

  [[nodiscard]] static result<Derived> try_allocate(allocator_ref alloc) noexcept { return Derived(alloc); }

  [[nodiscard]] static result<Derived> try_create() noexcept { return Derived(default_allocator()); }

  /** @brief Deep copy using @p alloc for the new nodes. Fails if allocating or cloning an element fails. */
  [[nodiscard]] result<Derived> try_clone(allocator_ref alloc) const noexcept {
    Derived out(alloc);
    node_type *last = nullptr;
    for (const T &value : *this) {
      auto node = out.alloc_node();
      if (!node) {
        return unexpected(node.error());
      }
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
      auto cloned = construction_helpers::try_clone_at(alloc, (*node)->value_ptr(), value);
      RELOCO_END_UNSAFE_BUFFER_USAGE
      if (!cloned) {
        out.free_node(*node);
        return unexpected(cloned.error());
      }
      out.link_after(last, **node);
      last = *node;
    }
    return std::move(out);
  }

  [[nodiscard]] result<Derived> try_clone() const noexcept { return try_clone(alloc_); }

  // ---- capacity ----

  [[nodiscard]] size_type size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] allocator_ref get_allocator() const & noexcept RELOCO_LIFETIMEBOUND { return alloc_; }
  [[nodiscard]] allocator_ref get_allocator() const && = delete;

  // ---- checked tier ----

  [[nodiscard]] reference front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "reloco list container is empty");
    return queue_.front()->value();
  }
  [[nodiscard]] const_reference front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "reloco list container is empty");
    return queue_.front()->value();
  }

  template <typename P = Policy, std::enable_if_t<P::has_back, int> = 0>
  [[nodiscard]] reference back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "reloco list container is empty");
    return queue_.back()->value();
  }
  template <typename P = Policy, std::enable_if_t<P::has_back, int> = 0>
  [[nodiscard]] const_reference back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "reloco list container is empty");
    return queue_.back()->value();
  }

  // ---- fallible tier ----

  [[nodiscard]] result<std::reference_wrapper<T>> try_front() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty()) {
      return unexpected(error::container_empty);
    }
    return std::ref(queue_.front()->value());
  }
  [[nodiscard]] result<std::reference_wrapper<const T>> try_front() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty()) {
      return unexpected(error::container_empty);
    }
    return std::cref(queue_.front()->value());
  }

  template <typename P = Policy, std::enable_if_t<P::has_back, int> = 0>
  [[nodiscard]] result<std::reference_wrapper<T>> try_back() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty()) {
      return unexpected(error::container_empty);
    }
    return std::ref(queue_.back()->value());
  }
  template <typename P = Policy, std::enable_if_t<P::has_back, int> = 0>
  [[nodiscard]] result<std::reference_wrapper<const T>> try_back() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty()) {
      return unexpected(error::container_empty);
    }
    return std::cref(queue_.back()->value());
  }

  // ---- explicitly-unsafe tier ----

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE reference unsafe_front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "reloco list container is empty");
    return queue_.front()->value();
  }
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const_reference unsafe_front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "reloco list container is empty");
    return queue_.front()->value();
  }
  auto unsafe_front() const && = delete;

  template <typename P = Policy, std::enable_if_t<P::has_back, int> = 0>
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE reference unsafe_back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "reloco list container is empty");
    return queue_.back()->value();
  }
  template <typename P = Policy, std::enable_if_t<P::has_back, int> = 0>
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const_reference unsafe_back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "reloco list container is empty");
    return queue_.back()->value();
  }
  auto unsafe_back() const && = delete;

  // ---- iteration ----

  [[nodiscard]] iterator begin() & noexcept RELOCO_LIFETIMEBOUND { return iterator(queue_.begin()); }
  [[nodiscard]] iterator end() & noexcept RELOCO_LIFETIMEBOUND { return iterator(queue_.end()); }
  [[nodiscard]] const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND { return const_iterator(queue_.begin()); }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return const_iterator(queue_.end()); }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return begin(); }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return end(); }

  // ---- insertion ----

  template <typename... Args>
  [[nodiscard]] result<std::reference_wrapper<T>> try_emplace_front(Args &&...args) & noexcept RELOCO_LIFETIMEBOUND {
    auto node = make_node(std::forward<Args>(args)...);
    if (!node) {
      return unexpected(node.error());
    }
    queue_.push_front(**node);
    ++size_;
    return std::ref((*node)->value());
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_push_front(T value) & noexcept RELOCO_LIFETIMEBOUND {
    return try_emplace_front(std::move(value));
  }

  template <typename... Args, typename P = Policy, std::enable_if_t<P::has_push_back, int> = 0>
  [[nodiscard]] result<std::reference_wrapper<T>> try_emplace_back(Args &&...args) & noexcept RELOCO_LIFETIMEBOUND {
    auto node = make_node(std::forward<Args>(args)...);
    if (!node) {
      return unexpected(node.error());
    }
    queue_.push_back(**node);
    ++size_;
    return std::ref((*node)->value());
  }

  template <typename P = Policy, std::enable_if_t<P::has_push_back, int> = 0>
  [[nodiscard]] result<std::reference_wrapper<T>> try_push_back(T value) & noexcept RELOCO_LIFETIMEBOUND {
    return try_emplace_back(std::move(value));
  }

  /** @brief Inserts a new element right after @p pos. Fails with `invalid_argument` if @p pos is `end()`. */
  template <typename... Args>
  [[nodiscard]] result<std::reference_wrapper<T>> try_emplace_after(const_iterator pos,
                                                                    Args &&...args) & noexcept RELOCO_LIFETIMEBOUND {
    if (pos == cend()) {
      return unexpected(error::invalid_argument);
    }
    auto node = make_node(std::forward<Args>(args)...);
    if (!node) {
      return unexpected(node.error());
    }
    queue_.insert_after(mutable_node(pos), **node);
    ++size_;
    return std::ref((*node)->value());
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_insert_after(const_iterator pos,
                                                                   T value) & noexcept RELOCO_LIFETIMEBOUND {
    return try_emplace_after(pos, std::move(value));
  }

  /**
   * @brief Inserts a new element right before @p pos (before `end()` means append, when the container has a tail;
   * otherwise `end()` is rejected with `invalid_argument`).
   */
  template <typename... Args, typename P = Policy, std::enable_if_t<P::doubly_linked, int> = 0>
  [[nodiscard]] result<std::reference_wrapper<T>> try_emplace(const_iterator pos,
                                                              Args &&...args) & noexcept RELOCO_LIFETIMEBOUND {
    if (pos == cend()) {
      if constexpr (Policy::has_push_back) {
        return try_emplace_back(std::forward<Args>(args)...);
      } else {
        return unexpected(error::invalid_argument);
      }
    } else {
      auto node = make_node(std::forward<Args>(args)...);
      if (!node) {
        return unexpected(node.error());
      }
      queue_.insert_before(mutable_node(pos), **node);
      ++size_;
      return std::ref((*node)->value());
    }
  }

  template <typename P = Policy, std::enable_if_t<P::doubly_linked, int> = 0>
  [[nodiscard]] result<std::reference_wrapper<T>> try_insert(const_iterator pos,
                                                             T value) & noexcept RELOCO_LIFETIMEBOUND {
    return try_emplace(pos, std::move(value));
  }

  // ---- removal ----

  /** @brief Removes and returns the first element. Asserts that the container is not empty. */
  [[nodiscard]] T pop_front() & noexcept {
    RELOCO_ASSERT(!empty(), "reloco list container is empty");
    return take(queue_.pop_front());
  }

  [[nodiscard]] result<T> try_pop_front() & noexcept {
    if (empty()) {
      return unexpected(error::container_empty);
    }
    return take(queue_.pop_front());
  }

  /** @brief O(1) removal of the last element; only on containers with both a tail and O(1) unlinking. */
  template <typename P = Policy, std::enable_if_t<P::has_back && P::doubly_linked, int> = 0>
  [[nodiscard]] T pop_back() & noexcept {
    RELOCO_ASSERT(!empty(), "reloco list container is empty");
    return take_back();
  }

  template <typename P = Policy, std::enable_if_t<P::has_back && P::doubly_linked, int> = 0>
  [[nodiscard]] result<T> try_pop_back() & noexcept {
    if (empty()) {
      return unexpected(error::container_empty);
    }
    return take_back();
  }

  /** @brief O(1) removal of the element at @p pos; returns the iterator following it. */
  template <typename P = Policy, std::enable_if_t<P::doubly_linked, int> = 0>
  iterator erase(const_iterator pos) & noexcept {
    RELOCO_ASSERT(pos != cend(), "reloco list container: erase of end()");
    node_type &node = mutable_node(pos);
    const_iterator next = pos;
    ++next;
    queue_.remove(node);
    destroy_node(&node);
    --size_;
    return make_mutable(next);
  }

  /**
   * @brief O(1) removal of the element following @p pos (singly linked containers). Fails with `invalid_argument`
   * if @p pos is `end()` and `out_of_bounds` if nothing follows @p pos. Returns the iterator following the removed
   * element.
   */
  template <typename P = Policy, std::enable_if_t<!P::doubly_linked, int> = 0>
  [[nodiscard]] result<iterator> try_erase_after(const_iterator pos) & noexcept {
    if (pos == cend()) {
      return unexpected(error::invalid_argument);
    }
    node_type *removed = queue_.remove_after(mutable_node(pos));
    if (removed == nullptr) {
      return unexpected(error::out_of_bounds);
    }
    destroy_node(removed);
    --size_;
    const_iterator next = pos;
    ++next;
    return make_mutable(next);
  }

  template <typename P = Policy, std::enable_if_t<!P::doubly_linked, int> = 0>
  iterator erase_after(const_iterator pos) & noexcept {
    auto result = try_erase_after(pos);
    RELOCO_ASSERT(result.has_value(), "reloco list container: erase_after needs a valid position with a successor");
    return *result;
  }

  /** @brief Destroys every element and releases all nodes. */
  void clear() & noexcept {
    while (node_type *node = queue_.pop_front()) {
      destroy_node(node);
    }
    size_ = 0;
  }

  // ---- relinking (no allocation, no element moves) ----

  /**
   * @brief Moves all nodes of @p other to the back of this container, leaving @p other empty. Both containers must
   * use the same allocator (`invalid_argument` otherwise), because the nodes are later freed by this container; an
   * empty destination is exempt and adopts the allocator of @p other.
   * O(size of @p other), plus O(size) on containers without a tail.
   */
  [[nodiscard]] result<void> try_append(Derived &&other) & noexcept {
    if (static_cast<const owning_list_base *>(&other) == this) {
      return unexpected(error::invalid_argument);
    }
    if (alloc_ != other.alloc_ && !empty()) {
      return unexpected(error::invalid_argument);
    }
    adopt_allocator(other);
    node_type *last = tail_node();
    while (node_type *node = other.queue_.pop_front()) {
      link_after(last, *node);
      last = node;
    }
    other.size_ = 0; // link_after already counted every moved node in size_
    return {};
  }

  /** @brief Moves all nodes of @p other to the front of this container, leaving @p other empty. O(n). */
  [[nodiscard]] result<void> try_prepend(Derived &other) & noexcept {
    if (auto ok = check_splice(other); !ok) {
      return ok;
    }
    adopt_allocator(other);
    queue_type merged;
    node_type *last = nullptr;
    auto drain = [&merged, &last](queue_type &src) {
      while (node_type *node = src.pop_front()) {
        if (last == nullptr) {
          merged.push_front(*node);
        } else {
          merged.insert_after(*last, *node);
        }
        last = node;
      }
    };
    drain(other.queue_);
    drain(queue_);
    queue_ = std::move(merged);
    size_ += std::exchange(other.size_, 0);
    return {};
  }

  // ---- splicing (doubly linked containers) ----
  //
  // All splice operations relink existing nodes: no allocation, no element moves, and moved elements keep their
  // addresses. Both containers must use the same allocator (unless this one is empty, then it adopts the source's) and
  // must be different objects, otherwise `invalid_argument` is returned and nothing is modified. Iterator arguments
  // must belong to @p other.

  /** @brief Moves every node of @p other before @p pos. `end()` is rejected on containers without a tail. */
  template <typename P = Policy, std::enable_if_t<P::doubly_linked, int> = 0>
  [[nodiscard]] result<void> try_splice(const_iterator pos, Derived &other) & noexcept {
    if (auto ok = check_splice_before(pos, other); !ok) {
      return ok;
    }
    adopt_allocator(other);
    while (node_type *node = other.queue_.pop_front()) {
      --other.size_;
      place_before(pos, *node);
    }
    return {};
  }

  /** @brief Moves the single element at @p it (in @p other) before @p pos. */
  template <typename P = Policy, std::enable_if_t<P::doubly_linked, int> = 0>
  [[nodiscard]] result<void> try_splice(const_iterator pos, Derived &other, const_iterator it) & noexcept {
    if (auto ok = check_splice_before(pos, other); !ok) {
      return ok;
    }
    if (it == other.cend()) {
      return unexpected(error::invalid_argument);
    }
    adopt_allocator(other);
    move_before(pos, other, other.mutable_node(it));
    return {};
  }

  /** @brief Moves the elements of the range `[first, last)` of @p other before @p pos. */
  template <typename P = Policy, std::enable_if_t<P::doubly_linked, int> = 0>
  [[nodiscard]] result<void> try_splice(const_iterator pos, Derived &other, const_iterator first,
                                        const_iterator last) & noexcept {
    if (auto ok = check_splice_before(pos, other); !ok) {
      return ok;
    }
    adopt_allocator(other);
    while (first != last) {
      node_type &node = other.mutable_node(first);
      ++first;
      move_before(pos, other, node);
    }
    return {};
  }

  // ---- splicing (singly linked containers) ----

  /** @brief Moves every node of @p other right after @p pos (which must not be `end()`). */
  template <typename P = Policy, std::enable_if_t<!P::doubly_linked, int> = 0>
  [[nodiscard]] result<void> try_splice_after(const_iterator pos, Derived &other) & noexcept {
    if (auto ok = check_splice_after(pos, other); !ok) {
      return ok;
    }
    adopt_allocator(other);
    node_type *cursor = &mutable_node(pos);
    while (node_type *node = other.queue_.pop_front()) {
      --other.size_;
      queue_.insert_after(*cursor, *node);
      ++size_;
      cursor = node;
    }
    return {};
  }

  /** @brief Moves the element following @p prev (in @p other) right after @p pos. `out_of_bounds` if none. */
  template <typename P = Policy, std::enable_if_t<!P::doubly_linked, int> = 0>
  [[nodiscard]] result<void> try_splice_after(const_iterator pos, Derived &other, const_iterator prev) & noexcept {
    if (auto ok = check_splice_after(pos, other); !ok) {
      return ok;
    }
    if (prev == other.cend()) {
      return unexpected(error::invalid_argument);
    }
    node_type *node = other.queue_.remove_after(other.mutable_node(prev));
    if (node == nullptr) {
      return unexpected(error::out_of_bounds);
    }
    adopt_allocator(other);
    --other.size_;
    queue_.insert_after(mutable_node(pos), *node);
    ++size_;
    return {};
  }

  /** @brief Moves the open range `(before_first, last)` of @p other right after @p pos. */
  template <typename P = Policy, std::enable_if_t<!P::doubly_linked, int> = 0>
  [[nodiscard]] result<void> try_splice_after(const_iterator pos, Derived &other, const_iterator before_first,
                                              const_iterator last) & noexcept {
    if (auto ok = check_splice_after(pos, other); !ok) {
      return ok;
    }
    if (before_first == other.cend()) {
      return unexpected(error::invalid_argument);
    }
    adopt_allocator(other);
    node_type &anchor = other.mutable_node(before_first);
    node_type *cursor = &mutable_node(pos);
    const_iterator it = before_first;
    ++it;
    while (it != last) {
      ++it; // step past the node before it is unlinked
      node_type *node = other.queue_.remove_after(anchor);
      --other.size_;
      queue_.insert_after(*cursor, *node);
      ++size_;
      cursor = node;
    }
    return {};
  }

  /** @brief Reverses the order of the elements by relinking nodes. O(n), no allocation. */
  void reverse() & noexcept {
    queue_type reversed;
    while (node_type *node = queue_.pop_front()) {
      reversed.push_front(*node);
    }
    queue_ = std::move(reversed);
  }

  /** @brief Keeps only the elements for which `pred(const T &)` is true, preserving order. O(n). */
  template <typename Pred> void retain(Pred pred) & noexcept {
    queue_type kept;
    while (node_type *node = queue_.pop_front()) {
      if (pred(std::as_const(node->value()))) {
        kept.push_front(*node);
      } else {
        destroy_node(node);
        --size_;
      }
    }
    while (node_type *node = kept.pop_front()) {
      queue_.push_front(*node);
    }
  }

private:
  [[nodiscard]] result<void> check_splice(const Derived &other) const noexcept {
    if (static_cast<const owning_list_base *>(&other) == this || (alloc_ != other.alloc_ && !empty())) {
      return unexpected(error::invalid_argument);
    }
    return {};
  }

  [[nodiscard]] result<void> check_splice_before(const_iterator pos, const Derived &other) const noexcept {
    if (!Policy::has_push_back && pos == cend()) {
      return unexpected(error::invalid_argument);
    }
    return check_splice(other);
  }

  [[nodiscard]] result<void> check_splice_after(const_iterator pos, const Derived &other) const noexcept {
    if (pos == cend()) {
      return unexpected(error::invalid_argument);
    }
    return check_splice(other);
  }

  // An empty container owns no nodes, so it may switch to the allocator that made the nodes it is about to receive.
  void adopt_allocator(const owning_list_base &other) noexcept {
    if (alloc_ != other.alloc_) {
      alloc_ = other.alloc_;
    }
  }

  // Links an already detached node before `pos` (append when `pos` is `end()`).
  void place_before(const_iterator pos, node_type &node) noexcept {
    if (pos == cend()) {
      if constexpr (Policy::has_push_back) {
        queue_.push_back(node);
      }
    } else {
      queue_.insert_before(mutable_node(pos), node);
    }
    ++size_;
  }

  void move_before(const_iterator pos, owning_list_base &src, node_type &node) noexcept {
    src.queue_.remove(node);
    --src.size_;
    place_before(pos, node);
  }

  [[nodiscard]] result<node_type *> alloc_node() noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    auto block = alloc_.allocate(sizeof(node_type), alignof(node_type));
    RELOCO_END_UNSAFE_BUFFER_USAGE
    if (!block) {
      return unexpected(block.error());
    }
    return ::new (block->ptr) node_type();
  }

  void free_node(node_type *node) noexcept {
    std::destroy_at(node);
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    alloc_.deallocate(node, sizeof(node_type));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  void destroy_node(node_type *node) noexcept {
    std::destroy_at(std::addressof(node->value()));
    free_node(node);
  }

  template <typename... Args> [[nodiscard]] result<node_type *> make_node(Args &&...args) noexcept {
    auto node = alloc_node();
    if (!node) {
      return node;
    }
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    auto constructed =
        construction_helpers::try_construct<T>(alloc_, (*node)->value_ptr(), std::forward<Args>(args)...);
    RELOCO_END_UNSAFE_BUFFER_USAGE
    if (!constructed) {
      free_node(*node);
      return unexpected(constructed.error());
    }
    return node;
  }

  [[nodiscard]] T take(node_type *node) noexcept {
    static_assert(std::is_nothrow_move_constructible_v<T>, "reloco list containers require noexcept move-construction");
    T value(std::move(node->value()));
    destroy_node(node);
    --size_;
    return value;
  }

  [[nodiscard]] T take_back() noexcept {
    node_type *node = queue_.back();
    queue_.remove(*node);
    return take(node);
  }

  // Links `node` right after `last`, or at the front when `last` is null; used for in-order appends.
  void link_after(node_type *last, node_type &node) noexcept {
    if (last == nullptr) {
      queue_.push_front(node);
    } else {
      queue_.insert_after(*last, node);
    }
    ++size_;
  }

  [[nodiscard]] node_type *tail_node() & noexcept RELOCO_LIFETIMEBOUND {
    if constexpr (Policy::has_back) {
      return queue_.back();
    } else {
      node_type *last = nullptr;
      for (node_type &node : queue_) {
        last = &node;
      }
      return last;
    }
  }

  // The container is non-const here, so mutating its own nodes through a const_iterator position is sound.
  [[nodiscard]] node_type &mutable_node(const_iterator pos) & noexcept RELOCO_LIFETIMEBOUND {
    // NOLINTNEXTLINE(reloco-banned-construct)
    return const_cast<node_type &>(*pos.inner());
  }

  [[nodiscard]] iterator make_mutable(const_iterator pos) noexcept {
    if (pos == cend()) {
      return end();
    }
    return iterator(Policy::make_iterator(mutable_node(pos)));
  }

  queue_type queue_;
  allocator_ref alloc_;
  size_type size_ = 0;
};

} // namespace reloco::detail
