// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/intrusive_c_slist.hpp>

namespace {

// Define a native-C-style struct to act as our node.
// Notice we use a generic struct for the link to mimic SLIST_ENTRY.
struct test_node {
  int value;
  struct {
    test_node *next = nullptr;
  } link;

  explicit test_node(int v) : value(v) {}
};

// Define the reloco c_slist view over it.
using test_list = reloco::c_slist<test_node, &test_node::link>;

TEST(CSlistTest, DefaultConstructedIsEmpty) {
  test_list list;
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.front(), nullptr);
  EXPECT_EQ(list.begin(), list.end());
}

TEST(CSlistTest, PushFrontAndPopFront) {
  test_list list;
  test_node n1(10);
  test_node n2(20);

  // Push n1
  list.push_front(n1);
  EXPECT_FALSE(list.empty());
  EXPECT_EQ(list.front(), &n1);

  // Push n2 (List is now: n2 -> n1)
  list.push_front(n2);
  EXPECT_EQ(list.front(), &n2);

  // Pop n2
  test_node *popped = list.pop_front();
  EXPECT_EQ(popped, &n2);
  EXPECT_EQ(popped->link.next, nullptr); // Ensure hook was zeroed
  EXPECT_EQ(list.front(), &n1);

  // Pop n1
  popped = list.pop_front();
  EXPECT_EQ(popped, &n1);
  EXPECT_EQ(popped->link.next, nullptr);
  EXPECT_TRUE(list.empty());

  // Pop empty
  EXPECT_EQ(list.pop_front(), nullptr);
}

TEST(CSlistTest, ForwardIteration) {
  test_list list;
  test_node n1(1);
  test_node n2(2);
  test_node n3(3);

  list.push_front(n3);
  list.push_front(n2);
  list.push_front(n1);
  // List is now: n1 -> n2 -> n3

  std::vector<int> values;
  for (test_node &node : list) {
    values.push_back(node.value);
  }

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 3);
}

TEST(CSlistTest, ConstIteration) {
  test_list list;
  test_node n1(42);
  list.push_front(n1);

  const test_list &clist = list;
  auto it = clist.begin();
  ASSERT_NE(it, clist.end());
  EXPECT_EQ(it->value, 42);

  // Verify iterator conversion (mutable to const)
  test_list::const_iterator cit = list.begin();
  EXPECT_EQ(cit, it);
}

TEST(CSlistTest, InsertAfterAndRemoveAfter) {
  test_list list;
  test_node n1(1);
  test_node n2(2);
  test_node n3(3);

  list.push_front(n1); // List: n1

  list.insert_after(n1, n3); // List: n1 -> n3
  EXPECT_EQ(list.front(), &n1);
  EXPECT_EQ((reloco::detail::c_slist_hook_access<test_node, &test_node::link>::next(&n1)), &n3);

  list.insert_after(n1, n2); // List: n1 -> n2 -> n3
  EXPECT_EQ((reloco::detail::c_slist_hook_access<test_node, &test_node::link>::next(&n1)), &n2);
  EXPECT_EQ((reloco::detail::c_slist_hook_access<test_node, &test_node::link>::next(&n2)), &n3);

  test_node *removed = list.remove_after(n1); // Removes n2. List: n1 -> n3
  EXPECT_EQ(removed, &n2);
  EXPECT_EQ(removed->link.next, nullptr); // Hook should be zeroed
  EXPECT_EQ((reloco::detail::c_slist_hook_access<test_node, &test_node::link>::next(&n1)), &n3);

  removed = list.remove_after(n3); // Nothing after n3
  EXPECT_EQ(removed, nullptr);
}

TEST(CSlistTest, RemoveArbitraryNode) {
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
  EXPECT_EQ(n3.link.next, nullptr); // Hook zeroed
  EXPECT_EQ((reloco::detail::c_slist_hook_access<test_node, &test_node::link>::next(&n2)), &n4);

  // Remove head (n1)
  list.remove(n1);
  EXPECT_EQ(n1.link.next, nullptr);
  EXPECT_EQ(list.front(), &n2);

  // Remove tail (n4)
  list.remove(n4);
  EXPECT_EQ(n4.link.next, nullptr);
  EXPECT_EQ((reloco::detail::c_slist_hook_access<test_node, &test_node::link>::next(&n2)), nullptr);

  // Remove last item (n2)
  list.remove(n2);
  EXPECT_TRUE(list.empty());
}

TEST(CSlistTest, Clear) {
  test_list list;
  test_node n1(1);
  test_node n2(2);

  list.push_front(n2);
  list.push_front(n1);

  list.clear();
  EXPECT_TRUE(list.empty());

  // Verify hooks are reset to prevent dangling pointers
  EXPECT_EQ(n1.link.next, nullptr);
  EXPECT_EQ(n2.link.next, nullptr);
}

TEST(CSlistTest, MoveConstructAndAssign) {
  test_list list1;
  test_node n1(1);
  list1.push_front(n1);

  // Move constructor
  test_list list2(std::move(list1));
  EXPECT_TRUE(list1.empty()); // NOLINT(bugprone-use-after-move)
  EXPECT_FALSE(list2.empty());
  EXPECT_EQ(list2.front(), &n1);

  // Move assignment
  test_list list3;
  list3 = std::move(list2);
  EXPECT_TRUE(list2.empty()); // NOLINT(bugprone-use-after-move)
  EXPECT_FALSE(list3.empty());
  EXPECT_EQ(list3.front(), &n1);
}

std::vector<int> splice_values(const test_list &list) {
  std::vector<int> out;
  for (const auto &n : list) {
    out.push_back(n.value);
  }
  return out;
}

TEST(CSlistTest, SpliceRanges) {
  using values = std::vector<int>;
  test_list dst;
  test_list src;
  test_node d1(1), d2(2), s1(10), s2(20), s3(30), s4(40);
  dst.push_front(d2);
  dst.push_front(d1);
  for (test_node *n : {&s4, &s3, &s2, &s1}) {
    src.push_front(*n);
  }

  dst.splice_after(d1, src, s1, s3); // nodes after s1 up to s3
  EXPECT_EQ(splice_values(dst), (values{1, 20, 30, 2}));
  EXPECT_EQ(splice_values(src), (values{10, 40}));

  dst.splice_after(d2, src, s4); // from the head of src up to s4
  EXPECT_TRUE(src.empty());
  EXPECT_EQ(splice_values(dst), (values{1, 20, 30, 2, 10, 40}));
}

TEST(CSlistTest, SlowSpliceWholeLists) {
  using values = std::vector<int>;
  test_list dst;
  test_list src;
  test_node d1(1), s1(10), s2(20), o1(100), o2(200);
  dst.push_front(d1);
  src.push_front(s2);
  src.push_front(s1);

  dst.slow_splice_back(src);
  EXPECT_TRUE(src.empty());
  EXPECT_EQ(splice_values(dst), (values{1, 10, 20}));

  src.push_front(o1);
  dst.slow_splice_front(src);
  EXPECT_EQ(splice_values(dst), (values{100, 1, 10, 20}));

  src.push_front(o2);
  dst.slow_splice_after(d1, src);
  EXPECT_EQ(splice_values(dst), (values{100, 1, 200, 10, 20}));
}

} // namespace