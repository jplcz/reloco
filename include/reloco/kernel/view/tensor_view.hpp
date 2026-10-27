// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/view/tensor_view.hpp
 * @brief `tensor_view<T, Rank, Strides>`: a non-owning, validated, rank-N window onto contiguous storage.
 *
 * The only way to obtain a view over real storage is `try_make_tensor`, which runs one overflow-checked check:
 * *every element offset the shape and strides can produce lies inside the storage span*. After that, any index
 * tuple inside the shape is a valid access, which is the invariant the checked accessors (`at`, `try_at`) rely on and
 * the bind layer builds its check-free access on.
 *
 * See `docs/kernel/views.md`.
 */

#include "../../array.hpp"
#include "../../detail/assert.hpp"
#include "../../error.hpp"
#include "../../fault_injection.hpp"
#include "../../int_ops.hpp"
#include "../../lifetime.hpp"
#include "../../span.hpp"
#include "../config.hpp"
#include "../detail/offset.hpp"
#include "shape.hpp"
#include "stride.hpp"

#include <concepts>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace reloco::kernel {

/** @brief Fault points of `try_make_tensor` (see `docs/fault-injection.md`); each hook receives the value by reference.
 */
namespace fault {
/** @brief Hook receives the computed largest reachable offset (`std::size_t &`); raise it to force `out_of_bounds`. */
RELOCO_FAULT_TAG(tensor_max_offset);
/** @brief Hook receives the "strides are disjoint" verdict (`bool &`) of a mutable view; clear it to force overlap. */
RELOCO_FAULT_TAG(tensor_disjoint);
} // namespace fault

namespace detail {
struct tensor_access;
}

} // namespace reloco::kernel

RELOCO_KERNEL_BEGIN

/**
 * @brief Non-owning view of `Rank`-dimensional data inside a `span<T>`; copying it copies the handle, not the data.
 * @tparam T element type; `const T` makes a read-only view.
 * @tparam Rank number of dimensions, at least 1.
 * @tparam Strides a `strides<...>` type with `Rank` entries; the default is `Rank` runtime strides.
 * @details Like `span`, the view is a pointer-like handle: element references stay valid as long as the *storage*
 * lives, so they remain usable after a temporary view is destroyed, and no rvalue access needs blocking.
 */
template <class T, std::size_t Rank, class Strides = dynamic_strides<Rank>> class RELOCO_POINTER tensor_view {
  static_assert(Rank >= 1, "a tensor needs at least one dimension");
  static_assert(Strides::rank == Rank, "Strides must have one entry per dimension");

public:
  using element_type = T;
  using value_type = std::remove_const_t<T>;
  using reference = T &;
  using shape_type = kernel::shape<Rank>;
  using strides_type = Strides;
  static constexpr std::size_t rank = Rank;

  /** @brief An empty view (all extents zero) over no storage. */
  constexpr tensor_view() noexcept = default;

  /** @brief Converts a mutable view to a read-only one (`tensor_view<U>` to `tensor_view<const U>`). */
  template <class U>
    requires(std::is_same_v<const U, T> && !std::is_const_v<U>)
  // NOLINTNEXTLINE(reloco-explicit-conversion): mutable to read-only is the usual implicit const conversion
  constexpr tensor_view(const tensor_view<U, Rank, Strides> &other) noexcept
      : storage_(other.storage()), shape_(other.shape()), strides_(other.strides()) {}

  /** @brief The validated shape. */
  [[nodiscard]] constexpr const shape_type &shape() const noexcept { return shape_; }
  /** @brief The strides, in elements. */
  [[nodiscard]] constexpr const Strides &strides() const noexcept { return strides_; }
  /** @brief The whole backing storage the view was validated against. */
  [[nodiscard]] constexpr span<T> storage() const noexcept { return storage_; }

  /** @brief Extent of dimension `d`; asserts `d < Rank`. */
  [[nodiscard]] constexpr std::size_t extent(std::size_t d) const noexcept { return shape_[d]; }
  /** @brief Stride of dimension `d`, in elements; asserts `d < Rank`. */
  [[nodiscard]] constexpr std::size_t stride(std::size_t d) const noexcept { return strides_[d]; }
  /** @brief `true` if the view has no elements. */
  [[nodiscard]] constexpr bool empty() const noexcept { return shape_.empty(); }

  /** @brief Element at the given indexes; traps (asserts) if any index is outside the shape. */
  template <std::integral... I>
    requires(sizeof...(I) == Rank)
  [[nodiscard]] constexpr T &at(I... indexes) const noexcept {
    const reloco::array<std::size_t, Rank> idx{static_cast<std::size_t>(indexes)...};
    RELOCO_ASSERT(in_shape(idx), "tensor_view::at: index out of range");
    return element(offset_of(idx));
  }

  /** @brief Element at the given indexes, or `out_of_bounds` if any index is outside the shape. */
  template <std::integral... I>
    requires(sizeof...(I) == Rank)
  [[nodiscard]] constexpr result<std::reference_wrapper<T>> try_at(I... indexes) const noexcept {
    const reloco::array<std::size_t, Rank> idx{static_cast<std::size_t>(indexes)...};
    if (!in_shape(idx))
      return unexpected(error::out_of_bounds);
    return std::ref(element(offset_of(idx)));
  }

  /** @brief Element at the given indexes with only a debug-build range check; the caller guarantees the range. */
  template <std::integral... I>
    requires(sizeof...(I) == Rank)
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr T &unsafe_at(I... indexes) const noexcept {
    const reloco::array<std::size_t, Rank> idx{static_cast<std::size_t>(indexes)...};
    RELOCO_DEBUG_ASSERT(in_shape(idx), "tensor_view::unsafe_at: index out of range");
    return element(offset_of(idx));
  }

private:
  friend struct detail::tensor_access;

  constexpr tensor_view(span<T> storage, const shape_type &extents, const Strides &strides) noexcept
      : storage_(storage), shape_(extents), strides_(strides) {}

  [[nodiscard]] constexpr bool in_shape(const reloco::array<std::size_t, Rank> &idx) const noexcept {
    return in_shape(idx, std::make_index_sequence<Rank>{});
  }

  // Valid indexes keep every term below the validated maximum offset, so this cannot overflow.
  [[nodiscard]] constexpr std::size_t offset_of(const reloco::array<std::size_t, Rank> &idx) const noexcept {
    return offset_of(idx, std::make_index_sequence<Rank>{});
  }

  // Both helpers expand over compile-time dimensions: every access below is in range by construction, so they use
  // the check-free accessors and leave no per-dimension bounds checks in the access path.
  template <std::size_t... D>
  [[nodiscard]] constexpr bool in_shape(const reloco::array<std::size_t, Rank> &idx,
                                        std::index_sequence<D...>) const noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return ((idx.unchecked_at(D) < shape_.template get<D>()) && ...);
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  template <std::size_t... D>
  [[nodiscard]] constexpr std::size_t offset_of(const reloco::array<std::size_t, Rank> &idx,
                                                std::index_sequence<D...>) const noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return (std::size_t{0} + ... + (idx.unchecked_at(D) * strides_.template get<D>()));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  // The offset was proven in range by try_make_tensor; the span access adds no check of its own.
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  [[nodiscard]] constexpr T &element(std::size_t offset) const noexcept {
    RELOCO_KERNEL_CHECK(offset < storage_.size(), "tensor_view: offset outside the validated storage");
    return storage_.unchecked_at(offset);
  }
  RELOCO_END_UNSAFE_BUFFER_USAGE

  span<T> storage_{};
  shape_type shape_{};
  Strides strides_{};
};

RELOCO_KERNEL_END

namespace reloco::kernel::detail {

/** @brief Internal: lets the factories and projections build an already-validated view. Not public API. */
struct tensor_access {
  template <class T, std::size_t Rank, class Strides>
  [[nodiscard]] static constexpr tensor_view<T, Rank, Strides> make(span<T> storage, const shape<Rank> &extents,
                                                                    const Strides &strides) noexcept {
    return tensor_view<T, Rank, Strides>(storage, extents, strides);
  }
};

} // namespace reloco::kernel::detail

RELOCO_KERNEL_BEGIN

/**
 * @brief Validates `extents` and `strides` against `storage` once and returns the view.
 * @return the view; `out_of_bounds` if some reachable element lies past the storage, `integer_overflow` if the offset
 * math overflows `size_t`, `invalid_argument` if a mutable (`T` non-const) view would map two indexes to one element.
 * @details An empty shape (any extent zero) is always valid. Overlapping strides (for example stride 0, a broadcast)
 * are accepted for read-only views and rejected for mutable ones, so no two writes can alias.
 */
template <class T, std::size_t Rank, class Strides = dynamic_strides<Rank>>
[[nodiscard]] constexpr result<tensor_view<T, Rank, Strides>> try_make_tensor(span<T> storage, shape<Rank> extents,
                                                                              Strides strides) noexcept {
  if (extents.empty())
    return detail::tensor_access::make(storage, extents, strides);

  reloco::array<std::size_t, Rank> ext{};
  reloco::array<std::size_t, Rank> str{};
  for (std::size_t d = 0; d < Rank; ++d) {
    ext[d] = extents[d];
    str[d] = strides[d];
  }

  auto reach = detail::max_offset<Rank>(ext, str);
  if (!reach)
    return unexpected(reach.error());
  std::size_t max = *reach;
  if (!std::is_constant_evaluated())
    RELOCO_FAULT_POINT_ARGS(fault::tensor_max_offset, max);
  if (max >= storage.size())
    return unexpected(error::out_of_bounds);

  if constexpr (!std::is_const_v<T>) {
    bool disjoint = detail::strides_are_disjoint<Rank>(ext, str);
    if (!std::is_constant_evaluated())
      RELOCO_FAULT_POINT_ARGS(fault::tensor_disjoint, disjoint);
    if (!disjoint)
      return unexpected(error::invalid_argument);
  }
  return detail::tensor_access::make(storage, extents, strides);
}

/**
 * @brief Row-major view with no padding: the last dimension has stride 1, each earlier one the product of the extents
 * after it. `integer_overflow` if that product overflows, `out_of_bounds` if the storage is too small.
 * @details Extra storage after the last element is fine. An empty shape yields an empty view.
 */
template <class T, std::size_t Rank>
[[nodiscard]] constexpr result<tensor_view<T, Rank>> try_make_contiguous_tensor(span<T> storage,
                                                                                shape<Rank> extents) noexcept {
  if (extents.empty())
    return detail::tensor_access::make(storage, extents, dynamic_strides<Rank>{});

  reloco::array<std::size_t, Rank> str{};
  std::size_t running = 1;
  for (std::size_t d = Rank; d-- > 0;) {
    str[d] = running;
    auto next = checked_mul(running, extents[d]);
    if (!next)
      return unexpected(next.error());
    running = *next;
  }
  return try_make_tensor<T, Rank>(storage, shape<Rank>(extents), dynamic_strides<Rank>(str));
}

RELOCO_KERNEL_END
