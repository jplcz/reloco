// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "container_of.hpp"
#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace reloco {

namespace detail {

// Defines the exact memory layout of FreeBSD's TAILQ_ENTRY(type).
/** @brief Describes the link layout used by a BSD-style tail queue. */
// NOLINTNEXTLINE(reloco-pointer-annotation): intrusive containers link caller-owned nodes.
template <typename T> struct c_tailq_hook_layout {
  T *next;
  T **prev; // address of previous next element
};

// Extracts the `next` and `prev` pointers via reinterpret_cast from ANY struct field.
/** @brief Accesses an intrusive BSD-style tail-queue hook in an object. */
template <typename T, auto Hook> struct c_tailq_hook_access {
  [[nodiscard]] static T *&next(T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    // C list/queue hook layout is ABI-compatible with the sys/queue.h macros by design.
    // NOLINTNEXTLINE(reloco-banned-construct)
    return reinterpret_cast<c_tailq_hook_layout<T> *>(&(node->*Hook))->next;
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  [[nodiscard]] static T *const &next(const T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    // C list/queue hook layout is ABI-compatible with the sys/queue.h macros by design.
    // NOLINTNEXTLINE(reloco-banned-construct)
    return reinterpret_cast<const c_tailq_hook_layout<T> *>(&(node->*Hook))->next;
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  [[nodiscard]] static T **&prev(T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    // C list/queue hook layout is ABI-compatible with the sys/queue.h macros by design.
    // NOLINTNEXTLINE(reloco-banned-construct)
    return reinterpret_cast<c_tailq_hook_layout<T> *>(&(node->*Hook))->prev;
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }
};

} // namespace detail

/** @brief Forward iterator over an intrusive doubly-linked tail queue. */
template <typename T, auto Hook, bool IsConst> class RELOCO_POINTER c_tailq_iterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const T *, T *>;
  using reference = std::conditional_t<IsConst, const T &, T &>;

  constexpr c_tailq_iterator() noexcept = default;
  explicit constexpr c_tailq_iterator(T *node) noexcept : current_(node) {}

  template <bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
  // iterator/handle constructed from its position by the owning container or macro.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr c_tailq_iterator(const c_tailq_iterator<T, Hook, WasConst> &other) noexcept : current_(other.node()) {}

  RELOCO_CHECKED [[nodiscard]] reference operator*() const noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_tailq_iterator: Dereference on null element");
    return *current_;
  }

  RELOCO_CHECKED [[nodiscard]] pointer operator->() const noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_tailq_iterator: Dereference on null element");
    return current_;
  }

  c_tailq_iterator &operator++() & noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_tailq_iterator: incrementing an end() iterator");
    current_ = detail::c_tailq_hook_access<T, Hook>::next(current_);
    return *this;
  }

  c_tailq_iterator operator++(int) noexcept {
    auto tmp = *this;
    ++(*this);
    return tmp;
  }

  [[nodiscard]] friend bool operator==(const c_tailq_iterator &lhs, const c_tailq_iterator &rhs) noexcept {
    return lhs.current_ == rhs.current_;
  }

  [[nodiscard]] friend bool operator!=(const c_tailq_iterator &lhs, const c_tailq_iterator &rhs) noexcept {
    return !(lhs == rhs);
  }

  [[nodiscard]] constexpr T *node() const & noexcept { return current_; }

  [[nodiscard]] constexpr T *node() const && = delete;

private:
  T *current_ = nullptr;
};

/**
 * @brief Intrusive Doubly-Linked Tail Queue. Layout-compatible with FreeBSD's `TAILQ_HEAD`.
 *
 * Provides O(1) push_front, push_back, and O(1) arbitrary removal.
 */
// NOLINTNEXTLINE(reloco-pointer-annotation): intrusive containers link caller-owned nodes.
template <typename T, auto Hook> class c_tailq {
public:
  using value_type = T;
  using iterator = c_tailq_iterator<T, Hook, false>;
  using const_iterator = c_tailq_iterator<T, Hook, true>;

  constexpr c_tailq() noexcept = default;

  c_tailq(const c_tailq &) = delete;
  c_tailq &operator=(const c_tailq &) = delete;

  c_tailq(c_tailq &&other) noexcept : first_(other.first_) {
    if (first_ != nullptr) {
      detail::c_tailq_hook_access<T, Hook>::prev(first_) = &first_;
      last_ptr_ = other.last_ptr_;
    } else {
      last_ptr_ = &first_;
    }
    other.first_ = nullptr;
    other.last_ptr_ = &other.first_;
  }

  c_tailq &operator=(c_tailq &&other) noexcept {
    if (this != &other) {
      first_ = other.first_;
      if (first_ != nullptr) {
        detail::c_tailq_hook_access<T, Hook>::prev(first_) = &first_;
        last_ptr_ = other.last_ptr_;
      } else {
        last_ptr_ = &first_;
      }
      other.first_ = nullptr;
      other.last_ptr_ = &other.first_;
    }
    return *this;
  }

  [[nodiscard]] bool empty() const & noexcept { return first_ == nullptr; }

  [[nodiscard]] iterator begin() & noexcept { return iterator(first_); }
  [[nodiscard]] iterator end() & noexcept { return iterator(nullptr); }
  [[nodiscard]] const_iterator begin() const & noexcept { return const_iterator(first_); }
  [[nodiscard]] const_iterator end() const & noexcept { return const_iterator(nullptr); }
  [[nodiscard]] const_iterator cbegin() const & noexcept { return begin(); }
  [[nodiscard]] const_iterator cend() const & noexcept { return end(); }

  // NOLINTNEXTLINE(reloco-lifetime-annotations): returns a caller-owned node, not storage of the list itself.
  [[nodiscard]] T *front() const & noexcept { return first_; }

  // NOLINTNEXTLINE(reloco-lifetime-annotations): returns a caller-owned node, not storage of the list itself.
  [[nodiscard]] T *front() const && = delete;

  /** @brief O(1) access to the last node, or `nullptr` when empty. */
  // NOLINTNEXTLINE(reloco-lifetime-annotations): returns a caller-owned node, not storage of the list itself.
  [[nodiscard]] T *back() const & noexcept {
    if (first_ == nullptr) {
      return nullptr;
    }
    using hook_type = container_of_member_t<Hook>;
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    // `last_ptr_` addresses the last node's `next` slot, which is the first member of its hook.
    // NOLINTNEXTLINE(reloco-banned-construct)
    return container_of<Hook>(reinterpret_cast<hook_type *>(last_ptr_));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  // NOLINTNEXTLINE(reloco-lifetime-annotations): returns a caller-owned node, not storage of the list itself.
  [[nodiscard]] T *back() const && = delete;

  /** @brief TAILQ_INSERT_HEAD: O(1) insertion at the front. */
  void push_front(T &node) & noexcept {
    T *n = first_;
    detail::c_tailq_hook_access<T, Hook>::next(&node) = n;
    if (n != nullptr) {
      detail::c_tailq_hook_access<T, Hook>::prev(n) = &detail::c_tailq_hook_access<T, Hook>::next(&node);
    } else {
      last_ptr_ = &detail::c_tailq_hook_access<T, Hook>::next(&node);
    }
    first_ = &node;
    detail::c_tailq_hook_access<T, Hook>::prev(&node) = &first_;
  }

  /** @brief TAILQ_INSERT_TAIL: O(1) insertion at the back. */
  void push_back(T &node) & noexcept {
    detail::c_tailq_hook_access<T, Hook>::next(&node) = nullptr;
    detail::c_tailq_hook_access<T, Hook>::prev(&node) = last_ptr_;
    *last_ptr_ = &node;
    last_ptr_ = &detail::c_tailq_hook_access<T, Hook>::next(&node);
  }

  /** @brief O(1) removal from the front. Returns the removed node. */
  // NOLINTNEXTLINE(reloco-lifetime-annotations): returns a caller-owned node, not storage of the list itself.
  T *pop_front() & noexcept {
    T *node = first_;
    if (node != nullptr) {
      remove(*node);
    }
    return node;
  }

  /** @brief TAILQ_INSERT_AFTER: O(1) insertion after a known node. */
  void insert_after(T &pos, T &node) & noexcept {
    T *next_node = detail::c_tailq_hook_access<T, Hook>::next(&pos);
    detail::c_tailq_hook_access<T, Hook>::next(&node) = next_node;
    if (next_node != nullptr) {
      detail::c_tailq_hook_access<T, Hook>::prev(next_node) = &detail::c_tailq_hook_access<T, Hook>::next(&node);
    } else {
      last_ptr_ = &detail::c_tailq_hook_access<T, Hook>::next(&node);
    }
    detail::c_tailq_hook_access<T, Hook>::next(&pos) = &node;
    detail::c_tailq_hook_access<T, Hook>::prev(&node) = &detail::c_tailq_hook_access<T, Hook>::next(&pos);
  }

  /** @brief TAILQ_INSERT_BEFORE: O(1) insertion before a known node. */
  void insert_before(T &pos, T &node) & noexcept {
    detail::c_tailq_hook_access<T, Hook>::prev(&node) = detail::c_tailq_hook_access<T, Hook>::prev(&pos);
    detail::c_tailq_hook_access<T, Hook>::next(&node) = &pos;
    *detail::c_tailq_hook_access<T, Hook>::prev(&pos) = &node;
    detail::c_tailq_hook_access<T, Hook>::prev(&pos) = &detail::c_tailq_hook_access<T, Hook>::next(&node);
  }

  /** @brief TAILQ_REMOVE: O(1) removal of an arbitrary node. */
  void remove(T &node) & noexcept {
    T *next_node = detail::c_tailq_hook_access<T, Hook>::next(&node);
    T **prev_ptr = detail::c_tailq_hook_access<T, Hook>::prev(&node);

    *prev_ptr = next_node;
    if (next_node != nullptr) {
      detail::c_tailq_hook_access<T, Hook>::prev(next_node) = prev_ptr;
    } else {
      last_ptr_ = prev_ptr;
    }

    detail::c_tailq_hook_access<T, Hook>::next(&node) = nullptr;
    detail::c_tailq_hook_access<T, Hook>::prev(&node) = nullptr;
  }

  /**
   * @brief O(1) move of the inclusive node range [@p first, @p last] of @p other to just after @p pos.
   *
   * Walking `next` from @p first must reach @p last inside @p other, and @p pos must be linked in this list outside
   * the range. Tail pointers of both lists are fixed up.
   */
  void splice_after(T &pos, c_tailq &other, T &first, T &last) & noexcept {
    other.detach_range(first, last);
    attach_after(pos, first, last);
  }

  /** @brief O(1) move of the inclusive node range [@p first, @p last] of @p other to just before @p pos. */
  void splice_before(T &pos, c_tailq &other, T &first, T &last) & noexcept {
    other.detach_range(first, last);
    attach_before(pos, first, last);
  }

  /** @brief O(1) move of the inclusive node range [@p first, @p last] of @p other to the front of this list. */
  void splice_front(c_tailq &other, T &first, T &last) & noexcept {
    other.detach_range(first, last);
    attach_front(first, last);
  }

  /** @brief O(1) move of the inclusive node range [@p first, @p last] of @p other to the back of this list. */
  void splice_back(c_tailq &other, T &first, T &last) & noexcept {
    other.detach_range(first, last);
    attach_back(first, last);
  }

  /** @brief O(1) move of all nodes of @p other (which must be a different list) to just after @p pos. */
  void splice_after(T &pos, c_tailq &other) & noexcept {
    RELOCO_ASSERT(&other != this, "reloco::c_tailq: splicing a list into itself");
    if (!other.empty()) {
      splice_after(pos, other, *other.first_, *other.back());
    }
  }

  /** @brief O(1) move of all nodes of @p other (which must be a different list) to just before @p pos. */
  void splice_before(T &pos, c_tailq &other) & noexcept {
    RELOCO_ASSERT(&other != this, "reloco::c_tailq: splicing a list into itself");
    if (!other.empty()) {
      splice_before(pos, other, *other.first_, *other.back());
    }
  }

  /** @brief O(1) move of all nodes of @p other (which must be a different list) to the front of this list. */
  void splice_front(c_tailq &other) & noexcept {
    RELOCO_ASSERT(&other != this, "reloco::c_tailq: splicing a list into itself");
    if (!other.empty()) {
      splice_front(other, *other.first_, *other.back());
    }
  }

  /** @brief O(1) move of all nodes of @p other (which must be a different list) to the back of this list. */
  void splice_back(c_tailq &other) & noexcept {
    RELOCO_ASSERT(&other != this, "reloco::c_tailq: splicing a list into itself");
    if (!other.empty()) {
      splice_back(other, *other.first_, *other.back());
    }
  }

  /** @brief Unlinks all nodes. */
  void clear() & noexcept {
    T *cur = first_;
    while (cur != nullptr) {
      T *next = detail::c_tailq_hook_access<T, Hook>::next(cur);
      detail::c_tailq_hook_access<T, Hook>::next(cur) = nullptr;
      detail::c_tailq_hook_access<T, Hook>::prev(cur) = nullptr;
      cur = next;
    }
    first_ = nullptr;
    last_ptr_ = &first_;
  }

private:
  // Unlinks [first, last] from this list, fixing the tail, and leaves the range's own hooks dangling.
  void detach_range(T &first, T &last) noexcept {
    T **prev_slot = detail::c_tailq_hook_access<T, Hook>::prev(&first);
    T *after = detail::c_tailq_hook_access<T, Hook>::next(&last);
    *prev_slot = after;
    if (after != nullptr) {
      detail::c_tailq_hook_access<T, Hook>::prev(after) = prev_slot;
    } else {
      last_ptr_ = prev_slot;
    }
  }

  void attach_after(T &pos, T &first, T &last) noexcept {
    T *after = detail::c_tailq_hook_access<T, Hook>::next(&pos);
    detail::c_tailq_hook_access<T, Hook>::next(&last) = after;
    if (after != nullptr) {
      detail::c_tailq_hook_access<T, Hook>::prev(after) = &detail::c_tailq_hook_access<T, Hook>::next(&last);
    } else {
      last_ptr_ = &detail::c_tailq_hook_access<T, Hook>::next(&last);
    }
    detail::c_tailq_hook_access<T, Hook>::next(&pos) = &first;
    detail::c_tailq_hook_access<T, Hook>::prev(&first) = &detail::c_tailq_hook_access<T, Hook>::next(&pos);
  }

  void attach_before(T &pos, T &first, T &last) noexcept {
    T **prev_slot = detail::c_tailq_hook_access<T, Hook>::prev(&pos);
    *prev_slot = &first;
    detail::c_tailq_hook_access<T, Hook>::prev(&first) = prev_slot;
    detail::c_tailq_hook_access<T, Hook>::next(&last) = &pos;
    detail::c_tailq_hook_access<T, Hook>::prev(&pos) = &detail::c_tailq_hook_access<T, Hook>::next(&last);
  }

  void attach_front(T &first, T &last) noexcept {
    T *after = first_;
    detail::c_tailq_hook_access<T, Hook>::next(&last) = after;
    if (after != nullptr) {
      detail::c_tailq_hook_access<T, Hook>::prev(after) = &detail::c_tailq_hook_access<T, Hook>::next(&last);
    } else {
      last_ptr_ = &detail::c_tailq_hook_access<T, Hook>::next(&last);
    }
    first_ = &first;
    detail::c_tailq_hook_access<T, Hook>::prev(&first) = &first_;
  }

  void attach_back(T &first, T &last) noexcept {
    detail::c_tailq_hook_access<T, Hook>::next(&last) = nullptr;
    detail::c_tailq_hook_access<T, Hook>::prev(&first) = last_ptr_;
    *last_ptr_ = &first;
    last_ptr_ = &detail::c_tailq_hook_access<T, Hook>::next(&last);
  }

  T *first_ = nullptr;
  T **last_ptr_ = &first_;
};

} // namespace reloco