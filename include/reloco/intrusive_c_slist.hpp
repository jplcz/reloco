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
/** @brief Accesses an intrusive singly linked-list hook in an object. */
template <typename T, auto Hook> struct c_slist_hook_access {
  [[nodiscard]] static T *&next(T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    // C list/queue hook layout is ABI-compatible with the sys/queue.h macros by design.
    // NOLINTNEXTLINE(reloco-banned-construct)
    return *reinterpret_cast<T **>(&(node->*Hook));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  [[nodiscard]] static T *const &next(const T *node) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    // C list/queue hook layout is ABI-compatible with the sys/queue.h macros by design.
    // NOLINTNEXTLINE(reloco-banned-construct)
    return *reinterpret_cast<T *const *>(&(node->*Hook));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }
};

} // namespace detail

/** @brief Forward iterator over an intrusive singly-linked list. */
template <typename T, auto Hook, bool IsConst> class RELOCO_POINTER c_slist_iterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = std::conditional_t<IsConst, const T *, T *>;
  using reference = std::conditional_t<IsConst, const T &, T &>;

  constexpr c_slist_iterator() noexcept = default;
  explicit constexpr c_slist_iterator(T *node) noexcept : current_(node) {}

  template <bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
  // iterator/handle constructed from its position by the owning container or macro.
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr c_slist_iterator(const c_slist_iterator<T, Hook, WasConst> &other) noexcept : current_(other.node()) {}

  RELOCO_CHECKED [[nodiscard]] reference operator*() const noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_slist_iterator: Dereference on null element");
    return *current_;
  }

  RELOCO_CHECKED [[nodiscard]] pointer operator->() const noexcept {
    RELOCO_ASSERT(current_ != nullptr, "reloco::c_slist_iterator: Dereference on null element");
    return current_;
  }

  c_slist_iterator &operator++() & noexcept {
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

  [[nodiscard]] constexpr T *node() const & noexcept { return current_; }

  [[nodiscard]] constexpr T *node() const && = delete;

private:
  T *current_ = nullptr;
};

/**
 * @brief Intrusive Singly-Linked List. Layout-compatible with FreeBSD's `SLIST_HEAD`.
 *
 * You can `reinterpret_cast` a raw C `SLIST_HEAD*` directly into a pointer to this
 * class to attach C++ iterators and ergonomics to existing kernel memory.
 */
// NOLINTNEXTLINE(reloco-pointer-annotation): intrusive containers link caller-owned nodes.
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

  // NOLINTNEXTLINE(reloco-lifetime-annotations): returns a caller-owned node, not storage of the list itself.
  [[nodiscard]] T *front() const & noexcept { return first_; }

  // NOLINTNEXTLINE(reloco-lifetime-annotations): returns a caller-owned node, not storage of the list itself.
  [[nodiscard]] T *front() const && = delete;

  void push_front(T &node) & noexcept {
    detail::c_slist_hook_access<T, Hook>::next(&node) = first_;
    first_ = &node;
  }

  // NOLINTNEXTLINE(reloco-lifetime-annotations): returns a caller-owned node, not storage of the list itself.
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

  // NOLINTNEXTLINE(reloco-lifetime-annotations): returns a caller-owned node, not storage of the list itself.
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

  /**
   * @brief O(1) move of the nodes following @p before_first up to and including @p last (all in @p other) to just
   * after @p pos.
   *
   * A singly linked node cannot find its predecessor, so the range is named by the node *before* its first element,
   * like `std::forward_list::splice_after`. @p last must be reachable from `next(before_first)`, and @p pos must be
   * linked in this list outside the range.
   */
  // `other` is unnamed on purpose: without a tail pointer nothing in the source list needs fixing, so it is never
  // consulted; it may even be this list (valid as long as the target lies outside the range).
  void splice_after(T &pos, c_slist & /*other*/, T &before_first, T &last) & noexcept {
    T *first = detach_range(&detail::c_slist_hook_access<T, Hook>::next(&before_first), last);
    attach(&detail::c_slist_hook_access<T, Hook>::next(&pos), *first, last);
  }

  /** @brief O(1) move of the nodes from the head of @p other up to and including @p last to just after @p pos. */
  void splice_after(T &pos, c_slist &other, T &last) & noexcept {
    T *first = other.detach_range(&other.first_, last);
    attach(&detail::c_slist_hook_access<T, Hook>::next(&pos), *first, last);
  }

  /** @brief O(1) move of the nodes after @p before_first up to and including @p last (in @p other) to the front. */
  void splice_front(c_slist &other, T &before_first, T &last) & noexcept {
    T *first = other.detach_range(&detail::c_slist_hook_access<T, Hook>::next(&before_first), last);
    attach(&first_, *first, last);
  }

  /** @brief O(1) move of the nodes from the head of @p other up to and including @p last to the front. */
  void splice_front(c_slist &other, T &last) & noexcept {
    T *first = other.detach_range(&other.first_, last);
    attach(&first_, *first, last);
  }

  /** @brief O(n) move of all nodes of @p other to just after @p pos; `n` is the length of @p other (tail unknown). */
  void slow_splice_after(T &pos, c_slist &other) & noexcept {
    RELOCO_ASSERT(&other != this, "reloco::c_slist: splicing a list into itself");
    if (!other.empty()) {
      splice_after(pos, other, *other.slow_last());
    }
  }

  /** @brief O(n) move of all nodes of @p other to the front; `n` is the length of @p other (tail unknown). */
  void slow_splice_front(c_slist &other) & noexcept {
    RELOCO_ASSERT(&other != this, "reloco::c_slist: splicing a list into itself");
    if (!other.empty()) {
      splice_front(other, *other.slow_last());
    }
  }

  /** @brief O(n) move of all nodes of @p other to the back; `n` is the length of both lists (tails unknown). */
  void slow_splice_back(c_slist &other) & noexcept {
    RELOCO_ASSERT(&other != this, "reloco::c_slist: splicing a list into itself");
    if (first_ == nullptr) {
      slow_splice_front(other);
    } else {
      slow_splice_after(*slow_last(), other);
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
  // NOLINTNEXTLINE(reloco-lifetime-annotations): returns a caller-owned node, not storage of the list itself.
  [[nodiscard]] T *slow_last() const & noexcept {
    T *cur = first_;
    while (cur != nullptr && detail::c_slist_hook_access<T, Hook>::next(cur) != nullptr) {
      cur = detail::c_slist_hook_access<T, Hook>::next(cur);
    }
    return cur;
  }

  // Unlinks the run starting at *slot and ending at @p last; returns its first node.
  T *detach_range(T **slot, T &last) noexcept {
    T *first = *slot;
    *slot = detail::c_slist_hook_access<T, Hook>::next(&last);
    return first;
  }

  // Links [first, last] into *slot, in front of whatever *slot currently points to.
  void attach(T **slot, T &first, T &last) noexcept {
    detail::c_slist_hook_access<T, Hook>::next(&last) = *slot;
    *slot = &first;
  }

  T *first_ = nullptr;
};

} // namespace reloco
