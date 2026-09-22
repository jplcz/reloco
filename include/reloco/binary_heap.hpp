// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file binary_heap.hpp
 * @brief Allocator-backed priority queue, matching Rust's
 * `std::collections::BinaryHeap<T>`.
 *
 * `binary_heap<T, Compare>` is a thin wrapper around `vector<T>` (see
 * `vector.hpp`) maintaining the standard binary max-heap invariant via
 * `<algorithm>`'s `push_heap`/`pop_heap`/`make_heap`/`sort_heap`, exactly
 * like `std::priority_queue<T, std::vector<T>, Compare>` but with reloco's
 * fallible/tri-tier access conventions layered on top. With the default
 * `Compare = std::less<T>`, `try_pop()`/`peek()` always return the
 * greatest element first (a max-heap), matching both `std::priority_queue`
 * and Rust's `BinaryHeap` defaults; pass `std::greater<T>` for a min-heap.
 *
 * Every fallible entry point returns `reloco::result<T>` (see `error.hpp`).
 */

#include "error.hpp"
#include "lifetime.hpp"
#include "vector.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace reloco {

/**
 * @brief Allocator-backed binary max-heap (min-heap with `Compare =
 * std::greater<T>`).
 *
 * Iteration (`begin()`/`end()`) walks the elements in unspecified heap
 * order, not sorted order -- matching Rust's `BinaryHeap::iter()` -- and is
 * `const`-only since mutating an element in place could silently break the
 * heap invariant. Use `try_pop()` repeatedly, or `into_sorted_vec()`, to
 * obtain elements in priority order.
 */
template <typename T, typename Compare = std::less<T>> class RELOCO_OWNER binary_heap {
public:
  using value_type = T;
  using size_type = typename vector<T>::size_type;
  using const_iterator = typename vector<T>::const_iterator;

  constexpr binary_heap() noexcept = default;

  [[nodiscard]] static result<binary_heap> try_allocate(allocator_ref alloc, size_type initial_cap = 0) noexcept {
    auto vec_res = vector<T>::try_allocate(alloc, initial_cap);
    if (!vec_res)
      return unexpected(vec_res.error());
    return binary_heap(std::move(*vec_res));
  }

  [[nodiscard]] static result<binary_heap> try_create(size_type initial_cap = 0) noexcept {
    return try_allocate(default_allocator(), initial_cap);
  }

  /**
   * @brief Performs a deep copy of the heap using a specific allocator.
   */
  [[nodiscard]] result<binary_heap> try_clone(allocator_ref alloc) const noexcept {
    auto vec_res = data_.try_clone(alloc);
    if (!vec_res)
      return unexpected(vec_res.error());
    return binary_heap(std::move(*vec_res));
  }

  [[nodiscard]] result<binary_heap> try_clone() const noexcept { return try_clone(data_.get_allocator()); }

  [[nodiscard]] constexpr size_type size() const noexcept { return data_.size(); }

  [[nodiscard]] constexpr bool empty() const noexcept { return data_.empty(); }

  [[nodiscard]] result<void> try_reserve(size_type new_cap) & noexcept { return data_.try_reserve(new_cap); }

  /**
   * @brief Rust `BinaryHeap::push` equivalent: inserts @p value and
   * restores the heap invariant.
   */
  [[nodiscard]] result<void> try_push(T value) & noexcept {
    auto res = data_.try_push_back(std::move(value));
    if (!res)
      return unexpected(res.error());
    std::push_heap(data_.begin(), data_.end(), comp_);
    return {};
  }

  /**
   * @brief Rust `BinaryHeap::pop` equivalent: removes and returns the
   * greatest element (by `Compare`), restoring the heap invariant. Fails
   * with `error::container_empty` if the heap is empty.
   */
  [[nodiscard]] result<T> try_pop() & noexcept {
    if (data_.empty())
      return unexpected(error::container_empty);
    std::pop_heap(data_.begin(), data_.end(), comp_);
    T value(std::move(data_.back()));
    // The heap is non-empty (checked above), so this can never fail.
    (void)data_.try_pop_back();
    return value;
  }

  /**
   * @brief Rust `BinaryHeap::peek` equivalent: checked-tier access to the
   * greatest element without removing it. `RELOCO_ASSERT`s on an empty
   * heap; see `try_peek()` for a fallible alternative.
   */
  [[nodiscard]] const T &peek() const & noexcept RELOCO_LIFETIMEBOUND { return data_.front(); }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_peek() const & noexcept RELOCO_LIFETIMEBOUND {
    return data_.try_front();
  }

  void clear() noexcept { data_.clear(); }

  /**
   * @brief Rust `BinaryHeap::into_sorted_vec` equivalent: consumes the heap
   * and returns its elements as a `vector<T>` in ascending order (by
   * `Compare`).
   */
  [[nodiscard]] vector<T> into_sorted_vec() && noexcept {
    std::sort_heap(data_.begin(), data_.end(), comp_);
    return std::move(data_);
  }

  [[nodiscard]] const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND { return data_.begin(); }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return data_.end(); }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return data_.cbegin(); }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return data_.cend(); }

private:
  explicit binary_heap(vector<T> &&v) noexcept : data_(std::move(v)) {
    std::make_heap(data_.begin(), data_.end(), comp_);
  }

  vector<T> data_;
  Compare comp_{};
};

/**
 * @brief `binary_heap<T, Compare>` only holds a `vector<T>` plus a
 * (typically empty) `Compare`, so it propagates `vector<T>`'s own
 * relocatability.
 */
template <typename T, typename Compare>
struct is_trivially_relocatable<binary_heap<T, Compare>> : is_trivially_relocatable<vector<T>> {};

} // namespace reloco
