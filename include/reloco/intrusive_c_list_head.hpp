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

// Defines the exact memory layout of Linux's `struct list_head`.
struct c_list_head_node {
  c_list_head_node *next;
  c_list_head_node *prev;
};

// Extracts the node and computes the `container_of` offset.
template <typename T, auto Hook> struct c_linux_hook_access {
  [[nodiscard]] static c_list_head_node *get(T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return reinterpret_cast<c_list_head_node *>(&(node->*Hook));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  [[nodiscard]] static const c_list_head_node *get(const T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return reinterpret_cast<const c_list_head_node *>(&(node->*Hook));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  // C++ equivalent of Linux's `container_of` macro
  [[nodiscard]] static T *container(c_list_head_node *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    std::byte *addr = reinterpret_cast<std::byte *>(node);
    return reinterpret_cast<T *>(addr - offset());
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  [[nodiscard]] static const T *container(const c_list_head_node *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    const std::byte *addr = reinterpret_cast<const std::byte *>(node);
    return reinterpret_cast<const T *>(addr - offset());
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

private:
  [[nodiscard]] static constexpr std::ptrdiff_t offset() noexcept {
    // Standard offset calculation without UB from null-pointer dereferences.
    // 8192 (0x2000) is used to avoid aggressive sanitizer null-checks.
    return reinterpret_cast<std::ptrdiff_t>(&(reinterpret_cast<T *>(8192)->*Hook)) - 8192;
  }
};

} // namespace detail

/** @brief Forward iterator over an intrusive circular Linux list. */
template <typename T, auto Hook, bool IsConst> class c_list_head_iterator {
public:
  using iterator_category = std::bidirectional_iterator_tag; // Circular lists can be iterated backwards!
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const T *, T *>;
  using reference = std::conditional_t<IsConst, const T &, T &>;
  using node_ptr = std::conditional_t<IsConst, const detail::c_list_head_node *, detail::c_list_head_node *>;

  constexpr c_list_head_iterator() noexcept = default;
  explicit constexpr c_list_head_iterator(node_ptr node) noexcept : current_(node) {}

  template <bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
  constexpr c_list_head_iterator(const c_list_head_iterator<T, Hook, WasConst> &other) noexcept
      : current_(other.node()) {}

  [[nodiscard]] reference operator*() const noexcept {
    return *detail::c_linux_hook_access<T, Hook>::container(current_);
  }

  [[nodiscard]] pointer operator->() const noexcept {
    return detail::c_linux_hook_access<T, Hook>::container(current_);
  }

  c_list_head_iterator &operator++() noexcept {
    current_ = current_->next;
    return *this;
  }

  c_list_head_iterator operator++(int) noexcept {
    auto tmp = *this;
    ++(*this);
    return tmp;
  }

  c_list_head_iterator &operator--() noexcept {
    current_ = current_->prev;
    return *this;
  }

  c_list_head_iterator operator--(int) noexcept {
    auto tmp = *this;
    --(*this);
    return tmp;
  }

  [[nodiscard]] friend bool operator==(const c_list_head_iterator &lhs, const c_list_head_iterator &rhs) noexcept {
    return lhs.current_ == rhs.current_;
  }

  [[nodiscard]] friend bool operator!=(const c_list_head_iterator &lhs, const c_list_head_iterator &rhs) noexcept {
    return !(lhs == rhs);
  }

  [[nodiscard]] constexpr node_ptr node() const noexcept { return current_; }

private:
  node_ptr current_ = nullptr;
};

/**
 * @brief Intrusive Circular Doubly-Linked List. Layout-compatible with Linux's `list_head`.
 *
 * This implements the classic Linux circular list pattern, meaning the list head
 * is a node itself, and the list is never "null-terminated".
 */
template <typename T, auto Hook> class c_list_head {
public:
  using value_type = T;
  using iterator = c_list_head_iterator<T, Hook, false>;
  using const_iterator = c_list_head_iterator<T, Hook, true>;

  c_list_head() noexcept {
    root_.next = &root_;
    root_.prev = &root_;
  }

  c_list_head(const c_list_head &) = delete;
  c_list_head &operator=(const c_list_head &) = delete;

  c_list_head(c_list_head &&other) noexcept {
    if (other.empty()) {
      root_.next = &root_;
      root_.prev = &root_;
    } else {
      root_.next = other.root_.next;
      root_.prev = other.root_.prev;
      root_.next->prev = &root_;
      root_.prev->next = &root_;

      // Reset other
      other.root_.next = &other.root_;
      other.root_.prev = &other.root_;
    }
  }

  c_list_head &operator=(c_list_head &&other) noexcept {
    if (this != &other) {
      if (other.empty()) {
        root_.next = &root_;
        root_.prev = &root_;
      } else {
        root_.next = other.root_.next;
        root_.prev = other.root_.prev;
        root_.next->prev = &root_;
        root_.prev->next = &root_;

        other.root_.next = &other.root_;
        other.root_.prev = &other.root_;
      }
    }
    return *this;
  }

  [[nodiscard]] bool empty() const & noexcept { return root_.next == &root_; }

  [[nodiscard]] iterator begin() & noexcept { return iterator(root_.next); }
  [[nodiscard]] iterator end() & noexcept { return iterator(&root_); }
  [[nodiscard]] const_iterator begin() const & noexcept { return const_iterator(root_.next); }
  [[nodiscard]] const_iterator end() const & noexcept { return const_iterator(&root_); }
  [[nodiscard]] const_iterator cbegin() const & noexcept { return begin(); }
  [[nodiscard]] const_iterator cend() const & noexcept { return end(); }

  [[nodiscard]] T *front() const & noexcept {
    return empty() ? nullptr : detail::c_linux_hook_access<T, Hook>::container(root_.next);
  }

  [[nodiscard]] T *back() const & noexcept {
    return empty() ? nullptr : detail::c_linux_hook_access<T, Hook>::container(root_.prev);
  }

  /** @brief list_add: O(1) insertion at the front. */
  void push_front(T &node) & noexcept {
    insert_between(detail::c_linux_hook_access<T, Hook>::get(&node), &root_, root_.next);
  }

  /** @brief list_add_tail: O(1) insertion at the back. */
  void push_back(T &node) & noexcept {
    insert_between(detail::c_linux_hook_access<T, Hook>::get(&node), root_.prev, &root_);
  }

  /** @brief O(1) removal from the front. Returns the removed node. */
  T *pop_front() & noexcept {
    if (empty())
      return nullptr;
    T *node = front();
    remove(*node);
    return node;
  }

  /** @brief O(1) removal from the back. Returns the removed node. */
  T *pop_back() & noexcept {
    if (empty())
      return nullptr;
    T *node = back();
    remove(*node);
    return node;
  }

  /** @brief list_del: O(1) arbitrary removal. */
  void remove(T &node) & noexcept {
    detail::c_list_head_node *n = detail::c_linux_hook_access<T, Hook>::get(&node);
    n->prev->next = n->next;
    n->next->prev = n->prev;

    // Poison the pointers (similar to LIST_POISON1/2 in Linux) to catch use-after-free
    n->next = nullptr;
    n->prev = nullptr;
  }

  /** @brief Unlinks all nodes. */
  void clear() & noexcept {
    detail::c_list_head_node *cur = root_.next;
    while (cur != &root_) {
      detail::c_list_head_node *next = cur->next;
      cur->next = nullptr;
      cur->prev = nullptr;
      cur = next;
    }
    root_.next = &root_;
    root_.prev = &root_;
  }

private:
  void insert_between(detail::c_list_head_node *new_node, detail::c_list_head_node *prev,
                      detail::c_list_head_node *next) & noexcept {
    next->prev = new_node;
    new_node->next = next;
    new_node->prev = prev;
    prev->next = new_node;
  }

  // In Linux, the list head is just a raw node.
  detail::c_list_head_node root_;
};

} // namespace reloco