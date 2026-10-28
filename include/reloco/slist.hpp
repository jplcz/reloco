// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file slist.hpp
 * @brief `slist<T>`: owning singly linked list (O(1) push/pop at the front, O(1) insert/erase after a position),
 * built on `c_slist`. See `docs/linked-containers.md`.
 */

#include "detail/owning_list_base.hpp"
#include "intrusive_c_slist.hpp"

namespace reloco {

namespace detail {

struct slist_policy {
  template <typename Node> using link = Node *;
  template <typename Node> using queue = c_slist<Node, &Node::link>;
  static constexpr bool has_back = false;
  static constexpr bool has_push_back = false;
  static constexpr bool doubly_linked = false;

  template <typename Node> [[nodiscard]] static typename queue<Node>::iterator make_iterator(Node &node) noexcept {
    return typename queue<Node>::iterator(&node);
  }
};

} // namespace detail

/** @brief Owning singly linked list; the smallest node overhead, ideal as a stack. */
template <typename T> class RELOCO_OWNER slist : public detail::owning_list_base<slist<T>, T, detail::slist_policy> {
  using base = detail::owning_list_base<slist<T>, T, detail::slist_policy>;

public:
  using base::base;
};

} // namespace reloco
