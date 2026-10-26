// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/index/proven_index.hpp
 * @brief `proven_index<Tag>`: an index that is in range by construction.
 *
 * A `proven_index` has no public constructor. Only the loop domains of this directory (`index_range`,
 * `block_domain`, `window_domain`, `strided_domain`) mint them, and only for values inside the extent the domain
 * was built from. The `Tag` brands the proof: an index minted for one extent cannot be applied to a view bound to
 * another (the view layer only accepts its own tag).
 *
 * `unbranded` is the tag of the plain factories (`domain(n)`, `blocks<B>(n)`, ...). Unbranded indexes carry a
 * range proof about the domain's own extent only and are never accepted by views; use them for plain integer loops.
 *
 * See `docs/kernel/index.md`.
 */

#include "../config.hpp"

#include <compare>
#include <cstddef>

RELOCO_KERNEL_BEGIN

/** @brief Brand of the plain (view-less) loop domains. */
struct unbranded {};

template <class Tag> class proven_index;

RELOCO_KERNEL_END

namespace reloco::kernel::detail {

/** @brief Passkey for domain constructors; only `index_access` can create one, so only the framework can mint. */
class mint_key {
  constexpr mint_key() noexcept = default;
  friend struct index_access;
};

/** @brief Internal: the single place that creates keys and proven indexes. Not part of the public API. */
struct index_access {
  [[nodiscard]] static constexpr mint_key key() noexcept { return mint_key{}; }

  template <class Tag> [[nodiscard]] static constexpr proven_index<Tag> make(std::size_t value) noexcept;
};

} // namespace reloco::kernel::detail

RELOCO_KERNEL_BEGIN

/**
 * @brief An index known to be smaller than the extent of the domain that produced it.
 * @tparam Tag brand tying the index to one extent (see file comment).
 * @details Trivially copyable, one `std::size_t`; the size never depends on `RELOCO_KERNEL_CHECKS`. There is no
 * implicit conversion to an integer, and arithmetic on `value()` yields a plain unproven integer.
 */
template <class Tag> class RELOCO_COPYABLE proven_index {
public:
  using tag_type = Tag;

  /** @brief The index as a plain integer. */
  [[nodiscard]] constexpr std::size_t value() const noexcept { return value_; }

  [[nodiscard]] explicit constexpr operator std::size_t() const noexcept { return value_; }

  [[nodiscard]] friend constexpr bool operator==(proven_index, proven_index) noexcept = default;
  [[nodiscard]] friend constexpr std::strong_ordering operator<=>(proven_index, proven_index) noexcept = default;

private:
  constexpr explicit proven_index(std::size_t value) noexcept : value_(value) {}

  friend struct detail::index_access;

  std::size_t value_;
};

RELOCO_KERNEL_END

namespace reloco::kernel::detail {

template <class Tag> constexpr proven_index<Tag> index_access::make(std::size_t value) noexcept {
  return proven_index<Tag>(value);
}

} // namespace reloco::kernel::detail
