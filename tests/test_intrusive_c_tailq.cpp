// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/intrusive_c_tailq.hpp>
#include <vector>

namespace {

// Native-C-style struct acting as our TAILQ node.
struct test_node {
  int value;
  struct {
    test_node *next = nullptr;
    test_node **prev = nullptr;
  } link;

  explicit test_node(int v) : value(v) {}
};

using test_tailq = reloco::c_tailq<test_node, &test_node::link>;

TEST(CTailqTest, DefaultConstructedIsEmpty) {
  test_tailq queue;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.front(), nullptr);
  EXPECT_EQ(queue.begin(), queue.end());
}

TEST(CTailqTest, PushBackAndPopFront) {
  test_tailq queue;
  test_node n1(10);
  test_node n2(20);
  test_node n3(30);

  // Push n1
  queue.push_back(n1);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.front(), &n1);
  EXPECT_EQ(n1.link.next, nullptr);
  EXPECT_EQ(*n1.link.prev, &n1); // Should point back to queue's first_

  // Push n2
  queue.push_back(n2);
  EXPECT_EQ(queue.front(), &n1);
  EXPECT_EQ(n1.link.next, &n2);
  EXPECT_EQ(n2.link.next, nullptr);
  EXPECT_EQ(n2.link.prev, &n1.link.next);

  // Push n3
  queue.push_back(n3);
  EXPECT_EQ(n2.link.next, &n3);
  EXPECT_EQ(n3.link.prev, &n2.link.next);

  // Pop n1
  test_node *popped = queue.pop_front();
  EXPECT_EQ(popped, &n1);
  EXPECT_EQ(popped->link.next, nullptr); // Hook zeroed
  EXPECT_EQ(popped->link.prev, nullptr); // Hook zeroed
  EXPECT_EQ(queue.front(), &n2);
  EXPECT_EQ(*n2.link.prev, &n2); // n2 is now head, prev points to queue's first_

  EXPECT_EQ(queue.pop_front(), &n2);
  EXPECT_EQ(queue.pop_front(), &n3);
  EXPECT_TRUE(queue.empty());

  // Verify last_ptr_ is restored properly on empty
  test_node n4(40);
  queue.push_back(n4);
  EXPECT_EQ(queue.front(), &n4);
}

TEST(CTailqTest, PushFront) {
  test_tailq queue;
  test_node n1(1);
  test_node n2(2);

  queue.push_front(n1); // Queue: n1
  queue.push_front(n2); // Queue: n2 -> n1

  EXPECT_EQ(queue.front(), &n2);
  EXPECT_EQ(n2.link.next, &n1);
  EXPECT_EQ(n1.link.prev, &n2.link.next);

  // Verify tail insertion works correctly after push_front
  test_node n3(3);
  queue.push_back(n3); // Queue: n2 -> n1 -> n3
  EXPECT_EQ(n1.link.next, &n3);
  EXPECT_EQ(n3.link.prev, &n1.link.next);
  EXPECT_EQ(n3.link.next, nullptr);
}

TEST(CTailqTest, InsertAfter) {
  test_tailq queue;
  test_node n1(1);
  test_node n2(2);
  test_node n3(3);
  test_node n4(4);

  queue.push_back(n1);
  queue.push_back(n4);
  // Queue: n1 -> n4

  queue.insert_after(n1, n2);
  // Queue: n1 -> n2 -> n4
  EXPECT_EQ(n1.link.next, &n2);
  EXPECT_EQ(n2.link.prev, &n1.link.next);
  EXPECT_EQ(n2.link.next, &n4);
  EXPECT_EQ(n4.link.prev, &n2.link.next);

  // Insert at the very end
  queue.insert_after(n4, n3);
  // Queue: n1 -> n2 -> n4 -> n3
  EXPECT_EQ(n4.link.next, &n3);
  EXPECT_EQ(n3.link.prev, &n4.link.next);
  EXPECT_EQ(n3.link.next, nullptr);

  // Verify last_ptr_ was successfully updated by pushing to back
  test_node n5(5);
  queue.push_back(n5);
  EXPECT_EQ(n3.link.next, &n5);
}

TEST(CTailqTest, InsertBefore) {
  test_tailq queue;
  test_node n1(1);
  test_node n2(2);
  test_node n3(3);

  queue.push_back(n3); // Queue: n3

  // Insert before head
  queue.insert_before(n3, n1); // Queue: n1 -> n3
  EXPECT_EQ(queue.front(), &n1);
  EXPECT_EQ(*n1.link.prev, &n1);
  EXPECT_EQ(n1.link.next, &n3);
  EXPECT_EQ(n3.link.prev, &n1.link.next);

  // Insert in middle
  queue.insert_before(n3, n2); // Queue: n1 -> n2 -> n3
  EXPECT_EQ(n1.link.next, &n2);
  EXPECT_EQ(n2.link.prev, &n1.link.next);
  EXPECT_EQ(n2.link.next, &n3);
  EXPECT_EQ(n3.link.prev, &n2.link.next);
}

TEST(CTailqTest, RemoveArbitraryNode) {
  test_tailq queue;
  test_node n1(1);
  test_node n2(2);
  test_node n3(3);

  queue.push_back(n1);
  queue.push_back(n2);
  queue.push_back(n3);
  // Queue: n1 -> n2 -> n3

  // Remove middle
  queue.remove(n2);
  EXPECT_EQ(n1.link.next, &n3);
  EXPECT_EQ(n3.link.prev, &n1.link.next);
  EXPECT_EQ(n2.link.next, nullptr);
  EXPECT_EQ(n2.link.prev, nullptr);

  // Remove tail (must update last_ptr_)
  queue.remove(n3);
  EXPECT_EQ(n1.link.next, nullptr);

  // Verify tail removal fixed last_ptr_ by pushing
  test_node n4(4);
  queue.push_back(n4);
  EXPECT_EQ(n1.link.next, &n4);

  // Remove head
  queue.remove(n1);
  EXPECT_EQ(queue.front(), &n4);
  EXPECT_EQ(*n4.link.prev, &n4);
}

TEST(CTailqTest, Clear) {
  test_tailq queue;
  test_node n1(1);
  test_node n2(2);

  queue.push_back(n1);
  queue.push_back(n2);
  queue.clear();

  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(n1.link.next, nullptr);
  EXPECT_EQ(n1.link.prev, nullptr);
  EXPECT_EQ(n2.link.next, nullptr);
  EXPECT_EQ(n2.link.prev, nullptr);

  // Verify pushing works correctly after clear (last_ptr_ is reset)
  queue.push_back(n1);
  EXPECT_EQ(queue.front(), &n1);
}

TEST(CTailqTest, MoveConstructAndAssign) {
  test_tailq q1;
  test_node n1(1);
  test_node n2(2);
  q1.push_back(n1);
  q1.push_back(n2);

  // Move construct
  test_tailq q2(std::move(q1));
  EXPECT_TRUE(q1.empty()); // NOLINT
  EXPECT_FALSE(q2.empty());
  EXPECT_EQ(q2.front(), &n1);
  EXPECT_EQ(*n1.link.prev, &n1); // Verify head pointer fixed up

  // Verify q2's last_ptr_ is properly wired by appending
  test_node n3(3);
  q2.push_back(n3);
  EXPECT_EQ(n2.link.next, &n3);

  // Move assign
  test_tailq q3;
  q3 = std::move(q2);
  EXPECT_TRUE(q2.empty()); // NOLINT
  EXPECT_FALSE(q3.empty());
  EXPECT_EQ(q3.front(), &n1);
  EXPECT_EQ(*n1.link.prev, &n1);

  // Verify q3's last_ptr_ is properly wired
  test_node n4(4);
  q3.push_back(n4);
  EXPECT_EQ(n3.link.next, &n4);
}

TEST(CTailqTest, MoveEmptyQueues) {
  test_tailq q1;

  test_tailq q2(std::move(q1));
  EXPECT_TRUE(q2.empty());

  // Ensure q2's last_ptr_ points to itself, not q1
  test_node n1(1);
  q2.push_back(n1);
  EXPECT_EQ(q2.front(), &n1);
}

} // namespace