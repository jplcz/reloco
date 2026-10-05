// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boost_intrusive_adapter.hpp
 * @brief Reloco-style overlay APIs for `boost::intrusive` containers
 * (`boost::intrusive::list`, `set`, `unordered_set`, ...): promotes the
 * `extract_if()`/`isolated_node_tx` pipeline (`intrusive_iteration.hpp`)
 * to a first-class, reusable entry point instead of a hand-rolled
 * per-translation-unit helper, adds small `Inserter`/`Disposer` factories
 * for the most common `relink_to`/`release_to` routing idioms, and
 * documents how to recover a hook's owning object via `container_of`
 * (`container_of.hpp`).
 *
 * Nothing here actually `#include`s a `<boost/intrusive/...>` header --
 * every entry point is a plain template duck-typed against whatever
 * container/iterator contract it needs (the same "erase(iterator) ->
 * iterator" contract `intrusive_iteration.hpp` already documents as
 * satisfied by `boost::intrusive::list`/`set`/`unordered_set`), so this
 * header has no hard dependency on Boost itself and works unmodified for
 * reloco's own `intrusive_c_*`/`intrusive_hash_table`/`intrusive_rbtree`/
 * `intrusive_splay_tree` containers too -- "Boost.Intrusive adapter" is
 * the primary intended audience (hence the file name and the worked
 * example below), not the only one it compiles against.
 *
 * ## `reloco::iter()` already works with `boost::intrusive` containers
 *
 * `boost::intrusive::list<T>`/`set<T>`/`unordered_set<T>` already expose
 * a plain `begin()`/`end()` pair yielding `T&`, exactly like any other
 * STL-style container -- so `reloco::iter(container)` (`iterator.hpp`)
 * and its whole `.map()`/`.filter()`/`.for_each()`/... pipeline already
 * works on them with zero glue code. There is nothing to adapt there;
 * see `tests/test_boost_intrusive_adapter.cpp` for a worked example.
 *
 * ## `extract_if()`
 *
 * `intrusive_iteration.hpp` documents `extract_if_iterator`/
 * `isolated_node_tx` as usable with `boost::intrusive` containers, but
 * leaves constructing one to the caller (every demo/test ends up
 * re-declaring the same two-line factory function). `extract_if()` below
 * is exactly that factory, promoted to a reusable API:
 *
 * ```cpp
 * reloco::extract_if(active_queue, [](Task &t) { return t.is_blocked || t.tick(); })
 *     .for_each([&](auto &&tx) {
 *       if (tx.get().is_blocked)
 *         tx.relink_to(blocked_queue, reloco::boost_intrusive::push_back_inserter());
 *       else
 *         tx.release_to(reloco::boost_intrusive::default_delete_disposer<Task>());
 *     });
 * ```
 *
 * ## Recovering an owner from a hook pointer
 *
 * A foreign API that only ever hands back a pointer to a
 * `boost::intrusive` hook subobject (e.g. a `list_member_hook<>*`
 * recovered from some lower-level callback, rather than the `T&` Boost's
 * own iterators would normally yield) can recover the owning `T*` the
 * same way reloco recovers the owner of any of its own hook members: via
 * `container_of<Member>()` (`container_of.hpp`). There is nothing
 * Boost-specific about that helper -- it only needs `Member` to actually
 * be a pointer-to-data-member of the hook subobject in question, which a
 * `boost::intrusive::list_member_hook<> T::*member` is -- so this file
 * does not re-wrap it under another name.
 */

#include "container_of.hpp"
#include "intrusive_iteration.hpp"

namespace reloco {

/**
 * @brief Factory for `extract_if_iterator<Container, Pred>`: walks @p
 * container once, atomically detaching every element for which @p pred
 * returns `true` into an `isolated_node_tx` ready to be routed via
 * `relink_to()`/`release_to()` (`intrusive_iteration.hpp`).
 *
 * @p Container only needs to satisfy the same "`begin()`/`end()`, and
 * `erase(iterator)` returns the iterator following the one just
 * unlinked" contract `extract_if_iterator` itself already requires --
 * satisfied by `boost::intrusive::list`/`set`/`unordered_set`, by
 * `std::list`/`std::set`/`std::unordered_set`, and by reloco's own
 * `intrusive_c_*`/`intrusive_hash_table`/`intrusive_rbtree`/
 * `intrusive_splay_tree` containers alike.
 */
template <typename Container, typename Pred>
[[nodiscard]] extract_if_iterator<Container, Pred> extract_if(Container &container, Pred pred) noexcept {
  return extract_if_iterator<Container, Pred>(container, static_cast<Pred &&>(pred));
}

namespace boost_intrusive {

/**
 * @brief `Inserter` for `isolated_node_tx::relink_to()`: routes the
 * isolated node to the back of the destination container via its
 * `push_back(T &)`, matching `boost::intrusive::list::push_back`/
 * `std::list::push_back`.
 */
[[nodiscard]] inline auto push_back_inserter() noexcept {
  return [](auto &container, auto &item) { container.push_back(item); };
}

/**
 * @brief `Inserter` for `isolated_node_tx::relink_to()`: routes the
 * isolated node to the front of the destination container via its
 * `push_front(T &)`, matching `boost::intrusive::list::push_front`/
 * `std::list::push_front`.
 */
[[nodiscard]] inline auto push_front_inserter() noexcept {
  return [](auto &container, auto &item) { container.push_front(item); };
}

/**
 * @brief `Inserter` for `isolated_node_tx::relink_to()`: routes the
 * isolated node into the destination container via its own `insert(T &)`
 * (e.g. the ordered/hashed `insert` every `boost::intrusive::set`/
 * `unordered_set` provides), rather than an ends-biased
 * `push_back`/`push_front`.
 */
[[nodiscard]] inline auto insert_inserter() noexcept {
  return [](auto &container, auto &item) { container.insert(item); };
}

/**
 * @brief `Disposer` for `isolated_node_tx::release_to()`: `delete`s the
 * node outright, for the common case where @p T was heap-allocated and
 * the intrusive container never owned it (`boost::intrusive`'s own
 * usual ownership model).
 */
template <typename T> [[nodiscard]] auto default_delete_disposer() noexcept {
  return [](T *node) { delete node; };
}

} // namespace boost_intrusive

} // namespace reloco
