#pragma once

#include "detail/assert.hpp"
#include "lifetime.hpp"
#include "rvalue_safety.hpp"

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief Policy for static-capacity or external containers (span, array, external_vector).
 * Caches the exact memory boundaries at iterator creation.
 */
template <typename T> class static_bounds_policy {
public:
  constexpr static_bounds_policy() noexcept = default;
  constexpr static_bounds_policy(T *b, T *e) noexcept : begin_(b), end_(e) {
    RELOCO_DEBUG_ASSERT(begin_ <= end_, "Pointer ordering failure");
  }

  template <typename U, std::enable_if_t<std::is_same_v<const U, T>, int> = 0>
  constexpr static_bounds_policy(const static_bounds_policy<U> &other) noexcept
      : begin_(other.begin_), end_(other.end_) {}

  constexpr void assert_deref(T *current) const noexcept {
    RELOCO_ASSERT(current >= begin_ && current < end_, "iterator dereference out of bounds");
  }

  constexpr void assert_math(T *current, std::ptrdiff_t n) const noexcept {
    if (begin_ == end_) {
      RELOCO_ASSERT(n == 0, "iterator arithmetic out of bounds (range is empty)");
      return;
    }
    RELOCO_ASSERT(n >= 0 ? (end_ - current >= n) : (current - begin_ >= -n), "iterator arithmetic out of bounds");
  }

  constexpr void assert_domain(const static_bounds_policy &other) const noexcept {
    RELOCO_ASSERT(begin_ == other.begin_ && end_ == other.end_, "comparing iterators from different domains");
  }

private:
  template <typename> friend class static_bounds_policy;
  T *begin_{nullptr};
  T *end_{nullptr};
};

/**
 * @brief Policy for dynamic containers (vector, string).
 * Queries the container live to instantly catch iterator invalidation (reallocations).
 */
template <typename Container, typename T> class dynamic_bounds_policy {
public:
  constexpr dynamic_bounds_policy() noexcept = default;
  constexpr explicit dynamic_bounds_policy(const Container *c) noexcept : container_(c) {}

  template <typename UContainer, typename U, std::enable_if_t<std::is_same_v<const U, T>, int> = 0>
  constexpr dynamic_bounds_policy(const dynamic_bounds_policy<UContainer, U> &other) noexcept
      : container_(other.container_) {}

  constexpr void assert_deref(T *current) const noexcept {
    RELOCO_ASSERT(container_ != nullptr, "dereferencing uninitialized iterator");
    const T *begin = container_->unsafe_data();
    const T *end = begin + container_->size();
    RELOCO_ASSERT(current >= begin && current < end, "iterator out of bounds OR invalidated by reallocation");
  }

  constexpr void assert_math(T *current, std::ptrdiff_t n) const noexcept {
    RELOCO_ASSERT(container_ != nullptr, "arithmetic on uninitialized iterator");

    if (container_->empty()) {
      RELOCO_ASSERT(n == 0, "iterator arithmetic out of bounds (container is empty)");
      return;
    }

    const T *begin = container_->unsafe_data();
    const T *end = begin + container_->size();
    RELOCO_ASSERT(n >= 0 ? (end - current >= n) : (current - begin >= -n),
                  "iterator arithmetic out of bounds OR invalidated by reallocation");
  }

  constexpr void assert_domain(const dynamic_bounds_policy &other) const noexcept {
    RELOCO_ASSERT(container_ == other.container_, "comparing iterators from different containers");
  }

private:
  template <typename, typename> friend class dynamic_bounds_policy;
  const Container *container_{nullptr};
};

/**
 * @brief Bounds-aware random-access iterator over contiguous storage.
 * @tparam T Element type.
 * @tparam BoundsPolicy Policy used to validate iterator bounds.
 */
template <typename T, typename BoundsPolicy> class contiguous_iterator {
public:
  using iterator_category = std::random_access_iterator_tag;
  using value_type = std::remove_cv_t<T>;
  using difference_type = std::ptrdiff_t;
  using pointer = T *;
  using reference = T &;

  constexpr contiguous_iterator() noexcept = default;

  constexpr contiguous_iterator(pointer current, BoundsPolicy policy) noexcept : current_(current), policy_(policy) {
    // We can do an initial math check against +0 to ensure the starting
    // pointer is valid within the domain at creation time.
    policy_.assert_math(current_, 0);
  }

  // Implicit conversion to const_iterator (requires the policy to support it)
  template <typename U, typename UPolicy, std::enable_if_t<std::is_same_v<const U, T>, int> = 0>
  constexpr contiguous_iterator(const contiguous_iterator<U, UPolicy> &other) noexcept
      : current_(other.unsafe_current()), policy_(other.policy()) {}

  // Deliberately *not* RELOCO_LIFETIMEBOUND: with no explicit parameter,
  // RELOCO_LIFETIMEBOUND binds the returned reference/pointer's lifetime to
  // the implicit object parameter (`*this`, i.e. the iterator itself), but
  // dereferencing a contiguous_iterator yields a reference into the
  // *external* storage it points into, which the iterator never owns and
  // routinely outlives. Marking these lifetimebound-to-the-iterator made
  // Clang statically (and incorrectly) treat the dereferenced element as
  // tied to the hidden range-for loop variable's scope -- e.g. `for (const
  // auto &e : container) return &e;` was flagged as
  // -Wreturn-stack-address, even though `e` genuinely outlives the loop.
  // Contrast with begin()/end() below, which correctly remain
  // RELOCO_LIFETIMEBOUND: the *iterator itself* must not outlive the
  // container it was created from.
  [[nodiscard]] constexpr reference operator*() const noexcept {
    policy_.assert_deref(current_);
    return *current_;
  }

  [[nodiscard]] constexpr pointer operator->() const noexcept {
    policy_.assert_deref(current_);
    return current_;
  }

  [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
    policy_.assert_deref(current_ + n);
    return current_[n];
  }

  constexpr contiguous_iterator &operator++() noexcept {
    policy_.assert_math(current_, 1);
    ++current_;
    return *this;
  }

  constexpr contiguous_iterator &operator+=(difference_type n) noexcept {
    policy_.assert_math(current_, n);
    // GCC, once it inlines this into algorithms like std::sort's final
    // insertion sort over a small stack array, can misjudge the
    // runtime-checked `assert_math` above as not actually bounding `n` and
    // flag this pointer arithmetic as out-of-bounds -- it never is, since
    // assert_math already traps on an out-of-range `n`.
    RELOCO_BEGIN_SUPPRESS_GCC_BOUNDS_FALSE_POSITIVE;
    current_ += n;
    RELOCO_END_SUPPRESS_GCC_BOUNDS_FALSE_POSITIVE;
    return *this;
  }

  [[nodiscard]] constexpr contiguous_iterator operator+(difference_type n) const noexcept {
    contiguous_iterator tmp = *this;
    tmp += n;
    return tmp;
  }

  [[nodiscard]] constexpr difference_type operator-(const contiguous_iterator &other) const noexcept {
    // UBSAN protection against I-I
    if (current_ == other.current_) {
      return 0;
    }
    policy_.assert_domain(other.policy());
    return current_ - other.current_;
  }

  [[nodiscard]] constexpr bool operator==(const contiguous_iterator &other) const noexcept {
    policy_.assert_domain(other.policy());
    return current_ == other.current_;
  }

  [[nodiscard]] constexpr bool operator<(const contiguous_iterator &other) const noexcept {
    policy_.assert_domain(other.policy());
    return current_ < other.current_;
  }

  [[nodiscard]] constexpr bool operator!=(const contiguous_iterator &other) const noexcept { return !(*this == other); }
  [[nodiscard]] constexpr bool operator>(const contiguous_iterator &other) const noexcept { return other < *this; }
  [[nodiscard]] constexpr bool operator<=(const contiguous_iterator &other) const noexcept { return !(other < *this); }
  [[nodiscard]] constexpr bool operator>=(const contiguous_iterator &other) const noexcept { return !(*this < other); }

  constexpr contiguous_iterator &operator--() noexcept { return *this += -1; }
  constexpr contiguous_iterator operator--(int) noexcept {
    contiguous_iterator tmp = *this;
    --(*this);
    return tmp;
  }
  constexpr contiguous_iterator operator++(int) noexcept {
    contiguous_iterator tmp = *this;
    ++(*this);
    return tmp;
  }
  constexpr contiguous_iterator &operator-=(difference_type n) noexcept { return *this += -n; }
  [[nodiscard]] constexpr contiguous_iterator operator-(difference_type n) const noexcept { return *this + (-n); }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr pointer unsafe_current() const noexcept { return current_; }
  [[nodiscard]] constexpr const BoundsPolicy &policy() const noexcept { return policy_; }

private:
  pointer current_{nullptr};
  BoundsPolicy policy_{};
};

template <typename T, typename P>
[[nodiscard]] constexpr contiguous_iterator<T, P> operator+(typename contiguous_iterator<T, P>::difference_type n,
                                                            const contiguous_iterator<T, P> &it) noexcept {
  return it + n;
}

// ========================================================================
// Macros for Injecting Iterators into Containers
// ========================================================================

// ========================================================================
// Macros for Injecting Iterators into Containers
// ========================================================================

#define RELOCO_GENERATE_CONTIGUOUS_ITERATOR_BOILERPLATE()                                                              \
  [[nodiscard]] constexpr const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return begin(); }            \
  [[nodiscard]] constexpr const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return end(); }                \
  [[nodiscard]] constexpr reverse_iterator rbegin() & noexcept RELOCO_LIFETIMEBOUND {                                  \
    return reverse_iterator(end());                                                                                    \
  }                                                                                                                    \
  [[nodiscard]] constexpr reverse_iterator rend() & noexcept RELOCO_LIFETIMEBOUND {                                    \
    return reverse_iterator(begin());                                                                                  \
  }                                                                                                                    \
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const & noexcept RELOCO_LIFETIMEBOUND {                      \
    return const_reverse_iterator(end());                                                                              \
  }                                                                                                                    \
  [[nodiscard]] constexpr const_reverse_iterator rend() const & noexcept RELOCO_LIFETIMEBOUND {                        \
    return const_reverse_iterator(begin());                                                                            \
  }                                                                                                                    \
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const & noexcept RELOCO_LIFETIMEBOUND { return rbegin(); }  \
  [[nodiscard]] constexpr const_reverse_iterator crend() const & noexcept RELOCO_LIFETIMEBOUND { return rend(); }

#define RELOCO_GENERATE_ITER()                                                                                         \
  [[nodiscard]] auto iter() & noexcept RELOCO_LIFETIMEBOUND { return reloco::iter(*this); }                            \
  [[nodiscard]] auto iter() const & noexcept RELOCO_LIFETIMEBOUND { return reloco::iter(*this); }                      \
  auto iter() && noexcept = delete;                                                                                    \
  auto iter() const && noexcept = delete;

/**
 * @brief Injects iterators for OWNING static-capacity containers (e.g., array).
 * Propagates constness: `const container` yields `const_iterator`.
 */
#define RELOCO_GENERATE_STATIC_OWNING_ITERATORS(ElementType, DataExpr, SizeExpr)                                       \
  using iterator = contiguous_iterator<ElementType, static_bounds_policy<ElementType>>;                                \
  using const_iterator =                                                                                               \
      contiguous_iterator<std::add_const_t<ElementType>, static_bounds_policy<std::add_const_t<ElementType>>>;         \
  using reverse_iterator = std::reverse_iterator<iterator>;                                                            \
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;                                                \
  [[nodiscard]] constexpr iterator begin() & noexcept RELOCO_LIFETIMEBOUND {                                           \
    return iterator((DataExpr), static_bounds_policy<ElementType>((DataExpr), (DataExpr) + (SizeExpr)));               \
  }                                                                                                                    \
  [[nodiscard]] constexpr iterator end() & noexcept RELOCO_LIFETIMEBOUND {                                             \
    return iterator((DataExpr) + (SizeExpr), static_bounds_policy<ElementType>((DataExpr), (DataExpr) + (SizeExpr)));  \
  }                                                                                                                    \
  [[nodiscard]] constexpr const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND {                               \
    return const_iterator((DataExpr),                                                                                  \
                          static_bounds_policy<std::add_const_t<ElementType>>((DataExpr), (DataExpr) + (SizeExpr)));   \
  }                                                                                                                    \
  [[nodiscard]] constexpr const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND {                                 \
    return const_iterator((DataExpr) + (SizeExpr),                                                                     \
                          static_bounds_policy<std::add_const_t<ElementType>>((DataExpr), (DataExpr) + (SizeExpr)));   \
  }                                                                                                                    \
  RELOCO_GENERATE_CONTIGUOUS_ITERATOR_BOILERPLATE()

/**
 * @brief Injects iterators for NON-OWNING views (e.g., span).
 * Does NOT propagate constness: `const span<T>` still yields a mutable `iterator`
 * (unless T itself is const). `cbegin()` handles the cast to `const_iterator`.
 */
#define RELOCO_GENERATE_VIEW_ITERATORS(ElementType, DataExpr, SizeExpr)                                                \
  using iterator = contiguous_iterator<ElementType, static_bounds_policy<ElementType>>;                                \
  using const_iterator =                                                                                               \
      contiguous_iterator<std::add_const_t<ElementType>, static_bounds_policy<std::add_const_t<ElementType>>>;         \
  using reverse_iterator = std::reverse_iterator<iterator>;                                                            \
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;                                                \
  [[nodiscard]] constexpr iterator begin() & noexcept RELOCO_LIFETIMEBOUND {                                           \
    return iterator((DataExpr), static_bounds_policy<ElementType>((DataExpr), (DataExpr) + (SizeExpr)));               \
  }                                                                                                                    \
  [[nodiscard]] constexpr iterator end() & noexcept RELOCO_LIFETIMEBOUND {                                             \
    return iterator((DataExpr) + (SizeExpr), static_bounds_policy<ElementType>((DataExpr), (DataExpr) + (SizeExpr)));  \
  }                                                                                                                    \
  [[nodiscard]] constexpr iterator begin() const & noexcept RELOCO_LIFETIMEBOUND {                                     \
    return iterator((DataExpr), static_bounds_policy<ElementType>((DataExpr), (DataExpr) + (SizeExpr)));               \
  }                                                                                                                    \
  [[nodiscard]] constexpr iterator end() const & noexcept RELOCO_LIFETIMEBOUND {                                       \
    return iterator((DataExpr) + (SizeExpr), static_bounds_policy<ElementType>((DataExpr), (DataExpr) + (SizeExpr)));  \
  }                                                                                                                    \
  RELOCO_GENERATE_CONTIGUOUS_ITERATOR_BOILERPLATE()

/**
 * @brief Injects bounds-checked iterators for dynamically reallocating containers.
 * @param ElementType The type of the element (e.g., T).
 * @param ContainerType The type of the container used for live-checking (e.g., typed_vector_base).
 * @param DataExpr The expression yielding the raw pointer (e.g., static_cast<T*>(this->data_)).
 * @param SizeExpr The expression yielding the size (e.g., this->size_ or Base::size_).
 */
#define RELOCO_GENERATE_DYNAMIC_CONTIGUOUS_ITERATORS(ElementType, ContainerType, DataExpr, SizeExpr)                   \
  using iterator = contiguous_iterator<ElementType, dynamic_bounds_policy<ContainerType, ElementType>>;                \
  using const_iterator = contiguous_iterator<std::add_const_t<ElementType>,                                            \
                                             dynamic_bounds_policy<ContainerType, std::add_const_t<ElementType>>>;     \
  using reverse_iterator = std::reverse_iterator<iterator>;                                                            \
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;                                                \
  [[nodiscard]] constexpr iterator begin() & noexcept RELOCO_LIFETIMEBOUND {                                           \
    return iterator((DataExpr), dynamic_bounds_policy<ContainerType, ElementType>(this));                              \
  }                                                                                                                    \
  [[nodiscard]] constexpr iterator end() & noexcept RELOCO_LIFETIMEBOUND {                                             \
    return iterator((DataExpr) + (SizeExpr), dynamic_bounds_policy<ContainerType, ElementType>(this));                 \
  }                                                                                                                    \
  [[nodiscard]] constexpr const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND {                               \
    return const_iterator((DataExpr), dynamic_bounds_policy<ContainerType, std::add_const_t<ElementType>>(this));      \
  }                                                                                                                    \
  [[nodiscard]] constexpr const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND {                                 \
    return const_iterator((DataExpr) + (SizeExpr),                                                                     \
                          dynamic_bounds_policy<ContainerType, std::add_const_t<ElementType>>(this));                  \
  }                                                                                                                    \
  RELOCO_GENERATE_CONTIGUOUS_ITERATOR_BOILERPLATE()

/**
 * @brief Injects bounds-checked iterators for strictly zero-sized/empty containers.
 * Safely bypasses `nullptr + 0` arithmetic, which is Undefined Behavior in C++.
 * @param ElementType The type of the element (e.g., T).
 */
#define RELOCO_GENERATE_EMPTY_CONTIGUOUS_ITERATORS(ElementType)                                                        \
  using iterator = contiguous_iterator<ElementType, static_bounds_policy<ElementType>>;                                \
  using const_iterator =                                                                                               \
      contiguous_iterator<std::add_const_t<ElementType>, static_bounds_policy<std::add_const_t<ElementType>>>;         \
  using reverse_iterator = std::reverse_iterator<iterator>;                                                            \
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;                                                \
  [[nodiscard]] constexpr iterator begin() & noexcept RELOCO_LIFETIMEBOUND {                                           \
    return iterator(nullptr, static_bounds_policy<ElementType>(nullptr, nullptr));                                     \
  }                                                                                                                    \
  [[nodiscard]] constexpr iterator end() & noexcept RELOCO_LIFETIMEBOUND {                                             \
    return iterator(nullptr, static_bounds_policy<ElementType>(nullptr, nullptr));                                     \
  }                                                                                                                    \
  [[nodiscard]] constexpr const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND {                               \
    return const_iterator(nullptr, static_bounds_policy<std::add_const_t<ElementType>>(nullptr, nullptr));             \
  }                                                                                                                    \
  [[nodiscard]] constexpr const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND {                                 \
    return const_iterator(nullptr, static_bounds_policy<std::add_const_t<ElementType>>(nullptr, nullptr));             \
  }                                                                                                                    \
  RELOCO_GENERATE_CONTIGUOUS_ITERATOR_BOILERPLATE()

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE