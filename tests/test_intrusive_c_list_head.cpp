// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/intrusive_c_list_head.hpp"
#include <gtest/gtest.h>
#include <vector>

namespace {

// Mimic Linux's `struct list_head` usage. Notice the link isn't necessarily
// the first field! container_of() handles the offset gracefully.
struct test_node {
  int padding; // Prove offset calculation works
  int value;
  struct {
    void *next = nullptr;
    void *prev = nullptr;
  } list;

  explicit test_node(int v) : padding(0), value(v) {}
};

using test_linux_list = reloco::c_list_head<test_node, &test_node::list>;

TEST(CLinuxListTest, DefaultConstructedIsEmpty) {
  test_linux_list list;
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.front(), nullptr);
  EXPECT_EQ(list.back(), nullptr);
  EXPECT_EQ(list.begin(), list.end());
}

TEST(CLinuxListTest, PushBackAndPopFront) {
  test_linux_list list;
  test_node n1(10);
  test_node n2(20);

  list.push_back(n1);
  EXPECT_FALSE(list.empty());
  EXPECT_EQ(list.front(), &n1);
  EXPECT_EQ(list.back(), &n1);

  list.push_back(n2);
  EXPECT_EQ(list.front(), &n1);
  EXPECT_EQ(list.back(), &n2);

  // Pop front
  test_node *popped = list.pop_front();
  EXPECT_EQ(popped, &n1);
  EXPECT_EQ(list.front(), &n2);
  EXPECT_EQ(list.back(), &n2);
  EXPECT_EQ(n1.list.next, nullptr); // Verify poison

  popped = list.pop_front();
  EXPECT_EQ(popped, &n2);
  EXPECT_TRUE(list.empty());
}

TEST(CLinuxListTest, PushFrontAndPopBack) {
  test_linux_list list;
  test_node n1(1);
  test_node n2(2);

  list.push_front(n1); // List: n1
  list.push_front(n2); // List: n2 -> n1

  EXPECT_EQ(list.front(), &n2);
  EXPECT_EQ(list.back(), &n1);

  test_node *popped = list.pop_back();
  EXPECT_EQ(popped, &n1);
  EXPECT_EQ(list.front(), &n2);
  EXPECT_EQ(list.back(), &n2);
}

TEST(CLinuxListTest, Iteration) {
  test_linux_list list;
  test_node n1(1);
  test_node n2(2);
  test_node n3(3);

  list.push_back(n1);
  list.push_back(n2);
  list.push_back(n3);

  // Forward iteration
  std::vector<int> fwd;
  for (test_node &node : list) {
    fwd.push_back(node.value);
  }
  ASSERT_EQ(fwd.size(), 3u);
  EXPECT_EQ(fwd[0], 1);
  EXPECT_EQ(fwd[1], 2);
  EXPECT_EQ(fwd[2], 3);

  // Reverse iteration (bidirectional capabilities)
  std::vector<int> rev;
  auto it = list.end();
  while (it != list.begin()) {
    --it;
    rev.push_back(it->value);
  }
  ASSERT_EQ(rev.size(), 3u);
  EXPECT_EQ(rev[0], 3);
  EXPECT_EQ(rev[1], 2);
  EXPECT_EQ(rev[2], 1);
}

TEST(CLinuxListTest, RemoveArbitraryNode) {
  test_linux_list list;
  test_node n1(1);
  test_node n2(2);
  test_node n3(3);

  list.push_back(n1);
  list.push_back(n2);
  list.push_back(n3);

  // Remove middle
  list.remove(n2);
  EXPECT_EQ(list.front(), &n1);
  EXPECT_EQ(list.back(), &n3);

  // Ensure n1 connects directly to n3
  auto it = list.begin();
  EXPECT_EQ(&*it, &n1);
  ++it;
  EXPECT_EQ(&*it, &n3);

  // Remove head
  list.remove(n1);
  EXPECT_EQ(list.front(), &n3);
  EXPECT_EQ(list.back(), &n3);
}

TEST(CLinuxListTest, MoveSemantics) {
  test_linux_list l1;
  test_node n1(1);
  test_node n2(2);
  l1.push_back(n1);
  l1.push_back(n2);

  // Move Construct
  test_linux_list l2(std::move(l1));
  EXPECT_TRUE(l1.empty()); // NOLINT
  EXPECT_FALSE(l2.empty());
  EXPECT_EQ(l2.front(), &n1);
  EXPECT_EQ(l2.back(), &n2);

  // Verify internal links are completely severed from l1
  auto it = l2.end();
  --it;
  EXPECT_EQ(&*it, &n2);

  // Move Assign
  test_linux_list l3;
  l3 = std::move(l2);
  EXPECT_TRUE(l2.empty()); // NOLINT
  EXPECT_FALSE(l3.empty());
  EXPECT_EQ(l3.front(), &n1);
  EXPECT_EQ(l3.back(), &n2);
}

} // namespace