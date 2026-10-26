// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/view/lane_traits.hpp
 * @brief `lane_traits<T, W>`: what a "vector of W elements" is, chosen by the user.
 *
 * reloco is architecture independent, so it does not know SIMD register types. The default lane is
 * `reloco::array<T, W>`, which optimizers vectorize well. To use a real register type, specialize `lane_traits`
 * for your `T` and `W`; the view layer only calls `load` / `store` with a span that is already proven to hold exactly
 * `W` elements, so a specialization may use an unchecked intrinsic load.
 *
 * See `docs/kernel/views.md`.
 */

#include "../../array.hpp"
#include "../../detail/assert.hpp"
#include "../../span.hpp"
#include "../config.hpp"

#include <cstddef>
#include <type_traits>

RELOCO_KERNEL_BEGIN

/**
 * @brief Describes the lane type for `W` elements of `T` and how to move it to and from memory.
 * @details Specializations must provide `using type`, `static type load(span<const T>)` and
 * `static void store(span<T>, const type &)`. Both spans hold exactly `W` elements.
 */
template <class T, std::size_t W> struct lane_traits {
  static_assert(W >= 1, "a lane needs at least one element");

  using type = reloco::array<std::remove_const_t<T>, W>;

  [[nodiscard]] static constexpr type load(span<const std::remove_const_t<T>> from) noexcept {
    RELOCO_KERNEL_CHECK(from.size() == W, "lane_traits::load: span must hold exactly W elements");
    type lane{};
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    for (std::size_t k = 0; k < W; ++k)
      lane.unchecked_at(k) = from.unchecked_at(k);
    RELOCO_END_UNSAFE_BUFFER_USAGE
    return lane;
  }

  static constexpr void store(span<std::remove_const_t<T>> to, const type &lane) noexcept {
    RELOCO_KERNEL_CHECK(to.size() == W, "lane_traits::store: span must hold exactly W elements");
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    for (std::size_t k = 0; k < W; ++k)
      to.unchecked_at(k) = lane.unchecked_at(k);
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }
};

RELOCO_KERNEL_END
