// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file array.hpp @brief Hardened fixed-size C++17-compatible array. */

#include "alignment.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "rvalue_safety.hpp"
#include "span.hpp"
#include <cstddef>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

// The built-in array operations below are bounds-checked before indexing.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T, std::size_t N> struct RELOCO_OWNER array {
  static_assert(N > 0, "use the zero-size array specialization");

  alignas(effective_alignment_v<T>) T data_[N];

  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer = T *;
  using const_pointer = const T *;
  using reference = T &;
  using const_reference = const T &;
  using iterator = T *;
  using const_iterator = const T *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  [[nodiscard]] result<std::reference_wrapper<T>> try_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= N)
      return unexpected(error::out_of_bounds);
    return std::ref(data_[index]);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= N)
      return unexpected(error::out_of_bounds);
    return std::cref(data_[index]);
  }

  [[nodiscard]] constexpr T &operator[](size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < N, "array index out of bounds");
    return data_[index];
  }

  [[nodiscard]] constexpr const T &operator[](size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < N, "array index out of bounds");
    return data_[index];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &unsafe_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < N, "array index out of bounds");
    return data_[index];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const T &
  unsafe_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < N, "array index out of bounds");
    return data_[index];
  }

  [[nodiscard]] constexpr T &front() & noexcept RELOCO_LIFETIMEBOUND { return data_[0]; }
  [[nodiscard]] constexpr const T &front() const & noexcept RELOCO_LIFETIMEBOUND { return data_[0]; }
  [[nodiscard]] constexpr T &back() & noexcept RELOCO_LIFETIMEBOUND { return data_[N - 1]; }
  [[nodiscard]] constexpr const T &back() const & noexcept RELOCO_LIFETIMEBOUND { return data_[N - 1]; }

  [[nodiscard]] result<std::reference_wrapper<T>> try_front() & noexcept RELOCO_LIFETIMEBOUND {
    return std::ref(data_[0]);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_front() const & noexcept RELOCO_LIFETIMEBOUND {
    return std::cref(data_[0]);
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_back() & noexcept RELOCO_LIFETIMEBOUND {
    return std::ref(data_[N - 1]);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_back() const & noexcept RELOCO_LIFETIMEBOUND {
    return std::cref(data_[N - 1]);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &unsafe_front() & noexcept RELOCO_LIFETIMEBOUND {
    return data_[0];
  }
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const T &unsafe_front() const & noexcept RELOCO_LIFETIMEBOUND {
    return data_[0];
  }
  T &unsafe_front() && = delete;
  const T &unsafe_front() const && = delete;

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &unsafe_back() & noexcept RELOCO_LIFETIMEBOUND {
    return data_[N - 1];
  }
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const T &unsafe_back() const & noexcept RELOCO_LIFETIMEBOUND {
    return data_[N - 1];
  }
  T &unsafe_back() && = delete;
  const T &unsafe_back() const && = delete;

  [[nodiscard]] constexpr span<T> as_span() & noexcept RELOCO_LIFETIMEBOUND { return span<T>(data_, N); }

  [[nodiscard]] constexpr span<const T> as_span() const & noexcept RELOCO_LIFETIMEBOUND {
    return span<const T>(data_, N);
  }

  [[nodiscard]] static constexpr size_type size() noexcept { return N; }
  [[nodiscard]] static constexpr bool empty() noexcept { return false; }

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) constexpr T *data() & noexcept RELOCO_LIFETIMEBOUND {
    return data_;
  }
  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) constexpr const T *data() const & noexcept
      RELOCO_LIFETIMEBOUND {
    return data_;
  }

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) constexpr iterator
      begin() & noexcept RELOCO_LIFETIMEBOUND {
    return data_;
  }
  [[nodiscard]] constexpr iterator end() & noexcept RELOCO_LIFETIMEBOUND { return data_ + N; }
  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) constexpr const_iterator
      begin() const & noexcept RELOCO_LIFETIMEBOUND {
    return data_;
  }
  [[nodiscard]] constexpr const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return data_ + N; }
  [[nodiscard]] constexpr const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return data_; }
  [[nodiscard]] constexpr const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return data_ + N; }
  [[nodiscard]] constexpr reverse_iterator rbegin() & noexcept RELOCO_LIFETIMEBOUND { return reverse_iterator(end()); }
  [[nodiscard]] constexpr reverse_iterator rend() & noexcept RELOCO_LIFETIMEBOUND { return reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(end());
  }
  [[nodiscard]] constexpr const_reverse_iterator rend() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(begin());
  }
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(cend());
  }
  [[nodiscard]] constexpr const_reverse_iterator crend() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(cbegin());
  }

  constexpr void fill(const T &value) noexcept {
    for (size_type i = 0; i < N; ++i)
      data_[i] = value;
  }

  constexpr void swap(array &other) noexcept {
    for (size_type i = 0; i < N; ++i) {
      using std::swap;
      swap(data_[i], other.data_[i]);
    }
  }

  template <std::size_t Offset, std::size_t Count>
  [[nodiscard]] constexpr span<T> static_subspan() & noexcept RELOCO_LIFETIMEBOUND {
    static_assert(Offset <= N, "static subspan offset exceeds array bounds");
    static_assert(Count <= N - Offset, "static subspan count exceeds array bounds");
    return span<T>(data_ + Offset, Count);
  }

  template <std::size_t Offset, std::size_t Count>
  [[nodiscard]] constexpr span<const T> static_subspan() const & noexcept RELOCO_LIFETIMEBOUND {
    static_assert(Offset <= N, "static subspan offset exceeds array bounds");
    static_assert(Count <= N - Offset, "static subspan count exceeds array bounds");
    return span<const T>(data_ + Offset, Count);
  }

  template <typename F> [[nodiscard]] constexpr auto map(F &&func) const & noexcept {
    using return_type = std::decay_t<decltype(func(data_[0]))>;
    array<return_type, N> result{};
    for (size_type i = 0; i < N; ++i)
      result.data_[i] = func(data_[i]);
    return result;
  }

  [[nodiscard]] friend constexpr bool operator==(const array &lhs, const array &rhs) noexcept {
    for (size_type i = 0; i < N; ++i) {
      if (lhs.data_[i] != rhs.data_[i])
        return false;
    }
    return true;
  }

  [[nodiscard]] friend constexpr bool operator!=(const array &lhs, const array &rhs) noexcept { return !(lhs == rhs); }

  [[nodiscard]] friend constexpr bool operator<(const array &lhs, const array &rhs) noexcept {
    for (size_type i = 0; i < N; ++i) {
      if (lhs.data_[i] < rhs.data_[i])
        return true;
      if (rhs.data_[i] < lhs.data_[i])
        return false;
    }
    return false;
  }

  [[nodiscard]] friend constexpr bool operator>(const array &lhs, const array &rhs) noexcept { return rhs < lhs; }

  [[nodiscard]] friend constexpr bool operator<=(const array &lhs, const array &rhs) noexcept { return !(rhs < lhs); }

  [[nodiscard]] friend constexpr bool operator>=(const array &lhs, const array &rhs) noexcept { return !(lhs < rhs); }
};

template <typename T> struct array<T, 0> {
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer = T *;
  using const_pointer = const T *;
  using reference = T &;
  using const_reference = const T &;
  using iterator = T *;
  using const_iterator = const T *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  [[nodiscard]] result<std::reference_wrapper<T>> try_at(size_type) & noexcept RELOCO_LIFETIMEBOUND {
    return unexpected(error::out_of_bounds);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_at(size_type) const & noexcept RELOCO_LIFETIMEBOUND {
    return unexpected(error::out_of_bounds);
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_front() & noexcept RELOCO_LIFETIMEBOUND {
    return unexpected(error::container_empty);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_front() const & noexcept RELOCO_LIFETIMEBOUND {
    return unexpected(error::container_empty);
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_back() & noexcept RELOCO_LIFETIMEBOUND {
    return unexpected(error::container_empty);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_back() const & noexcept RELOCO_LIFETIMEBOUND {
    return unexpected(error::container_empty);
  }

  [[nodiscard]] constexpr T &operator[](size_type) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(false, "array index out of bounds");
    return *static_cast<T *>(nullptr);
  }

  [[nodiscard]] constexpr const T &operator[](size_type) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(false, "array index out of bounds");
    return *static_cast<const T *>(nullptr);
  }

  [[nodiscard]] constexpr T &front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(false, "front() called on empty array");
    return *static_cast<T *>(nullptr);
  }

  [[nodiscard]] constexpr const T &front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(false, "front() called on empty array");
    return *static_cast<const T *>(nullptr);
  }

  [[nodiscard]] constexpr T &back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(false, "back() called on empty array");
    return *static_cast<T *>(nullptr);
  }

  [[nodiscard]] constexpr const T &back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(false, "back() called on empty array");
    return *static_cast<const T *>(nullptr);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &unsafe_at(size_type) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(false, "array index out of bounds");
    return *static_cast<T *>(nullptr);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const T &
  unsafe_at(size_type) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(false, "array index out of bounds");
    return *static_cast<const T *>(nullptr);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &unsafe_front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(false, "front() called on empty array");
    return *static_cast<T *>(nullptr);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const T &unsafe_front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(false, "front() called on empty array");
    return *static_cast<const T *>(nullptr);
  }
  T &unsafe_front() && = delete;
  const T &unsafe_front() const && = delete;

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &unsafe_back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(false, "back() called on empty array");
    return *static_cast<T *>(nullptr);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const T &unsafe_back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(false, "back() called on empty array");
    return *static_cast<const T *>(nullptr);
  }
  T &unsafe_back() && = delete;
  const T &unsafe_back() const && = delete;

  [[nodiscard]] constexpr span<T> as_span() & noexcept RELOCO_LIFETIMEBOUND { return {}; }
  [[nodiscard]] constexpr span<const T> as_span() const & noexcept RELOCO_LIFETIMEBOUND { return {}; }

  [[nodiscard]] static constexpr size_type size() noexcept { return 0; }
  [[nodiscard]] static constexpr bool empty() noexcept { return true; }

  [[nodiscard]] constexpr T *data() & noexcept RELOCO_LIFETIMEBOUND { return nullptr; }
  [[nodiscard]] constexpr const T *data() const & noexcept RELOCO_LIFETIMEBOUND { return nullptr; }
  [[nodiscard]] constexpr iterator begin() & noexcept RELOCO_LIFETIMEBOUND { return nullptr; }
  [[nodiscard]] constexpr iterator end() & noexcept RELOCO_LIFETIMEBOUND { return nullptr; }
  [[nodiscard]] constexpr const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND { return nullptr; }
  [[nodiscard]] constexpr const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return nullptr; }
  [[nodiscard]] constexpr const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return nullptr; }
  [[nodiscard]] constexpr const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return nullptr; }
  [[nodiscard]] constexpr reverse_iterator rbegin() & noexcept RELOCO_LIFETIMEBOUND { return reverse_iterator(end()); }
  [[nodiscard]] constexpr reverse_iterator rend() & noexcept RELOCO_LIFETIMEBOUND { return reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(end());
  }
  [[nodiscard]] constexpr const_reverse_iterator rend() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(begin());
  }
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(cend());
  }
  [[nodiscard]] constexpr const_reverse_iterator crend() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(cbegin());
  }

  constexpr void fill(const T &) noexcept {}
  constexpr void swap(array &) noexcept {}

  template <std::size_t Offset, std::size_t Count> [[nodiscard]] constexpr span<T> static_subspan() & noexcept {
    static_assert(Offset == 0 && Count == 0, "static subspan exceeds array bounds");
    return {};
  }

  template <std::size_t Offset, std::size_t Count>
  [[nodiscard]] constexpr span<const T> static_subspan() const & noexcept {
    static_assert(Offset == 0 && Count == 0, "static subspan exceeds array bounds");
    return {};
  }

  template <typename F> [[nodiscard]] constexpr auto map(F &&) const & noexcept {
    using return_type = std::decay_t<decltype(std::declval<F &>()(std::declval<const T &>()))>;
    return array<return_type, 0>{};
  }

  [[nodiscard]] friend constexpr bool operator==(const array &, const array &) noexcept { return true; }
  [[nodiscard]] friend constexpr bool operator!=(const array &, const array &) noexcept { return false; }
  [[nodiscard]] friend constexpr bool operator<(const array &, const array &) noexcept { return false; }
  [[nodiscard]] friend constexpr bool operator>(const array &, const array &) noexcept { return false; }
  [[nodiscard]] friend constexpr bool operator<=(const array &, const array &) noexcept { return true; }
  [[nodiscard]] friend constexpr bool operator>=(const array &, const array &) noexcept { return true; }
};

template <typename T, std::size_t N>
[[nodiscard]] constexpr array<std::remove_cv_t<T>, N> to_array(T (&source)[N]) noexcept {
  array<std::remove_cv_t<T>, N> result{};
  for (std::size_t i = 0; i < N; ++i)
    result[i] = source[i];
  return result;
}

template <typename T, typename... U, std::enable_if_t<(std::is_same_v<T, U> && ...), int> = 0>
array(T, U...) -> array<T, 1 + sizeof...(U)>;

template <std::size_t I, typename T, std::size_t N>
[[nodiscard]] constexpr T &get(array<T, N> &value RELOCO_LIFETIMEBOUND) noexcept {
  static_assert(I < N, "array index out of bounds");
  return value.data_[I];
}

template <std::size_t I, typename T, std::size_t N>
[[nodiscard]] constexpr const T &get(const array<T, N> &value RELOCO_LIFETIMEBOUND) noexcept {
  static_assert(I < N, "array index out of bounds");
  return value.data_[I];
}

template <std::size_t I, typename T, std::size_t N> T &&get(array<T, N> &&) noexcept = delete;

} // namespace reloco

namespace std {

template <typename T, std::size_t N> struct tuple_size<reloco::array<T, N>> : std::integral_constant<std::size_t, N> {};

template <std::size_t I, typename T, std::size_t N> struct tuple_element<I, reloco::array<T, N>> {
  static_assert(I < N, "array index out of bounds");
  using type = T;
};

} // namespace std

RELOCO_END_UNSAFE_BUFFER_USAGE
