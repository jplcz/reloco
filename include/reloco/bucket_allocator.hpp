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
 * bucket's own `pool_allocator_context<Lock>`.
 *
 * A request whose size fits some configured bucket, but whose alignment
 * exceeds `alignment` (the one alignment shared by every bucket, see
 * below), fails with `error::allocation_failed` -- that bucket's own
 * `pool_allocator_context<Lock>` rejects it, exactly like `pool_allocator`
 * itself, and this backend does not retry it against upstream. A request
 * bigger than the *largest* configured bucket, however, is forwarded
 * directly to the shared upstream `allocator_ref` instead of failing (the
 * one case where this backend does defer, rather than reject), with
 * whatever alignment was requested -- once a request no longer fits any
 * bucket, there is no smaller size class left to round it up to and check
 * against, and refusing outright would make `bucket_allocator` a poor
 * drop-in replacement for a plain allocator once request sizes grow past
 * the configured range.
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
 * exact match. If `bytes` exceeds every bucket (i.e. the block was one
 * of the direct-to-upstream allocations above), `deallocate()` forwards
 * straight to the upstream `allocator_ref` instead, with the same
 * (possibly-truncated) `bytes` it was given -- exactly mirroring
 * `allocate()`'s own fallback path.
 *
 * Unlike `pool_allocator` (whose fixed block size leaves no headroom to
 * grow into -- `allocate()` already reports the one and only size a block
 * ever has), `bucket_allocator` *does* implement `expand_in_place`/
 * `reallocate`, because a bucket's actual block size can exceed the
 * `old_size`/`bytes` a caller records for it (see the deallocation note
 * above): `try_expand_in_place` re-derives the owning bucket from
 * `old_size` the same way `deallocate()` does, and, if `new_size` still
 * fits that bucket's own block size, succeeds with no copy at all (the
 * memory was already there). This zero-copy shortcut is compiled out
 * under AddressSanitizer (`RELOCO_ASAN_ENABLED`, see
 * `detail/sanitizer.hpp`) so that sanitizer-instrumented test runs keep
 * exercising `try_reallocate_block`'s real allocate/copy/deallocate path
 * instead of always taking the free shortcut. `try_reallocate_block`
 * itself is a plain allocate-new/copy-`min(old_size, new_size)`-bytes/
 * deallocate-old sequence, exactly like `heap_allocator`'s own fallback
 * for an overaligned `realloc`. Both operations, when the block being
 * grown/reallocated is one of the direct-to-upstream allocations (i.e.
 * `old_size` exceeds every bucket), defer entirely to the upstream
 * `allocator_ref`'s own `expand_in_place`/`reallocate` instead (failing if
 * upstream doesn't support the operation) -- mirroring `allocate()`'s/
 * `deallocate()`'s own upstream fallback.
 */

#include "allocator.hpp"
#include "detail/compat.hpp"
#include "detail/sanitizer.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "pool_allocator.hpp"

#include <array>
#include <cstddef>
#include <cstring>
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
   * @param upstream Allocator every bucket obtains its slabs from, and
   *   also the fallback used directly for any request bigger than the
   *   largest configured bucket (see the file docs). Must outlive this
   *   context.
   * @param slab_bytes Forwarded to every bucket's own
   *   `pool_allocator_context` (see its constructor) -- one shared slab
   *   byte size across every bucket, not configurable per-bucket.
   */
  constexpr bucket_allocator_context(std::size_t alignment, allocator_ref upstream, std::size_t slab_bytes) noexcept
      : upstream_(upstream), pools_(std::make_index_sequence<bucket_count>{}, alignment, upstream, slab_bytes) {}

  bucket_allocator_context(const bucket_allocator_context &) = delete;
  bucket_allocator_context &operator=(const bucket_allocator_context &) = delete;
  bucket_allocator_context(bucket_allocator_context &&) = delete;
  bucket_allocator_context &operator=(bucket_allocator_context &&) = delete;

  [[nodiscard]] result<mem_block> try_allocate_block(std::size_t bytes, std::size_t alignment) noexcept {
    const std::size_t idx = find_bucket(bytes);
    if (idx == bucket_count) {
      // Bigger than every configured bucket: defer straight to upstream_
      // (with whatever alignment was requested) rather than reject -- see
      // the file docs. A too-large-alignment-but-in-range request is
      // still rejected by the bucket's own pool below, not retried here.
      return upstream_.allocate(bytes, alignment);
    }
    return pools_[idx].try_allocate_block(bytes, alignment);
  }

  void deallocate_block(void *ptr, std::size_t bytes) noexcept {
    // `bytes` may be smaller than the bucket's actual block size (see the
    // file docs) -- re-derive which bucket this block came from, then
    // hand that bucket's own exact block size to it, since
    // pool_allocator_context::deallocate_block requires an exact match.
    const std::size_t idx = find_bucket(bytes);
    if (idx == bucket_count) {
      // Mirrors the allocate() fallback above: this block was handed out
      // directly by upstream_, so free it there too.
      upstream_.deallocate(ptr, bytes);
      return;
    }
    pools_[idx].deallocate_block(ptr, bucket_sizes[idx]);
  }

  [[nodiscard]] result<std::size_t> try_expand_in_place(void *ptr, std::size_t old_size,
                                                        std::size_t new_size) noexcept {
    const std::size_t idx = find_bucket(old_size);
    if (idx == bucket_count) {
      // The block was one of the direct-to-upstream allocations (see
      // try_allocate_block) -- defer to upstream_'s own expand_in_place,
      // if it has one, instead of trying (and failing) to interpret
      // old_size as one of our own buckets.
      if (!upstream_.can_expand_in_place())
        return unexpected(error::allocation_failed);
      return upstream_.expand_in_place(ptr, old_size, new_size);
    }

#if !RELOCO_ASAN_ENABLED
    // The owning bucket's pool already carved out bucket_sizes[idx]
    // physical bytes for this block (see the file docs on why old_size
    // can be smaller than that) -- growing within that same footprint
    // needs no copy/move at all. Deliberately compiled out under
    // AddressSanitizer so a sanitizer-instrumented test run keeps
    // exercising try_reallocate_block's real allocate/copy/deallocate
    // path instead of always taking this free shortcut.
    if (new_size <= bucket_sizes[idx])
      return new_size;
#endif
    return unexpected(error::allocation_failed);
  }

  [[nodiscard]] result<mem_block> try_reallocate_block(void *ptr, std::size_t old_size, std::size_t new_size,
                                                       std::size_t alignment) noexcept {
    if (find_bucket(old_size) == bucket_count) {
      // Ditto for reallocate(): the old block belongs to upstream_, not
      // to any bucket, so upstream_ is the only thing that can resize it.
      if (!upstream_.can_reallocate())
        return unexpected(error::allocation_failed);
      return upstream_.reallocate(ptr, old_size, new_size, alignment);
    }

    // Generic allocate/copy/deallocate scheme: route the new size through
    // this same context (it may land in a different bucket, the same
    // bucket, or upstream_ directly, exactly like a fresh allocate()
    // would), preserve the overlapping bytes, and release the old block.
    auto new_block = try_allocate_block(new_size, alignment);
    if (!new_block)
      return new_block;
    std::memcpy(new_block->ptr, ptr, old_size < new_size ? old_size : new_size);
    deallocate_block(ptr, old_size);
    return new_block;
  }

private:
  // Helper struct so pools_ can be constructed via an index-sequence
  // delegating constructor while upstream_ is initialized directly (see
  // the mem-initializer list above) -- std::array itself has no such
  // constructor, so this wraps it in a tiny aggregate-like helper that
  // does.
  struct pools_holder {
    template <std::size_t... Is>
    constexpr pools_holder(std::index_sequence<Is...>, std::size_t alignment, allocator_ref upstream,
                           std::size_t slab_bytes) noexcept
        : pools{pool_allocator_context<Lock>(bucket_sizes[Is], alignment, upstream, slab_bytes)...} {}

    [[nodiscard]] constexpr pool_allocator_context<Lock> &operator[](std::size_t idx) noexcept { return pools[idx]; }

    std::array<pool_allocator_context<Lock>, bucket_count> pools;
  };

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

  allocator_ref upstream_;
  pools_holder pools_;
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

  [[nodiscard]] static result<std::size_t> expand_in_place(value_ref<context_type> ctx, void *ptr, std::size_t old_size,
                                                           std::size_t new_size) noexcept {
    return ctx->try_expand_in_place(ptr, old_size, new_size);
  }

  [[nodiscard]] static result<mem_block> reallocate(value_ref<context_type> ctx, void *ptr, std::size_t old_size,
                                                    std::size_t new_size, std::size_t alignment) noexcept {
    return ctx->try_reallocate_block(ptr, old_size, new_size, alignment);
  }

  // NOTE: `advise` is intentionally omitted (no partial-range usage advice
  // to give); allocator_ref::can_advise() detects its absence and reports
  // unsupported seamlessly. expand_in_place/reallocate ARE supported --
  // see the file docs for why that differs from pool_allocator_tag.
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
 *       alignof(std::max_align_t), reloco::default_allocator(), 65536);
 *   auto vec = reloco::vector<int>::try_allocate(pool.ref());
 * @endcode
 *
 * Neither copyable nor movable, for the same reasons as
 * `pool_allocator<Lock>` (see its own docs): only ever exposes a
 * type-erased `allocator_ref` via `.ref()`.
 */
template <typename Lock, std::size_t... BucketSizes> class RELOCO_OWNER bucket_allocator {
public:
  constexpr bucket_allocator(std::size_t alignment, allocator_ref upstream, std::size_t slab_bytes) noexcept
      : context_(alignment, upstream, slab_bytes) {}

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
