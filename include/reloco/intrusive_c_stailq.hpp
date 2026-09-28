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

// Extracts the `next` pointer via reinterpret_cast from ANY struct field.
// This allows binding directly to FreeBSD's anonymous `STAILQ_ENTRY` structs.
template <typename T, auto Hook> struct c_stailq_hook_access {
  [[nodiscard]] static T *&next(T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return *reinterpret_cast<T **>(&(node->*Hook));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  [[nodiscard]] static T *const &next(const T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return *reinterpret_cast<T *const *>(&(node->*Hook));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }
};

} // namespace detail

/** @brief Forward iterator over an intrusive singly-linked tail queue. */
template <typename T, auto Hook, bool IsConst> class c_stailq_iterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const T *, T *>;
  using reference = std::conditional_t<IsConst, const T &, T &>;

  constexpr c_stailq_iterator() noexcept = default;
  explicit constexpr c_stailq_iterator(T *node) noexcept : current_(node) {}

  template <bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
  constexpr c_stailq_iterator(const c_stailq_iterator<T, Hook, WasConst> &other) noexcept : current_(other.node()) {}

  [[nodiscard]] reference operator*() const noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_stailq_iterator: Dereference on null element");
    return *current_;
  }

  [[nodiscard]] pointer operator->() const noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_stailq_iterator: Dereference on null element");
    return current_;
  }

  c_stailq_iterator &operator++() noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_stailq_iterator: incrementing an end() iterator");
    current_ = detail::c_stailq_hook_access<T, Hook>::next(current_);
    return *this;
  }

  c_stailq_iterator operator++(int) noexcept {
    auto tmp = *this;
    ++(*this);
    return tmp;
  }

  [[nodiscard]] friend bool operator==(const c_stailq_iterator &lhs, const c_stailq_iterator &rhs) noexcept {
    return lhs.current_ == rhs.current_;
  }

  [[nodiscard]] friend bool operator!=(const c_stailq_iterator &lhs, const c_stailq_iterator &rhs) noexcept {
    return !(lhs == rhs);
  }

  [[nodiscard]] constexpr T *node() const noexcept { return current_; }

private:
  T *current_ = nullptr;
};

/**
 * @brief Intrusive Singly-Linked Tail Queue. Layout-compatible with FreeBSD's `STAILQ_HEAD`.
 *
 * Provides O(1) push_front, pop_front, and push_back.
 * Ideal for FIFO queues.
 */
template <typename T, auto Hook> class c_stailq {
public:
  using value_type = T;
  using iterator = c_stailq_iterator<T, Hook, false>;
  using const_iterator = c_stailq_iterator<T, Hook, true>;

  constexpr c_stailq() noexcept = default;

  c_stailq(const c_stailq &) = delete;
  c_stailq &operator=(const c_stailq &) = delete;

  c_stailq(c_stailq &&other) noexcept : first_(other.first_) {
    if (first_ == nullptr) {
      last_ptr_ = &first_;
    } else {
      last_ptr_ = other.last_ptr_;
    }
    other.first_ = nullptr;
    other.last_ptr_ = &other.first_;
  }

  c_stailq &operator=(c_stailq &&other) noexcept {
    if (this != &other) {
      first_ = other.first_;
      if (first_ == nullptr) {
        last_ptr_ = &first_;
      } else {
        last_ptr_ = other.last_ptr_;
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

  [[nodiscard]] T *front() const & noexcept { return first_; }

  /** @brief STAILQ_INSERT_HEAD: O(1) insertion at the front. */
  void push_front(T &node) & noexcept {
    T *&next = detail::c_stailq_hook_access<T, Hook>::next(&node);
    next = first_;
    if (first_ == nullptr) {
      last_ptr_ = &next;
    }
    first_ = &node;
  }

  /** @brief STAILQ_INSERT_TAIL: O(1) insertion at the back. */
  void push_back(T &node) & noexcept {
    detail::c_stailq_hook_access<T, Hook>::next(&node) = nullptr;
    *last_ptr_ = &node;
    last_ptr_ = &detail::c_stailq_hook_access<T, Hook>::next(&node);
  }

  /** @brief STAILQ_REMOVE_HEAD: O(1) removal from the front. Returns the removed node. */
  T *pop_front() & noexcept {
    T *node = first_;
    if (node != nullptr) {
      first_ = detail::c_stailq_hook_access<T, Hook>::next(node);
      detail::c_stailq_hook_access<T, Hook>::next(node) = nullptr;
      if (first_ == nullptr) {
        last_ptr_ = &first_;
      }
    }
    return node;
  }

  /** @brief STAILQ_INSERT_AFTER: O(1) insertion after a known node. */
  void insert_after(T &pos, T &node) & noexcept {
    T *&node_next = detail::c_stailq_hook_access<T, Hook>::next(&node);
    T *&pos_next = detail::c_stailq_hook_access<T, Hook>::next(&pos);

    node_next = pos_next;
    pos_next = &node;

    if (node_next == nullptr) {
      // If we inserted after the last node, update last_ptr_
      last_ptr_ = &node_next;
    }
  }

  /** @brief STAILQ_REMOVE: O(n) removal of an arbitrary node. */
  void remove(T &node) & noexcept {
    if (first_ == &node) {
      pop_front();
      return;
    }

    T *cur = first_;
    while (cur != nullptr) {
      T *next = detail::c_stailq_hook_access<T, Hook>::next(cur);
      if (next == &node) {
        T *node_next = detail::c_stailq_hook_access<T, Hook>::next(&node);
        detail::c_stailq_hook_access<T, Hook>::next(cur) = node_next;
        detail::c_stailq_hook_access<T, Hook>::next(&node) = nullptr;

        if (node_next == nullptr) {
          // We removed the last element, update last_ptr_ to point to cur's next
          last_ptr_ = &detail::c_stailq_hook_access<T, Hook>::next(cur);
        }
        return;
      }
      cur = next;
    }
  }

  /** @brief Unlinks all nodes. */
  void clear() & noexcept {
    T *cur = first_;
    while (cur != nullptr) {
      T *next = detail::c_stailq_hook_access<T, Hook>::next(cur);
      detail::c_stailq_hook_access<T, Hook>::next(cur) = nullptr;
      cur = next;
    }
    first_ = nullptr;
    last_ptr_ = &first_;
  }

private:
  T *first_ = nullptr;
  T **last_ptr_ = &first_;
};

} // namespace reloco