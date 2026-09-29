// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "reloco/array.hpp"
#include "reloco/error.hpp"
#include "reloco/lifetime.hpp"
#include <cstddef>
#include <cstdint>

namespace reloco {

/**
 * @brief A completely decoupled, traits-driven Buddy Allocator.
 *
 * @tparam FreeList An intrusive list container providing:
 *         clear(), empty(), push_front(os_page_type),
 *         remove(os_page_type), pop_front() -> os_page_type.
 * @tparam PageView Strongly-typed reloco::page_view.
 * @tparam MaxOrder The maximum power-of-two order for block sizes.
 */
template <typename FreeList, typename PageView, size_t MaxOrder = 11> class buddy_allocator {
public:
  using free_list_type = FreeList;
  using page_type = PageView;
  using os_page_type = typename page_type::os_page_type;

  constexpr buddy_allocator() noexcept = default;

  /**
   * @brief Initializes the allocator, carving the memory region into
   * maximally aligned power-of-two blocks.
   */
  [[nodiscard]] result<void> init(page_type start, size_t num_pages) noexcept {
    if (start.is_null() || num_pages == 0) {
      return unexpected(error::invalid_argument);
    }

    for (size_t i = 0; i <= MaxOrder; i++) {
      free_areas_[i].clear();
    }

    page_type current = start;
    size_t remaining = num_pages;

    while (remaining > 0) {
      size_t order = MaxOrder;

      // Find the largest properly-aligned order that fits
      while (order > 0) {
        size_t block_pages = 1ULL << order;
        bool is_aligned = (current.pfn() % block_pages) == 0;
        bool fits = (remaining >= block_pages);

        if (is_aligned && fits) {
          break;
        }
        order--;
      }

      if (order > 0) {
        auto zone_check = current.try_add((1ULL << order) - 1);
        if (!zone_check) {
          return unexpected(zone_check.error());
        }
      }

      current.set_buddy_order(static_cast<uint16_t>(order));
      current.set_buddy_free(true);
      free_areas_[order].push_front(current.get_os_page());

      remaining -= (1ULL << order);

      if (remaining > 0) {
        auto next_res = current.try_add(1ULL << order);
        if (!next_res)
          return unexpected(next_res.error());
        current = *next_res;
      }
    }

    return {};
  }

  /**
   * @brief Allocates a contiguous physical block of 2^order pages.
   */
  [[nodiscard]] result<page_type> allocate(size_t order) noexcept {
    if (order > MaxOrder) {
      return unexpected(error::invalid_argument);
    }

    size_t current_order = order;
    while (current_order <= MaxOrder && free_areas_[current_order].empty()) {
      current_order++;
    }

    if (current_order > MaxOrder) {
      return unexpected(error::allocation_failed);
    }

    // Pop the block and mark it as allocated
    page_type block = page_type::from_os_page(free_areas_[current_order].pop_front());
    block.set_buddy_free(false);

    // Split the block down to the requested size
    while (current_order > order) {
      current_order--;

      auto buddy_res = block.try_add(1ULL << current_order);
      if (!buddy_res)
        return unexpected(buddy_res.error()); // Unreachable if OS layout is sane

      page_type buddy = *buddy_res;
      buddy.set_buddy_order(static_cast<uint16_t>(current_order));
      buddy.set_buddy_free(true);
      free_areas_[current_order].push_front(buddy.get_os_page());
    }

    block.set_buddy_order(static_cast<uint16_t>(order));
    return block;
  }

  /**
   * @brief Frees a block, automatically coalescing with its buddies if possible.
   */
  [[nodiscard]] result<void> free(page_type p, size_t order) noexcept {
    if (p.is_null() || order > MaxOrder) {
      return unexpected(error::invalid_argument);
    }

    while (order < MaxOrder) {
      // Safely calculate mathematical buddy (bounds & zone verified by PageView)
      auto buddy_res = p.try_get_buddy(static_cast<uint16_t>(order));
      if (!buddy_res) {
        break; // Buddy spans across a zone boundary or out of memory bounds
      }

      page_type buddy = *buddy_res;

      // Ensure buddy is entirely free and of the exact matching size
      if (!buddy.is_buddy_free() || buddy.buddy_order() != order) {
        break;
      }

      // Merge them
      free_areas_[order].remove(buddy.get_os_page());
      buddy.set_buddy_free(false);

      if (buddy.pfn() < p.pfn()) {
        p = buddy;
      }
      order++;
    }

    p.set_buddy_order(static_cast<uint16_t>(order));
    p.set_buddy_free(true);
    free_areas_[order].push_front(p.get_os_page());

    return {};
  }

  /**
   * @brief Exact page allocation via Binary Decomposition.
   * Eliminates internal fragmentation by returning the unused tail back to the free list.
   */
  [[nodiscard]] result<page_type> allocate_n(size_t num_pages) noexcept {
    if (num_pages == 0)
      return unexpected(error::invalid_argument);

    auto order_res = pages_to_order(num_pages);
    if (!order_res)
      return unexpected(order_res.error());
    size_t order = *order_res;

    // Find the smallest available block >= requested size
    size_t current_order = order;
    while (current_order <= MaxOrder && free_areas_[current_order].empty()) {
      current_order++;
    }
    if (current_order > MaxOrder)
      return unexpected(error::allocation_failed);

    page_type block = page_type::from_os_page(free_areas_[current_order].pop_front());
    block.set_buddy_free(false);

    // Standard split down to the bounding 'order'
    while (current_order > order) {
      current_order--;
      auto buddy_res = block.try_add(1ULL << current_order);
      if (!buddy_res)
        return unexpected(buddy_res.error());

      page_type buddy = *buddy_res;
      buddy.set_buddy_order(static_cast<uint16_t>(current_order));
      buddy.set_buddy_free(true);
      free_areas_[current_order].push_front(buddy.get_os_page());
    }

    // Exact Page Splitting: Trim the tail!
    size_t remaining_needed = num_pages;
    page_type current_chunk = block;
    size_t chunk_order = order;

    while (remaining_needed > 0 && chunk_order > 0) {
      chunk_order--;
      size_t half_size = 1ULL << chunk_order;

      auto buddy_res = current_chunk.try_add(half_size);
      if (!buddy_res)
        return unexpected(buddy_res.error());

      page_type buddy = *buddy_res;

      if (remaining_needed <= half_size) {
        // We only need the first half. Free the second half back to the system.
        buddy.set_buddy_order(static_cast<uint16_t>(chunk_order));
        buddy.set_buddy_free(true);
        free_areas_[chunk_order].push_front(buddy.get_os_page());
      } else {
        // The first half is fully consumed. Mark it, and shift focus to the second half.
        current_chunk.set_buddy_order(static_cast<uint16_t>(chunk_order));
        current_chunk.set_buddy_free(false);

        remaining_needed -= half_size;
        current_chunk = buddy;
      }
    }

    if (remaining_needed > 0) {
      current_chunk.set_buddy_order(static_cast<uint16_t>(chunk_order));
      current_chunk.set_buddy_free(false);
    }

    return block;
  }

  /**
   * @brief Exact page freeing.
   * Parses the binary decomposition of `num_pages` and frees the individual power-of-two chunks.
   */
  [[nodiscard]] result<void> free_n(page_type p, size_t num_pages) noexcept {
    if (p.is_null() || num_pages == 0)
      return unexpected(error::invalid_argument);

    page_type current = p;
    size_t remaining = num_pages;

    // Find the MSB (highest order) required to cover num_pages
    size_t order = MaxOrder;
    while (order > 0 && (1ULL << order) > remaining) {
      order--;
    }

    // Traverse from MSB to LSB, freeing chunks exactly as allocate_n carved them
    while (remaining > 0) {
      size_t chunk_size = 1ULL << order;
      if (remaining >= chunk_size) {

        // Free this specific power-of-two chunk
        auto free_res = free(current, order);
        if (!free_res)
          return unexpected(free_res.error());

        remaining -= chunk_size;

        if (remaining > 0) {
          auto next_res = current.try_add(chunk_size);
          if (!next_res)
            return unexpected(next_res.error());
          current = *next_res;
        }
      }
      if (order > 0)
        order--;
    }
    return {};
  }

private:
  [[nodiscard]] static result<size_t> pages_to_order(size_t num_pages) noexcept {
    if (num_pages == 0)
      return unexpected(error::invalid_argument);

    size_t order = 0;
    while ((1ULL << order) < num_pages) {
      order++;
      if (order > MaxOrder)
        return unexpected(error::allocation_failed);
    }
    return order;
  }

  array<free_list_type, MaxOrder + 1> free_areas_{};
};

} // namespace reloco