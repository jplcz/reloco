// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file bucket_growth.hpp
 * @brief Optional, integer-only bucket-count growth curves for hash
 * containers whose bucket array must be sized externally by the caller.
 * `intrusive_hash_table`'s `rehash()` / `suggest_bucket_count_for_insert`
 * / `suggest_bucket_count_for_remove` (see `intrusive_hash_table.hpp`)
 * are the motivating case, but nothing here is tied to that type
 * specifically.
 *
 * `intrusive_hash_table::suggest_bucket_count_for_insert`/`_for_remove`
 * already cover the simplest, most common policy entirely on their own
 * -- no dependency on this header at all: a tight linear fit, `buckets
 * == ceil(projected_elements / elements_per_bucket)`, recomputed from
 * scratch every call regardless of the table's *current* bucket count.
 * That is the right default. It is not, however, the only sensible
 * *growth* policy: repeatedly calling it while inserting one element at
 * a time would suggest rehashing on every single insert once `size()`
 * crosses each `elements_per_bucket` multiple -- the same
 * "reallocate on every push" cost a `vector` would pay without amortized
 * growth. The strategies in this header are exactly that amortization,
 * factored out as small, stateless-or-parameterized callable types (not
 * virtual/type-erased -- see `detail/flat_hash_base.hpp`'s doc comment
 * for the same "`Hash`/`KeyEqual` are ordinary template parameters"
 * rationale) that a caller can invoke directly, entirely independently
 * of `intrusive_hash_table` itself:
 *
 * @code
 * reloco::bucket_growth::doubling_then_ratio strategy{.knee_buckets = 1024};
 * std::size_t next = strategy.next_bucket_count(table.bucket_count(), table.size() + n,
 *                                                elements_per_bucket, min_buckets, max_buckets);
 * @endcode
 *
 * Every strategy here takes the same five arguments and returns the
 * same thing: a bucket count already clamped to the caller's
 * `[min_buckets, max_buckets]` "sensible" range (matching
 * `intrusive_hash_table::suggest_bucket_count_for_insert`'s own
 * contract), so any of them is a drop-in replacement for another.
 * None of them ever use a `float`/`double` -- see
 * `detail/flat_hash_base.hpp`'s `load_factor_permille` doc comment for
 * why reloco avoids floating point everywhere.
 *
 * - `linear`: the tight-fit policy `intrusive_hash_table`'s own default
 *   already implements inline -- `ceil(projected_elements /
 *   elements_per_bucket)`, ignoring `current_buckets` entirely. Included
 *   here mainly so a caller that picks a strategy at runtime has every
 *   strategy, including this one, behind the exact same interface.
 * - `doubling_then_ratio`: doubles (fast, halves the number of rehashes
 *   needed to reach any given size) while `current_buckets` is below
 *   `knee_buckets`; above that, grows by a smaller
 *   `ratio_numerator/ratio_denominator` factor per step instead (default
 *   `5/4`) -- trading rehash frequency for a tighter memory fit once the
 *   table is already large. This is the "grow fast for small N, then
 *   slow down" curve some `vector`/hash-map growth policies use to avoid
 *   overshooting memory budgets at scale.
 * - `fixed_ratio`: grows by one configurable
 *   `ratio_numerator/ratio_denominator` factor throughout, no doubling
 *   phase -- for callers who want one predictable rate everywhere
 *   instead of `doubling_then_ratio`'s two-phase curve.
 * - `power_of_two`: rounds the tight linear target up to the next power
 *   of two, ignoring `current_buckets` -- the classic doubling-to-pow2
 *   curve `detail/flat_hash_base.hpp` uses internally, offered here for
 *   callers whose own indexing/masking assumes a power-of-two bucket
 *   count (`intrusive_hash_table` itself does not need this -- see
 *   `index_for`'s plain modulo -- so this is purely for interop/habit).
 * - `prime_growth`: rounds the tight linear target up to the next prime
 *   (via trial division, never a precomputed/copied table), matching
 *   the classic technique some `unordered_map` implementations use to
 *   spread poorly-distributed/weak hash functions more evenly across
 *   buckets than a power-of-two bucket count would -- meaningful here
 *   specifically because `index_for` is plain modulo, not a pow2 mask,
 *   so a prime bucket count actually changes probe behavior.
 * - `chunked`: rounds the tight linear target up to the next multiple of
 *   a configurable `chunk_size` -- for callers whose bucket storage
 *   comes from a fixed-size pool/page/slab allocator rather than an
 *   arbitrarily-sized `span<T *>`.
 * - `sqrt_curve`: sub-linear -- bucket count grows roughly with the
 *   *square root* of the projected element count (via integer Newton's
 *   method, never a `float`), so it slows down far more aggressively
 *   than any ratio-based strategy above as the table grows -- at the
 *   cost of average chain length growing right along with it (roughly
 *   `sqrt(N)` instead of staying flat). Only sensible when memory is far
 *   more precious than lookup speed and the largest anticipated `N` is
 *   modest.
 */

#include "detail/assert.hpp"
#include "int_ops.hpp"

#include <cstddef>

namespace reloco::bucket_growth {

namespace detail {

[[nodiscard]] constexpr std::size_t ceil_div(std::size_t numerator, std::size_t denominator) noexcept {
  return numerator / denominator + (numerator % denominator != 0 ? 1 : 0);
}

[[nodiscard]] constexpr std::size_t clamp_bucket_count(std::size_t target, std::size_t min_buckets,
                                                       std::size_t max_buckets) noexcept {
  if (target < min_buckets)
    return min_buckets;
  if (target > max_buckets)
    return max_buckets;
  return target;
}

/**
 * @brief `floor(sqrt(n))` via integer Newton's method -- never forms a
 * `float`/`double`, converges in `O(log n)` steps.
 */
[[nodiscard]] constexpr std::size_t isqrt(std::size_t n) noexcept {
  if (n < 2)
    return n;
  std::size_t x = n;
  std::size_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + n / x) / 2;
  }
  return x;
}

/**
 * @brief Smallest power of two `>= n` (`1` for `n <= 1`). Left-shifts an
 * unsigned `std::size_t`, which is well-defined (wraps to `0`) even past
 * the top bit -- the `next <= p` check stops the loop right there
 * instead of spinning forever, returning the largest representable
 * power of two in that (practically unreachable) corner case.
 */
[[nodiscard]] constexpr std::size_t next_pow2(std::size_t n) noexcept {
  if (n <= 1)
    return 1;
  std::size_t p = 1;
  while (p < n) {
    std::size_t next = p << 1;
    if (next <= p) // overflowed past the top bit
      break;
    p = next;
  }
  return p;
}

/**
 * @brief Trial-division primality test, odd-divisor-only past `2`/`3`
 * (`6k +/- 1` stepping) -- `O(sqrt(n))`, fine for an advisory,
 * off-the-hot-path sizing call. No precomputed prime table (nothing to
 * keep in sync/attribute), just arithmetic.
 */
[[nodiscard]] constexpr bool is_prime(std::size_t n) noexcept {
  if (n < 2)
    return false;
  if (n % 2 == 0)
    return n == 2;
  if (n % 3 == 0)
    return n == 3;
  std::size_t limit = isqrt(n);
  for (std::size_t i = 5; i <= limit; i += 6) {
    if (n % i == 0 || n % (i + 2) == 0)
      return false;
  }
  return true;
}

/** @brief Smallest prime `>= n` (`2` for `n <= 2`), via `is_prime`. */
[[nodiscard]] constexpr std::size_t next_prime(std::size_t n) noexcept {
  if (n <= 2)
    return 2;
  std::size_t candidate = n % 2 == 0 ? n + 1 : n;
  while (!is_prime(candidate))
    candidate += 2;
  return candidate;
}

} // namespace detail

/**
 * @brief See the file-level doc comment. `ceil(projected_elements /
 * elements_per_bucket)`, clamped to `[min_buckets, max_buckets]`;
 * ignores @p current_buckets entirely.
 *
 * Rough shape (bucket count vs. elements inserted over time -- tiny,
 * constant-size steps the whole way, tracking the element count as
 * tightly as possible):
 *
 * @verbatim
 * buckets
 *   ^                                              _.-'
 *   |                                          _.-'
 *   |                                      _.-'
 *   |                                  _.-'
 *   |                              _.-'
 *   |                          _.-'
 *   |                      _.-'
 *   |                  _.-'
 *   |              _.-'
 *   |          _.-'
 *   |      _.-'
 *   |  _.-'
 *   +------------------------------------------------> elements inserted
 * @endverbatim
 */
struct linear {
  [[nodiscard]] std::size_t next_bucket_count(std::size_t /*current_buckets*/, std::size_t projected_elements,
                                              std::size_t elements_per_bucket, std::size_t min_buckets,
                                              std::size_t max_buckets) const noexcept {
    RELOCO_ASSERT(elements_per_bucket >= 1, "reloco::bucket_growth::linear: elements_per_bucket must be >= 1");
    RELOCO_ASSERT(min_buckets >= 1 && min_buckets <= max_buckets,
                  "reloco::bucket_growth::linear: min_buckets must be >= 1 and <= max_buckets");
    return detail::clamp_bucket_count(detail::ceil_div(projected_elements, elements_per_bucket), min_buckets,
                                      max_buckets);
  }
};

/**
 * @brief See the file-level doc comment. Doubles while @p current_buckets
 * is below `knee_buckets`, then grows by `ratio_numerator /
 * ratio_denominator` per step above that. `ratio_numerator` must exceed
 * `ratio_denominator` (otherwise growth would stall or shrink);
 * `RELOCO_ASSERT`ed. Growth is `saturating_mul`-safe (see `int_ops.hpp`)
 * -- an overflow-adjacent step simply falls back to the plain linear
 * target instead of wrapping.
 *
 * Rough shape (bucket count vs. elements inserted over time -- big,
 * widely-spaced jumps below `knee_buckets`, small, frequent jumps once
 * past it):
 *
 * @verbatim
 * buckets
 *   ^                                        ___,--*--,--*--,--*
 *   |                                  ___,-*'  (ratio phase: small,
 *   |                            __,--*'         frequent 5/4 steps)
 *   |  - - - - - - - - - - -,--*'- - - - - - - - - - - - - - - - -  <- knee_buckets
 *   |                   _,-*'
 *   |               _,-*'   (doubling phase: big, rare x2 jumps)
 *   |           _,-*'
 *   |        ,-*'
 *   |     ,-*
 *   |   ,*
 *   +------------------------------------------------> elements inserted
 * @endverbatim
 */
struct doubling_then_ratio {
  std::size_t knee_buckets = 1024;
  std::size_t ratio_numerator = 5;
  std::size_t ratio_denominator = 4;

  [[nodiscard]] std::size_t next_bucket_count(std::size_t current_buckets, std::size_t projected_elements,
                                              std::size_t elements_per_bucket, std::size_t min_buckets,
                                              std::size_t max_buckets) const noexcept {
    RELOCO_ASSERT(elements_per_bucket >= 1,
                  "reloco::bucket_growth::doubling_then_ratio: elements_per_bucket must be >= 1");
    RELOCO_ASSERT(min_buckets >= 1 && min_buckets <= max_buckets,
                  "reloco::bucket_growth::doubling_then_ratio: min_buckets must be >= 1 and <= max_buckets");
    RELOCO_ASSERT(ratio_numerator > ratio_denominator,
                  "reloco::bucket_growth::doubling_then_ratio: ratio_numerator must be > ratio_denominator");

    std::size_t target = detail::ceil_div(projected_elements, elements_per_bucket);
    std::size_t buckets = current_buckets == 0 ? 1 : current_buckets;
    while (buckets < target) {
      std::size_t next = buckets < knee_buckets
                             ? saturating_mul(buckets, std::size_t{2})
                             : detail::ceil_div(saturating_mul(buckets, ratio_numerator), ratio_denominator);
      if (next <= buckets) // saturated at SIZE_MAX -- fall back to the plain linear target below
        break;
      buckets = next;
    }
    return detail::clamp_bucket_count(buckets < target ? target : buckets, min_buckets, max_buckets);
  }
};

/**
 * @brief See the file-level doc comment. Grows by one fixed
 * `ratio_numerator / ratio_denominator` factor throughout -- no doubling
 * phase. Same `ratio_numerator > ratio_denominator` requirement and
 * overflow fallback as `doubling_then_ratio`.
 *
 * Rough shape (bucket count vs. elements inserted over time -- one
 * consistent step size the whole way, no knee):
 *
 * @verbatim
 * buckets
 *   ^                                              ,--*--,--*--,--*
 *   |                                        ,--*-''
 *   |                                  ,--*-''
 *   |                            ,--*-''
 *   |                      ,--*-''
 *   |                ,--*-''
 *   |          ,--*-''
 *   |    ,--*-''
 *   | ,*''
 *   +------------------------------------------------> elements inserted
 * @endverbatim
 */
struct fixed_ratio {
  std::size_t ratio_numerator = 5;
  std::size_t ratio_denominator = 4;

  [[nodiscard]] std::size_t next_bucket_count(std::size_t current_buckets, std::size_t projected_elements,
                                              std::size_t elements_per_bucket, std::size_t min_buckets,
                                              std::size_t max_buckets) const noexcept {
    RELOCO_ASSERT(elements_per_bucket >= 1, "reloco::bucket_growth::fixed_ratio: elements_per_bucket must be >= 1");
    RELOCO_ASSERT(min_buckets >= 1 && min_buckets <= max_buckets,
                  "reloco::bucket_growth::fixed_ratio: min_buckets must be >= 1 and <= max_buckets");
    RELOCO_ASSERT(ratio_numerator > ratio_denominator,
                  "reloco::bucket_growth::fixed_ratio: ratio_numerator must be > ratio_denominator");

    std::size_t target = detail::ceil_div(projected_elements, elements_per_bucket);
    std::size_t buckets = current_buckets == 0 ? 1 : current_buckets;
    while (buckets < target) {
      std::size_t next = detail::ceil_div(saturating_mul(buckets, ratio_numerator), ratio_denominator);
      if (next <= buckets) // saturated at SIZE_MAX -- fall back to the plain linear target below
        break;
      buckets = next;
    }
    return detail::clamp_bucket_count(buckets < target ? target : buckets, min_buckets, max_buckets);
  }
};

/**
 * @brief See the file-level doc comment. Rounds `ceil(projected_elements
 * / elements_per_bucket)` up to the next power of two, clamped to
 * `[min_buckets, max_buckets]`; ignores @p current_buckets entirely --
 * the classic doubling-to-pow2 curve, offered here for callers whose own
 * indexing/masking assumes a power-of-two bucket count.
 *
 * Rough shape (bucket count vs. elements inserted over time -- flat
 * plateaus at each power of two, then a sudden jump to the next one):
 *
 * @verbatim
 * buckets
 *   ^                                            ________________
 *   |                                    ________|
 *   |                            ________|
 *   |                    ________|
 *   |            ________|
 *   |     _______|
 *   |_____|
 *   +------------------------------------------------> elements inserted
 * @endverbatim
 */
struct power_of_two {
  [[nodiscard]] std::size_t next_bucket_count(std::size_t /*current_buckets*/, std::size_t projected_elements,
                                              std::size_t elements_per_bucket, std::size_t min_buckets,
                                              std::size_t max_buckets) const noexcept {
    RELOCO_ASSERT(elements_per_bucket >= 1, "reloco::bucket_growth::power_of_two: elements_per_bucket must be >= 1");
    RELOCO_ASSERT(min_buckets >= 1 && min_buckets <= max_buckets,
                  "reloco::bucket_growth::power_of_two: min_buckets must be >= 1 and <= max_buckets");
    std::size_t target = detail::ceil_div(projected_elements, elements_per_bucket);
    return detail::clamp_bucket_count(detail::next_pow2(target), min_buckets, max_buckets);
  }
};

/**
 * @brief See the file-level doc comment. Rounds `ceil(projected_elements
 * / elements_per_bucket)` up to the next prime (trial division, no
 * precomputed table), clamped to `[min_buckets, max_buckets]`; ignores
 * @p current_buckets entirely. Meaningful specifically because
 * `index_for` is plain modulo, not a power-of-two mask -- a prime bucket
 * count spreads a poorly-distributed/weak hash function's output more
 * evenly across chains than a power of two would (the classic technique
 * some `unordered_map` implementations use).
 *
 * Rough shape (bucket count vs. elements inserted over time -- similar
 * plateau-then-jump shape to `power_of_two`, but the jumps land on
 * primes instead of powers of two, so plateaus are shorter/more
 * irregular):
 *
 * @verbatim
 * buckets
 *   ^                                       __--___--___--___
 *   |                             __--___---
 *   |                       __---
 *   |                 __---
 *   |             __--
 *   |         __--
 *   |     __--
 *   |_____|
 *   +------------------------------------------------> elements inserted
 * @endverbatim
 */
struct prime_growth {
  [[nodiscard]] std::size_t next_bucket_count(std::size_t /*current_buckets*/, std::size_t projected_elements,
                                              std::size_t elements_per_bucket, std::size_t min_buckets,
                                              std::size_t max_buckets) const noexcept {
    RELOCO_ASSERT(elements_per_bucket >= 1, "reloco::bucket_growth::prime_growth: elements_per_bucket must be >= 1");
    RELOCO_ASSERT(min_buckets >= 1 && min_buckets <= max_buckets,
                  "reloco::bucket_growth::prime_growth: min_buckets must be >= 1 and <= max_buckets");
    std::size_t target = detail::ceil_div(projected_elements, elements_per_bucket);
    return detail::clamp_bucket_count(detail::next_prime(target), min_buckets, max_buckets);
  }
};

/**
 * @brief See the file-level doc comment. Rounds `ceil(projected_elements
 * / elements_per_bucket)` up to the next multiple of @p chunk_size,
 * clamped to `[min_buckets, max_buckets]`; ignores @p current_buckets
 * entirely -- for callers whose bucket storage comes from a fixed-size
 * pool/page/slab allocator rather than an arbitrarily-sized `span<T *>`.
 * `RELOCO_ASSERT`s `chunk_size >= 1`.
 *
 * Rough shape (bucket count vs. elements inserted over time -- a
 * staircase with identical, evenly-spaced steps of height
 * `chunk_size`):
 *
 * @verbatim
 * buckets
 *   ^                                              _______________
 *   |                                       _______|
 *   |                                _______|
 *   |                         _______|
 *   |                  _______|
 *   |           _______|
 *   |    _______|
 *   |____|
 *   +------------------------------------------------> elements inserted
 * @endverbatim
 */
struct chunked {
  std::size_t chunk_size = 64;

  [[nodiscard]] std::size_t next_bucket_count(std::size_t /*current_buckets*/, std::size_t projected_elements,
                                              std::size_t elements_per_bucket, std::size_t min_buckets,
                                              std::size_t max_buckets) const noexcept {
    RELOCO_ASSERT(chunk_size >= 1, "reloco::bucket_growth::chunked: chunk_size must be >= 1");
    RELOCO_ASSERT(elements_per_bucket >= 1, "reloco::bucket_growth::chunked: elements_per_bucket must be >= 1");
    RELOCO_ASSERT(min_buckets >= 1 && min_buckets <= max_buckets,
                  "reloco::bucket_growth::chunked: min_buckets must be >= 1 and <= max_buckets");
    std::size_t target = detail::ceil_div(projected_elements, elements_per_bucket);
    std::size_t rounded = detail::ceil_div(target, chunk_size) * chunk_size;
    return detail::clamp_bucket_count(rounded, min_buckets, max_buckets);
  }
};

/**
 * @brief See the file-level doc comment. `floor(sqrt(ceil(projected_elements
 * / elements_per_bucket)))` (integer Newton's method, never a
 * `float`/`double`), clamped to `[min_buckets, max_buckets]`; ignores
 * @p current_buckets entirely.
 *
 * Rough shape (bucket count vs. elements inserted over time -- rises
 * quickly at first, then visibly flattens out as elements accumulate,
 * unlike the other three strategies' roughly-diagonal shapes above):
 *
 * @verbatim
 * buckets
 *   ^                                    ______------------
 *   |                          _____----'
 *   |                     __--'
 *   |                 _,-'
 *   |              ,-'
 *   |           ,-'
 *   |        ,-'
 *   |      ,'
 *   |   ,-'
 *   +------------------------------------------------> elements inserted
 * @endverbatim
 */
struct sqrt_curve {
  [[nodiscard]] std::size_t next_bucket_count(std::size_t /*current_buckets*/, std::size_t projected_elements,
                                              std::size_t elements_per_bucket, std::size_t min_buckets,
                                              std::size_t max_buckets) const noexcept {
    RELOCO_ASSERT(elements_per_bucket >= 1, "reloco::bucket_growth::sqrt_curve: elements_per_bucket must be >= 1");
    RELOCO_ASSERT(min_buckets >= 1 && min_buckets <= max_buckets,
                  "reloco::bucket_growth::sqrt_curve: min_buckets must be >= 1 and <= max_buckets");
    std::size_t target = detail::ceil_div(projected_elements, elements_per_bucket);
    return detail::clamp_bucket_count(detail::isqrt(target), min_buckets, max_buckets);
  }
};

} // namespace reloco::bucket_growth
