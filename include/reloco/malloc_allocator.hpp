// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file malloc_allocator.hpp
 * @brief General-purpose, variable-size `allocator_traits` backend
 * providing classic `malloc`/`free`/`memalign`/`realloc` semantics --
 * notably, `free()` takes only a pointer, no explicit size -- carving
 * memory out of "arenas" obtained on demand from an upstream
 * `allocator_ref`.
 *
 * Unlike `pool_allocator`/`bucket_allocator` (one or a fixed compile-time
 * list of fixed block sizes), `malloc_allocator<Lock>` hands out blocks of
 * any runtime size/alignment from a single boundary-tag free list, exactly
 * like a textbook `malloc` implementation (K&R's, or a simplified
 * dlmalloc): every block, free or in-use, carries a `size`+`in-use` header
 * at its front and a matching footer at its end, so a neighbor in either
 * direction can always recover a block's extent without a side table,
 * which is what lets `deallocate`/`free()` do without an explicit size --
 * the information needed to merge/free a block is self-contained right
 * next to it in memory.
 *
 * ## Intended usage: transient, early-boot/bootloader heaps
 *
 * This is meant for exactly the case `pool_allocator`/`bucket_allocator`
 * don't fit well: a short-lived, variable-size heap over whatever backing
 * store happens to be available this early (a fixed static/`.bss` pool via
 * `stack_allocator`, a bootloader-reserved region wrapped in a one-off
 * `allocator_traits` backend, or simply `default_allocator()`), needed
 * only for the duration of early boot/bring-up before a "real" allocator
 * takes over, or for a bootloader stage that never runs long enough to
 * need one. Two things follow from that use case, both different from
 * `pool_allocator`/`bucket_allocator`'s steady-state, long-lived design:
 *
 * - Arenas are requested from `upstream` lazily, sized to fit whatever
 *   request triggered the refill (see `default_arena_bytes` below) --
 *   there is no fixed slab size to tune for a workload that may only run
 *   for a few allocations total.
 * - `free()` eagerly coalesces a freed block with any free neighbor in
 *   either direction (standard boundary-tag merging), and if the result
 *   spans an *entire* arena's interior (i.e. the whole arena is idle
 *   again), that arena is handed straight back to `upstream` immediately
 *   -- rather than waiting for this allocator's own destructor, the way
 *   `pool_allocator`'s slabs are. A transient heap that empties out
 *   partway through its lifetime gives that memory back promptly instead
 *   of sitting on it until teardown.
 *
 * ## Block layout
 *
 * ```
 * [ header: size_and_flag | next_free | prev_free (free only) ]
 * [ back-offset (size_t), then the aligned user pointer, then payload ]
 * [ footer: size_and_flag (copy of header's) ]
 * ```
 *
 * Every allocation -- not just over-aligned ones -- reserves one
 * `size_t` immediately before the returned user pointer, storing the
 * byte offset back to the block's header. This makes `free()`/`realloc()`
 * able to recover a block's header from nothing but the user pointer
 * `memalign()`/`malloc()` returned, uniformly, regardless of whether that
 * allocation needed extra alignment padding or not -- trading a constant,
 * small (one `size_t`) per-allocation overhead for a single code path with
 * no "was this over-aligned?" branch anywhere in the free/realloc logic.
 *
 * Each arena begins and ends with a tiny, permanently in-use "sentinel"
 * block (header+footer only, no payload) -- not a special case in the
 * coalescing logic, just an ordinary minimum-size in-use block -- so a
 * real block at either end of an arena naturally fails to coalesce past
 * it, without the allocator ever having to range-check against arena
 * bounds explicitly.
 *
 * ## Concurrency
 *
 * Like `pool_allocator`, every upstream call (`refill`'s `allocate`, and
 * the whole-arena-free path's `deallocate`) happens with this allocator's
 * own `Lock` released first -- see `pool_allocator.hpp`'s docs for why.
 * `Lock` defaults to `null_mutex` (see `pool_allocator.hpp`); pass
 * `reloco::mutex`/`reloco::spin_lock` for a genuinely multi-threaded heap.
 *
 * `malloc_allocator<Lock>` is neither copyable nor movable, for the same
 * reasons as `pool_allocator<Lock>`/`bucket_allocator<Lock, ...>`: only
 * ever exposes a type-erased `allocator_ref` (via `.ref()`) plus its own
 * direct `malloc`/`memalign`/`free`/`realloc` surface.
 */

#include "allocator.hpp"
#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "pool_allocator.hpp" // for null_mutex

#include <cstddef>
#include <cstdint>
#include <cstring>

// Raw pointer arithmetic/reinterpret_cast throughout (block headers,
// footers, and the back-offset word), matching allocator.hpp/
// pool_allocator.hpp's own treatment of the same concern.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

namespace detail {

[[nodiscard]] constexpr bool malloc_allocator_is_power_of_two(std::size_t value) noexcept {
  return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] constexpr std::uintptr_t malloc_allocator_align_up(std::uintptr_t value, std::size_t alignment) noexcept {
  return (value + (alignment - 1)) & ~static_cast<std::uintptr_t>(alignment - 1);
}

/**
 * @brief Boundary-tag header shared by every block, free or in-use. See
 * the file-level docs for the overall block layout.
 */
struct malloc_block_header {
  // Low bit: 1 = in use, 0 = free. The block size itself (bit 0 cleared)
  // is always a multiple of alignof(std::max_align_t), so bit 0 is
  // otherwise unused and free to serve as the in-use flag.
  std::size_t size_and_flag;
  // Valid only while this block is free: intrusive free-list links,
  // overlaid onto what would otherwise be the start of an in-use block's
  // payload -- the same technique pool_allocator.hpp uses for its own
  // free list.
  malloc_block_header *next_free;
  malloc_block_header *prev_free;

  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_and_flag & ~static_cast<std::size_t>(1); }

  [[nodiscard]] constexpr bool in_use() const noexcept { return (size_and_flag & 1U) != 0; }

  constexpr void set(std::size_t block_size, bool used) noexcept {
    size_and_flag = block_size | (used ? std::size_t{1} : std::size_t{0});
  }
};

struct malloc_block_footer {
  std::size_t size_and_flag;
};

inline constexpr std::size_t malloc_header_size = sizeof(malloc_block_header);
inline constexpr std::size_t malloc_footer_size = sizeof(malloc_block_footer);
inline constexpr std::size_t malloc_overhead = malloc_header_size + malloc_footer_size;
// A sentinel carries no payload at all -- just header+footer -- and is
// also the smallest block this allocator ever hands out or splits off,
// since a free block's "payload" when otherwise empty is just the
// header's own next_free/prev_free fields (already counted above).
inline constexpr std::size_t malloc_min_block_size = malloc_overhead;

/**
 * @brief Per-arena bookkeeping, placed at the very start of each arena,
 * *before* its head sentinel block -- deliberately outside the
 * header/footer boundary-tag chain entirely, so block coalescing never
 * has to special-case "is this actually an arena_header, not a block".
 */
struct malloc_arena_header {
  malloc_arena_header *next;
  // The exact byte count originally returned by upstream_.allocate() for
  // this arena -- not necessarily what was requested (mem_block::size may
  // be larger) -- needed to give the exact same count back to
  // upstream_.deallocate() when this arena is released.
  std::size_t total_size;
};

} // namespace detail

/**
 * @brief Owning state backing `malloc_allocator<Lock>`: the single
 * boundary-tag free list, the list of arenas obtained from `upstream`,
 * and the shared `Lock`. See the file-level docs for the full design.
 *
 * Neither copyable nor movable (matches `malloc_allocator<Lock>` itself).
 */
template <typename Lock> class RELOCO_OWNER malloc_allocator_context {
public:
  /**
   * @param upstream Allocator used to obtain each arena. Must outlive
   *   this context.
   * @param default_arena_bytes The usual arena size requested from
   *   @p upstream when the free list can't satisfy a request and needs
   *   refilling. A single request too big to fit a `default_arena_bytes`
   *   arena simply gets a bigger arena, sized to fit it exactly (plus
   *   overhead) -- there is no separate "too big, forward to upstream
   *   directly" path the way `bucket_allocator` has, since every arena is
   *   tracked and coalesced/released uniformly regardless of size.
   */
  constexpr malloc_allocator_context(allocator_ref upstream, std::size_t default_arena_bytes) noexcept
      : upstream_(upstream), default_arena_bytes_(default_arena_bytes) {
    RELOCO_ASSERT(default_arena_bytes_ >= detail::malloc_overhead * 4,
                  "malloc_allocator: default_arena_bytes is too small to be useful");
  }

  malloc_allocator_context(const malloc_allocator_context &) = delete;
  malloc_allocator_context &operator=(const malloc_allocator_context &) = delete;
  malloc_allocator_context(malloc_allocator_context &&) = delete;
  malloc_allocator_context &operator=(malloc_allocator_context &&) = delete;

  /**
   * @brief Returns every arena still held back to `upstream`. Not
   * thread-safe against concurrent calls through an already-handed-out
   * `allocator_ref`/direct API -- only safe once nothing else can reach
   * this context anymore, exactly like any other destructor.
   */
  ~malloc_allocator_context() noexcept {
    auto *arena = arena_list_;
    while (arena != nullptr) {
      auto *next = arena->next;
      upstream_.deallocate(arena, arena->total_size);
      arena = next;
    }
    arena_list_ = nullptr;
    free_list_ = nullptr;
  }

  /** @brief `malloc(bytes)`: allocates with the platform's natural alignment. */
  [[nodiscard]] result<mem_block> try_malloc(std::size_t bytes) noexcept {
    return try_memalign(alignof(std::max_align_t), bytes);
  }

  /** @brief `memalign(alignment, bytes)`: allocates with an explicit (power-of-two) alignment. */
  [[nodiscard]] result<mem_block> try_memalign(std::size_t alignment, std::size_t bytes) noexcept {
    RELOCO_ASSERT(detail::malloc_allocator_is_power_of_two(alignment),
                  "malloc_allocator: alignment must be a power of two");
    const std::size_t needed = required_block_size(alignment, bytes);

    for (;;) {
      lock_.lock();
      auto *blk = find_free_block(needed);
      if (blk != nullptr) {
        carve(blk, needed);
        lock_.unlock();
        return finalize_block(blk, alignment, bytes);
      }
      lock_.unlock();

      // Kernel-style "unlock, allocate, relock": never call into the
      // (potentially slow/blocking) upstream allocator while holding
      // lock_ -- see pool_allocator.hpp's refill() for the same
      // discipline. Loop back and retry the search afterward rather than
      // assuming the block the refill just added is still there.
      if (auto refill_res = refill(needed); !refill_res)
        return unexpected(refill_res.error());
    }
  }

  /** @brief `free(ptr)`: releases a block without needing its size. A `nullptr` is a no-op. */
  void free(void *ptr) noexcept {
    if (ptr == nullptr)
      return;

    auto *hdr = header_from_user_ptr(ptr);

    lock_.lock();
    std::size_t size = hdr->size();
    auto *block_start = reinterpret_cast<std::byte *>(hdr);

    // Coalesce forward: at most one neighbor can ever be free here (any
    // free block is already maximally merged with its own next neighbor
    // the moment it was freed), so a single step suffices.
    auto *next = reinterpret_cast<detail::malloc_block_header *>(block_start + size);
    if (!next->in_use()) {
      remove_free(next);
      size += next->size();
    }

    // Coalesce backward, via the footer immediately preceding this block
    // -- always readable, since every arena starts with an in-use head
    // sentinel, never a real block with nothing before it.
    auto *prev_footer = reinterpret_cast<detail::malloc_block_footer *>(block_start - detail::malloc_footer_size);
    if ((prev_footer->size_and_flag & 1U) == 0) {
      const std::size_t prev_size = prev_footer->size_and_flag;
      auto *prev_hdr = reinterpret_cast<detail::malloc_block_header *>(block_start - prev_size);
      remove_free(prev_hdr);
      hdr = prev_hdr;
      size += prev_size;
    }

    hdr->set(size, false);
    write_footer(hdr);

    // If the merged block now spans an entire arena's interior (both its
    // immediate neighbors are that arena's sentinels), the whole arena is
    // idle again -- hand it straight back to upstream_ rather than
    // free-listing it, matching this allocator's transient-heap use case
    // (see the file docs).
    if (auto *arena = find_and_unlink_whole_arena(hdr); arena != nullptr) {
      lock_.unlock();
      upstream_.deallocate(arena, arena->total_size);
      return;
    }

    push_free(hdr);
    lock_.unlock();
  }

  /**
   * @brief Attempts to grow the block at @p ptr to at least @p new_size in
   * place, without moving it, by absorbing consecutive free neighbors.
   * `old_size` is accepted (and ignored) only to satisfy
   * `allocator_traits`'s shape -- the real capacity is read from the
   * block's own header.
   */
  [[nodiscard]] result<std::size_t> try_expand_in_place(void *ptr, std::size_t /*old_size*/,
                                                        std::size_t new_size) noexcept {
    auto *hdr = header_from_user_ptr(ptr);
    auto *user_ptr = static_cast<std::byte *>(ptr);

    lock_.lock();
    auto capacity_of = [&]() noexcept -> std::size_t {
      return static_cast<std::size_t>(reinterpret_cast<std::byte *>(hdr) + hdr->size() - detail::malloc_footer_size -
                                      user_ptr);
    };

    while (capacity_of() < new_size) {
      auto *next = reinterpret_cast<detail::malloc_block_header *>(reinterpret_cast<std::byte *>(hdr) + hdr->size());
      if (next->in_use()) {
        lock_.unlock();
        return unexpected(error::allocation_failed);
      }
      // Absorb `next` entirely: extend hdr over it and drop it from the
      // free list -- exactly the forward half of free()'s own coalescing.
      remove_free(next);
      hdr->set(hdr->size() + next->size(), true);
    }
    write_footer(hdr);
    const std::size_t capacity = capacity_of();
    lock_.unlock();
    return capacity;
  }

  /**
   * @brief `realloc(ptr, new_size)`: resizes a block, possibly moving it.
   * `ptr == nullptr` behaves like `try_malloc`/`try_memalign`.
   * `old_size`/`alignment` are accepted to satisfy `allocator_traits`'s
   * shape (`alignment` is honored for the alignment of a *new* block, if
   * one turns out to be needed); like `free()`, the real old size is
   * always read back from the block's own header, never from a caller-
   * supplied value.
   */
  [[nodiscard]] result<mem_block> try_reallocate(void *ptr, std::size_t /*old_size*/, std::size_t new_size,
                                                 std::size_t alignment) noexcept {
    if (ptr == nullptr)
      return try_memalign(alignment, new_size);

    // In-place growth never moves the pointer, so it only helps if the
    // existing pointer already satisfies the requested alignment.
    if (reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0) {
      if (auto grown = try_expand_in_place(ptr, 0, new_size); grown)
        return mem_block{ptr, *grown};
    }

    const std::size_t old_capacity = capacity_of_locked(ptr);
    auto new_block = try_memalign(alignment, new_size);
    if (!new_block)
      return new_block;
    std::memcpy(new_block->ptr, ptr, old_capacity < new_size ? old_capacity : new_size);
    free(ptr);
    return new_block;
  }

private:
  // Required total block size (header + worst-case alignment slack +
  // back-offset word + payload + footer) to satisfy an (alignment, bytes)
  // request, rounded up to a multiple of alignof(std::max_align_t) so
  // every subsequent header in the arena stays at least pointer-aligned.
  [[nodiscard]] static std::size_t required_block_size(std::size_t alignment, std::size_t bytes) noexcept {
    const std::size_t raw =
        detail::malloc_header_size + sizeof(std::size_t) + (alignment - 1) + bytes + detail::malloc_footer_size;
    return detail::malloc_allocator_align_up(raw, alignof(std::max_align_t));
  }

  [[nodiscard]] static detail::malloc_block_header *header_from_user_ptr(void *ptr) noexcept {
    auto *p = static_cast<std::byte *>(ptr);
    const std::size_t offset = *(reinterpret_cast<std::size_t *>(p) - 1);
    return reinterpret_cast<detail::malloc_block_header *>(p - offset);
  }

  [[nodiscard]] std::size_t capacity_of_locked(void *ptr) noexcept {
    lock_.lock();
    auto *hdr = header_from_user_ptr(ptr);
    const std::size_t capacity = static_cast<std::size_t>(reinterpret_cast<std::byte *>(hdr) + hdr->size() -
                                                          detail::malloc_footer_size - static_cast<std::byte *>(ptr));
    lock_.unlock();
    return capacity;
  }

  static void write_footer(detail::malloc_block_header *hdr) noexcept {
    auto *footer = reinterpret_cast<detail::malloc_block_footer *>(reinterpret_cast<std::byte *>(hdr) + hdr->size() -
                                                                   detail::malloc_footer_size);
    footer->size_and_flag = hdr->size_and_flag;
  }

  void push_free(detail::malloc_block_header *blk) noexcept {
    blk->next_free = free_list_;
    blk->prev_free = nullptr;
    if (free_list_ != nullptr)
      free_list_->prev_free = blk;
    free_list_ = blk;
  }

  void remove_free(detail::malloc_block_header *blk) noexcept {
    if (blk->prev_free != nullptr)
      blk->prev_free->next_free = blk->next_free;
    else
      free_list_ = blk->next_free;
    if (blk->next_free != nullptr)
      blk->next_free->prev_free = blk->prev_free;
  }

  // First-fit: a plain linear scan over the free list. Simple and
  // deterministic, matching this codebase's other allocators'
  // preference for predictable, easy-to-audit search behavior over
  // micro-optimized (segregated-free-list/binning) alternatives.
  [[nodiscard]] detail::malloc_block_header *find_free_block(std::size_t required_block_size) noexcept {
    for (auto *b = free_list_; b != nullptr; b = b->next_free) {
      if (b->size() >= required_block_size)
        return b;
    }
    return nullptr;
  }

  // Removes `blk` from the free list and marks it in-use, splitting off
  // a trailing remainder as a new free block when there's enough left
  // over to make that worthwhile (>= malloc_min_block_size); otherwise
  // the whole block (including any small leftover slack) is handed to
  // the caller as-is.
  void carve(detail::malloc_block_header *blk, std::size_t required_block_size) noexcept {
    remove_free(blk);
    const std::size_t total = blk->size();
    const std::size_t remainder = total - required_block_size;
    if (remainder >= detail::malloc_min_block_size) {
      blk->set(required_block_size, true);
      write_footer(blk);
      auto *rem =
          reinterpret_cast<detail::malloc_block_header *>(reinterpret_cast<std::byte *>(blk) + required_block_size);
      rem->set(remainder, false);
      write_footer(rem);
      push_free(rem);
    } else {
      blk->set(total, true);
      write_footer(blk);
    }
  }

  // Computes the aligned user pointer within an already-carved, in-use
  // block, writes the back-offset word immediately before it, and
  // returns the resulting mem_block (whose size reflects the block's
  // *actual* remaining capacity, which may exceed `bytes`).
  [[nodiscard]] static mem_block finalize_block(detail::malloc_block_header *blk, std::size_t alignment,
                                                std::size_t bytes) noexcept {
    auto *block_start = reinterpret_cast<std::byte *>(blk);
    auto *payload_start = block_start + detail::malloc_header_size;
    auto *payload_end = block_start + blk->size() - detail::malloc_footer_size;
    auto *user_ptr = reinterpret_cast<std::byte *>(detail::malloc_allocator_align_up(
        reinterpret_cast<std::uintptr_t>(payload_start + sizeof(std::size_t)), alignment));
    RELOCO_DEBUG_ASSERT(user_ptr + bytes <= payload_end,
                        "malloc_allocator: carved block is too small for the requested size/alignment");
    *(reinterpret_cast<std::size_t *>(user_ptr) - 1) = static_cast<std::size_t>(user_ptr - block_start);
    return mem_block{user_ptr, static_cast<std::size_t>(payload_end - user_ptr)};
  }

  // If `hdr` (a just-merged free block, under lock_) spans exactly the
  // interior of some arena -- i.e. both its immediate neighbors are that
  // arena's own sentinels -- unlinks and returns that arena so the caller
  // can release it to upstream_; otherwise returns nullptr and leaves
  // arena_list_ untouched.
  [[nodiscard]] detail::malloc_arena_header *find_and_unlink_whole_arena(detail::malloc_block_header *hdr) noexcept {
    auto *block_addr = reinterpret_cast<std::byte *>(hdr);
    detail::malloc_arena_header *prev = nullptr;
    for (auto *arena = arena_list_; arena != nullptr; arena = arena->next) {
      auto *arena_base = reinterpret_cast<std::byte *>(arena);
      const bool spans_whole_arena =
          block_addr == arena_base + sizeof(detail::malloc_arena_header) + detail::malloc_min_block_size &&
          hdr->size() == arena->total_size - sizeof(detail::malloc_arena_header) - 2 * detail::malloc_min_block_size;
      if (spans_whole_arena) {
        if (prev != nullptr)
          prev->next = arena->next;
        else
          arena_list_ = arena->next;
        return arena;
      }
      prev = arena;
    }
    return nullptr;
  }

  // Obtains one new arena from upstream_, sized to comfortably fit
  // `min_block_size` (the single payload block spanning its whole
  // interior, between its two sentinels), and splices that block onto
  // free_list_. Called with lock_ NOT held; only reacquires it once the
  // (potentially slow/blocking) upstream call has already completed.
  [[nodiscard]] result<void> refill(std::size_t min_block_size) noexcept {
    const std::size_t wanted_payload = default_arena_bytes_ > min_block_size ? default_arena_bytes_ : min_block_size;
    const std::size_t arena_bytes =
        sizeof(detail::malloc_arena_header) + 2 * detail::malloc_min_block_size + wanted_payload;

    auto arena_res = upstream_.allocate(arena_bytes, alignof(std::max_align_t));
    if (!arena_res)
      return unexpected(arena_res.error());

    auto *base = static_cast<std::byte *>(arena_res->ptr);
    const std::size_t actual_bytes = arena_res->size;
    // Use every byte upstream actually gave us (mem_block::size may
    // exceed what was requested) rather than leaving slack unmanaged.
    const std::size_t payload_block_size =
        actual_bytes - sizeof(detail::malloc_arena_header) - 2 * detail::malloc_min_block_size;

    auto *arena_hdr = reinterpret_cast<detail::malloc_arena_header *>(base);

    auto *head_sentinel = reinterpret_cast<detail::malloc_block_header *>(base + sizeof(detail::malloc_arena_header));
    head_sentinel->set(detail::malloc_min_block_size, true);
    write_footer(head_sentinel);

    auto *big_block = reinterpret_cast<detail::malloc_block_header *>(reinterpret_cast<std::byte *>(head_sentinel) +
                                                                      detail::malloc_min_block_size);
    big_block->set(payload_block_size, false);
    write_footer(big_block);

    auto *tail_sentinel =
        reinterpret_cast<detail::malloc_block_header *>(reinterpret_cast<std::byte *>(big_block) + payload_block_size);
    tail_sentinel->set(detail::malloc_min_block_size, true);
    write_footer(tail_sentinel);

    lock_.lock();
    arena_hdr->next = arena_list_;
    arena_hdr->total_size = actual_bytes;
    arena_list_ = arena_hdr;
    push_free(big_block);
    lock_.unlock();
    return {};
  }

  allocator_ref upstream_;
  std::size_t default_arena_bytes_;
  detail::malloc_block_header *free_list_ = nullptr;
  detail::malloc_arena_header *arena_list_ = nullptr;
  RELOCO_NO_UNIQUE_ADDRESS Lock lock_{};
};

/**
 * @brief Tag identifying the `malloc_allocator<Lock>` backend.
 */
template <typename Lock> struct malloc_allocator_tag {};

template <typename Lock> struct allocator_traits<malloc_allocator_tag<Lock>> {
  using context_type = malloc_allocator_context<Lock>;

  [[nodiscard]] static result<mem_block> allocate(value_ref<context_type> ctx, std::size_t bytes,
                                                  std::size_t alignment) noexcept {
    return ctx->try_memalign(alignment, bytes);
  }

  static void deallocate(value_ref<context_type> ctx, void *ptr, std::size_t) noexcept { ctx->free(ptr); }

  [[nodiscard]] static result<std::size_t> expand_in_place(value_ref<context_type> ctx, void *ptr, std::size_t old_size,
                                                           std::size_t new_size) noexcept {
    return ctx->try_expand_in_place(ptr, old_size, new_size);
  }

  [[nodiscard]] static result<mem_block> reallocate(value_ref<context_type> ctx, void *ptr, std::size_t old_size,
                                                    std::size_t new_size, std::size_t alignment) noexcept {
    return ctx->try_reallocate(ptr, old_size, new_size, alignment);
  }

  // NOTE: `advise` is intentionally omitted (no partial-range usage advice
  // to give); allocator_ref::can_advise() detects its absence and reports
  // unsupported seamlessly.
};

/**
 * @brief General-purpose, variable-size malloc/free/memalign/realloc
 * allocator; see the file-level docs for the full rationale (block
 * layout, arena sourcing/release, locking discipline).
 *
 * @code
 *   reloco::malloc_allocator<> heap(reloco::default_allocator(), 65536);
 *
 *   // Direct malloc-style surface (free() needs no size):
 *   auto blk = heap.malloc(128);
 *   heap.free(blk->ptr);
 *
 *   // Or as a type-erased allocator_ref, like any other backend:
 *   auto vec = reloco::vector<int>::try_allocate(heap.ref());
 * @endcode
 *
 * Neither copyable nor movable: only ever exposes a type-erased
 * `allocator_ref` (via `.ref()`) plus its own direct
 * `malloc`/`memalign`/`free`/`realloc` surface.
 */
template <typename Lock = null_mutex> class RELOCO_OWNER malloc_allocator {
public:
  constexpr malloc_allocator(allocator_ref upstream, std::size_t default_arena_bytes) noexcept
      : context_(upstream, default_arena_bytes) {}

  malloc_allocator(const malloc_allocator &) = delete;
  malloc_allocator &operator=(const malloc_allocator &) = delete;
  malloc_allocator(malloc_allocator &&) = delete;
  malloc_allocator &operator=(malloc_allocator &&) = delete;

  [[nodiscard]] constexpr allocator_ref ref() & noexcept RELOCO_LIFETIMEBOUND {
    return allocator_ref(malloc_allocator_tag<Lock>{}, context_);
  }

  allocator_ref ref() && = delete;

  /** @brief Allocates `bytes` with the platform's natural alignment. */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE result<mem_block> malloc(std::size_t bytes) noexcept {
    return context_.try_malloc(bytes);
  }

  /** @brief Allocates `bytes` with an explicit (power-of-two) `alignment`. */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE result<mem_block> memalign(std::size_t alignment,
                                                                      std::size_t bytes) noexcept {
    return context_.try_memalign(alignment, bytes);
  }

  /** @brief Releases a block previously returned by `malloc`/`memalign`/`realloc` -- no size needed. */
  RELOCO_UNSAFE_BUFFER_USAGE void free(void *ptr) noexcept { context_.free(ptr); }

  /** @brief Resizes a block previously returned by `malloc`/`memalign`/`realloc`, possibly moving it. */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE result<mem_block> realloc(void *ptr, std::size_t new_size) noexcept {
    return context_.try_reallocate(ptr, 0, new_size, alignof(std::max_align_t));
  }

private:
  malloc_allocator_context<Lock> context_;
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
