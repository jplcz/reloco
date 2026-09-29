#if !defined(_MSC_VER)
// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/array.hpp>
#include <reloco/buddy_allocator.hpp>
#include <reloco/phys_page.hpp>

namespace {

using namespace reloco;

// ============================================================================
// Mock Environment Setup
// ============================================================================

struct mock_page_meta {
  uint32_t next{~0u};
  uint32_t prev{~0u};
  uint16_t buddy_order{0};
  bool is_free{false};
  uint16_t zone_id{0};
};

// 1024 pages of mock physical RAM
static constexpr size_t MOCK_MEM_PAGES = 1024;
static reloco::array<mock_page_meta, MOCK_MEM_PAGES> mock_ram;

// The Intrusive List implementation working on uint32_t indices
struct test_free_list {
  uint32_t head{~0u};

  void clear() noexcept { head = ~0u; }
  bool empty() const noexcept { return head == ~0u; }

  void push_front(uint32_t p) noexcept {
    mock_ram[p].prev = ~0u;
    mock_ram[p].next = head;
    if (head != ~0u)
      mock_ram[head].prev = p;
    head = p;
  }

  void remove(uint32_t p) noexcept {
    uint32_t prev = mock_ram[p].prev;
    uint32_t next = mock_ram[p].next;
    if (prev != ~0u)
      mock_ram[prev].next = next;
    else
      head = next;
    if (next != ~0u)
      mock_ram[next].prev = prev;
  }

  uint32_t pop_front() noexcept {
    uint32_t p = head;
    if (p != ~0u)
      remove(p);
    return p;
  }
};

// OS Traits bridging page_view to mock_ram
struct test_os_traits : os_traits_base<test_os_traits, uint32_t> {
  using os_page_type = uint32_t;

  static os_page_type null_page() noexcept { return ~0u; }
  static bool is_null(os_page_type p) noexcept { return p == ~0u; }
  static uint64_t to_pfn(os_page_type p) noexcept { return p; }

  static result<os_page_type> from_pfn(uint64_t pfn) noexcept {
    if (pfn >= MOCK_MEM_PAGES)
      return unexpected(error::out_of_range);
    return static_cast<uint32_t>(pfn);
  }

  static bool is_same_zone(os_page_type a, os_page_type b) noexcept {
    return mock_ram[a].zone_id == mock_ram[b].zone_id;
  }

  static uint16_t buddy_order(os_page_type p) noexcept { return mock_ram[p].buddy_order; }
  static void set_buddy_order(os_page_type p, uint16_t order) noexcept { mock_ram[p].buddy_order = order; }

  static bool is_buddy_free(os_page_type p) noexcept { return mock_ram[p].is_free; }
  static void set_buddy_free(os_page_type p, bool free) noexcept { mock_ram[p].is_free = free; }
};

using test_page = page_view<page_4k, test_os_traits>;
// Buddy allocator with max order 10 (1024 pages max)
using test_allocator = buddy_allocator<test_free_list, test_page, 10>;

// ============================================================================
// Test Fixture
// ============================================================================

class BuddyAllocatorTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Zero out memory and put it all in Zone 0
    for (size_t i = 0; i < MOCK_MEM_PAGES; i++) {
      mock_ram[i] = {~0u, ~0u, 0, false, 0};
    }
  }
};

// ============================================================================
// Tests
// ============================================================================

TEST_F(BuddyAllocatorTest, InitCarvingPerfectPowerOfTwo) {
  test_allocator allocator;

  // Initialize with exactly 1024 pages (Order 10)
  auto init_res = allocator.init(test_page::from_os_page(0), 1024);
  ASSERT_TRUE(init_res.has_value());

  EXPECT_TRUE(mock_ram[0].is_free);
  EXPECT_EQ(mock_ram[0].buddy_order, 10);

  // Allocate that entire block
  auto alloc_res = allocator.allocate(10);
  ASSERT_TRUE(alloc_res.has_value());
  EXPECT_EQ(alloc_res.value().pfn(), 0u);
  EXPECT_FALSE(mock_ram[0].is_free);
}

TEST_F(BuddyAllocatorTest, InitCarvingUnalignedSize) {
  test_allocator allocator;

  // Initialize with 19 pages. Should carve:
  // Order 4 (16 pages) at PFN 0
  // Order 1 (2 pages)  at PFN 16
  // Order 0 (1 page)   at PFN 18
  auto init_res = allocator.init(test_page::from_os_page(0), 19);
  ASSERT_TRUE(init_res.has_value());

  EXPECT_TRUE(mock_ram[0].is_free);
  EXPECT_EQ(mock_ram[0].buddy_order, 4);

  EXPECT_TRUE(mock_ram[16].is_free);
  EXPECT_EQ(mock_ram[16].buddy_order, 1);

  EXPECT_TRUE(mock_ram[18].is_free);
  EXPECT_EQ(mock_ram[18].buddy_order, 0);
}

TEST_F(BuddyAllocatorTest, PowerOfTwoSplitAndCoalesce) {
  test_allocator allocator;
  ASSERT_TRUE(allocator.init(test_page::from_os_page(0), 16)); // 16 pages = Order 4

  // Allocate Order 2 (4 pages).
  // Splits Order 4 -> Order 3 (PFN 8) & Order 3 (PFN 0).
  // Splits Order 3 (PFN 0) -> Order 2 (PFN 4) & Order 2 (PFN 0).
  auto alloc_res = allocator.allocate(2);
  ASSERT_TRUE(alloc_res.has_value());

  test_page p = alloc_res.value();
  EXPECT_EQ(p.pfn(), 0u);

  // Validate remaining free buddies
  EXPECT_FALSE(mock_ram[0].is_free);
  EXPECT_EQ(mock_ram[0].buddy_order, 2);

  EXPECT_TRUE(mock_ram[4].is_free);
  EXPECT_EQ(mock_ram[4].buddy_order, 2);

  EXPECT_TRUE(mock_ram[8].is_free);
  EXPECT_EQ(mock_ram[8].buddy_order, 3);

  // Free the block. It should instantly coalesce back to Order 4!
  auto free_res = allocator.free(p, 2);
  ASSERT_TRUE(free_res.has_value());

  EXPECT_TRUE(mock_ram[0].is_free);
  EXPECT_EQ(mock_ram[0].buddy_order, 4);
}

TEST_F(BuddyAllocatorTest, ExactPageAllocation_BinaryDecomposition) {
  test_allocator allocator;
  ASSERT_TRUE(allocator.init(test_page::from_os_page(0), 16)); // Order 4 pool

  // Ask for exactly 5 pages. 5 in binary is 101.
  // Closest bounding order is 3 (8 pages).
  // 8 pages gets popped and split exactly.
  auto alloc_res = allocator.allocate_n(5);
  ASSERT_TRUE(alloc_res.has_value());
  EXPECT_EQ(alloc_res.value().pfn(), 0u);

  // Because of binary decomposition:
  // It consumes PFN 0..3 (Order 2) and PFN 4 (Order 0). Total 5 pages.
  // It MUST have returned the unused tail (PFN 5, 6, 7) back to the free list.

  EXPECT_FALSE(mock_ram[0].is_free);
  EXPECT_EQ(mock_ram[0].buddy_order, 2); // Consumed 4 pages

  EXPECT_FALSE(mock_ram[4].is_free);
  EXPECT_EQ(mock_ram[4].buddy_order, 0); // Consumed 1 page

  // Checking the perfectly returned tail pieces!
  EXPECT_TRUE(mock_ram[5].is_free);
  EXPECT_EQ(mock_ram[5].buddy_order, 0); // Freed 1 page

  EXPECT_TRUE(mock_ram[6].is_free);
  EXPECT_EQ(mock_ram[6].buddy_order, 1); // Freed 2 pages

  // The original unused half from the bounding order split (PFN 8)
  EXPECT_TRUE(mock_ram[8].is_free);
  EXPECT_EQ(mock_ram[8].buddy_order, 3); // Freed 8 pages
}

TEST_F(BuddyAllocatorTest, ExactPageFree_BinaryDecomposition) {
  test_allocator allocator;
  ASSERT_TRUE(allocator.init(test_page::from_os_page(0), 16));

  auto p = allocator.allocate_n(5).value();

  // Ensure free_n traverses the exact same MSB-to-LSB logic
  // and coalesces everything flawlessly back into Order 4 (16 pages).
  auto free_res = allocator.free_n(p, 5);
  ASSERT_TRUE(free_res.has_value());

  EXPECT_TRUE(mock_ram[0].is_free);
  EXPECT_EQ(mock_ram[0].buddy_order, 4);
}

TEST_F(BuddyAllocatorTest, OutOfMemoryConditions) {
  test_allocator allocator;
  ASSERT_TRUE(allocator.init(test_page::from_os_page(0), 4)); // Only 4 pages (Order 2)

  // Try to allocate more than total capacity
  auto oom_res1 = allocator.allocate(3); // Order 3 (8 pages)
  EXPECT_FALSE(oom_res1.has_value());
  EXPECT_EQ(oom_res1.error(), error::allocation_failed);

  // Try to allocate an order higher than MaxOrder (10)
  auto oom_res2 = allocator.allocate(11);
  EXPECT_FALSE(oom_res2.has_value());
  EXPECT_EQ(oom_res2.error(), error::invalid_argument);

  // Exhaustion
  EXPECT_TRUE(allocator.allocate(2).has_value()); // Take all 4 pages
  auto oom_res3 = allocator.allocate(0);          // Take 1 more page
  EXPECT_FALSE(oom_res3.has_value());
  EXPECT_EQ(oom_res3.error(), error::allocation_failed);
}

TEST_F(BuddyAllocatorTest, ZoneBoundaryProtection) {
  test_allocator allocator;

  // Set up a zone boundary exactly in the middle of our 16 pages
  for (size_t i = 8; i < 16; i++) {
    mock_ram[i].zone_id = 1;
  }

  // Passing 16 pages to Init.
  // Init will try to group all 16 pages into Order 4.
  // The try_add(16) check inside OS traits MUST intercept this because
  // the block crosses from Zone 0 to Zone 1!
  auto init_res = allocator.init(test_page::from_os_page(0), 16);
  EXPECT_FALSE(init_res.has_value());
  EXPECT_EQ(init_res.error(), error::security_violation);
}

} // namespace

#endif
