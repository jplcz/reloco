// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boost_intrusive_safe_set_hook.hpp
 * @brief `safe_set_hook<Options...>`/`safe_set_member_hook<Options...>`:
 * `boost::intrusive::set_base_hook`/`set_member_hook` derivatives
 * (backing `set`/`multiset` -- the red-black-tree-specific hook; see
 * `boost_intrusive_safe_bs_set_hook.hpp` for the `bs_set_hook`-family
 * hook shared by plain `bstree`/`splay_set` instead, and
 * `boost_intrusive_safe_avl_set_hook.hpp` for `avl_set`'s own
 * `avl_set_hook` family) that always run with `link_mode<safe_link>`
 * and wire their own "destroyed while still linked" check through
 * `RELOCO_ASSERT` instead of relying solely on Boost's own
 * `BOOST_ASSERT`.
 *
 * See `boost_intrusive_safe_list_hook.hpp`'s file-level docs for the full
 * rationale (`BOOST_ASSERT`'s `NDEBUG`-compiled-out, non-`RELOCO_KERNEL`
 * aware behavior vs. `RELOCO_ASSERT`'s, and why the member-hook flavor
 * needs its own wrapper too) -- identical here, just applied to
 * `set_base_hook`/`set_member_hook` instead of
 * `list_base_hook`/`list_member_hook`.
 *
 * ```cpp
 * // Base-hook style.
 * struct task : public reloco::boost_intrusive::safe_set_hook<> {
 *   int priority;
 *   friend bool operator<(const task &a, const task &b) noexcept { return a.priority < b.priority; }
 * };
 *
 * // Member-hook style.
 * struct job {
 *   reloco::boost_intrusive::safe_set_member_hook<> hook;
 *   int priority;
 * };
 *
 * // Either way, destroying one still linked into some
 * // `boost::intrusive::set` now fails loudly through reloco's own
 * // assert handler.
 * ```
 */

#include "detail/assert.hpp"

#include <boost/intrusive/set_hook.hpp>

namespace reloco::boost_intrusive {

/**
 * @brief Derive from this instead of `boost::intrusive::set_base_hook`
 * directly to get a `RELOCO_ASSERT`-backed "destroyed while still
 * linked" check (see the file-level docs). @p Options are forwarded
 * verbatim to `set_base_hook` (e.g. `tag<MyTag>`, `optimize_size<>`) --
 * `link_mode<>` is always `safe_link` and is not itself an accepted
 * option here, since `is_linked()` (which the destructor check relies
 * on) is only ever valid in `safe_link`/`auto_unlink` mode.
 */
template <typename... Options>
class safe_set_hook : public ::boost::intrusive::set_base_hook<::boost::intrusive::link_mode<::boost::intrusive::safe_link>,
                                                                Options...> {
public:
  using base_hook_type =
      ::boost::intrusive::set_base_hook<::boost::intrusive::link_mode<::boost::intrusive::safe_link>, Options...>;

  safe_set_hook() noexcept = default;
  safe_set_hook(const safe_set_hook &) noexcept = default;
  safe_set_hook &operator=(const safe_set_hook &) noexcept = default;

  ~safe_set_hook() {
    RELOCO_ASSERT(!this->is_linked(),
                  "reloco::boost_intrusive::safe_set_hook destroyed while still linked into a "
                  "boost::intrusive ordered container (set/multiset) -- unlink it "
                  "(or route it through isolated_node_tx::relink_to/release_to, see "
                  "intrusive_iteration.hpp) before destruction");
  }
};

/**
 * @brief Member-hook counterpart of `safe_set_hook`: embed this as a
 * data member instead of `boost::intrusive::set_member_hook` directly,
 * for the same `RELOCO_ASSERT`-backed "destroyed while still linked"
 * check -- see the file-level docs. @p Options are forwarded verbatim
 * to `set_member_hook` (e.g. `void_pointer<>`, `optimize_size<>`);
 * `link_mode<>` is always `safe_link`, same rationale as `safe_set_hook`.
 */
template <typename... Options>
class safe_set_member_hook
    : public ::boost::intrusive::set_member_hook<::boost::intrusive::link_mode<::boost::intrusive::safe_link>,
                                                  Options...> {
public:
  using member_hook_type =
      ::boost::intrusive::set_member_hook<::boost::intrusive::link_mode<::boost::intrusive::safe_link>, Options...>;

  safe_set_member_hook() noexcept = default;
  safe_set_member_hook(const safe_set_member_hook &) noexcept = default;
  safe_set_member_hook &operator=(const safe_set_member_hook &) noexcept = default;

  ~safe_set_member_hook() {
    RELOCO_ASSERT(!this->is_linked(),
                  "reloco::boost_intrusive::safe_set_member_hook destroyed while still linked into a "
                  "boost::intrusive ordered container (set/multiset) -- unlink it (or route it "
                  "through isolated_node_tx::relink_to/release_to, see intrusive_iteration.hpp) "
                  "before destruction");
  }
};

} // namespace reloco::boost_intrusive
