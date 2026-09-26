#include <gtest/gtest.h>
#include <reloco/vec_deque.hpp>

using namespace reloco;

TEST(VecDequeTest, BasicPushAndPop) {
  auto deque_res = vec_deque<int>::try_create(4);
  ASSERT_TRUE(deque_res.has_value());
  auto &d = *deque_res;

  EXPECT_TRUE(d.empty());
  EXPECT_EQ(d.size(), 0);

  // Push back: [10, 20]
  ASSERT_TRUE(d.try_push_back(10).has_value());
  ASSERT_TRUE(d.try_push_back(20).has_value());
  EXPECT_EQ(d.size(), 2);
  EXPECT_EQ(d.front(), 10);
  EXPECT_EQ(d.back(), 20);

  // Push front: [5, 10, 20]
  ASSERT_TRUE(d.try_push_front(5).has_value());
  EXPECT_EQ(d.size(), 3);
  EXPECT_EQ(d.front(), 5);
  EXPECT_EQ(d[1], 10);
  EXPECT_EQ(d[2], 20);

  // Pop back -> [5, 10]
  ASSERT_TRUE(d.try_pop_back().has_value());
  EXPECT_EQ(d.back(), 10);

  // Pop front -> [10]
  ASSERT_TRUE(d.try_pop_front().has_value());
  EXPECT_EQ(d.front(), 10);
  EXPECT_EQ(d.size(), 1);
}

TEST(VecDequeTest, WrapAroundAndAsSlices) {
  auto deque_res = vec_deque<int>::try_create(4);
  ASSERT_TRUE(deque_res.has_value());
  auto &d = *deque_res;

  // Fill to capacity
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  ASSERT_TRUE(d.try_push_back(4).has_value());

  // Array is logically [1, 2, 3, 4], physically [1, 2, 3, 4]
  auto slices1 = d.as_slices();
  EXPECT_EQ(slices1.first.size(), 4);
  EXPECT_TRUE(slices1.second.empty());

  // Pop 2 from front: logically [3, 4], physically [_, _, 3, 4]
  ASSERT_TRUE(d.try_pop_front().has_value());
  ASSERT_TRUE(d.try_pop_front().has_value());

  // Push 2 to back: logically [3, 4, 5, 6], physically [5, 6, 3, 4] (WRAPPED!)
  ASSERT_TRUE(d.try_push_back(5).has_value());
  ASSERT_TRUE(d.try_push_back(6).has_value());

  // Verify logical indexing perfectly hides the wrap
  EXPECT_EQ(d[0], 3);
  EXPECT_EQ(d[1], 4);
  EXPECT_EQ(d[2], 5);
  EXPECT_EQ(d[3], 6);

  // Verify as_slices exposes the physical wrap
  auto slices2 = d.as_slices();
  EXPECT_EQ(slices2.first.size(), 2);  // [3, 4] at the end of the buffer
  EXPECT_EQ(slices2.second.size(), 2); // [5, 6] at the start of the buffer
  EXPECT_EQ(slices2.first[0], 3);
  EXPECT_EQ(slices2.second[0], 5);
}

TEST(VecDequeTest, TryMakeContiguousUnwrapsRingBuffer) {
  auto deque_res = vec_deque<int>::try_create(5);
  ASSERT_TRUE(deque_res.has_value());
  auto &d = *deque_res;

  // Force a wrapped state: physically [4, 5, _, 1, 2, 3]
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  ASSERT_TRUE(d.try_push_front(5).has_value());
  ASSERT_TRUE(d.try_push_front(4).has_value());

  auto slices = d.as_slices();
  EXPECT_FALSE(slices.second.empty()); // Proves it is physically wrapped

  // Make it contiguous!
  auto span_res = d.try_make_contiguous();
  ASSERT_TRUE(span_res.has_value());

  // Now it should be a single physical slice
  auto slices_after = d.as_slices();
  EXPECT_TRUE(slices_after.second.empty());
  EXPECT_EQ(slices_after.first.size(), 5);

  // Logical order must remain flawless
  EXPECT_EQ(d[0], 4);
  EXPECT_EQ(d[1], 5);
  EXPECT_EQ(d[2], 1);
  EXPECT_EQ(d[3], 2);
  EXPECT_EQ(d[4], 3);
}

namespace {
// A simple tracker to ensure destructors and move constructors are called
// exactly the right number of times when the buffer wraps and grows.
static int live_instances = 0;
struct Tracker {
  int val;

  // Normal constructor: Creates a new logical instance
  Tracker(int v) : val(v) { live_instances++; }

  Tracker(const Tracker &) = delete;
  Tracker &operator=(const Tracker &) = delete;

  // Move constructor: Transfers life, doesn't create a new one
  Tracker(Tracker &&other) noexcept : val(other.val) { other.val = -1; }

  // Move assignment: Destroys current life (if any), transfers new one
  Tracker &operator=(Tracker &&other) noexcept {
    if (val != -1)
      live_instances--;
    val = other.val;
    other.val = -1;
    return *this;
  }

  ~Tracker() {
    if (val != -1)
      live_instances--;
  }
};
} // namespace

// Reloco needs to know how to construct it non-trivially
namespace reloco {
template <> struct is_trivially_relocatable<Tracker> : std::false_type {};
} // namespace reloco

TEST(VecDequeTest, NonTrivialRelocationAndGrowth) {
  live_instances = 0; // Reset for this test
  {
    auto deque_res = vec_deque<Tracker>::try_create(2);
    ASSERT_TRUE(deque_res.has_value());
    auto &d = *deque_res;

    ASSERT_TRUE(d.try_emplace_back(1).has_value());
    ASSERT_TRUE(d.try_emplace_back(2).has_value());

    ASSERT_TRUE(d.try_pop_front().has_value());     // pop 1
    ASSERT_TRUE(d.try_emplace_back(3).has_value()); // physically wrapped now: [3, 2]

    EXPECT_EQ(live_instances, 2);

    // Pushing a 3rd element will force a reallocation.
    // The type-erased `try_reserve_base` must linearize the ring buffer
    // using `move_range` instead of `memcpy` because Tracker is not trivially relocatable.
    ASSERT_TRUE(d.try_emplace_back(4).has_value());

    EXPECT_EQ(d.size(), 3);
    EXPECT_EQ(d[0].val, 2);
    EXPECT_EQ(d[1].val, 3);
    EXPECT_EQ(d[2].val, 4);

    EXPECT_EQ(live_instances, 3); // Ensures no leaks and no double-frees
  }
  // Deque destroyed
  EXPECT_EQ(live_instances, 0);
}

TEST(VecDequeTest, FallibleCloning) {
  auto d1_res = vec_deque<int>::try_create();
  ASSERT_TRUE(d1_res.has_value());
  auto &d1 = *d1_res;

  ASSERT_TRUE(d1.try_push_back(100).has_value());
  ASSERT_TRUE(d1.try_push_front(200).has_value()); // wrapped: [_, _, ..., 100, 200]

  // Clone it!
  auto d2_res = d1.try_clone();
  ASSERT_TRUE(d2_res.has_value());
  auto &d2 = *d2_res;

  EXPECT_EQ(d2.size(), 2);
  EXPECT_EQ(d2[0], 200);
  EXPECT_EQ(d2[1], 100);

  // The clone should be perfectly contiguous upon creation
  auto slices = d2.as_slices();
  EXPECT_TRUE(slices.second.empty());
}

TEST(VecDequeTest, RotationOperations) {
  auto deque_res = vec_deque<int>::try_create(5);
  ASSERT_TRUE(deque_res.has_value());
  auto &d = *deque_res;

  // Fill partially: physically [_, _, 1, 2, 3]
  ASSERT_TRUE(d.try_push_front(3));
  ASSERT_TRUE(d.try_push_front(2));
  ASSERT_TRUE(d.try_push_front(1));

  // Rotate left by 1 -> logically [2, 3, 1]
  d.rotate_left(1);
  EXPECT_EQ(d[0], 2);
  EXPECT_EQ(d[1], 3);
  EXPECT_EQ(d[2], 1);

  // Rotate right by 1 -> logically [1, 2, 3]
  d.rotate_right(1);
  EXPECT_EQ(d[0], 1);
  EXPECT_EQ(d[1], 2);
  EXPECT_EQ(d[2], 3);

  // Force a wrapped buffer: physically [4, 5, 1, 2, 3] (fully packed)
  ASSERT_TRUE(d.try_push_back(4));
  ASSERT_TRUE(d.try_push_back(5));

  // Rotate left by 2 on a fully packed ring is O(1) (moves 0 elements)
  // logically becomes [3, 4, 5, 1, 2]
  d.rotate_left(2);
  EXPECT_EQ(d[0], 3);
  EXPECT_EQ(d[1], 4);
  EXPECT_EQ(d[2], 5);
  EXPECT_EQ(d[3], 1);
  EXPECT_EQ(d[4], 2);

  // To undo rotate_left(2), we rotate_right(2)!
  // logically returns to [1, 2, 3, 4, 5]
  d.rotate_right(2);
  EXPECT_EQ(d[0], 1);
  EXPECT_EQ(d[1], 2);
  EXPECT_EQ(d[4], 5);
}

TEST(VecDequeTest, ShortestShiftEraseAt) {
  auto deque_res = vec_deque<int>::try_create(6);
  ASSERT_TRUE(deque_res.has_value());
  auto &d = *deque_res;

  // Build wrapped state: [4, 5, 6, 1, 2, 3] physically.
  // Logically: [1, 2, 3, 4, 5, 6]
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  ASSERT_TRUE(d.try_push_front(6).has_value());
  ASSERT_TRUE(d.try_push_front(5).has_value());
  ASSERT_TRUE(d.try_push_front(4).has_value());

  // Erase index 1 (the '5').
  // Since 1 < 6/2, it shifts the left chunk [0, 1) -> [4] rightwards.
  // Logically becomes: [4, 6, 1, 2, 3]
  ASSERT_TRUE(d.try_erase_at(1).has_value());
  EXPECT_EQ(d.size(), 5);
  EXPECT_EQ(d[0], 4);
  EXPECT_EQ(d[1], 6);
  EXPECT_EQ(d[2], 1);
  EXPECT_EQ(d[3], 2);
  EXPECT_EQ(d[4], 3);

  // Erase index 3 (the '2').
  // Since 3 > 5/2, it shifts the right chunk (3, 5) -> [3] leftwards.
  // Logically becomes: [4, 6, 1, 3]
  ASSERT_TRUE(d.try_erase_at(3).has_value());
  EXPECT_EQ(d.size(), 4);
  EXPECT_EQ(d[0], 4);
  EXPECT_EQ(d[1], 6);
  EXPECT_EQ(d[2], 1);
  EXPECT_EQ(d[3], 3);

  // Erase ends (falls into fast-paths internally)
  ASSERT_TRUE(d.try_erase_at(0).has_value()); // pop_front
  EXPECT_EQ(d[0], 6);

  ASSERT_TRUE(d.try_erase_at(d.size() - 1).has_value()); // pop_back
  EXPECT_EQ(d[d.size() - 1], 1);
  EXPECT_EQ(d.size(), 2);
}

TEST(VecDequeTest, InsertAtMiddleFrontAndBack) {
  auto deque_res = vec_deque<int>::try_create(4);
  ASSERT_TRUE(deque_res.has_value());
  auto &d = *deque_res;

  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(4).has_value());
  ASSERT_TRUE(d.try_push_back(5).has_value());
  // Logically: [1, 2, 4, 5]

  // Insert into the middle: [1, 2, 3, 4, 5]
  ASSERT_TRUE(d.try_insert_at(2, 3).has_value());
  EXPECT_EQ(d.size(), 5);
  EXPECT_EQ(d[0], 1);
  EXPECT_EQ(d[1], 2);
  EXPECT_EQ(d[2], 3);
  EXPECT_EQ(d[3], 4);
  EXPECT_EQ(d[4], 5);

  // Insert at front (index 0): [0, 1, 2, 3, 4, 5]
  ASSERT_TRUE(d.try_insert_at(0, 0).has_value());
  EXPECT_EQ(d[0], 0);
  EXPECT_EQ(d.size(), 6);

  // Insert at back (index == size()): [0, 1, 2, 3, 4, 5, 6]
  ASSERT_TRUE(d.try_insert_at(d.size(), 6).has_value());
  EXPECT_EQ(d.back(), 6);
  EXPECT_EQ(d.size(), 7);

  for (int i = 0; i <= 6; ++i)
    EXPECT_EQ(d[static_cast<std::size_t>(i)], i);
}

TEST(VecDequeTest, InsertAtOnWrappedBuffer) {
  auto deque_res = vec_deque<int>::try_create(4);
  ASSERT_TRUE(deque_res.has_value());
  auto &d = *deque_res;

  // Build wrapped state, logically [1, 2, 3, 4, 5, 6] (see WrapAroundAndAsSlices).
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  ASSERT_TRUE(d.try_push_back(4).has_value());
  ASSERT_TRUE(d.try_pop_front().has_value());
  ASSERT_TRUE(d.try_pop_front().has_value());
  ASSERT_TRUE(d.try_push_back(5).has_value());
  ASSERT_TRUE(d.try_push_back(6).has_value());
  // Logically: [3, 4, 5, 6]

  ASSERT_TRUE(d.try_insert_at(2, 99).has_value());
  // Logically: [3, 4, 99, 5, 6]
  EXPECT_EQ(d.size(), 5);
  EXPECT_EQ(d[0], 3);
  EXPECT_EQ(d[1], 4);
  EXPECT_EQ(d[2], 99);
  EXPECT_EQ(d[3], 5);
  EXPECT_EQ(d[4], 6);
}

TEST(VecDequeTest, SwapRemoveFrontAndBack) {
  auto deque_res = vec_deque<int>::try_create(4);
  ASSERT_TRUE(deque_res.has_value());
  auto &d = *deque_res;

  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  ASSERT_TRUE(d.try_push_back(4).has_value());
  // Logically: [1, 2, 3, 4]

  // swap_remove_back(1): swaps index 1 ('2') with the last ('4'), then pops back.
  // Result: [1, 4, 3]
  ASSERT_TRUE(d.try_swap_remove_back(1).has_value());
  EXPECT_EQ(d.size(), 3);
  EXPECT_EQ(d[0], 1);
  EXPECT_EQ(d[1], 4);
  EXPECT_EQ(d[2], 3);

  // swap_remove_front(2): swaps index 2 ('3') with the first ('1'), then pops front.
  // Result: [4, 1]
  ASSERT_TRUE(d.try_swap_remove_front(2).has_value());
  EXPECT_EQ(d.size(), 2);
  EXPECT_EQ(d[0], 4);
  EXPECT_EQ(d[1], 1);

  EXPECT_FALSE(d.try_swap_remove_back(5).has_value());
  EXPECT_FALSE(d.try_swap_remove_front(5).has_value());
}

TEST(VecDequeTest, Contains) {
  auto deque_res = vec_deque<int>::try_create(4);
  ASSERT_TRUE(deque_res.has_value());
  auto &d = *deque_res;

  EXPECT_FALSE(d.contains(1));

  // Build a wrapped buffer, logically [3, 4, 5, 6] (see WrapAroundAndAsSlices).
  ASSERT_TRUE(d.try_push_back(1).has_value());
  ASSERT_TRUE(d.try_push_back(2).has_value());
  ASSERT_TRUE(d.try_push_back(3).has_value());
  ASSERT_TRUE(d.try_push_back(4).has_value());
  ASSERT_TRUE(d.try_pop_front().has_value());
  ASSERT_TRUE(d.try_pop_front().has_value());
  ASSERT_TRUE(d.try_push_back(5).has_value());
  ASSERT_TRUE(d.try_push_back(6).has_value());

  EXPECT_TRUE(d.contains(3));
  EXPECT_TRUE(d.contains(6));
  EXPECT_FALSE(d.contains(1));
  EXPECT_FALSE(d.contains(42));
}
