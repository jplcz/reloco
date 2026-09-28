// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/intrusive_c_list.hpp"
#include <gtest/gtest.h>
#include <vector>

namespace {

// Define a native-C-style struct to act as our node.
// We use an anonymous struct for the link to mimic FreeBSD's LIST_ENTRY.
struct test_node {
  int value;
  struct {
    test_node *next = nullptr;
    test_node **prev = nullptr;
  } link;

  explicit test_node(int v) : value(v) {}
};

// Define the reloco c_list view over it.
using test_list = reloco::c_list<test_node, &test_node::link>;

TEST(CListTest, DefaultConstructedIsEmpty) {
  test_list list;
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.front(), nullptr);
  EXPECT_EQ(list.begin(), list.end());
}

TEST(CListTest, PushFrontAndPopFront) {
  test_list list;
  test_node n1(10);
  test_node n2(20);

  // Push n1
  list.push_front(n1);
  EXPECT_FALSE(list.empty());
  EXPECT_EQ(list.front(), &n1);
  EXPECT_EQ(n1.link.next, nullptr);
  EXPECT_EQ(*n1.link.prev, &n1); // Should point back to list.first_

  // Push n2 (List is now: n2 -> n1)
  list.push_front(n2);
  EXPECT_EQ(list.front(), &n2);
  EXPECT_EQ(n2.link.next, &n1);
  EXPECT_EQ(*n2.link.prev, &n2);
  EXPECT_EQ(n1.link.prev, &n2.link.next); // n1's prev points to n2's next

  // Pop n2
  test_node *popped = list.pop_front();
  EXPECT_EQ(popped, &n2);
  EXPECT_EQ(popped->link.next, nullptr); // Hook zeroed
  EXPECT_EQ(popped->link.prev, nullptr); // Hook zeroed
  EXPECT_EQ(list.front(), &n1);
  EXPECT_EQ(*n1.link.prev, &n1);

  // Pop n1
  popped = list.pop_front();
  EXPECT_EQ(popped, &n1);
  EXPECT_TRUE(list.empty());

  // Pop empty
  EXPECT_EQ(list.pop_front(), nullptr);
}

TEST(CListTest, InsertAfter) {
  test_list list;
  test_node n1(1);
  test_node n2(2);
  test_node n3(3);

  list.push_front(n1); // List: n1

  list.insert_after(n1, n3); // List: n1 -> n3
  EXPECT_EQ(n1.link.next, &n3);
  EXPECT_EQ(n3.link.next, nullptr);
  EXPECT_EQ(n3.link.prev, &n1.link.next);

  list.insert_after(n1, n2); // List: n1 -> n2 -> n3
  EXPECT_EQ(n1.link.next, &n2);
  EXPECT_EQ(n2.link.prev, &n1.link.next);
  EXPECT_EQ(n2.link.next, &n3);
  EXPECT_EQ(n3.link.prev, &n2.link.next);
}

TEST(CListTest, InsertBefore) {
  test_list list;
  test_node n1(1);
  test_node n2(2);
  test_node n3(3);

  list.push_front(n3); // List: n3

  // Insert before head
  list.insert_before(n3, n1);   // List: n1 -> n3
  EXPECT_EQ(list.front(), &n1); // n1 should now be the head
  EXPECT_EQ(*n1.link.prev, &n1);
  EXPECT_EQ(n1.link.next, &n3);
  EXPECT_EQ(n3.link.prev, &n1.link.next);

  // Insert in middle
  list.insert_before(n3, n2); // List: n1 -> n2 -> n3
  EXPECT_EQ(n1.link.next, &n2);
  EXPECT_EQ(n2.link.prev, &n1.link.next);
  EXPECT_EQ(n2.link.next, &n3);
  EXPECT_EQ(n3.link.prev, &n2.link.next);
}

TEST(CListTest, RemoveArbitraryNode) {
  test_list list;
  test_node n1(1);
  test_node n2(2);
  test_node n3(3);
  test_node n4(4);

  list.push_front(n4);
  list.push_front(n3);
  list.push_front(n2);
  list.push_front(n1);
  // List: n1 -> n2 -> n3 -> n4

  // Remove middle (n3)
  list.remove(n3);
  EXPECT_EQ(n3.link.next, nullptr);
  EXPECT_EQ(n3.link.prev, nullptr);
  EXPECT_EQ(n2.link.next, &n4);
  EXPECT_EQ(n4.link.prev, &n2.link.next);

  // Remove head (n1)
  list.remove(n1);
  EXPECT_EQ(n1.link.next, nullptr);
  EXPECT_EQ(list.front(), &n2);
  EXPECT_EQ(*n2.link.prev, &n2); // n2's prev must now point to list.first_

  // Remove tail (n4)
  list.remove(n4);
  EXPECT_EQ(n2.link.next, nullptr);

  // Remove last item (n2)
  list.remove(n2);
  EXPECT_TRUE(list.empty());
}

TEST(CListTest, Clear) {
  test_list list;
  test_node n1(1);
  test_node n2(2);

  list.push_front(n2);
  list.push_front(n1);

  list.clear();
  EXPECT_TRUE(list.empty());

  // Verify hooks are reset
  EXPECT_EQ(n1.link.next, nullptr);
  EXPECT_EQ(n1.link.prev, nullptr);
  EXPECT_EQ(n2.link.next, nullptr);
  EXPECT_EQ(n2.link.prev, nullptr);
}

TEST(CListTest, ForwardIteration) {
  test_list list;
  test_node n1(1);
  test_node n2(2);
  test_node n3(3);

  list.push_front(n3);
  list.push_front(n2);
  list.push_front(n1);

  std::vector<int> values;
  for (test_node &node : list) {
    values.push_back(node.value);
  }

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 3);
}

TEST(CListTest, ConstIteration) {
  test_list list;
  test_node n1(42);
  list.push_front(n1);

  const test_list &clist = list;
  auto it = clist.begin();
  ASSERT_NE(it, clist.end());
  EXPECT_EQ(it->value, 42);

  test_list::const_iterator cit = list.begin();
  EXPECT_EQ(cit, it);
}

TEST(CListTest, MoveConstructFixesHeadPointer) {
  test_list list1;
  test_node n1(1);
  test_node n2(2);
  list1.push_front(n2);
  list1.push_front(n1);

  // When we move list1 into list2, the memory address of `first_` changes.
  // The first element's `prev` pointer must be automatically updated to point
  // to list2's `first_` member.
  test_list list2(std::move(list1));

  EXPECT_TRUE(list1.empty()); // NOLINT
  EXPECT_EQ(list2.front(), &n1);
  EXPECT_EQ(*n1.link.prev, &n1); // This verifies list2.first_ == &n1 through the double pointer

  // Removing from list2 should work perfectly and not touch list1's ghost state
  list2.remove(n1);
  EXPECT_EQ(list2.front(), &n2);
  EXPECT_EQ(*n2.link.prev, &n2);
}

TEST(CListTest, MoveAssignFixesHeadPointer) {
  test_list list1;
  test_node n1(1);
  list1.push_front(n1);

  test_list list2;
  test_node n2(2);
  list2.push_front(n2);

  // Move assign
  list2 = std::move(list1);

  EXPECT_TRUE(list1.empty()); // NOLINT
  EXPECT_EQ(list2.front(), &n1);
  EXPECT_EQ(*n1.link.prev, &n1); // Ensures pointer indirection was fixed up
}

} // namespace