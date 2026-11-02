// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace reloco {

namespace detail {

// Defines the exact memory layout of FreeBSD's LIST_ENTRY(type).
/** @brief Describes the link layout used by a BSD-style C list. */
// NOLINTNEXTLINE(reloco-pointer-annotation): intrusive containers link caller-owned nodes.
template <typename T> struct c_list_hook_layout {
  T *next;
  T **prev; // address of previous next element
};

// Extracts the `next` and `prev` pointers via reinterpret_cast from ANY struct field.
/** @brief Accesses an intrusive BSD-style C-list hook in an object. */
template <typename T, auto Hook> struct c_list_hook_access {
  [[nodiscard]] static T *&next(T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    // C list/queue hook layout is ABI-compatible with the sys/queue.h macros by design.
    // NOLINTNEXTLINE(reloco-banned-construct)
    return reinterpret_cast<c_list_hook_layout<T> *>(&(node->*Hook))->next;
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  [[nodiscard]] static T *const &next(const T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    // C list/queue hook layout is ABI-compatible with the sys/queue.h macros by design.
    // NOLINTNEXTLINE(reloco-banned-construct)
    return reinterpret_cast<const c_list_hook_layout<T> *>(&(node->*Hook))->next;
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  [[nodiscard]] static T **&prev(T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    // C list/queue hook layout is ABI-compatible with the sys/queue.h macros by design.
    // NOLINTNEXTLINE(reloco-banned-construct)
    return reinterpret_cast<c_list_hook_layout<T> *>(&(node->*Hook))->prev;
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }
};

} // namespace detail

/**
 * @brief Forward iterator over an intrusive doubly-linked list.
 * Note: Despite being doubly-linked, standard FreeBSD LISTs are only forward-iterable
 * because `prev` points to a pointer field, not the previous node structure itself.
 */
template <typename T, auto Hook, bool IsConst> class RELOCO_POINTER c_list_iterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const T *, T *>;
  using reference = std::conditional_t<IsConst, const T &, T &>;

  constexpr c_list_iterator() noexcept = default;
  explicit constexpr c_list_iterator(T *node) noexcept : current_(node) {}

  template <bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
  // iterator/handle constructed from its position by the owning container or macro.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr c_list_iterator(const c_list_iterator<T, Hook, WasConst> &other) noexcept : current_(other.node()) {}

  RELOCO_CHECKED [[nodiscard]] reference operator*() const noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_list_iterator: Dereference on null element");
    return *current_;
  }

  RELOCO_CHECKED [[nodiscard]] pointer operator->() const noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_list_iterator: Dereference on null element");
    return current_;
  }

  c_list_iterator &operator++() & noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_list_iterator: incrementing an end() iterator");
    current_ = detail::c_list_hook_access<T, Hook>::next(current_);
    return *this;
  }

  c_list_iterator operator++(int) noexcept {
    auto tmp = *this;
    ++(*this);
    return tmp;
  }

  [[nodiscard]] friend bool operator==(const c_list_iterator &lhs, const c_list_iterator &rhs) noexcept {
    return lhs.current_ == rhs.current_;
  }

  [[nodiscard]] friend bool operator!=(const c_list_iterator &lhs, const c_list_iterator &rhs) noexcept {
    return !(lhs == rhs);
  }

  [[nodiscard]] constexpr T *node() const & noexcept { return current_; }

  [[nodiscard]] constexpr T *node() const && = delete;

private:
  T *current_ = nullptr;
};

/**
 * @brief Intrusive List. Layout-compatible with FreeBSD's `LIST_HEAD`.
 *
 * Supports O(1) removal of any node without traversing the list, making it
 * ideal for tracking resources that might be removed out-of-order.
 */
// NOLINTNEXTLINE(reloco-pointer-annotation): intrusive containers link caller-owned nodes.
template <typename T, auto Hook> class c_list {
public:
  using value_type = T;
  using iterator = c_list_iterator<T, Hook, false>;
  using const_iterator = c_list_iterator<T, Hook, true>;

  constexpr c_list() noexcept = default;

  c_list(const c_list &) = delete;
  c_list &operator=(const c_list &) = delete;

  c_list(c_list &&other) noexcept : first_(std::exchange(other.first_, nullptr)) {
    if (first_ != nullptr) {
      detail::c_list_hook_access<T, Hook>::prev(first_) = &first_;
    }
  }

  c_list &operator=(c_list &&other) noexcept {
    if (this != &other) {
      first_ = std::exchange(other.first_, nullptr);
      if (first_ != nullptr) {
        detail::c_list_hook_access<T, Hook>::prev(first_) = &first_;
      }
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

  /** @brief LIST_INSERT_HEAD: O(1) insertion at the front. */
  void push_front(T &node) & noexcept {
    T *n = first_;
    detail::c_list_hook_access<T, Hook>::next(&node) = n;
    if (n != nullptr) {
      detail::c_list_hook_access<T, Hook>::prev(n) = &detail::c_list_hook_access<T, Hook>::next(&node);
    }
    first_ = &node;
    detail::c_list_hook_access<T, Hook>::prev(&node) = &first_;
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

  /** @brief LIST_INSERT_AFTER: O(1) insertion after a known node. */
  void insert_after(T &pos, T &node) & noexcept {
    T *next_node = detail::c_list_hook_access<T, Hook>::next(&pos);
    detail::c_list_hook_access<T, Hook>::next(&node) = next_node;
    if (next_node != nullptr) {
      detail::c_list_hook_access<T, Hook>::prev(next_node) = &detail::c_list_hook_access<T, Hook>::next(&node);
    }
    detail::c_list_hook_access<T, Hook>::next(&pos) = &node;
    detail::c_list_hook_access<T, Hook>::prev(&node) = &detail::c_list_hook_access<T, Hook>::next(&pos);
  }

  /** @brief LIST_INSERT_BEFORE: O(1) insertion before a known node. */
  void insert_before(T &pos, T &node) & noexcept {
    detail::c_list_hook_access<T, Hook>::prev(&node) = detail::c_list_hook_access<T, Hook>::prev(&pos);
    detail::c_list_hook_access<T, Hook>::next(&node) = &pos;
    *detail::c_list_hook_access<T, Hook>::prev(&pos) = &node;
    detail::c_list_hook_access<T, Hook>::prev(&pos) = &detail::c_list_hook_access<T, Hook>::next(&node);
  }

  /** @brief LIST_REMOVE: O(1) removal of an arbitrary node. */
  void remove(T &node) & noexcept {
    T *next_node = detail::c_list_hook_access<T, Hook>::next(&node);
    T **prev_ptr = detail::c_list_hook_access<T, Hook>::prev(&node);

    *prev_ptr = next_node;
    if (next_node != nullptr) {
      detail::c_list_hook_access<T, Hook>::prev(next_node) = prev_ptr;
    }

    detail::c_list_hook_access<T, Hook>::next(&node) = nullptr;
    detail::c_list_hook_access<T, Hook>::prev(&node) = nullptr;
  }

  /**
   * @brief O(1) move of the inclusive node range [@p first, @p last] to just after @p pos.
   *
   * @p first..@p last must be linked in @p other (walking `next` from @p first reaches @p last) and @p pos must be
   * linked in this list and outside the range. Because every node knows its predecessor slot, the source list is
   * never consulted; it is taken for symmetry with the other C containers.
   */
  // `other` is unnamed on purpose: every node stores its predecessor slot, so the source list is never consulted; it
  // may even be this list (moving a range within one list is valid as long as the target lies outside the range).
  void splice_after(T &pos, c_list & /*other*/, T &first, T &last) & noexcept {
    detach_range(first, last);
    attach_after(pos, first, last);
  }

  /** @brief O(1) move of the inclusive node range [@p first, @p last] of @p other to just before @p pos. */
  // `other` is unnamed on purpose: every node stores its predecessor slot, so the source list is never consulted; it
  // may even be this list (moving a range within one list is valid as long as the target lies outside the range).
  void splice_before(T &pos, c_list & /*other*/, T &first, T &last) & noexcept {
    detach_range(first, last);
    attach_before(pos, first, last);
  }

  /** @brief O(1) move of the inclusive node range [@p first, @p last] of @p other to the front of this list. */
  // `other` is unnamed on purpose: every node stores its predecessor slot, so the source list is never consulted; it
  // may even be this list (moving a range within one list is valid as long as the target lies outside the range).
  void splice_front(c_list & /*other*/, T &first, T &last) & noexcept {
    detach_range(first, last);
    attach_front(first, last);
  }

  /** @brief O(n) move of all nodes of @p other to the front; `n` is the length of @p other (its tail is unknown). */
  void slow_splice_front(c_list &other) & noexcept {
    RELOCO_ASSERT(&other != this, "reloco::c_list: splicing a list into itself");
    if (other.first_ == nullptr) {
      return;
    }
    T *first = other.first_;
    T *last = other.slow_last();
    other.first_ = nullptr;
    attach_front(*first, *last);
  }

  /** @brief O(n) move of all nodes of @p other to just after @p pos; `n` is the length of @p other. */
  void slow_splice_after(T &pos, c_list &other) & noexcept {
    RELOCO_ASSERT(&other != this, "reloco::c_list: splicing a list into itself");
    if (other.first_ == nullptr) {
      return;
    }
    T *first = other.first_;
    T *last = other.slow_last();
    other.first_ = nullptr;
    attach_after(pos, *first, *last);
  }

  /** @brief O(n) move of all nodes of @p other to just before @p pos; `n` is the length of @p other. */
  void slow_splice_before(T &pos, c_list &other) & noexcept {
    RELOCO_ASSERT(&other != this, "reloco::c_list: splicing a list into itself");
    if (other.first_ == nullptr) {
      return;
    }
    T *first = other.first_;
    T *last = other.slow_last();
    other.first_ = nullptr;
    attach_before(pos, *first, *last);
  }

  /** @brief O(n) move of all nodes of @p other to the back; `n` is the length of this list plus @p other. */
  void slow_splice_back(c_list &other) & noexcept {
    RELOCO_ASSERT(&other != this, "reloco::c_list: splicing a list into itself");
    if (first_ == nullptr) {
      slow_splice_front(other);
      return;
    }
    slow_splice_after(*slow_last(), other);
  }

  /** @brief Unlinks all nodes. */
  void clear() & noexcept {
    T *cur = first_;
    while (cur != nullptr) {
      T *next = detail::c_list_hook_access<T, Hook>::next(cur);
      detail::c_list_hook_access<T, Hook>::next(cur) = nullptr;
      detail::c_list_hook_access<T, Hook>::prev(cur) = nullptr;
      cur = next;
    }
    first_ = nullptr;
  }

private:
  // NOLINTNEXTLINE(reloco-lifetime-annotations): returns a caller-owned node, not storage of the list itself.
  [[nodiscard]] T *slow_last() const & noexcept {
    T *cur = first_;
    while (cur != nullptr && detail::c_list_hook_access<T, Hook>::next(cur) != nullptr) {
      cur = detail::c_list_hook_access<T, Hook>::next(cur);
    }
    return cur;
  }

  // Unlinks [first, last] from whichever list holds it and leaves the range's own hooks dangling.
  static void detach_range(T &first, T &last) noexcept {
    T **prev_slot = detail::c_list_hook_access<T, Hook>::prev(&first);
    T *after = detail::c_list_hook_access<T, Hook>::next(&last);
    *prev_slot = after;
    if (after != nullptr) {
      detail::c_list_hook_access<T, Hook>::prev(after) = prev_slot;
    }
  }

  void attach_after(T &pos, T &first, T &last) noexcept {
    T *after = detail::c_list_hook_access<T, Hook>::next(&pos);
    detail::c_list_hook_access<T, Hook>::next(&last) = after;
    if (after != nullptr) {
      detail::c_list_hook_access<T, Hook>::prev(after) = &detail::c_list_hook_access<T, Hook>::next(&last);
    }
    detail::c_list_hook_access<T, Hook>::next(&pos) = &first;
    detail::c_list_hook_access<T, Hook>::prev(&first) = &detail::c_list_hook_access<T, Hook>::next(&pos);
  }

  void attach_front(T &first, T &last) noexcept {
    T *after = first_;
    detail::c_list_hook_access<T, Hook>::next(&last) = after;
    if (after != nullptr) {
      detail::c_list_hook_access<T, Hook>::prev(after) = &detail::c_list_hook_access<T, Hook>::next(&last);
    }
    first_ = &first;
    detail::c_list_hook_access<T, Hook>::prev(&first) = &first_;
  }

  void attach_before(T &pos, T &first, T &last) noexcept {
    T **prev_slot = detail::c_list_hook_access<T, Hook>::prev(&pos);
    *prev_slot = &first;
    detail::c_list_hook_access<T, Hook>::prev(&first) = prev_slot;
    detail::c_list_hook_access<T, Hook>::next(&last) = &pos;
    detail::c_list_hook_access<T, Hook>::prev(&pos) = &detail::c_list_hook_access<T, Hook>::next(&last);
  }

  T *first_ = nullptr;
};

} // namespace reloco