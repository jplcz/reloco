// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boost_intrusive_safe_list.hpp
 * @brief `safe_list<T, Options...>`: a `boost::intrusive::list<T,
 * Options...>` derivative that adds reloco's overlay APIs
 * (`iterator.hpp`'s `.map()`/`.filter()`/`.for_each()`/... pipeline and
 * `intrusive_iteration.hpp`'s `extract_if()`/`isolated_node_tx`
 * relink/dispose pipeline) as member functions, instead of requiring the
 * caller to reach for the free-function forms documented in
 * `boost_intrusive_adapter.hpp`.
 *
 * This is a container-level complement to the node-level
 * `safe_list_hook`/`safe_list_member_hook` (`boost_intrusive_safe_list_hook.hpp`):
 * the hook makes destroying a still-linked node fail loudly, and
 * `safe_list` makes the two main "safe" ways of removing a node in the
 * first place (iterate-and-relink/dispose) a one-liner on the container
 * itself. Using both together is the intended pairing, but neither
 * requires the other -- `safe_list<T>` works with a plain
 * `list_base_hook`/`list_member_hook` just as well, and `safe_list_hook`
 * works inside a plain `boost::intrusive::list` just as well.
 *
 * `safe_list` adds no data members and no virtual functions (reloco
 * generally avoids both, see `docs/extending.md`), so it is exactly as
 * cheap as `boost::intrusive::list` itself -- a plain derived class
 * reusing the base's storage, constructors (via an inheriting
 * `using`-declaration), and implicitly-declared move
 * constructor/assignment (the base is move-only via
 * `BOOST_MOVABLE_BUT_NOT_COPYABLE`; since `safe_list` adds no members of
 * its own, its implicit move special members just move the base
 * subobject, and copy stays implicitly deleted the same way).
 *
 * ```cpp
 * struct task : public reloco::boost_intrusive::safe_list_hook<> {
 *   int priority;
 *   bool is_blocked;
 *   bool tick(); // returns true once finished
 * };
 *
 * reloco::boost_intrusive::safe_list<task> active_queue, blocked_queue;
 *
 * // Member-function overlay instead of reloco::extract_if(active_queue, ...).
 * active_queue.extract_if([](task &t) { return t.is_blocked || t.tick(); })
 *     .for_each([&](auto &&tx) {
 *       if (tx.get().is_blocked)
 *         tx.relink_to(blocked_queue, reloco::boost_intrusive::push_back_inserter());
 *       else
 *         tx.release_to(reloco::boost_intrusive::default_delete_disposer<task>());
 *     });
 *
 * // Member-function overlay instead of reloco::iter(active_queue).
 * int total_priority = active_queue.iter().map([](task &t) { return t.priority; }).sum();
 * ```
 */

#include "boost_intrusive_adapter.hpp"
#include "iterator.hpp"

#include <boost/intrusive/list.hpp>

namespace reloco::boost_intrusive {

/**
 * @brief See the file-level docs: a `boost::intrusive::list<T,
 * Options...>` derivative adding `extract_if()`/`iter()` as member
 * functions. @p Options are forwarded verbatim to
 * `boost::intrusive::list`.
 */
template <typename T, typename... Options> class safe_list : public ::boost::intrusive::list<T, Options...> {
public:
  using base_type = ::boost::intrusive::list<T, Options...>;
  using base_type::base_type;

  /**
   * @brief Member-function overlay for `reloco::extract_if(*this, pred)`
   * (`boost_intrusive_adapter.hpp`): walks this list once, atomically
   * detaching every element for which @p pred returns `true` into an
   * `isolated_node_tx` ready to be routed via
   * `relink_to()`/`release_to()` (`intrusive_iteration.hpp`).
   */
  template <typename Pred> [[nodiscard]] auto extract_if(Pred pred) noexcept {
    return ::reloco::extract_if(*this, static_cast<Pred &&>(pred));
  }

  /**
   * @brief Member-function overlay for `reloco::iter(*this)`
   * (`iterator.hpp`): a Rust-style pull iterator over this list's
   * elements, ready for `.map()`/`.filter()`/`.for_each()`/....
   */
  [[nodiscard]] auto iter() noexcept { return ::reloco::iter(*this); }

  /** @copydoc iter() */
  [[nodiscard]] auto iter() const noexcept { return ::reloco::iter(*this); }
};

} // namespace reloco::boost_intrusive
