// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/view/stride.hpp
 * @brief `strides<...>`: per-dimension element strides, each either a runtime value or a compile-time constant.
 *
 * A stride is the distance, in elements, between two neighbours along one dimension. Most kernels have a runtime row
 * stride (padded rows) and a compile-time unit stride along the row, so the unit stride costs no storage and folds to
 * the constant 1 in the generated code.
 *
 * See `docs/kernel/views.md`.
 */

#include "../../array.hpp"
#include "../../detail/assert.hpp"
#include "../config.hpp"

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

RELOCO_KERNEL_BEGIN

/** @brief Marks a dimension whose stride is chosen at run time. */
struct dynamic_stride {};

/** @brief Marks a dimension whose stride is the compile-time constant `N` (stores nothing). */
template <std::size_t N> struct static_stride {};

RELOCO_KERNEL_END

namespace reloco::kernel::detail {

/** @brief The I-th type of a pack (avoids pulling in `<tuple>`). */
template <std::size_t I, class First, class... Rest> struct type_at {
  using type = typename type_at<I - 1, Rest...>::type;
};
template <class First, class... Rest> struct type_at<0, First, Rest...> {
  using type = First;
};

template <class S> inline constexpr bool is_dynamic_stride_v = std::is_same_v<S, dynamic_stride>;

template <class S> inline constexpr bool is_stride_marker_v = false;
template <> inline constexpr bool is_stride_marker_v<dynamic_stride> = true;
template <std::size_t N> inline constexpr bool is_stride_marker_v<static_stride<N>> = true;

/** @brief Number of dynamic markers among `S[0..I)`: where the I-th stride's runtime value sits in the input list. */
template <std::size_t I, class... S> [[nodiscard]] consteval std::size_t dynamic_before() noexcept {
  std::size_t k = 0;
  std::size_t count = 0;
  ((count += (k++ < I && is_dynamic_stride_v<S>) ? std::size_t{1} : std::size_t{0}), ...);
  return count;
}

/** @brief Storage of one stride; the static specialization is empty so `strides` pays nothing for it. */
template <std::size_t I, class S> class stride_leaf;

template <std::size_t I> class stride_leaf<I, dynamic_stride> {
public:
  constexpr stride_leaf() noexcept = default;
  constexpr explicit stride_leaf(std::size_t value) noexcept : value_(value) {}
  [[nodiscard]] constexpr std::size_t get() const noexcept { return value_; }

private:
  std::size_t value_ = 0;
};

template <std::size_t I, std::size_t N> class stride_leaf<I, static_stride<N>> {
public:
  constexpr stride_leaf() noexcept = default;
  constexpr explicit stride_leaf(std::size_t) noexcept {}
  [[nodiscard]] static constexpr std::size_t get() noexcept { return N; }
};

template <class Seq, class... S> class stride_base;

template <std::size_t... Is, class... S>
class stride_base<std::index_sequence<Is...>, S...> : public stride_leaf<Is, S>... {
public:
  constexpr stride_base() noexcept = default;

  template <std::size_t K>
  constexpr explicit stride_base(const reloco::array<std::size_t, K> &dynamic_values) noexcept
      : stride_leaf<Is, S>(leaf_value<Is, S, K>(dynamic_values))... {}

private:
  template <std::size_t I, class Marker, std::size_t K>
  [[nodiscard]] static constexpr std::size_t leaf_value(const reloco::array<std::size_t, K> &values) noexcept {
    if constexpr (is_dynamic_stride_v<Marker>)
      return values[dynamic_before<I, S...>()];
    else
      return 0;
  }
};

} // namespace reloco::kernel::detail

RELOCO_KERNEL_BEGIN

/**
 * @brief The strides of a tensor, one marker per dimension.
 * @tparam S `dynamic_stride` or `static_stride<N>` for each dimension, outermost first.
 * @details Construct it with one runtime value per `dynamic_stride`, in order; `static_stride` dimensions take no
 * value. `strides<dynamic_stride, static_stride<1>>(row_stride)` is the padded-row-major layout.
 */
template <class... S> class strides : private detail::stride_base<std::index_sequence_for<S...>, S...> {
  static_assert(sizeof...(S) >= 1, "strides needs at least one dimension");
  static_assert((detail::is_stride_marker_v<S> && ...), "strides<...> takes dynamic_stride / static_stride<N> only");

  using base = detail::stride_base<std::index_sequence_for<S...>, S...>;

public:
  /** @brief Number of dimensions. */
  static constexpr std::size_t rank = sizeof...(S);
  /** @brief Number of strides that are runtime values (the number of constructor arguments). */
  static constexpr std::size_t dynamic_count = (std::size_t{0} + ... + (detail::is_dynamic_stride_v<S> ? 1 : 0));

  constexpr strides() noexcept = default;

  /** @brief Takes exactly `dynamic_count` runtime strides, in dimension order. */
  template <std::integral... V>
    requires(sizeof...(V) == dynamic_count)
  constexpr strides(V... values) noexcept // NOLINT(reloco-explicit-conversion): needed for `{stride, 1}` braces
      : base(reloco::array<std::size_t, dynamic_count>{static_cast<std::size_t>(values)...}) {}

  /** @brief Takes the runtime strides as an array (one entry per `dynamic_stride`). */
  constexpr explicit strides(const reloco::array<std::size_t, dynamic_count> &values) noexcept : base(values) {}

  /** @brief The stride of dimension `D`, known at compile time to be in range. */
  template <std::size_t D> [[nodiscard]] constexpr std::size_t get() const noexcept {
    static_assert(D < rank, "stride dimension out of range");
    return static_cast<const detail::stride_leaf<D, nth<D>> &>(*this).get();
  }

  /** @brief The stride of dimension `d`; asserts `d < rank`. */
  RELOCO_CHECKED [[nodiscard]] constexpr std::size_t operator[](std::size_t d) const noexcept {
    RELOCO_ASSERT(d < rank, "strides: dimension out of range");
    return select(d, std::make_index_sequence<rank>{});
  }

private:
  template <std::size_t D> using nth = typename detail::type_at<D, S...>::type;

  template <std::size_t... Is>
  [[nodiscard]] constexpr std::size_t select(std::size_t d, std::index_sequence<Is...>) const noexcept {
    std::size_t result = 0;
    ((d == Is ? (result = get<Is>(), 0) : 0), ...);
    return result;
  }
};

/** @brief `Rank` strides that are all runtime values. */
template <std::size_t Rank> struct dynamic_strides_for {
  template <std::size_t... Is>
  static auto make(std::index_sequence<Is...>) noexcept -> strides<decltype((void(Is), dynamic_stride{}))...>;
  using type = decltype(make(std::make_index_sequence<Rank>{}));
};

template <std::size_t Rank> using dynamic_strides = typename dynamic_strides_for<Rank>::type;

RELOCO_KERNEL_END
