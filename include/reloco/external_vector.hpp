// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "rvalue_safety.hpp"
#include "span.hpp"
#include <functional>
#include <iterator>
#include <new>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief A bounded, fallible vector that operates on externally managed memory.
 * Caller is responsible for managing lifetime of elements (e.g. calling destructors),
 * which is done automatically by standard containers like array, or event plain arrays
 */
template <typename T> class RELOCO_POINTER external_vector {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T &;
  using const_reference = const T &;
  using pointer = T *;
  using const_pointer = const T *;

  RELOCO_BLOCK_RVALUE_ACCESS(T);
  RELOCO_GENERATE_VIEW_ITERATORS(T, data_.unsafe_data(), size_)
  RELOCO_GENERATE_ITER()

  /**
   * @brief Binds the vector to an uninitialized memory buffer.
   */
  constexpr explicit external_vector(span<T> data RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept : data_(data) {}

  // Delete copy semantics to prevent unsafe constructs
  external_vector(const external_vector &) = delete;
  external_vector &operator=(const external_vector &) = delete;

  // Allow move semantics to transfer ownership of the active elements
  constexpr external_vector(external_vector &&other) noexcept : data_(other.data_), size_(other.size_) {
    other.size_ = 0;
    other.data_ = {};
  }

  // ========================================================================
  // Fallible Mutation API
  // ========================================================================

  [[nodiscard]] result<void> try_push_back(const T &value) & noexcept {
    if (size_ >= capacity())
      return unexpected(error::capacity_exceeded);
    data_.unsafe_at(size_) = value;
    size_++;
    return {};
  }

  [[nodiscard]] result<void> try_push_back(T &&value) & noexcept {
    if (size_ >= capacity())
      return unexpected(error::capacity_exceeded);
    data_.unsafe_at(size_) = std::move(value);
    size_++;
    return {};
  }

  template <typename... Args>
  [[nodiscard]] result<T *> try_emplace_back(Args &&...args) & noexcept RELOCO_LIFETIMEBOUND {
    if (size_ >= capacity())
      return unexpected(error::capacity_exceeded);

    // Re-construct element in place
    data_.unsafe_at(size_).~T();
    T *ptr = new (&data_.unsafe_at(size_)) T(std::forward<Args>(args)...);
    size_++;
    return ptr;
  }

  [[nodiscard]] result<void> try_pop_back() & noexcept {
    if (empty())
      return unexpected(error::container_empty);
    size_--;
    return {};
  }

  // ========================================================================
  // State Management
  // ========================================================================

  RELOCO_REINITIALIZES void clear() & noexcept { size_ = 0; }

  constexpr size_type size() const & noexcept { return size_; }
  constexpr size_type capacity() const & noexcept { return data_.size(); }
  constexpr bool empty() const & noexcept { return size_ == 0; }
  constexpr bool full() const & noexcept { return size_ == data_.size(); }

  // ========================================================================
  // Tri-tier accessors (Checked, Fallible, Unsafe)
  // ========================================================================

  // ---- checked tier: RELOCO_ASSERT, active even when NDEBUG is defined ----

  [[nodiscard]] T &operator[](size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < size_, "external_vector index out of bounds");
    return data_.unsafe_at(index);
  }
  [[nodiscard]] const T &operator[](size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < size_, "external_vector index out of bounds");
    return data_.unsafe_at(index);
  }

  [[nodiscard]] T &front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "external_vector is empty");
    return data_.unsafe_at(0);
  }
  [[nodiscard]] const T &front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "external_vector is empty");
    return data_.unsafe_at(0);
  }

  [[nodiscard]] T &back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "external_vector is empty");
    return data_.unsafe_at(size_ - 1);
  }
  [[nodiscard]] const T &back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "external_vector is empty");
    return data_.unsafe_at(size_ - 1);
  }

  [[nodiscard]] T *data() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "external_vector is empty");
    return data_.unsafe_data();
  }
  [[nodiscard]] const T *data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "external_vector is empty");
    return data_.unsafe_data();
  }

  // ---- fallible tier: reloco::result instead of trapping ----

  [[nodiscard]] result<std::reference_wrapper<T>> try_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= size_)
      return unexpected(error::out_of_bounds);
    return std::ref(data_.unsafe_at(index));
  }
  [[nodiscard]] result<std::reference_wrapper<const T>> try_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= size_)
      return unexpected(error::out_of_bounds);
    return std::cref(data_.unsafe_at(index));
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_front() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::ref(data_.unsafe_at(0));
  }
  [[nodiscard]] result<std::reference_wrapper<const T>> try_front() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::cref(data_.unsafe_at(0));
  }

  [[nodiscard]] result<T *> try_data() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return data_.unsafe_data();
  }
  [[nodiscard]] result<const T *> try_data() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return data_.unsafe_data();
  }

  // ---- explicitly-unsafe tier: RELOCO_UNSAFE_BUFFER_USAGE, RELOCO_DEBUG_ASSERT only ----

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < size_, "external_vector index out of bounds");
    return data_.unsafe_at(index);
  }
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < size_, "external_vector index out of bounds");
    return data_.unsafe_at(index);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T *unsafe_data() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "external_vector has no data");
    return data_.unsafe_data();
  }
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T *unsafe_data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "external_vector has no data");
    return data_.unsafe_data();
  }

private:
  span<T> data_;
  size_type size_{0};
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE