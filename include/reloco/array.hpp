#pragma once
#include <array>
#include <reloco/span.hpp>
#include <utility>

namespace reloco {

template <typename T, std::size_t N> struct array {
  T data_[N]; // Fixed-size storage

  using value_type = T;
  using size_type = std::size_t;
  using iterator = T *;
  using const_iterator = const T *;

  [[nodiscard]] constexpr result<std::reference_wrapper<T>>
  try_at(size_type index) noexcept {
    if (index >= N) [[unlikely]] {
      return unexpected(error::out_of_bounds);
    }
    return std::ref(data_[index]);
  }

  constexpr T &operator[](size_type index) noexcept {
    RELOCO_ASSERT(index < N && "Array index out of bounds");
    return data_[index];
  }

  [[nodiscard]] constexpr T &unsafe_at(size_type index) noexcept {
    return data_[index];
  }

  [[nodiscard]] constexpr span<T> as_span() noexcept {
    return span<T>(data_, N);
  }

  [[nodiscard]] constexpr span<const T> as_span() const noexcept {
    return span<const T>(data_, N);
  }

  static constexpr size_type size() noexcept { return N; }
  constexpr T *data() noexcept { return data_; }
  constexpr iterator begin() noexcept { return data_; }
  constexpr iterator end() noexcept { return data_ + N; }

  constexpr void fill(const T &value) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
      data_[i] = value;
    }
  }

  constexpr void swap(array &other) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
      using std::swap;
      swap(data_[i], other.data_[i]);
    }
  }

  template <std::size_t Offset, std::size_t Count>
  [[nodiscard]] constexpr span<T, Count> static_subspan() noexcept {
    static_assert(Offset + Count <= N, "Static subspan exceeds array bounds");
    return span<T, Count>(data_ + Offset, Count);
  }

  template <typename F>
  [[nodiscard]] constexpr auto map(F &&func) const noexcept {
    using ReturnType = decltype(func(data_[0]));
    array<ReturnType, N> result;
    for (std::size_t i = 0; i < N; ++i) {
      result.data_[i] = func(data_[i]);
    }
    return result;
  }

  [[nodiscard]] friend constexpr bool operator==(const array &a,
                                                 const array &b) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
      if (a.data_[i] != b.data_[i])
        return false;
    }
    return true;
  }

  [[nodiscard]] friend constexpr auto operator<=>(const array &a,
                                                  const array &b) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
      if (auto cmp = a.data_[i] <=> b.data_[i]; cmp != 0)
        return cmp;
    }
    return std::strong_ordering::equal;
  }
};

template <typename T, std::size_t N>
[[nodiscard]] constexpr array<std::remove_cv_t<T>, N>to_array(T (&static_arr)[N]) noexcept {
  array<std::remove_cv_t<T>, N> result;
  for (std::size_t i = 0; i < N; ++i) {
    result[i] = static_arr[i];
  }
  return result;
}

template <typename T, typename... U>
array(T, U...) -> array<T, 1 + sizeof...(U)>;

} // namespace reloco

namespace std {
template <typename T, std::size_t N>
struct tuple_size<reloco::array<T, N>>
    : std::integral_constant<std::size_t, N> {};

template <std::size_t I, typename T, std::size_t N>
struct tuple_element<I, reloco::array<T, N>> {
  static_assert(I < N, "Index out of bounds for structured binding");
  using type = T;
};
} // namespace std

namespace reloco {
// 3. Provide the 'get' functions inside your namespace (found via ADL)
template <std::size_t I, typename T, std::size_t N>
constexpr T &get(array<T, N> &a) noexcept {
  static_assert(I < N, "Index out of bounds");
  return a.data_[I];
}

template <std::size_t I, typename T, std::size_t N>
constexpr const T &get(const array<T, N> &a) noexcept {
  static_assert(I < N, "Index out of bounds");
  return a.data_[I];
}

template <std::size_t I, typename T, std::size_t N>
constexpr T &&get(array<T, N> &&a) noexcept {
  static_assert(I < N, "Index out of bounds");
  return std::move(a.data_[I]);
}
} // namespace reloco
