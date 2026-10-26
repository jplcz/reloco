// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/view/shape.hpp
 * @brief `shape<Rank>`: the extent (number of elements) along each dimension of a tensor.
 *
 * See `docs/kernel/views.md`.
 */

#include "../../array.hpp"
#include "../../detail/assert.hpp"
#include "../../error.hpp"
#include "../../int_ops.hpp"
#include "../config.hpp"

#include <concepts>
#include <cstddef>

RELOCO_KERNEL_BEGIN

/**
 * @brief The extents of a `Rank`-dimensional tensor, outermost dimension first.
 * @details `shape<2>{rows, cols}` describes a matrix. A zero extent is legal and makes the tensor empty. Negative
 * signed arguments wrap to huge values, which `try_make_tensor` then rejects as out of bounds.
 */
template <std::size_t Rank> class shape {
  static_assert(Rank >= 1, "a tensor needs at least one dimension");

public:
  static constexpr std::size_t rank = Rank;

  /** @brief The empty shape (all extents zero). */
  constexpr shape() noexcept = default;

  /** @brief Takes one extent per dimension; the braces form `{rows, cols}` works too. */
  template <std::integral... E>
    requires(sizeof...(E) == Rank)
  constexpr shape(E... extents) noexcept // NOLINT(reloco-explicit-conversion): needed for `{rows, cols}` braces
      : extents_{static_cast<std::size_t>(extents)...} {}

  /** @brief Takes the extents as an array, outermost first. */
  constexpr explicit shape(const reloco::array<std::size_t, Rank> &extents) noexcept : extents_(extents) {}

  /** @brief Extent of dimension `d`; asserts `d < Rank`. */
  RELOCO_CHECKED [[nodiscard]] constexpr std::size_t operator[](std::size_t d) const noexcept {
    RELOCO_ASSERT(d < Rank, "shape: dimension out of range");
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return extents_.unchecked_at(d); // d was just asserted
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  /** @brief Extent of dimension `D`, with the range verified at compile time (no run-time check). */
  template <std::size_t D> [[nodiscard]] constexpr std::size_t get() const noexcept {
    static_assert(D < Rank, "shape dimension out of range");
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return extents_.unchecked_at(D);
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  /** @brief `true` if any extent is zero, i.e. the tensor has no elements. */
  [[nodiscard]] constexpr bool empty() const noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    for (std::size_t d = 0; d < Rank; ++d) // d < Rank by the loop bound
      if (extents_.unchecked_at(d) == 0)
        return true;
    RELOCO_END_UNSAFE_BUFFER_USAGE
    return false;
  }

  /** @brief Total number of elements (product of the extents); `integer_overflow` if it does not fit `size_t`. */
  [[nodiscard]] constexpr result<std::size_t> try_elements() const noexcept {
    if (empty())
      return std::size_t{0};
    std::size_t total = 1;
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    for (std::size_t d = 0; d < Rank; ++d) { // d < Rank by the loop bound
      auto next = checked_mul(total, extents_.unchecked_at(d));
      if (!next)
        return unexpected(next.error());
      total = *next;
    }
    RELOCO_END_UNSAFE_BUFFER_USAGE
    return total;
  }

  [[nodiscard]] friend constexpr bool operator==(const shape &, const shape &) noexcept = default;

private:
  reloco::array<std::size_t, Rank> extents_{};
};

RELOCO_KERNEL_END
