// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file bucket_allocator.hpp
 * @brief General-purpose `allocator_traits` backend combining a compile-
 * time list of `pool_allocator`s (see `pool_allocator.hpp`), one per size
 * class ("bucket"), routing each request to the smallest bucket that fits.
 *
 * `bucket_allocator<Lock, BucketSizes...>` requires `BucketSizes...` be
 * listed in strictly ascending order (enforced by a `static_assert`, not a
 * runtime check -- the whole point of it being a template parameter pack
 * is that the bucket list, and therefore its ordering, is fixed at
 * compile time). `try_allocate_block`/`allocate` picks the smallest
 * configured bucket size that is `>= bytes` via a search bounded by
 * `sizeof...(BucketSizes)` (a plain linear scan over a small,
 * compile-time-sized array -- deliberately not a data structure that
 * could itself need to allocate, or a search whose cost depends on
 * anything other than the fixed bucket count), and forwards to that
 * bucket's own `pool_allocator_context<Lock>`. A request bigger than the
 * largest configured bucket, or more aligned than `alignment` (the one
 * alignment shared by every bucket, see below), fails with
 * `error::allocation_failed` -- exactly like `pool_allocator` itself,
 * this backend never falls through to a "handle anything" allocation
 * path.
 *
 * Every bucket shares one `alignment` (a constructor parameter, not part
 * of `BucketSizes`) and one upstream `allocator_ref` `pool_allocator`
 * obtains its slabs from -- see `pool_allocator_context`'s own
 * constructor for the per-bucket invariants this implies (`alignment`
 * must be a power of two and divide every bucket size).
 *
 * Deallocation's one subtlety: the `bytes` a caller passes to
 * `deallocate()` is not always exactly equal to the bucket size the
 * block was originally carved from -- `allocator_ref`'s contract only
 * guarantees `allocate()`'s returned `mem_block::size` is *at least* the
 * requested size, and containers are free to record (and later hand back
 * to `deallocate()`) any value that is still `<=` that actual capacity
 * (e.g. `count * sizeof(T)` after integer-dividing a returned byte count
 * by an element size that doesn't evenly divide it). `deallocate()`
 * therefore re-runs the exact same "smallest bucket `>= bytes`" search
 * used by `allocate()` to recover which bucket the block actually came
 * from, then forwards *that bucket's own exact block size* -- not the
 * possibly-smaller `bytes` it was given -- to the underlying
 * `pool_allocator_context<Lock>::deallocate_block`, which requires an
 * exact match.
 */

#include "allocator.hpp"
#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "pool_allocator.hpp"

#include <array>
#include <cstddef>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief Owning state backing `bucket_allocator<Lock, BucketSizes...>`:
 * one `pool_allocator_context<Lock>` per configured bucket size. See the
 * file-level docs for the bucket-selection/deallocation-size-translation
 * rationale.
 *
 * Neither copyable nor movable (each `pool_allocator_context<Lock>` is
 * neither, and this type holds `sizeof...(BucketSizes)` of them
 * directly, not through an indirection that could be moved instead).
 */
template <typename Lock, std::size_t... BucketSizes> class RELOCO_OWNER bucket_allocator_context {
  static constexpr std::size_t bucket_count = sizeof...(BucketSizes);
  static_assert(bucket_count >= 1, "bucket_allocator: at least one bucket size is required");

  static constexpr std::array<std::size_t, bucket_count> bucket_sizes = {BucketSizes...};

  [[nodiscard]] static constexpr bool bucket_sizes_strictly_ascending() noexcept {
    for (std::size_t i = 1; i < bucket_count; ++i) {
      if (bucket_sizes[i] <= bucket_sizes[i - 1])
        return false;
    }
    return true;
  }

  static_assert(bucket_sizes_strictly_ascending(),
                "bucket_allocator: BucketSizes... must be listed in strictly ascending order");

public:
  /**
   * @param alignment Alignment shared by every bucket. Must be a power of
   *   two and divide every one of `BucketSizes...` -- see
   *   `pool_allocator_context`'s constructor, which each bucket's
   *   underlying pool asserts this through individually.
   * @param upstream Allocator every bucket obtains its slabs from. Must
   *   outlive this context.
   * @param blocks_per_slab Forwarded to every bucket's own
   *   `pool_allocator_context` (see its constructor) -- one shared slab
   *   granularity across every bucket, not configurable per-bucket.
   */
  constexpr bucket_allocator_context(std::size_t alignment, allocator_ref upstream,
                                     std::size_t blocks_per_slab) noexcept
      : bucket_allocator_context(std::make_index_sequence<bucket_count>{}, alignment, upstream, blocks_per_slab) {}

  bucket_allocator_context(const bucket_allocator_context &) = delete;
  bucket_allocator_context &operator=(const bucket_allocator_context &) = delete;
  bucket_allocator_context(bucket_allocator_context &&) = delete;
  bucket_allocator_context &operator=(bucket_allocator_context &&) = delete;

  [[nodiscard]] result<mem_block> try_allocate_block(std::size_t bytes, std::size_t alignment) noexcept {
    const std::size_t idx = find_bucket(bytes);
    if (idx == bucket_count)
      return unexpected(error::allocation_failed);
    return pools_[idx].try_allocate_block(bytes, alignment);
  }

  void deallocate_block(void *ptr, std::size_t bytes) noexcept {
    // `bytes` may be smaller than the bucket's actual block size (see the
    // file docs) -- re-derive which bucket this block came from, then
    // hand that bucket's own exact block size to it, since
    // pool_allocator_context::deallocate_block requires an exact match.
    const std::size_t idx = find_bucket(bytes);
    RELOCO_DEBUG_ASSERT(idx != bucket_count,
                        "bucket_allocator: deallocate() called with a size larger than every configured bucket");
    if (idx == bucket_count)
      return;
    pools_[idx].deallocate_block(ptr, bucket_sizes[idx]);
  }

private:
  template <std::size_t... Is>
  constexpr bucket_allocator_context(std::index_sequence<Is...>, std::size_t alignment, allocator_ref upstream,
                                     std::size_t blocks_per_slab) noexcept
      : pools_{pool_allocator_context<Lock>(bucket_sizes[Is], alignment, upstream, blocks_per_slab)...} {}

  // Smallest bucket index whose size is >= target, or bucket_count if
  // target exceeds every bucket. A plain linear scan bounded by the fixed,
  // compile-time bucket_count -- deliberately simple/deterministic rather
  // than e.g. a binary search, since bucket_count is expected to stay
  // small (a handful of size classes).
  [[nodiscard]] static constexpr std::size_t find_bucket(std::size_t target) noexcept {
    for (std::size_t i = 0; i < bucket_count; ++i) {
      if (bucket_sizes[i] >= target)
        return i;
    }
    return bucket_count;
  }

  std::array<pool_allocator_context<Lock>, bucket_count> pools_;
};

/**
 * @brief Tag identifying the `bucket_allocator<Lock, BucketSizes...>`
 * backend.
 */
template <typename Lock, std::size_t... BucketSizes> struct bucket_allocator_tag {};

template <typename Lock, std::size_t... BucketSizes>
struct allocator_traits<bucket_allocator_tag<Lock, BucketSizes...>> {
  using context_type = bucket_allocator_context<Lock, BucketSizes...>;

  [[nodiscard]] static result<mem_block> allocate(value_ref<context_type> ctx, std::size_t bytes,
                                                  std::size_t alignment) noexcept {
    return ctx->try_allocate_block(bytes, alignment);
  }

  static void deallocate(value_ref<context_type> ctx, void *ptr, std::size_t bytes) noexcept {
    ctx->deallocate_block(ptr, bytes);
  }

  // NOTE: expand_in_place/reallocate/advise intentionally omitted, exactly
  // like pool_allocator_tag -- see that header's own NOTE.
};

/**
 * @brief General-purpose, fixed-bucket-list pool-of-pools allocator; see
 * the file-level docs for the full rationale (bucket selection,
 * deallocation-size translation, shared alignment/upstream).
 *
 * `Lock` must always be given explicitly (even to pick the default
 * `null_mutex` -- e.g. `bucket_allocator<null_mutex, 16, 32, 64, 128>`):
 * a template parameter pack must be the last template parameter, so
 * `Lock`, which precedes `BucketSizes...`, cannot itself default while
 * still letting a caller supply the (mandatory) bucket list after it.
 *
 * @code
 *   reloco::bucket_allocator<reloco::null_mutex, 16, 32, 64, 128, 256> pool(
 *       alignof(std::max_align_t), reloco::default_allocator(), 32);
 *   auto vec = reloco::vector<int>::try_allocate(pool.ref());
 * @endcode
 *
 * Neither copyable nor movable, for the same reasons as
 * `pool_allocator<Lock>` (see its own docs): only ever exposes a
 * type-erased `allocator_ref` via `.ref()`.
 */
template <typename Lock, std::size_t... BucketSizes> class RELOCO_OWNER bucket_allocator {
public:
  constexpr bucket_allocator(std::size_t alignment, allocator_ref upstream, std::size_t blocks_per_slab) noexcept
      : context_(alignment, upstream, blocks_per_slab) {}

  bucket_allocator(const bucket_allocator &) = delete;
  bucket_allocator &operator=(const bucket_allocator &) = delete;
  bucket_allocator(bucket_allocator &&) = delete;
  bucket_allocator &operator=(bucket_allocator &&) = delete;

  [[nodiscard]] constexpr allocator_ref ref() & noexcept RELOCO_LIFETIMEBOUND {
    return allocator_ref(bucket_allocator_tag<Lock, BucketSizes...>{}, context_);
  }

  allocator_ref ref() && = delete;

private:
  bucket_allocator_context<Lock, BucketSizes...> context_;
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
