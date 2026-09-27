// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/digraph.hpp>

using reloco::digraph;
using reloco::error;

TEST(DigraphTest, TryCreateStartsEmpty) {
  auto g = digraph::try_create();
  ASSERT_TRUE(g.has_value());
  EXPECT_EQ(g->node_count(), 0u);
}

TEST(DigraphTest, AddNodeReturnsDenseIncreasingIndices) {
  auto g_res = digraph::try_create();
  digraph g = std::move(g_res.value());
  auto a = g.try_add_node();
  auto b = g.try_add_node();
  auto c = g.try_add_node();
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(*a, 0u);
  EXPECT_EQ(*b, 1u);
  EXPECT_EQ(*c, 2u);
  EXPECT_EQ(g.node_count(), 3u);
}

TEST(DigraphTest, TryAddEdgeOutOfBoundsFails) {
  auto g_res = digraph::try_create();
  digraph g = std::move(g_res.value());
  auto a = g.try_add_node().value();
  auto res = g.try_add_edge(a, 42);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::out_of_bounds);
}

TEST(DigraphTest, SelfLoopRejectedAsDeadlock) {
  auto g_res = digraph::try_create();
  digraph g = std::move(g_res.value());
  auto a = g.try_add_node().value();
  auto res = g.try_add_edge(a, a);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::deadlock);
}

TEST(DigraphTest, AddEdgeIsIdempotent) {
  auto g_res = digraph::try_create();
  digraph g = std::move(g_res.value());
  auto a = g.try_add_node().value();
  auto b = g.try_add_node().value();
  EXPECT_TRUE(g.try_add_edge(a, b).has_value());
  EXPECT_TRUE(g.try_add_edge(a, b).has_value());
  EXPECT_TRUE(g.has_edge(a, b));
}

TEST(DigraphTest, RejectsEdgeThatWouldCloseACycle) {
  // Lock-order graph: A -> B -> C already recorded. Observing C locked
  // before A elsewhere would close a cycle (a lock-order inversion), and
  // must be rejected instead of silently accepted.
  auto g_res = digraph::try_create();
  digraph g = std::move(g_res.value());
  auto a = g.try_add_node().value();
  auto b = g.try_add_node().value();
  auto c = g.try_add_node().value();
  ASSERT_TRUE(g.try_add_edge(a, b).has_value());
  ASSERT_TRUE(g.try_add_edge(b, c).has_value());

  auto res = g.try_add_edge(c, a);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::deadlock);
  // The graph is left unchanged on rejection.
  EXPECT_FALSE(g.has_edge(c, a));
}

TEST(DigraphTest, TryIsReachableFollowsTransitiveEdges) {
  auto g_res = digraph::try_create();
  digraph g = std::move(g_res.value());
  auto a = g.try_add_node().value();
  auto b = g.try_add_node().value();
  auto c = g.try_add_node().value();
  auto d = g.try_add_node().value();
  ASSERT_TRUE(g.try_add_edge(a, b).has_value());
  ASSERT_TRUE(g.try_add_edge(b, c).has_value());

  EXPECT_TRUE(g.try_is_reachable(a, c).value());
  EXPECT_TRUE(g.try_is_reachable(a, a).value());
  EXPECT_FALSE(g.try_is_reachable(c, a).value());
  EXPECT_FALSE(g.try_is_reachable(a, d).value());
}

TEST(DigraphTest, TryFindPathReturnsShortestNodeSequence) {
  auto g_res = digraph::try_create();
  digraph g = std::move(g_res.value());
  auto a = g.try_add_node().value();
  auto b = g.try_add_node().value();
  auto c = g.try_add_node().value();
  ASSERT_TRUE(g.try_add_edge(a, b).has_value());
  ASSERT_TRUE(g.try_add_edge(b, c).has_value());
  // A shortcut edge a->c must not change the shortest path length between
  // unrelated nodes' reachability, but does shorten a->c itself.
  ASSERT_TRUE(g.try_add_edge(a, c).has_value());

  auto path = g.try_find_path(a, c);
  ASSERT_TRUE(path.has_value());
  EXPECT_EQ(path->size(), 2u);
  EXPECT_EQ((*path)[0], a);
  EXPECT_EQ((*path)[1], c);
}

TEST(DigraphTest, TryFindPathFailsWithNotFoundWhenUnreachable) {
  auto g_res = digraph::try_create();
  digraph g = std::move(g_res.value());
  auto a = g.try_add_node().value();
  auto b = g.try_add_node().value();

  auto path = g.try_find_path(a, b);
  ASSERT_FALSE(path.has_value());
  EXPECT_EQ(path.error(), error::not_found);
}

TEST(DigraphTest, TryRemoveEdgeIsANoOpWhenAbsent) {
  auto g_res = digraph::try_create();
  digraph g = std::move(g_res.value());
  auto a = g.try_add_node().value();
  auto b = g.try_add_node().value();
  EXPECT_TRUE(g.try_remove_edge(a, b).has_value());
  EXPECT_FALSE(g.has_edge(a, b));
}

TEST(DigraphTest, TryRemoveEdgeAllowsReAddingUnderTheOldOrder) {
  // Retracting a->b then adding b->a must succeed: the cycle-rejection
  // check only sees the graph as it stands right now.
  auto g_res = digraph::try_create();
  digraph g = std::move(g_res.value());
  auto a = g.try_add_node().value();
  auto b = g.try_add_node().value();
  ASSERT_TRUE(g.try_add_edge(a, b).has_value());
  ASSERT_TRUE(g.try_remove_edge(a, b).has_value());
  EXPECT_FALSE(g.has_edge(a, b));
  EXPECT_TRUE(g.try_add_edge(b, a).has_value());
}

TEST(DigraphTest, TryCloneIsADeepCopy) {
  auto g_res = digraph::try_create();
  digraph g = std::move(g_res.value());
  auto a = g.try_add_node().value();
  auto b = g.try_add_node().value();
  ASSERT_TRUE(g.try_add_edge(a, b).has_value());

  auto clone_res = g.try_clone();
  ASSERT_TRUE(clone_res.has_value());
  digraph clone = std::move(clone_res.value());
  EXPECT_TRUE(clone.has_edge(a, b));

  ASSERT_TRUE(clone.try_remove_edge(a, b).has_value());
  EXPECT_FALSE(clone.has_edge(a, b));
  EXPECT_TRUE(g.has_edge(a, b)); // Original untouched by mutating the clone.
}

TEST(DigraphTest, ClearRemovesAllNodesAndEdges) {
  auto g_res = digraph::try_create();
  digraph g = std::move(g_res.value());
  auto a = g.try_add_node().value();
  auto b = g.try_add_node().value();
  ASSERT_TRUE(g.try_add_edge(a, b).has_value());

  g.clear();
  EXPECT_EQ(g.node_count(), 0u);
}
