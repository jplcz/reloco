// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file stailq.hpp
 * @brief `stailq<T>`: owning singly linked FIFO queue (O(1) push at both ends, O(1) pop_front), built on
 * `c_stailq`. See `docs/linked-containers.md`.
 */

#include "detail/owning_list_base.hpp"
#include "intrusive_c_stailq.hpp"

namespace reloco {

namespace detail {

struct stailq_policy {
  template <typename Node> using link = Node *;
  template <typename Node> using queue = c_stailq<Node, &Node::link>;
  static constexpr bool has_back = true;
  static constexpr bool has_push_back = true;
  static constexpr bool doubly_linked = false;

  template <typename Node> [[nodiscard]] static typename queue<Node>::iterator make_iterator(Node &node) noexcept {
    return typename queue<Node>::iterator(&node);
  }
};

} // namespace detail

/** @brief Owning singly linked tail queue; one pointer of overhead per node. */
template <typename T> class RELOCO_OWNER stailq : public detail::owning_list_base<stailq<T>, T, detail::stailq_policy> {
  using base = detail::owning_list_base<stailq<T>, T, detail::stailq_policy>;

public:
  using base::base;
};

} // namespace reloco
