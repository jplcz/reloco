// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file list.hpp
 * @brief `list<T>`: owning doubly linked list that keeps only a head pointer (O(1) push/pop at the front, O(1)
 * erase/insert at an iterator), built on `c_list`. See `docs/linked-containers.md`.
 */

#include "detail/owning_list_base.hpp"
#include "intrusive_c_list.hpp"

namespace reloco {

namespace detail {

struct list_policy {
  template <typename Node> using link = c_list_hook_layout<Node>;
  template <typename Node> using queue = c_list<Node, &Node::link>;
  static constexpr bool has_back = false;
  static constexpr bool has_push_back = false;
  static constexpr bool doubly_linked = true;

  template <typename Node> [[nodiscard]] static typename queue<Node>::iterator make_iterator(Node &node) noexcept {
    return typename queue<Node>::iterator(&node);
  }
};

} // namespace detail

/** @brief Owning doubly linked list with a head-only handle (no tail); LIFO use plus O(1) erase. */
template <typename T> class RELOCO_OWNER list : public detail::owning_list_base<list<T>, T, detail::list_policy> {
  using base = detail::owning_list_base<list<T>, T, detail::list_policy>;

public:
  using base::base;
};

} // namespace reloco
