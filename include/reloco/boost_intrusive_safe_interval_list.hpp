// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boost_intrusive_safe_interval_list.hpp
 * @brief `safe_interval_list<T, StartOf, EndOf>`: a `safe_list<T>`
 * derivative (`boost_intrusive_safe_list.hpp`) adding the handful of
 * operations an *address-sorted interval list* needs that a plain list
 * doesn't -- the classic "list of VM areas" shape (c.f. the historical
 * singly/doubly linked `vm_map_entry` list predating BSD's/Linux's
 * tree-indexed VM maps, or any firmware/embedded free-region tracker
 * that doesn't want a full tree just to track a handful of reservations):
 *
 * - `find_containing(address)`: which interval (if any) covers a given
 *   address.
 * - `find_overlap(start, end)`: which interval (if any) overlaps a
 *   candidate `[start, end)` range -- the check every "can I place a new
 *   mapping/reservation here" call needs before inserting.
 * - `find_gap(space_start, space_end, min_size, alignment)`: first-fit
 *   free space search between/around the tracked intervals -- the
 *   counterpart lookup for "where *can* I place it".
 * - `find_random_gap(space_start, space_end, min_size, alignment, rng)`:
 *   ASLR-style placement -- uniformly picks one of the valid aligned
 *   placements across *all* fitting gaps (not just the first one),
 *   driven by a caller-supplied templated `rng` callable.
 * - `for_each_gap(space_start, space_end, visit)`: the lower-level gap
 *   enumerator `find_gap()`/`find_random_gap()` are both built on,
 *   exposed directly for callers that need a custom gap-scoring policy
 *   (best-fit, extra placement constraints, ...) neither of those cover.
 * - `insert_sorted(node)`: insert keeping ascending `start_of()` order,
 *   since every operation above assumes that invariant.
 * - `try_resize(node, new_start, new_end, resizer)` (or the
 *   `try_resize(node, new_end, resizer)` shorthand that keeps the
 *   current start): grow/shrink/move an already-tracked interval in
 *   place, failing with `error::already_exists` instead of mutating
 *   anything if the new range would collide with a neighbor.
 * - `try_rebase(node, new_start, rebaser)`: move an already-tracked
 *   interval to a new base address while keeping its size unchanged --
 *   a `try_resize()` convenience for the common "relocate this mapping,
 *   don't resize it" case (e.g. re-randomizing a region's placement).
 * - `merge_adjacent(validator, merger, disposer)`: coalesce
 *   contiguous/compatible neighbors (e.g. two adjacent free regions, or
 *   two adjacent mappings with identical permissions) into one, chaining
 *   through any number of mergeable runs.
 *
 * None of this requires a specific field layout: @p StartOf/@p EndOf are
 * caller-supplied projections (`Address StartOf(const T&)`/
 * `Address EndOf(const T&)`), matching `extract_if()`'s own
 * caller-supplied `Pred` -- `make_safe_interval_list<T>(start_of, end_of)`
 * below is the recommended way to construct one, since class template
 * argument deduction cannot infer `T` itself from the projections alone.
 *
 * All the search operations here are `O(n)` linear scans (with an
 * early-exit once the ascending-order invariant proves no further match
 * is possible) -- appropriate for the small, mostly-static interval
 * counts typical of VM area/region-reservation lists on embedded/RTOS
 * targets, not a replacement for a tree-indexed VM map on a workload with
 * thousands of mappings.
 *
 * ```cpp
 * struct vma : public reloco::boost_intrusive::safe_list_hook<> {
 *   std::uintptr_t start, end;
 *   bool is_free;
 * };
 *
 * auto vmas = reloco::boost_intrusive::make_safe_interval_list<vma>(
 *     [](const vma &v) { return v.start; }, [](const vma &v) { return v.end; });
 *
 * // "Is this address mapped, and by which vma?"
 * if (auto found = vmas.find_containing(fault_addr))
 *   handle_fault(**found);
 *
 * // "Can I place a new SIZE-byte mapping anywhere in [lo, hi)?"
 * if (auto gap = vmas.find_gap(lo, hi, size, page_size))
 *   place_new_mapping(gap->start, size);
 *
 * // ASLR: same question, but randomized across every valid placement
 * // instead of always the first. `rng` only needs to be callable with
 * // no arguments and return some unsigned integral value -- reduced via
 * // modulo against the number of valid placements, so any PRNG works
 * // (a kernel entropy source, a seeded `std::mt19937`, ...); this header
 * // itself never includes `<random>`.
 * if (auto gap = vmas.find_random_gap(lo, hi, size, page_size, [&] { return kernel_get_random_u64(); }))
 *   place_new_mapping(gap->start, size);
 *
 * // Custom placement policy built directly on the gap enumerator, e.g.
 * // best-fit instead of first-fit/random:
 * std::uintptr_t best_start = 0, best_size = SIZE_MAX;
 * vmas.for_each_gap(lo, hi, [&](std::uintptr_t gap_start, std::uintptr_t gap_end) {
 *   std::uintptr_t gap_size = gap_end - gap_start;
 *   if (gap_size >= size && gap_size < best_size) {
 *     best_start = gap_start;
 *     best_size = gap_size;
 *   }
 *   return false; // Keep scanning every gap.
 * });
 *
 * // Keep the list's own ordering invariant when inserting new entries.
 * vmas.insert_sorted(new_vma);
 *
 * // Grow an existing mapping forward in place (fails instead of
 * // mutating anything if that would collide with the next vma).
 * if (auto resized = vmas.try_resize(existing_vma, new_end,
 *                                     [](vma &v, std::uintptr_t, std::uintptr_t end) { v.end = end; }))
 *   use_resized_mapping(**resized);
 *
 * // Relocate an existing mapping to a new base address without
 * // resizing it (fails instead of mutating anything if that would
 * // collide with a neighbor at the new location).
 * if (auto rebased = vmas.try_rebase(existing_vma, new_start,
 *                                      [](vma &v, std::uintptr_t start, std::uintptr_t end) {
 *                                        v.start = start;
 *                                        v.end = end;
 *                                      }))
 *   use_rebased_mapping(**rebased);
 *
 * // Coalesce adjacent free regions after an unmap.
 * vmas.merge_adjacent(
 *     [](const vma &a, const vma &b) { return a.is_free && b.is_free && a.end == b.start; },
 *     [](vma &a, const vma &b) { a.end = b.end; },
 *     [](vma *b) { delete b; });
 * ```
 */

#include "boost_intrusive_safe_list.hpp"
#include "error.hpp"
#include "expected.hpp"

#include <type_traits>
#include <utility>

namespace reloco::boost_intrusive {

/**
 * @brief Half-open address gap `[start, end)` found by
 * `safe_interval_list::find_gap()`.
 */
template <typename Address> struct interval_gap {
  Address start;
  Address end;

  [[nodiscard]] constexpr Address size() const noexcept { return static_cast<Address>(end - start); }
};

/**
 * @brief See the file-level docs. @p StartOf/@p EndOf are stored by
 * value (expected to be small, typically stateless, callables, e.g. a
 * lambda or a pointer-to-member-wrapping function object) and invoked as
 * `start_of(const T&)`/`end_of(const T&)` to recover each element's
 * `[start, end)` range -- `Address` is deduced as whatever they return.
 */
template <typename T, typename StartOf, typename EndOf> class safe_interval_list : public safe_list<T> {
public:
  using base_type = safe_list<T>;
  using address_type = std::decay_t<std::invoke_result_t<const StartOf &, const T &>>;

  static_assert(std::is_same_v<address_type, std::decay_t<std::invoke_result_t<const EndOf &, const T &>>>,
                "safe_interval_list: StartOf and EndOf must return the same Address type");

  safe_interval_list(StartOf start_of, EndOf end_of) noexcept
      : start_of_(static_cast<StartOf &&>(start_of)), end_of_(static_cast<EndOf &&>(end_of)) {}

  /**
   * @brief Locates the (at most one, assuming disjoint intervals) element
   * whose `[start_of(item), end_of(item))` range covers @p address.
   * Fails with `error::not_found`. Relies on ascending `start_of()` order
   * (see `insert_sorted()`) only to exit early, not for correctness.
   */
  [[nodiscard]] result<T *> find_containing(address_type address) noexcept {
    for (T &item : *this) {
      if (start_of_(item) > address)
        break; // Ascending order: no later item can cover `address` either.
      if (address < end_of_(item))
        return &item;
    }
    return unexpected(error::not_found);
  }

  /**
   * @brief Locates the first element whose `[start_of(item),
   * end_of(item))` range overlaps the candidate `[start, end)` range --
   * the "is this placement free" check. Fails with `error::not_found`.
   */
  [[nodiscard]] result<T *> find_overlap(address_type start, address_type end) noexcept {
    for (T &item : *this) {
      if (start_of_(item) >= end)
        break; // Ascending order: no later item can overlap `[start, end)` either.
      if (start < end_of_(item))
        return &item;
    }
    return unexpected(error::not_found);
  }

  /**
   * @brief First-fit search for a gap of at least @p min_size bytes,
   * aligned to @p alignment, inside `[space_start, space_end)` and
   * around/between the tracked intervals. Fails with `error::not_found`.
   * Relies on ascending `start_of()` order for correctness, not just as
   * an optimization (see `insert_sorted()`).
   */
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

  /**
   * @brief ASLR-style counterpart to `find_gap()`: instead of always the
   * first fitting placement, uniformly picks one of the valid aligned
   * placements across *every* gap that fits @p min_size (not just the
   * first gap encountered), weighted by how many aligned positions each
   * gap actually offers. Fails with `error::not_found` if nothing fits
   * anywhere in `[space_start, space_end)`.
   *
   * Two `O(n)` passes over the gaps (no allocation): the first sums the
   * total number of valid placements, the second walks the same gaps
   * again to locate the one @p rng's draw landed in. Relies on ascending
   * `start_of()` order for correctness, same as `find_gap()`.
   *
   * @p rng is invoked as `rng()` (no arguments) and must return some
   * unsigned integral value; it is reduced via `% total_slots`, so a
   * non-uniform or narrower-than-`address_type` source only biases
   * (does not break) the distribution -- acceptable for ASLR, not meant
   * for cryptographic placement guarantees. @p Rng is a template
   * parameter (not type-erased), so e.g. a project-specific kernel PRNG
   * struct or a stateless lambda both work with no virtual call/vtable
   * overhead; this header itself never includes `<random>`.
   */
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
   * @brief Inserts @p node keeping ascending `start_of()` order -- the
   * invariant every search operation above relies on. `O(n)`, same as
   * the searches themselves.
   */
  void insert_sorted(T &target) noexcept {
    address_type node_start = start_of_(target);
    for (auto it = this->begin(), last = this->end(); it != last; ++it) {
      if (start_of_(*it) > node_start) {
        this->insert(it, target);
        return;
      }
    }
    this->push_back(target);
  }

  /**
   * @brief Attempts to resize @p node in place to span `[new_start,
   * new_end)` instead of its current `[start_of(node), end_of(node))`
   * range -- the "grow/shrink/move an existing VM area without
   * reallocating it" operation (c.f. `mremap(2)`'s in-place resize).
   * Fails with `error::invalid_argument` if `new_start >= new_end`, or
   * `error::already_exists` if the new range would overlap any *other*
   * tracked interval (@p node's own current range is ignored -- that's
   * exactly what's being replaced); @p node is left completely untouched
   * on failure, and @p resizer is not invoked. On success, @p
   * resizer(node, new_start, new_end) performs the actual field
   * mutation (e.g. `node.start = new_start; node.end = new_end;`), then
   * @p node is unlinked and reinserted via `insert_sorted()` to keep the
   * ascending `start_of()` order every search above relies on (even if
   * @p node didn't actually need to move).
   *
   * `O(n)`: one pass to validate against every other interval (with the
   * same ascending-order early exit `find_overlap()` uses), plus
   * `insert_sorted()`'s own `O(n)` reinsertion.
   */
  template <typename Resizer>
  result<T *> try_resize(T &target, address_type new_start, address_type new_end, Resizer resizer) noexcept {
    if (!(new_start < new_end))
      return unexpected(error::invalid_argument);
    for (T &item : *this) {
      if (&item == &target)
        continue;
      if (start_of_(item) >= new_end)
        break; // Ascending order: no later item can overlap `[new_start, new_end)` either.
      if (new_start < end_of_(item))
        return unexpected(error::already_exists);
    }
    resizer(target, new_start, new_end);
    this->erase(this->iterator_to(target));
    insert_sorted(target);
    return &target;
  }

  /**
   * @brief Convenience overload of `try_resize()` that keeps @p node's
   * current `start_of(node)` and only changes its end -- the common
   * "grow/shrink this mapping forward" case.
   */
  template <typename Resizer> result<T *> try_resize(T &target, address_type new_end, Resizer resizer) noexcept {
    return try_resize(target, start_of_(target), new_end, static_cast<Resizer &&>(resizer));
  }

  /**
   * @brief Attempts to relocate @p node in place to a new base address
   * @p new_start, keeping its current size (`end_of(node) -
   * start_of(node)`) unchanged -- the "move this VM area without
   * resizing it" operation (e.g. re-randomizing a region's placement
   * post-fork, or sliding a reservation to make room for something
   * else). Built directly on `try_resize()`: fails with
   * `error::invalid_argument` on an address_type overflow while
   * computing the new end, or `error::already_exists` if the relocated
   * range would overlap any *other* tracked interval; @p node is left
   * completely untouched on failure, and @p rebaser is not invoked. On
   * success, @p rebaser(node, new_start, new_end) performs the actual
   * field mutation (the same signature as `try_resize()`'s own @p
   * resizer, so one callback can serve both), then @p node is
   * repositioned to keep the ascending `start_of()` order invariant.
   *
   * `O(n)`, same as `try_resize()` itself.
   */
  template <typename Rebaser> result<T *> try_rebase(T &target, address_type new_start, Rebaser rebaser) noexcept {
    address_type size = static_cast<address_type>(end_of_(target) - start_of_(target));
    address_type new_end = static_cast<address_type>(new_start + size);
    if (new_end < new_start) // Overflow: the relocated range would wrap around.
      return unexpected(error::invalid_argument);
    return try_resize(target, new_start, new_end, static_cast<Rebaser &&>(rebaser));
  }

  /**
   * @brief Walks the list once, coalescing each contiguous run of
   * elements for which @p validator(a, b) holds for every consecutive
   * pair into their first element: @p merger(a, b) mutates @p a in place
   * to absorb @p b's range, @p b is unlinked, then handed to @p
   * disposer(T *) (matching `isolated_node_tx::release_to()`'s own
   * `Disposer` convention, e.g. `default_delete_disposer<T>()` from
   * `boost_intrusive_adapter.hpp`). Chains: if @p a absorbs @p b and the
   * new next element is also mergeable with @p a, it is absorbed too.
   */
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
        // `it` still denotes `a` (now covering the merged range); re-check
        // it against the new `next` to absorb further contiguous runs.
      } else {
        it = next;
        ++next;
      }
    }
  }

  /**
   * @brief Enumerates every free `[gap_start, gap_end)` region inside
   * `[space_start, space_end)`, around/between the tracked intervals, in
   * ascending order. @p visit(gap_start, gap_end) returns `true` to stop
   * enumeration early (e.g. first-fit) or `false` to keep going (e.g.
   * summing across all gaps). `find_gap()`/`find_random_gap()` are both
   * built on this; it is also exposed directly for callers that need a
   * custom gap-scoring policy (e.g. best-fit, or a placement constraint
   * beyond plain size/alignment) that neither of those covers.
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

private:
  [[nodiscard]] static constexpr result<interval_gap<address_type>>
  fits(address_type gap_start, address_type gap_end, address_type min_size, address_type alignment) noexcept {
    address_type aligned_start = align_up(gap_start, alignment);
    if (aligned_start >= gap_end || static_cast<address_type>(gap_end - aligned_start) < min_size)
      return unexpected(error::not_found);
    return interval_gap<address_type>{aligned_start, gap_end};
  }

  /**
   * @brief Number of distinct @p alignment-spaced positions at which a
   * @p min_size-sized placement fits inside `[gap_start, gap_end)`; `0`
   * if it doesn't fit at all. Used by `find_random_gap()` to weight each
   * gap by how many valid placements it actually offers.
   */
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

  StartOf start_of_;
  EndOf end_of_;
};

/**
 * @brief Factory for `safe_interval_list<T, StartOf, EndOf>`: @p T must
 * be given explicitly (it does not appear in @p start_of/@p end_of's own
 * types), @p StartOf/@p EndOf are deduced from the arguments.
 */
template <typename T, typename StartOf, typename EndOf>
[[nodiscard]] auto make_safe_interval_list(StartOf start_of, EndOf end_of) noexcept {
  return safe_interval_list<T, StartOf, EndOf>(static_cast<StartOf &&>(start_of), static_cast<EndOf &&>(end_of));
}

} // namespace reloco::boost_intrusive
