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
template <typename T> struct c_list_hook_layout {
  T *next;
  T **prev; // address of previous next element
};

// Extracts the `next` and `prev` pointers via reinterpret_cast from ANY struct field.
template <typename T, auto Hook> struct c_list_hook_access {
  [[nodiscard]] static T *&next(T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return reinterpret_cast<c_list_hook_layout<T> *>(&(node->*Hook))->next;
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  [[nodiscard]] static T *const &next(const T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return reinterpret_cast<const c_list_hook_layout<T> *>(&(node->*Hook))->next;
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  [[nodiscard]] static T **&prev(T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
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
template <typename T, auto Hook, bool IsConst> class c_list_iterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const T *, T *>;
  using reference = std::conditional_t<IsConst, const T &, T &>;

  constexpr c_list_iterator() noexcept = default;
  explicit constexpr c_list_iterator(T *node) noexcept : current_(node) {}

  template <bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
  constexpr c_list_iterator(const c_list_iterator<T, Hook, WasConst> &other) noexcept : current_(other.node()) {}

  [[nodiscard]] reference operator*() const noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_list_iterator: Dereference on null element");
    return *current_;
  }

  [[nodiscard]] pointer operator->() const noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_list_iterator: Dereference on null element");
    return current_;
  }

  c_list_iterator &operator++() noexcept {
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

  [[nodiscard]] constexpr T *node() const noexcept { return current_; }

private:
  T *current_ = nullptr;
};

/**
 * @brief Intrusive List. Layout-compatible with FreeBSD's `LIST_HEAD`.
 *
 * Supports O(1) removal of any node without traversing the list, making it
 * ideal for tracking resources that might be removed out-of-order.
 */
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

  [[nodiscard]] T *front() const & noexcept { return first_; }

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
  T *first_ = nullptr;
};

} // namespace reloco