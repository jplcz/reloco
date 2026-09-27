// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file digraph.hpp
 * @brief Allocator-backed directed graph over dense node indices, purpose-
 * built for lock-order/witness-style dependency tracking (see FreeBSD's
 * `witness(4)` and Linux's `lockdep`).
 *
 * `digraph` models a set of nodes (allocated in order by `try_add_node()`,
 * identified by the `std::size_t` returned) and directed edges between
 * them. Its one deliberately-narrow job is *incremental* edge insertion
 * that never introduces a cycle: `try_add_edge(from, to)` first checks
 * whether `to` can already reach `from`; if so the new edge would close a
 * cycle (in witness/lockdep terms, a lock-order inversion that could
 * deadlock), so it is rejected with `error::deadlock` and the graph is left
 * unchanged. This mirrors exactly how a lock-order validator uses a
 * "locked after" graph: nodes are lock classes, an edge `A -> B` records
 * that `A` was observed locked before `B` somewhere in the program, and
 * rejecting any edge that would create a cycle is precisely what catches a
 * potential deadlock *before* it can happen, at the point the conflicting
 * lock order is first observed.
 *
 * There is no generic weighted-shortest-path/traversal-algorithm layer
 * here (see the file's own docs on why: reloco never uses floating point,
 * so a "graph algorithms" module stays scoped to what integer-weighted,
 * allocator-fallible code can express) -- `digraph` only ever answers
 * reachability queries (`try_is_reachable`, `try_find_path`) needed to
 * explain *why* an edge was rejected, well suited to formatting a
 * lockdep-style "would create a cycle: A -> B -> C -> A" diagnostic.
 *
 * Every traversal (`try_add_edge`, `try_is_reachable`, `try_find_path`) is
 * iterative -- an explicit worklist `vector<size_type>`, never recursion --
 * so `digraph` stays safe to use from a kernel/freestanding build
 * (`RELOCO_KERNEL`, see `reloco_config.hpp`) with a bounded stack, exactly
 * like the rest of reloco.
 *
 * Nodes are never removed (matching lock-class identifiers, which persist
 * for the lifetime of the registering subsystem); `try_remove_edge`
 * exists to retract a single dependency without discarding the node
 * itself.
 */

#include "default_allocator.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "vector.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief Allocator-backed directed graph over dense `std::size_t` node
 * indices, rejecting any edge that would close a cycle.
 *
 * See the file-level docs above for the intended use case (lock-order/
 * witness-style dependency tracking) and the rationale for the narrow,
 * integer-only, iterative API surface.
 */
class RELOCO_OWNER digraph {
public:
  using size_type = std::size_t;

  /// Sentinel returned by node/edge lookups that found nothing.
  static constexpr size_type npos = static_cast<size_type>(-1);

  constexpr digraph() noexcept = default;

  [[nodiscard]] static result<digraph> try_allocate(allocator_ref alloc) noexcept {
    auto adjacency_res = vector<vector<size_type>>::try_allocate(alloc);
    if (!adjacency_res)
      return unexpected(adjacency_res.error());
    return digraph(std::move(*adjacency_res));
  }

  [[nodiscard]] static result<digraph> try_create() noexcept { return try_allocate(default_allocator()); }

  /**
   * @brief Performs a deep copy of the graph (every node's adjacency list)
   * using a specific allocator.
   */
  [[nodiscard]] result<digraph> try_clone(allocator_ref alloc) const noexcept {
    auto clone_res = vector<vector<size_type>>::try_allocate(alloc, adjacency_.size());
    if (!clone_res)
      return unexpected(clone_res.error());
    for (const auto &edges : adjacency_) {
      auto edges_res = edges.try_clone(alloc);
      if (!edges_res)
        return unexpected(edges_res.error());
      if (auto push_res = clone_res->try_push_back(std::move(*edges_res)); !push_res)
        return unexpected(push_res.error());
    }
    return digraph(std::move(*clone_res));
  }

  [[nodiscard]] result<digraph> try_clone() const noexcept { return try_clone(adjacency_.get_allocator()); }

  [[nodiscard]] allocator_ref get_allocator() const noexcept { return adjacency_.get_allocator(); }

  /// Number of nodes currently in the graph.
  [[nodiscard]] size_type node_count() const noexcept { return adjacency_.size(); }

  [[nodiscard]] bool has_node(size_type node) const noexcept { return node < adjacency_.size(); }

  /**
   * @brief Allocates a new node and returns its index. Node indices are
   * dense and monotonically increasing (`0, 1, 2, ...`); nodes are never
   * removed or renumbered.
   */
  [[nodiscard]] result<size_type> try_add_node() noexcept {
    auto edges_res = vector<size_type>::try_allocate(adjacency_.get_allocator());
    if (!edges_res)
      return unexpected(edges_res.error());
    if (auto push_res = adjacency_.try_push_back(std::move(*edges_res)); !push_res)
      return unexpected(push_res.error());
    return adjacency_.size() - 1;
  }

  [[nodiscard]] bool has_edge(size_type from, size_type to) const noexcept {
    if (!has_node(from))
      return false;
    for (size_type n : adjacency_[from]) {
      if (n == to)
        return true;
    }
    return false;
  }

  /**
   * @brief Records that `from` was observed before `to` (e.g. lock `from`
   * acquired while already holding `to`... see the file docs for the
   * exact convention). Fails with:
   *   - `error::out_of_bounds` if either node does not exist.
   *   - `error::deadlock` if `from == to`, or if `to` can already reach
   *     `from` -- adding the edge would close a cycle. The graph is left
   *     unchanged in either failure case; use `try_find_path(to, from)` to
   *     recover the existing path that, together with the rejected edge,
   *     forms the cycle.
   * Adding an edge that already exists succeeds without duplicating it.
   */
  [[nodiscard]] result<void> try_add_edge(size_type from, size_type to) noexcept {
    if (!has_node(from) || !has_node(to))
      return unexpected(error::out_of_bounds);
    if (from == to)
      return unexpected(error::deadlock);
    if (has_edge(from, to))
      return {};
    auto reachable_res = try_is_reachable(to, from);
    if (!reachable_res)
      return unexpected(reachable_res.error());
    if (*reachable_res)
      return unexpected(error::deadlock);
    return adjacency_[from].try_push_back(to);
  }

  /**
   * @brief Removes the edge `from -> to`, if present. Never fails on a
   * missing edge/node -- it is simply a no-op.
   */
  result<void> try_remove_edge(size_type from, size_type to) noexcept {
    if (!has_node(from))
      return {};
    auto &edges = adjacency_[from];
    for (size_type i = 0; i < edges.size(); ++i) {
      if (edges[i] == to)
        return edges.try_erase_at(i);
    }
    return {};
  }

  /**
   * @brief Iterative (worklist-based, never recursive) reachability
   * check: whether a directed path `from -> ... -> to` exists. `from` can
   * always reach itself.
   */
  [[nodiscard]] result<bool> try_is_reachable(size_type from, size_type to) const noexcept {
    auto path_res = try_find_path(from, to);
    if (!path_res) {
      if (path_res.error() == error::not_found)
        return false;
      return unexpected(path_res.error());
    }
    return true;
  }

  /**
   * @brief Iterative (worklist-based, never recursive) BFS returning the
   * shortest node sequence `[from, ..., to]` (inclusive of both
   * endpoints), or `error::not_found` if `to` is not reachable from
   * `from`. Intended to explain a `try_add_edge` rejection: on failure,
   * `try_find_path(to, from)` recovers the path that -- together with the
   * rejected edge -- forms the cycle.
   */
  [[nodiscard]] result<vector<size_type>> try_find_path(size_type from, size_type to) const noexcept {
    if (!has_node(from) || !has_node(to))
      return unexpected(error::out_of_bounds);

    auto alloc = adjacency_.get_allocator();
    auto parent_res = vector<size_type>::try_allocate(alloc, adjacency_.size());
    if (!parent_res)
      return unexpected(parent_res.error());
    vector<size_type> parent = std::move(*parent_res);
    if (auto resize_res = parent.try_resize(adjacency_.size(), npos); !resize_res)
      return unexpected(resize_res.error());

    auto worklist_res = vector<size_type>::try_allocate(alloc);
    if (!worklist_res)
      return unexpected(worklist_res.error());
    vector<size_type> worklist = std::move(*worklist_res);

    if (auto push_res = worklist.try_push_back(from); !push_res)
      return unexpected(push_res.error());
    parent[from] = from; // Marks `from` visited; it is its own root.

    bool found = from == to;
    for (size_type head = 0; !found && head < worklist.size(); ++head) {
      const size_type current = worklist[head];
      for (size_type next : adjacency_[current]) {
        if (parent[next] != npos)
          continue;
        parent[next] = current;
        if (next == to) {
          found = true;
          break;
        }
        if (auto push_res = worklist.try_push_back(next); !push_res)
          return unexpected(push_res.error());
      }
    }
    if (!found)
      return unexpected(error::not_found);

    auto path_res = vector<size_type>::try_allocate(alloc);
    if (!path_res)
      return unexpected(path_res.error());
    vector<size_type> path = std::move(*path_res);
    for (size_type node = to;; node = parent[node]) {
      if (auto push_res = path.try_push_back(node); !push_res)
        return unexpected(push_res.error());
      if (node == from)
        break;
    }
    // `path` was built backwards (to -> ... -> from); reverse it in place.
    for (size_type i = 0, j = path.size() - 1; i < j; ++i, --j) {
      size_type tmp = path[i];
      path[i] = path[j];
      path[j] = tmp;
    }
    return path;
  }

  /// Removes every node and edge, keeping the current allocator.
  void clear() noexcept { adjacency_.clear(); }

private:
  explicit digraph(vector<vector<size_type>> &&adjacency) noexcept : adjacency_(std::move(adjacency)) {}

  vector<vector<size_type>> adjacency_;
};

/// `digraph`'s handle is just a `vector<vector<size_type>>`, itself always
/// trivially relocatable regardless of the (fixed, non-template) element
/// type -- see `relocatable.hpp`.
template <> struct is_trivially_relocatable<digraph> : std::true_type {};

} // namespace reloco
