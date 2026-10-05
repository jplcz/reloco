// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boost_intrusive_safe_interval_avl_set.hpp
 * @brief `safe_interval_avl_set<T, StartOf, EndOf, Options...>`: the same
 * address-sorted "list of VM areas" API as
 * `boost_intrusive_safe_interval_list.hpp`'s `safe_interval_list`, built
 * on `boost::intrusive::avl_set` instead of `boost::intrusive::list` --
 * `O(log n)` `find_containing()`/`insert_sorted()`/`try_resize()`
 * repositioning instead of `O(n)`, at the cost of needing a comparable
 * hook (`safe_avl_set_hook`/`safe_avl_set_member_hook`,
 * `boost_intrusive_safe_avl_set_hook.hpp`) instead of a plain list hook.
 *
 * Every operation documented in `boost_intrusive_safe_interval_list.hpp`
 * is available here with the same name/signature/semantics:
 * `find_containing`, `find_overlap`, `find_gap`, `find_random_gap`,
 * `for_each_gap`, `try_resize` (+ its `new_end`-only shorthand),
 * `try_rebase`, `insert_sorted`, `merge_adjacent` -- see that file's
 * docs for the full description of each. The differences worth calling
 * out:
 *
 * - `insert_sorted(node)` returns `result<T*>` here (not `void`): a
 *   `boost::intrusive::avl_set` enforces unique keys, so inserting a node
 *   whose `start_of()` collides with an already-tracked one fails with
 *   `error::already_exists` instead of silently appending (which is what
 *   a plain list would do). In practice this should never trigger for
 *   well-formed non-degenerate intervals -- two intervals sharing a
 *   `start_of()` necessarily overlap, which `find_overlap()`/
 *   `try_resize()`'s own overlap check already rejects -- but the tree's
 *   own invariant is enforced regardless.
 * - `insert_sorted()`/`try_resize()`'s repositioning step are `O(log n)`
 *   here (`avl_set::insert_unique()`), vs. the list version's `O(n)`
 *   linear walk to find the insertion point.
 * - `find_containing()` is `O(log n)` here (`upper_bound()` by address,
 *   then one step back), vs. the list version's `O(n)` linear scan.
 * - `find_overlap()`/`try_resize()`'s own overlap check/`merge_adjacent()`
 *   remain `O(n)` linear scans, same as the list version -- true
 *   interval-tree-style `O(log n + k)` overlap queries would need an
 *   augmented "max end in subtree" field per node, which is out of scope
 *   here (see the list header's own docs for the same tradeoff
 *   rationale: appropriate for small, mostly-static interval counts, not
 *   a replacement for a dedicated interval tree on thousands of entries).
 *
 * `StartOf` doubles as the tree's own ordering key (wrapped internally by
 * `avl_set_interval_start_compare`, passed to the base `avl_set` as a runtime
 * `boost::intrusive::compare<>` instance) -- iteration is therefore
 * *always* in ascending `start_of()` order, with no separate invariant to
 * maintain by discipline (unlike the list version, where only calling
 * `insert_sorted()` consistently keeps that true).
 *
 * ```cpp
 * struct vma : public reloco::boost_intrusive::safe_avl_set_hook<> {
 *   std::uintptr_t start, end;
 *   bool is_free;
 * };
 *
 * auto vmas = reloco::boost_intrusive::make_safe_interval_avl_set<vma>(
 *     [](const vma &v) { return v.start; }, [](const vma &v) { return v.end; });
 *
 * if (auto found = vmas.find_containing(fault_addr))
 *   handle_fault(**found);
 *
 * if (auto gap = vmas.find_gap(lo, hi, size, page_size))
 *   place_new_mapping(gap->start, size);
 *
 * if (auto inserted = vmas.insert_sorted(new_vma))
 *   use_inserted_mapping(**inserted);
 *
 * if (auto rebased = vmas.try_rebase(existing_vma, new_start,
 *                                      [](vma &v, std::uintptr_t start, std::uintptr_t end) {
 *                                        v.start = start;
 *                                        v.end = end;
 *                                      }))
 *   use_rebased_mapping(**rebased);
 * ```
 */

#include "boost_intrusive_safe_interval_list.hpp" // interval_gap<Address>
#include "error.hpp"
#include "expected.hpp"

#include <boost/intrusive/avl_set.hpp>

#include <type_traits>
#include <utility>

namespace reloco::boost_intrusive {

/**
 * @brief Runtime comparator passed to the base `avl_set` as a
 * `boost::intrusive::compare<>` instance: orders two `T`s by
 * `start_of()`, and additionally supports comparing a raw `Address`
 * key against a `T` (in both argument orders) -- a superset of the
 * overload set `boost::intrusive`'s heterogeneous `lower_bound()`/
 * `upper_bound()`/`find()` (and the tree's own internal value-to-value
 * ordering during `insert_unique()`) actually dispatch through,
 * verified directly against `<boost/intrusive/avl_set.hpp>` rather than
 * assumed. @p StartOf is stored by value (same ownership model as
 * `safe_interval_list`'s own `StartOf`/`EndOf` members).
 */
template <typename T, typename StartOf> struct avl_set_interval_start_compare {
  using address_type = std::decay_t<std::invoke_result_t<const StartOf &, const T &>>;

  StartOf start_of;

  [[nodiscard]] bool operator()(const T &a, const T &b) const noexcept { return start_of(a) < start_of(b); }
  [[nodiscard]] bool operator()(address_type a, const T &b) const noexcept { return a < start_of(b); }
  [[nodiscard]] bool operator()(const T &a, address_type b) const noexcept { return start_of(a) < b; }
  [[nodiscard]] bool operator()(address_type a, address_type b) const noexcept { return a < b; }
};

/**
 * @brief See the file-level docs. @p StartOf doubles as the tree's own
 * ordering key (see `avl_set_interval_start_compare`); @p EndOf is stored
 * separately, exactly like `safe_interval_list`. @p Options are
 * forwarded verbatim to `boost::intrusive::avl_set` (e.g. a
 * `boost::intrusive::member_hook<>` option to use
 * `safe_avl_set_member_hook` instead of the base-hook default).
 */
template <typename T, typename StartOf, typename EndOf, typename... Options>
class safe_interval_avl_set
    : public ::boost::intrusive::avl_set<T, ::boost::intrusive::compare<avl_set_interval_start_compare<T, StartOf>>,
                                         Options...> {
public:
  using compare_type = avl_set_interval_start_compare<T, StartOf>;
  using base_type = ::boost::intrusive::avl_set<T, ::boost::intrusive::compare<compare_type>, Options...>;
  using address_type = typename compare_type::address_type;

  static_assert(std::is_same_v<address_type, std::decay_t<std::invoke_result_t<const EndOf &, const T &>>>,
                "safe_interval_avl_set: StartOf and EndOf must return the same Address type");

  safe_interval_avl_set(StartOf start_of, EndOf end_of) noexcept
      : base_type(compare_type{static_cast<StartOf &&>(start_of)}), end_of_(static_cast<EndOf &&>(end_of)) {}

  /** @copydoc safe_interval_list::find_containing */
  [[nodiscard]] result<T *> find_containing(address_type address) noexcept {
    auto it = this->upper_bound(address, this->key_comp());
    if (it == this->begin())
      return unexpected(error::not_found);
    --it;
    if (address < end_of_(*it))
      return &*it;
    return unexpected(error::not_found);
  }

  /** @copydoc safe_interval_list::find_overlap */
  [[nodiscard]] result<T *> find_overlap(address_type start, address_type end) noexcept {
    for (T &item : *this) {
      if (start_of_(item) >= end)
        break; // Ascending order: no later item can overlap `[start, end)` either.
      if (start < end_of_(item))
        return &item;
    }
    return unexpected(error::not_found);
  }

  /** @copydoc safe_interval_list::find_gap */
  [[nodiscard]] result<interval_gap<address_type>> find_gap(address_type space_start, address_type space_end,
                                                            address_type min_size,
                                                            address_type alignment = address_type{1}) noexcept {
    result<interval_gap<address_type>> found = unexpected(error::not_found);
    for_each_gap(space_start, space_end, [&](address_type gap_start, address_type gap_end) noexcept {
      if (auto candidate = fits(gap_start, gap_end, min_size, alignment); candidate) {
        found = candidate;
        return true; // Stop at the first fit.
      }
      return false;
    });
    return found;
  }

  /** @copydoc safe_interval_list::find_random_gap */
  template <typename Rng>
  [[nodiscard]] result<interval_gap<address_type>> find_random_gap(address_type space_start, address_type space_end,
                                                                   address_type min_size, address_type alignment,
                                                                   Rng &&rng) noexcept {
    address_type total_slots = address_type{0};
    for_each_gap(space_start, space_end, [&](address_type gap_start, address_type gap_end) noexcept {
      total_slots = static_cast<address_type>(total_slots + slot_count(gap_start, gap_end, min_size, alignment));
      return false; // Never stop early: the total needs every gap.
    });
    if (total_slots == address_type{0})
      return unexpected(error::not_found);

    address_type pick = static_cast<address_type>(static_cast<address_type>(rng()) % total_slots);
    address_type step = alignment <= address_type{1} ? address_type{1} : alignment;

    result<interval_gap<address_type>> chosen = unexpected(error::not_found);
    for_each_gap(space_start, space_end, [&](address_type gap_start, address_type gap_end) noexcept {
      address_type slots = slot_count(gap_start, gap_end, min_size, alignment);
      if (slots == address_type{0})
        return false;
      if (pick < slots) {
        address_type chosen_start = static_cast<address_type>(align_up(gap_start, alignment) + pick * step);
        chosen = interval_gap<address_type>{chosen_start, static_cast<address_type>(chosen_start + min_size)};
        return true; // Stop: this is the gap `pick` landed in.
      }
      pick = static_cast<address_type>(pick - slots);
      return false;
    });
    return chosen;
  }

  /**
   * @brief Inserts @p node, keeping the tree's own ascending
   * `start_of()` order automatically (`O(log n)` via
   * `avl_set::insert_unique()` -- no manual walk needed, unlike
   * `safe_interval_list::insert_sorted()`). Fails with
   * `error::already_exists` if a node with an identical `start_of()` is
   * already tracked (see the file-level docs for why that should only
   * happen for malformed/overlapping input).
   */
  result<T *> insert_sorted(T &node) noexcept {
    if (auto [it, inserted] = this->insert_unique(node); inserted)
      return &*it;
    return unexpected(error::already_exists);
  }

  /** @copydoc safe_interval_list::try_resize */
  template <typename Resizer>
  result<T *> try_resize(T &node, address_type new_start, address_type new_end, Resizer resizer) noexcept {
    if (!(new_start < new_end))
      return unexpected(error::invalid_argument);
    for (T &item : *this) {
      if (&item == &node)
        continue;
      if (start_of_(item) >= new_end)
        break; // Ascending order: no later item can overlap `[new_start, new_end)` either.
      if (new_start < end_of_(item))
        return unexpected(error::already_exists);
    }
    resizer(node, new_start, new_end);
    this->erase(this->iterator_to(node));
    if (auto [it, inserted] = this->insert_unique(node); inserted)
      return &*it;
    return unexpected(error::already_exists); // Should be unreachable: the overlap check above already excludes this.
  }

  /** @copydoc safe_interval_list::try_resize */
  template <typename Resizer> result<T *> try_resize(T &node, address_type new_end, Resizer resizer) noexcept {
    return try_resize(node, start_of_(node), new_end, static_cast<Resizer &&>(resizer));
  }

  /** @copydoc safe_interval_list::try_rebase */
  template <typename Rebaser> result<T *> try_rebase(T &node, address_type new_start, Rebaser rebaser) noexcept {
    address_type size = static_cast<address_type>(end_of_(node) - start_of_(node));
    address_type new_end = static_cast<address_type>(new_start + size);
    if (new_end < new_start) // Overflow: the relocated range would wrap around.
      return unexpected(error::invalid_argument);
    return try_resize(node, new_start, new_end, static_cast<Rebaser &&>(rebaser));
  }

  /**
   * @brief Enumerates every free `[gap_start, gap_end)` region inside
   * `[space_start, space_end)`, around/between the tracked intervals, in
   * ascending order. @p visit(gap_start, gap_end) returns `true` to stop
   * enumeration early (e.g. first-fit) or `false` to keep going (e.g.
   * summing across all gaps). `find_gap()`/`find_random_gap()` are both
   * built on this; it is also exposed directly for callers that need a
   * custom gap-scoring policy.
   */
  template <typename Visitor>
  void for_each_gap(address_type space_start, address_type space_end, Visitor visit) noexcept {
    address_type cursor = space_start;
    for (T &item : *this) {
      address_type item_start = start_of_(item);
      if (item_start > cursor) {
        if (visit(cursor, item_start))
          return;
      }
      address_type item_end = end_of_(item);
      if (item_end > cursor)
        cursor = item_end;
      if (cursor >= space_end)
        return; // No room left before space_end -- no trailing gap either.
    }
    if (cursor < space_end)
      visit(cursor, space_end);
  }

  /** @copydoc safe_interval_list::merge_adjacent */
  template <typename Validator, typename Merger, typename Disposer>
  void merge_adjacent(Validator validator, Merger merger, Disposer disposer) noexcept {
    auto it = this->begin();
    if (it == this->end())
      return;
    auto next = it;
    ++next;
    while (next != this->end()) {
      T &a = *it;
      T &b = *next;
      if (validator(a, b)) {
        merger(a, b);
        next = this->erase(next);
        disposer(&b);
      } else {
        it = next;
        ++next;
      }
    }
  }

private:
  [[nodiscard]] address_type start_of_(const T &item) const noexcept { return this->key_comp().start_of(item); }

  [[nodiscard]] static constexpr result<interval_gap<address_type>>
  fits(address_type gap_start, address_type gap_end, address_type min_size, address_type alignment) noexcept {
    address_type aligned_start = align_up(gap_start, alignment);
    if (aligned_start >= gap_end || static_cast<address_type>(gap_end - aligned_start) < min_size)
      return unexpected(error::not_found);
    return interval_gap<address_type>{aligned_start, gap_end};
  }

  [[nodiscard]] static constexpr address_type slot_count(address_type gap_start, address_type gap_end,
                                                         address_type min_size, address_type alignment) noexcept {
    address_type aligned_start = align_up(gap_start, alignment);
    if (aligned_start >= gap_end)
      return address_type{0};
    address_type usable = static_cast<address_type>(gap_end - aligned_start);
    if (usable < min_size)
      return address_type{0};
    address_type step = alignment <= address_type{1} ? address_type{1} : alignment;
    return static_cast<address_type>(static_cast<address_type>(usable - min_size) / step + address_type{1});
  }

  [[nodiscard]] static constexpr address_type align_up(address_type value, address_type alignment) noexcept {
    if (alignment <= address_type{1})
      return value;
    address_type remainder = static_cast<address_type>(value % alignment);
    return remainder == address_type{0} ? value : static_cast<address_type>(value + (alignment - remainder));
  }

  EndOf end_of_;
};

/**
 * @brief Factory for `safe_interval_avl_set<T, StartOf, EndOf,
 * Options...>`: @p T must be given explicitly (it does not appear in @p
 * start_of/@p end_of's own types), @p StartOf/@p EndOf are deduced from
 * the arguments.
 */
template <typename T, typename... Options, typename StartOf, typename EndOf>
[[nodiscard]] auto make_safe_interval_avl_set(StartOf start_of, EndOf end_of) noexcept {
  return safe_interval_avl_set<T, StartOf, EndOf, Options...>(static_cast<StartOf &&>(start_of),
                                                              static_cast<EndOf &&>(end_of));
}

} // namespace reloco::boost_intrusive
