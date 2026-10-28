// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file circular_list.hpp
 * @brief `circular_list<T>`: owning circular doubly linked list (Linux `list_head` style) with bidirectional
 * iteration and O(1) operations at both ends, built on `c_list_head`. See `docs/linked-containers.md`.
 */

#include "detail/owning_list_base.hpp"
#include "intrusive_c_list_head.hpp"

namespace reloco {

namespace detail {

struct circular_list_policy {
  template <typename Node> using link = c_list_head_node;
  template <typename Node> using queue = c_list_head<Node, &Node::link>;
  static constexpr bool has_back = true;
  static constexpr bool has_push_back = true;
  static constexpr bool doubly_linked = true;

  template <typename Node> [[nodiscard]] static typename queue<Node>::iterator make_iterator(Node &node) noexcept {
    return typename queue<Node>::iterator(c_linux_hook_access<Node, &Node::link>::get(&node));
  }
};

} // namespace detail

/** @brief Owning circular doubly linked list; the only owning list with bidirectional iterators. */
template <typename T>
class RELOCO_OWNER circular_list : public detail::owning_list_base<circular_list<T>, T, detail::circular_list_policy> {
  using base = detail::owning_list_base<circular_list<T>, T, detail::circular_list_policy>;

public:
  using base::base;
};

} // namespace reloco
