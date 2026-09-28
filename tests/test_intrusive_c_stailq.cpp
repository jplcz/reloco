// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/intrusive_c_stailq.hpp"
#include <gtest/gtest.h>
#include <vector>

namespace {

// Define a native-C-style struct to act as our node.
// Anonymous struct link mimics STAILQ_ENTRY.
struct test_node {
  int value;
  struct {
    test_node *next = nullptr;
  } link;

  explicit test_node(int v) : value(v) {}
};

// Define the reloco c_stailq view over it.
using test_stailq = reloco::c_stailq<test_node, &test_node::link>;

TEST(CStailqTest, DefaultConstructedIsEmpty) {
  test_stailq queue;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.front(), nullptr);
  EXPECT_EQ(queue.begin(), queue.end());
}

TEST(CStailqTest, PushBackFIFO) {
  test_stailq queue;
  test_node n1(10);
  test_node n2(20);
  test_node n3(30);

  queue.push_back(n1); // Queue: n1
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.front(), &n1);
  EXPECT_EQ(n1.link.next, nullptr);

  queue.push_back(n2); // Queue: n1 -> n2
  EXPECT_EQ(queue.front(), &n1);
  EXPECT_EQ(n1.link.next, &n2);
  EXPECT_EQ(n2.link.next, nullptr);

  queue.push_back(n3); // Queue: n1 -> n2 -> n3
  EXPECT_EQ(n2.link.next, &n3);

  EXPECT_EQ(queue.pop_front(), &n1);
  EXPECT_EQ(queue.front(), &n2);

  EXPECT_EQ(queue.pop_front(), &n2);
  EXPECT_EQ(queue.pop_front(), &n3);
  EXPECT_TRUE(queue.empty());
}

TEST(CStailqTest, PushFrontAndPopFront) {
  test_stailq queue;
  test_node n1(1);
  test_node n2(2);

  queue.push_front(n1); // Queue: n1
  queue.push_front(n2); // Queue: n2 -> n1

  EXPECT_EQ(queue.front(), &n2);
  EXPECT_EQ(n2.link.next, &n1);

  EXPECT_EQ(queue.pop_front(), &n2);
  EXPECT_EQ(n2.link.next, nullptr); // Hook zeroed
  EXPECT_EQ(queue.front(), &n1);

  // If we push_back now, it must properly attach to n1
  test_node n3(3);
  queue.push_back(n3); // Queue: n1 -> n3
  EXPECT_EQ(n1.link.next, &n3);
}

TEST(CStailqTest, InsertAfter) {
  test_stailq queue;
  test_node n1(1);
  test_node n2(2);
  test_node n3(3);

  queue.push_back(n1);
  queue.push_back(n3);
  // Queue: n1 -> n3

  queue.insert_after(n1, n2);
  // Queue: n1 -> n2 -> n3
  EXPECT_EQ(n1.link.next, &n2);
  EXPECT_EQ(n2.link.next, &n3);

  // Insert after the tail
  test_node n4(4);
  queue.insert_after(n3, n4);
  // Queue: n1 -> n2 -> n3 -> n4
  EXPECT_EQ(n3.link.next, &n4);

  // If we push_back again, it should attach to n4 (verifies last_ptr_ updated correctly)
  test_node n5(5);
  queue.push_back(n5);
  EXPECT_EQ(n4.link.next, &n5);
}

TEST(CStailqTest, RemoveArbitraryNode) {
  test_stailq queue;
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
  EXPECT_EQ(n2.link.next, nullptr);

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
}

TEST(CStailqTest, Clear) {
  test_stailq queue;
  test_node n1(1);
  test_node n2(2);

  queue.push_back(n1);
  queue.push_back(n2);
  queue.clear();

  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(n1.link.next, nullptr);
  EXPECT_EQ(n2.link.next, nullptr);

  // Verify pushing works correctly after clear
  queue.push_back(n1);
  EXPECT_EQ(queue.front(), &n1);
}

TEST(CStailqTest, MoveConstructAndAssign) {
  test_stailq q1;
  test_node n1(1);
  q1.push_back(n1);

  // Move construct
  test_stailq q2(std::move(q1));
  EXPECT_TRUE(q1.empty()); // NOLINT
  EXPECT_FALSE(q2.empty());
  EXPECT_EQ(q2.front(), &n1);

  // Test q2's last_ptr_ works
  test_node n2(2);
  q2.push_back(n2);
  EXPECT_EQ(n1.link.next, &n2);

  // Move assign
  test_stailq q3;
  q3 = std::move(q2);
  EXPECT_TRUE(q2.empty()); // NOLINT
  EXPECT_FALSE(q3.empty());
  EXPECT_EQ(q3.front(), &n1);

  // Test q3's last_ptr_ works
  test_node n3(3);
  q3.push_back(n3);
  EXPECT_EQ(n2.link.next, &n3);
}

TEST(CStailqTest, MoveConstructEmpty) {
  test_stailq q1;

  test_stailq q2(std::move(q1));
  EXPECT_TRUE(q2.empty());

  // Ensure q2's last_ptr_ points to itself, not q1
  test_node n1(1);
  q2.push_back(n1);
  EXPECT_EQ(q2.front(), &n1);
}

} // namespace