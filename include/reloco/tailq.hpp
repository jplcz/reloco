// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file tailq.hpp
 * @brief `tailq<T>`: owning doubly linked list with O(1) push/pop at both ends and O(1) erase/insert at an
 * iterator, built on `c_tailq`. See `docs/linked-containers.md`.
 */

#include "detail/owning_list_base.hpp"
#include "intrusive_c_tailq.hpp"

namespace reloco {

namespace detail {

struct tailq_policy {
  template <typename Node> using link = c_tailq_hook_layout<Node>;
  template <typename Node> using queue = c_tailq<Node, &Node::link>;
  static constexpr bool has_back = true;
  static constexpr bool has_push_back = true;
  static constexpr bool doubly_linked = true;

  template <typename Node> [[nodiscard]] static typename queue<Node>::iterator make_iterator(Node &node) noexcept {
    return typename queue<Node>::iterator(&node);
  }
};

} // namespace detail

/** @brief Owning doubly linked tail queue; forward iteration, stable element addresses. */
template <typename T> class RELOCO_OWNER tailq : public detail::owning_list_base<tailq<T>, T, detail::tailq_policy> {
  using base = detail::owning_list_base<tailq<T>, T, detail::tailq_policy>;

public:
  using base::base;
};

} // namespace reloco
