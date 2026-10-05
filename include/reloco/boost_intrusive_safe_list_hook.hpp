// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boost_intrusive_safe_list_hook.hpp
 * @brief `safe_list_hook<Options...>`/`safe_list_member_hook<Options...>`:
 * `boost::intrusive::list_base_hook`/`list_member_hook` derivatives that
 * always run with `link_mode<safe_link>` and wire their own "destroyed
 * while still linked" check through `RELOCO_ASSERT` instead of relying
 * solely on Boost's own `BOOST_ASSERT`.
 *
 * Boost.Intrusive's `safe_link` mode already does this check in its own
 * hook destructor (`generic_hook`'s `destructor_impl` specialization for
 * `safe_link` asserts `!is_linked()` -- identically for the base-hook and
 * member-hook flavors, since both are ultimately instantiations of the
 * same `generic_hook` template, only differing in `HookId`), but that
 * check goes through `BOOST_ASSERT`, which (a) is typically a plain
 * `<cassert>` `assert()` -- compiled out entirely under `NDEBUG`,
 * independently of whether the embedding application wants reloco's own
 * asserts kept live via `RELOCO_DEBUG`/`RELOCO_DISABLE_ASSERT` -- and (b)
 * has no notion of `RELOCO_KERNEL`'s port-supplied `RELOCO_KERNEL_PANIC`,
 * so it cannot participate in a freestanding/kernel port's own panic/log
 * facility at all. Deriving from `safe_list_hook`/`safe_list_member_hook`
 * instead of `list_base_hook`/`list_member_hook` directly gets a
 * `RELOCO_ASSERT` check that runs *before* Boost's own (derived class
 * destructors run before their base's), honors reloco's assert
 * configuration consistently with every other reloco container, and
 * works unmodified in `RELOCO_KERNEL` builds.
 *
 * ```cpp
 * // Base-hook style: struct derives from the hook.
 * struct task : public reloco::boost_intrusive::safe_list_hook<> {
 *   int id;
 * };
 *
 * // Member-hook style: struct holds the hook as a plain data member --
 * // see `container_of<&owner::member_hook>()` (container_of.hpp) for
 * // recovering the owner from a foreign API that only ever hands back
 * // a pointer to the member hook itself.
 * struct job {
 *   reloco::boost_intrusive::safe_list_member_hook<> hook;
 *   int id;
 * };
 *
 * // Either way, destroying one still linked into some
 * // `boost::intrusive::list` now fails loudly through reloco's own
 * // assert handler.
 * ```
 */

#include "detail/assert.hpp"

#include <boost/intrusive/list_hook.hpp>

namespace reloco::boost_intrusive {

/**
 * @brief Derive from this instead of `boost::intrusive::list_base_hook`
 * directly to get a `RELOCO_ASSERT`-backed "destroyed while still
 * linked" check (see the file-level docs for why that differs from
 * `safe_link`'s own built-in `BOOST_ASSERT`). @p Options are forwarded
 * verbatim to `list_base_hook` (e.g. `tag<MyTag>`, `void_pointer<>`) --
 * `link_mode<>` is always `safe_link` and is not itself an accepted
 * option here, since `is_linked()` (which the destructor check relies
 * on) is only ever valid in `safe_link`/`auto_unlink` mode.
 */
template <typename... Options>
class safe_list_hook : public ::boost::intrusive::list_base_hook<::boost::intrusive::link_mode<::boost::intrusive::safe_link>,
                                                                 Options...> {
public:
  using base_hook_type =
      ::boost::intrusive::list_base_hook<::boost::intrusive::link_mode<::boost::intrusive::safe_link>, Options...>;

  safe_list_hook() noexcept = default;
  safe_list_hook(const safe_list_hook &) noexcept = default;
  safe_list_hook &operator=(const safe_list_hook &) noexcept = default;

  ~safe_list_hook() {
    RELOCO_ASSERT(!this->is_linked(),
                  "reloco::boost_intrusive::safe_list_hook destroyed while still linked into a "
                  "boost::intrusive::list -- unlink it (or route it through "
                  "isolated_node_tx::relink_to/release_to, see intrusive_iteration.hpp) before destruction");
  }
};

/**
 * @brief Member-hook counterpart of `safe_list_hook`: embed this as a
 * data member (`member_hook<Owner, safe_list_member_hook<>, &Owner::hook>`
 * when configuring the container) instead of
 * `boost::intrusive::list_member_hook` directly, for the same
 * `RELOCO_ASSERT`-backed "destroyed while still linked" check -- see the
 * file-level docs. @p Options are forwarded verbatim to
 * `list_member_hook` (e.g. `void_pointer<>`); `link_mode<>` is always
 * `safe_link`, same rationale as `safe_list_hook`.
 */
template <typename... Options>
class safe_list_member_hook
    : public ::boost::intrusive::list_member_hook<::boost::intrusive::link_mode<::boost::intrusive::safe_link>,
                                                   Options...> {
public:
  using member_hook_type =
      ::boost::intrusive::list_member_hook<::boost::intrusive::link_mode<::boost::intrusive::safe_link>, Options...>;

  safe_list_member_hook() noexcept = default;
  safe_list_member_hook(const safe_list_member_hook &) noexcept = default;
  safe_list_member_hook &operator=(const safe_list_member_hook &) noexcept = default;

  ~safe_list_member_hook() {
    RELOCO_ASSERT(!this->is_linked(),
                  "reloco::boost_intrusive::safe_list_member_hook destroyed while still linked into a "
                  "boost::intrusive::list -- unlink it (or route it through "
                  "isolated_node_tx::relink_to/release_to, see intrusive_iteration.hpp) before destruction");
  }
};

} // namespace reloco::boost_intrusive
