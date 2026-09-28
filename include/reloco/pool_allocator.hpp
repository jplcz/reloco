// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file pool_allocator.hpp
 * @brief Fixed-block-size `allocator_traits` backend that carves blocks out
 * of slabs obtained from an upstream `allocator_ref`.
 *
 * `pool_allocator<Lock>` hands out blocks of exactly one runtime-configured
 * `block_size`/`block_alignment` (not a template parameter -- a single
 * instance serves one size class, so a caller needing several simply
 * constructs several `pool_allocator`s, rather than this type growing a
 * compile-time list of size classes and the template/binary-size cost that
 * comes with it). It never services a request bigger than `block_size` or
 * more aligned than `block_alignment` -- both fail with
 * `error::allocation_failed`, exactly like asking a fixed-size slab
 * allocator (FreeBSD's `uma_zone`, Linux's `kmem_cache`) for something
 * outside its zone.
 *
 * All pool bookkeeping -- the free-block list, and the list of slabs
 * themselves (for teardown) -- is threaded intrusively through the blocks
 * and slabs' own memory (the first `sizeof(void*)` bytes of a free block or
 * a slab store a link to the next one): no separate tracking allocation is
 * ever made, and no metadata lives outside what the upstream allocator
 * itself handed back. A slab reserves exactly one whole block's worth of
 * space at its front for that per-slab link. Slab size is configured as an
 * exact `slab_bytes` byte count (not a block count), so a caller can size
 * slabs by whatever granularity matters to the upstream allocator (e.g.
 * `65536` for a 64 KiB slab, regardless of `block_size`) -- the number of
 * usable blocks per slab (`slab_bytes / block_size - 1`, after reserving
 * the header block) is derived from it, not configured directly.
 *
 * When the free list is empty, `try_allocate_block` releases its internal
 * `Lock` *before* calling into the (potentially slow, potentially
 * blocking/sleeping) upstream allocator, and only reacquires it to splice
 * the newly obtained slab's blocks onto the free list -- the same
 * "unlock, allocate, relock" discipline a kernel slab allocator must
 * follow to avoid holding a spinlock across a call that might block or
 * itself try to acquire a sleepable lock. `Lock` defaults to `null_mutex`,
 * a no-op satisfying the same `lock()`/`unlock()`/`try_lock()` surface as
 * `reloco::mutex`/`reloco::spin_lock` (either of which is a drop-in `Lock`
 * for a genuinely multi-threaded pool); pass `null_mutex` (the default)
 * when the pool is only ever touched under some other, external
 * synchronization (or from a single thread).
 *
 * `pool_allocator` only ever exposes a type-erased `allocator_ref` (via
 * `.ref()`) -- it is neither copyable nor movable itself (a real `Lock`,
 * e.g. `reloco::mutex`, is not movable/copyable either, and relocating live
 * pool state out from under in-flight `allocate`/`deallocate` calls made
 * through an already-handed-out `allocator_ref` would not be safe in any
 * case). Construct it once, in place, for the lifetime it needs to serve.
 */

#include "allocator.hpp"
#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"

#include <cstddef>

// Raw pointer arithmetic/reinterpret_cast throughout (the intrusive free-
// list/slab-list links), matching allocator.hpp/heap_allocator.hpp/
// stack_allocator.hpp's own treatment of the same concern.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief No-op lock satisfying the same `lock()`/`unlock()`/`try_lock()`
 * surface as `reloco::mutex`/`reloco::spin_lock`.
 *
 * The default `Lock` for `pool_allocator<Lock>`: use it when the pool is
 * only ever accessed from a single thread, or is already protected by some
 * other, external synchronization.
 */
struct null_mutex {
  void lock() noexcept {}

  void unlock() noexcept {}

  [[nodiscard]] bool try_lock() noexcept { return true; }
};

namespace detail {

[[nodiscard]] constexpr bool pool_allocator_is_power_of_two(std::size_t value) noexcept {
  return value != 0 && (value & (value - 1)) == 0;
}

} // namespace detail

/**
 * @brief Owning state backing `pool_allocator<Lock>`: the fixed block
 * size/alignment, the upstream allocator slabs are obtained from, and the
 * intrusive free-block/slab lists. See the file-level docs for the slab
 * layout and locking discipline.
 *
 * Neither copyable nor movable (matches `pool_allocator<Lock>` itself,
 * for the same reasons -- see its docs).
 */
template <typename Lock> class RELOCO_OWNER pool_allocator_context {
public:
  /**
   * @param block_size Size, in bytes, of every block this pool hands out.
   *   Must be at least `sizeof(void*)` (needed to thread a free block onto
   *   the intrusive free list) and a multiple of @p block_alignment (so
   *   every block within a slab stays aligned, not just the first).
   * @param block_alignment Alignment of every block this pool hands out.
   *   Must be a power of two.
   * @param upstream Allocator used to obtain each slab. Must outlive this
   *   context.
   * @param slab_bytes Exact size, in bytes, requested from @p upstream for
   *   each slab (e.g. `65536` to obtain 64 KiB slabs regardless of
   *   `block_size`) -- not a block count, so the caller can size slabs by
   *   the granularity that matters to the upstream allocator (a page, a
   *   huge page, ...) rather than by how many blocks happen to fit. Must
   *   be a multiple of @p block_size and cover at least 2 blocks' worth:
   *   one reserved, hidden block used to hold the slab's own link to the
   *   next one (see the file docs), plus at least one usable block.
   */
  constexpr pool_allocator_context(std::size_t block_size, std::size_t block_alignment, allocator_ref upstream,
                                   std::size_t slab_bytes) noexcept
      : block_size_(block_size), block_alignment_(block_alignment), upstream_(upstream), slab_bytes_(slab_bytes) {
    RELOCO_ASSERT(block_size_ >= sizeof(void *),
                  "pool_allocator: block_size must be >= sizeof(void*) to hold an intrusive free-list link");
    RELOCO_ASSERT(detail::pool_allocator_is_power_of_two(block_alignment_),
                  "pool_allocator: block_alignment must be a power of two");
    RELOCO_ASSERT(block_size_ % block_alignment_ == 0,
                  "pool_allocator: block_size must be a multiple of block_alignment");
    RELOCO_ASSERT(slab_bytes_ % block_size_ == 0, "pool_allocator: slab_bytes must be a multiple of block_size");
    RELOCO_ASSERT(slab_bytes_ / block_size_ >= 2,
                  "pool_allocator: slab_bytes must cover at least 2 blocks (1 header + >=1 usable block)");
  }

  pool_allocator_context(const pool_allocator_context &) = delete;
  pool_allocator_context &operator=(const pool_allocator_context &) = delete;
  pool_allocator_context(pool_allocator_context &&) = delete;
  pool_allocator_context &operator=(pool_allocator_context &&) = delete;

  /**
   * @brief Returns every slab currently held to `upstream`. Not
   * thread-safe against concurrent `try_allocate_block`/`deallocate_block`
   * calls (through an already-handed-out `allocator_ref`) -- only safe
   * once nothing else can reach this context anymore, exactly like any
   * other destructor.
   */
  ~pool_allocator_context() noexcept { release_all_slabs(); }

  [[nodiscard]] result<mem_block> try_allocate_block(std::size_t bytes, std::size_t alignment) noexcept {
    // This pool never services a request outside its one fixed size
    // class -- neither bigger nor more aligned than what it was
    // configured for.
    if (bytes > block_size_ || alignment > block_alignment_)
      return unexpected(error::allocation_failed);

    for (;;) {
      lock_.lock();
      if (free_list_ != nullptr) {
        void *ptr = free_list_;
        free_list_ = *static_cast<void **>(ptr);
        lock_.unlock();
        return mem_block{ptr, block_size_};
      }
      lock_.unlock();

      // Kernel-style "unlock, allocate, relock": never call into the
      // upstream allocator -- which may block/sleep -- while holding
      // lock_. Loop back around afterward and retry the pop above: under
      // contention, another thread may have refilled (or drained) the
      // free list first, so this always re-checks rather than assuming
      // the block it just made available is still there.
      if (auto refill_res = refill(); !refill_res)
        return unexpected(refill_res.error());
    }
  }

  void deallocate_block(void *ptr, std::size_t bytes) noexcept {
    (void)bytes;
    RELOCO_DEBUG_ASSERT(bytes == block_size_,
                        "pool_allocator: deallocate() called with a size that does not match this pool's "
                        "fixed block_size");
    lock_.lock();
    *static_cast<void **>(ptr) = free_list_;
    free_list_ = ptr;
    lock_.unlock();
  }

private:
  // Number of ordinary (non-header) blocks carved out of each slab: the
  // fixed slab_bytes_ minus one block's worth reserved for the slab's own
  // link to the next one (see the file docs), divided by block_size_.
  [[nodiscard]] constexpr std::size_t blocks_per_slab() const noexcept { return slab_bytes_ / block_size_ - 1; }

  // Obtains one new slab from upstream_ and splices its blocks onto
  // free_list_. Called with lock_ NOT held; only reacquires it once the
  // (potentially slow/blocking) upstream call has already completed.
  [[nodiscard]] result<void> refill() noexcept {
    auto slab_res = upstream_.allocate(slab_bytes_, block_alignment_);
    if (!slab_res)
      return unexpected(slab_res.error());

    auto *base = static_cast<std::byte *>(slab_res->ptr);

    lock_.lock();
    *reinterpret_cast<void **>(static_cast<void *>(base)) = slab_list_;
    slab_list_ = base;

    std::byte *block = base + block_size_;
    for (std::size_t i = 0, n = blocks_per_slab(); i < n; ++i, block += block_size_) {
      *reinterpret_cast<void **>(static_cast<void *>(base)) = free_list_;
      free_list_ = block;
    }
    lock_.unlock();
    return {};
  }

  void release_all_slabs() noexcept {
    void *slab = slab_list_;
    while (slab != nullptr) {
      void *next = *static_cast<void **>(slab);
      upstream_.deallocate(slab, slab_bytes_);
      slab = next;
    }
    slab_list_ = nullptr;
    free_list_ = nullptr;
  }

  std::size_t block_size_;
  std::size_t block_alignment_;
  allocator_ref upstream_;
  std::size_t slab_bytes_;
  void *free_list_ = nullptr;
  void *slab_list_ = nullptr;
  RELOCO_NO_UNIQUE_ADDRESS Lock lock_{};
};

/**
 * @brief Tag identifying the `pool_allocator<Lock>` backend, parameterized
 * on its `Lock` type (see the file docs).
 */
template <typename Lock> struct pool_allocator_tag {};

template <typename Lock> struct allocator_traits<pool_allocator_tag<Lock>> {
  using context_type = pool_allocator_context<Lock>;

  [[nodiscard]] static result<mem_block> allocate(value_ref<context_type> ctx, std::size_t bytes,
                                                  std::size_t alignment) noexcept {
    return ctx->try_allocate_block(bytes, alignment);
  }

  static void deallocate(value_ref<context_type> ctx, void *ptr, std::size_t bytes) noexcept {
    ctx->deallocate_block(ptr, bytes);
  }

  // NOTE: `expand_in_place`, `reallocate`, and `advise` are intentionally
  // omitted: every block is a fixed size, so there is nothing to grow or
  // shrink in place, and no partial-range advice to give.
  // allocator_ref::can_reallocate()/can_advise() detect their absence at
  // compile time and report unsupported/false seamlessly.
};

/**
 * @brief Fixed-block-size pool allocator; see the file-level docs for the
 * full rationale (slab layout, locking discipline, `Lock` selection).
 *
 * @code
 *   reloco::pool_allocator<> pool(64, alignof(std::max_align_t),
 *                                 reloco::default_allocator(), 65536);
 *   auto vec = reloco::vector<int>::try_allocate(pool.ref());
 * @endcode
 *
 * Neither copyable nor movable: only ever exposes a type-erased
 * `allocator_ref` (via `.ref()`), so nothing needs (or is able) to move
 * the pool itself once other code may already hold that handle.
 */
template <typename Lock = null_mutex> class RELOCO_OWNER pool_allocator {
public:
  constexpr pool_allocator(std::size_t block_size, std::size_t block_alignment, allocator_ref upstream,
                           std::size_t slab_bytes) noexcept
      : context_(block_size, block_alignment, upstream, slab_bytes) {}

  pool_allocator(const pool_allocator &) = delete;
  pool_allocator &operator=(const pool_allocator &) = delete;
  pool_allocator(pool_allocator &&) = delete;
  pool_allocator &operator=(pool_allocator &&) = delete;

  [[nodiscard]] constexpr allocator_ref ref() & noexcept RELOCO_LIFETIMEBOUND {
    return allocator_ref(pool_allocator_tag<Lock>{}, context_);
  }

  allocator_ref ref() && = delete;

private:
  pool_allocator_context<Lock> context_;
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
