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
// This allows binding directly to FreeBSD's anonymous `SLIST_ENTRY` structs
// without knowing the internal field names (`sle_next`).
template <typename T, auto Hook> struct c_slist_hook_access {
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

/** @brief Forward iterator over an intrusive singly-linked list. */
template <typename T, auto Hook, bool IsConst> class c_slist_iterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const T *, T *>;
  using reference = std::conditional_t<IsConst, const T &, T &>;

  constexpr c_slist_iterator() noexcept = default;
  explicit constexpr c_slist_iterator(T *node) noexcept : current_(node) {}

  template <bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
  constexpr c_slist_iterator(const c_slist_iterator<T, Hook, WasConst> &other) noexcept : current_(other.node()) {}

  [[nodiscard]] reference operator*() const noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_slist_iterator: Dereference on null element");
    return *current_;
  }

  [[nodiscard]] pointer operator->() const noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_slist_iterator: Dereference on null element");
    return current_;
  }

  c_slist_iterator &operator++() noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_slist_iterator: incrementing an end() iterator");
    current_ = detail::c_slist_hook_access<T, Hook>::next(current_);
    return *this;
  }

  c_slist_iterator operator++(int) noexcept {
    auto tmp = *this;
    ++(*this);
    return tmp;
  }

  [[nodiscard]] friend bool operator==(const c_slist_iterator &lhs, const c_slist_iterator &rhs) noexcept {
    return lhs.current_ == rhs.current_;
  }

  [[nodiscard]] friend bool operator!=(const c_slist_iterator &lhs, const c_slist_iterator &rhs) noexcept {
    return !(lhs == rhs);
  }

  [[nodiscard]] constexpr T *node() const noexcept { return current_; }

private:
  T *current_ = nullptr;
};

/**
 * @brief Intrusive Singly-Linked List. Layout-compatible with FreeBSD's `SLIST_HEAD`.
 *
 * You can `reinterpret_cast` a raw C `SLIST_HEAD*` directly into a pointer to this
 * class to attach C++ iterators and ergonomics to existing kernel memory.
 */
template <typename T, auto Hook> class c_slist {
public:
  using value_type = T;
  using iterator = c_slist_iterator<T, Hook, false>;
  using const_iterator = c_slist_iterator<T, Hook, true>;

  constexpr c_slist() noexcept = default;

  c_slist(const c_slist &) = delete;
  c_slist &operator=(const c_slist &) = delete;

  c_slist(c_slist &&other) noexcept : first_(std::exchange(other.first_, nullptr)) {}
  c_slist &operator=(c_slist &&other) noexcept {
    if (this != &other) {
      first_ = std::exchange(other.first_, nullptr);
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

  void push_front(T &node) & noexcept {
    detail::c_slist_hook_access<T, Hook>::next(&node) = first_;
    first_ = &node;
  }

  T *pop_front() & noexcept {
    T *node = first_;
    if (node != nullptr) {
      first_ = detail::c_slist_hook_access<T, Hook>::next(node);
      detail::c_slist_hook_access<T, Hook>::next(node) = nullptr;
    }
    return node;
  }

  void insert_after(T &pos, T &node) & noexcept {
    detail::c_slist_hook_access<T, Hook>::next(&node) = detail::c_slist_hook_access<T, Hook>::next(&pos);
    detail::c_slist_hook_access<T, Hook>::next(&pos) = &node;
  }

  T *remove_after(T &pos) & noexcept {
    T *node = detail::c_slist_hook_access<T, Hook>::next(&pos);
    if (node != nullptr) {
      detail::c_slist_hook_access<T, Hook>::next(&pos) = detail::c_slist_hook_access<T, Hook>::next(node);
      detail::c_slist_hook_access<T, Hook>::next(node) = nullptr;
    }
    return node;
  }

  void remove(T &node) & noexcept {
    if (first_ == &node) {
      pop_front();
      return;
    }

    T *cur = first_;
    while (cur != nullptr) {
      T *next = detail::c_slist_hook_access<T, Hook>::next(cur);
      if (next == &node) {
        detail::c_slist_hook_access<T, Hook>::next(cur) = detail::c_slist_hook_access<T, Hook>::next(&node);
        detail::c_slist_hook_access<T, Hook>::next(&node) = nullptr;
        return;
      }
      cur = next;
    }
  }

  void clear() & noexcept {
    T *cur = first_;
    while (cur != nullptr) {
      T *next = detail::c_slist_hook_access<T, Hook>::next(cur);
      detail::c_slist_hook_access<T, Hook>::next(cur) = nullptr;
      cur = next;
    }
    first_ = nullptr;
  }

private:
  T *first_ = nullptr;
};

} // namespace reloco
