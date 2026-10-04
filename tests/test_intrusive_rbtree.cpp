// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/intrusive_iteration.hpp>
#include <reloco/intrusive_rbtree.hpp>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

namespace {

struct node {
  int key = 0;
  std::string value;
  reloco::intrusive_rbtree_hook<node> hook;
};

struct node_key_of {
  const int &operator()(const node &n) const noexcept { return n.key; }
};

using tree_type = reloco::intrusive_rbtree<node, &node::hook, node_key_of>;

} // namespace

TEST(IntrusiveRbtreeTest, DefaultStateIsEmpty) {
  tree_type tree;
  EXPECT_EQ(tree.size(), 0);
  EXPECT_TRUE(tree.empty());
  EXPECT_TRUE(tree.begin() == tree.end());
}

TEST(IntrusiveRbtreeTest, InsertFindContains) {
  tree_type tree;
  node a{1, "one", {}};
  node b{2, "two", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());
  ASSERT_TRUE(tree.try_insert(b).has_value());
  EXPECT_EQ(tree.size(), 2);
  EXPECT_FALSE(tree.empty());

  EXPECT_TRUE(tree.contains(1));
  EXPECT_TRUE(tree.contains(2));
  EXPECT_FALSE(tree.contains(3));

  auto found = tree.try_find(1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get().value, "one");
}

TEST(IntrusiveRbtreeTest, DuplicateKeyRejected) {
  tree_type tree;
  node a{1, "one", {}};
  node b{1, "also one", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());
  auto dup_res = tree.try_insert(b);
  ASSERT_FALSE(dup_res.has_value());
  EXPECT_EQ(dup_res.error(), reloco::error::already_exists);
  EXPECT_EQ(tree.size(), 1);
}

TEST(IntrusiveRbtreeTest, TryFindMissingFails) {
  tree_type tree;
  node a{1, "one", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());

  auto found = tree.try_find(42);
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error(), reloco::error::not_found);
}

TEST(IntrusiveRbtreeTest, TryRemoveByKey) {
  tree_type tree;
  node a{1, "one", {}};
  node b{2, "two", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());
  ASSERT_TRUE(tree.try_insert(b).has_value());

  ASSERT_TRUE(tree.try_remove(1).has_value());
  EXPECT_EQ(tree.size(), 1);
  EXPECT_FALSE(tree.contains(1));
  EXPECT_TRUE(tree.contains(2));
  EXPECT_FALSE(a.hook.is_linked());

  auto missing_res = tree.try_remove(1);
  ASSERT_FALSE(missing_res.has_value());
  EXPECT_EQ(missing_res.error(), reloco::error::not_found);
}

TEST(IntrusiveRbtreeTest, RemoveByNodeReference) {
  tree_type tree;
  node a{1, "one", {}};
  node b{2, "two", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());
  ASSERT_TRUE(tree.try_insert(b).has_value());

  tree.remove(a);
  EXPECT_EQ(tree.size(), 1);
  EXPECT_FALSE(a.hook.is_linked());
  EXPECT_FALSE(tree.contains(1));
  EXPECT_TRUE(tree.contains(2));
}

TEST(IntrusiveRbtreeTest, ReinsertAfterRemove) {
  tree_type tree;
  node a{1, "one", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());
  tree.remove(a);
  ASSERT_TRUE(tree.try_insert(a).has_value());
  EXPECT_TRUE(tree.contains(1));
  EXPECT_EQ(tree.size(), 1);
}

TEST(IntrusiveRbtreeTest, IterationIsAscendingOrder) {
  tree_type tree;
  std::vector<node> nodes(10);
  std::vector<int> keys{5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    nodes[i].key = keys[i];
    ASSERT_TRUE(tree.try_insert(nodes[i]).has_value());
  }

  std::vector<int> observed;
  for (const auto &n : tree)
    observed.push_back(n.key);
  EXPECT_EQ(observed, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));

  // Bidirectional traversal (reverse via operator--).
  std::vector<int> reversed;
  auto it = tree.end();
  while (it != tree.begin()) {
    --it;
    reversed.push_back(it->key);
  }
  EXPECT_EQ(reversed, (std::vector<int>{9, 8, 7, 6, 5, 4, 3, 2, 1, 0}));
}

TEST(IntrusiveRbtreeTest, FirstLastPopFirstPopLast) {
  tree_type tree;
  EXPECT_EQ(tree.try_first().error(), reloco::error::container_empty);
  EXPECT_EQ(tree.try_last().error(), reloco::error::container_empty);

  std::vector<node> nodes(5);
  for (std::size_t i = 0; i < 5; ++i) {
    nodes[i].key = static_cast<int>(i);
    ASSERT_TRUE(tree.try_insert(nodes[i]).has_value());
  }

  auto first = tree.try_first();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->get().key, 0);
  auto last = tree.try_last();
  ASSERT_TRUE(last.has_value());
  EXPECT_EQ(last->get().key, 4);

  auto popped_first = tree.try_pop_first();
  ASSERT_TRUE(popped_first.has_value());
  EXPECT_EQ(popped_first->get().key, 0);
  EXPECT_FALSE(nodes[0].hook.is_linked());

  auto popped_last = tree.try_pop_last();
  ASSERT_TRUE(popped_last.has_value());
  EXPECT_EQ(popped_last->get().key, 4);
  EXPECT_FALSE(nodes[4].hook.is_linked());

  EXPECT_EQ(tree.size(), 3);
}

TEST(IntrusiveRbtreeTest, ClearUnlinksEveryNode) {
  tree_type tree;
  node a{1, "one", {}};
  node b{2, "two", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());
  ASSERT_TRUE(tree.try_insert(b).has_value());

  tree.clear();
  EXPECT_EQ(tree.size(), 0);
  EXPECT_TRUE(tree.empty());
  EXPECT_FALSE(a.hook.is_linked());
  EXPECT_FALSE(b.hook.is_linked());
  EXPECT_FALSE(tree.contains(1));
  EXPECT_FALSE(tree.contains(2));

  // The tree must be reusable after clear().
  ASSERT_TRUE(tree.try_insert(a).has_value());
  EXPECT_TRUE(tree.contains(1));
}

TEST(IntrusiveRbtreeTest, IteratorToAndErase) {
  tree_type tree;
  node a{1, "one", {}};
  node b{2, "two", {}};
  node c{3, "three", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());
  ASSERT_TRUE(tree.try_insert(b).has_value());
  ASSERT_TRUE(tree.try_insert(c).has_value());

  auto it = tree.iterator_to(b);
  EXPECT_EQ(it->key, 2);

  auto next = tree.erase(it);
  EXPECT_EQ(next->key, 3);
  EXPECT_FALSE(b.hook.is_linked());
  EXPECT_EQ(tree.size(), 2);
}

TEST(IntrusiveRbtreeTest, ExtractIfIteratorIntegration) {
  tree_type tree;
  node a{1, "one", {}};
  node b{2, "two", {}};
  node c{3, "three", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());
  ASSERT_TRUE(tree.try_insert(b).has_value());
  ASSERT_TRUE(tree.try_insert(c).has_value());

  std::vector<int> extracted;
  reloco::extract_if_iterator<tree_type, bool (*)(const node &)>(tree, [](const node &n) { return n.key != 2; })
      .for_each([&](auto &&tx_obj) {
        auto &tx = tx_obj.as_known();
        extracted.push_back(tx.get().key);
        tx.release_to([](node *) {});
      });

  std::sort(extracted.begin(), extracted.end());
  EXPECT_EQ(extracted, (std::vector<int>{1, 3}));
  EXPECT_EQ(tree.size(), 1);
  EXPECT_TRUE(tree.contains(2));
  EXPECT_FALSE(a.hook.is_linked());
  EXPECT_FALSE(c.hook.is_linked());
}

TEST(IntrusiveRbtreeTest, StressInsertRemovePreservesOrderAndSize) {
  tree_type tree;
  constexpr std::size_t n = 2000;
  std::vector<node> nodes(n);
  std::vector<int> keys(n);
  for (std::size_t i = 0; i < n; ++i)
    keys[i] = static_cast<int>(i);
  std::mt19937 rng(12345);
  std::shuffle(keys.begin(), keys.end(), rng);
  for (std::size_t i = 0; i < n; ++i) {
    nodes[i].key = keys[i];
    ASSERT_TRUE(tree.try_insert(nodes[i]).has_value());
  }
  ASSERT_EQ(tree.size(), n);

  int prev = -1;
  std::size_t count = 0;
  for (const auto &node_ref : tree) {
    EXPECT_GT(node_ref.key, prev);
    prev = node_ref.key;
    ++count;
  }
  EXPECT_EQ(count, n);

  std::shuffle(keys.begin(), keys.end(), rng);
  for (std::size_t i = 0; i < n / 2; ++i) {
    auto found = tree.try_find(keys[i]);
    ASSERT_TRUE(found.has_value());
    tree.remove(found->get());
  }
  EXPECT_EQ(tree.size(), n / 2);

  for (std::size_t i = 0; i < n / 2; ++i)
    EXPECT_FALSE(tree.contains(keys[i]));
  for (std::size_t i = n / 2; i < n; ++i)
    EXPECT_TRUE(tree.contains(keys[i]));

  prev = -1;
  count = 0;
  for (const auto &node_ref : tree) {
    EXPECT_GT(node_ref.key, prev);
    prev = node_ref.key;
    ++count;
  }
  EXPECT_EQ(count, static_cast<std::size_t>(n / 2));
}

TEST(IntrusiveRbtreeTest, MoveConstructTransfersOwnership) {
  tree_type tree;
  node a{1, "one", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());

  tree_type moved(std::move(tree));
  EXPECT_EQ(moved.size(), 1);
  EXPECT_TRUE(moved.contains(1));
  EXPECT_EQ(tree.size(), 0); // NOLINT(bugprone-use-after-move) -- moved-from state is well-defined here
}

TEST(IntrusiveRbtreeTest, LowerBoundUpperBound) {
  tree_type tree;
  node nodes[] = {{10, "a", {}}, {20, "b", {}}, {30, "c", {}}, {40, "d", {}}};
  for (auto &n : nodes)
    ASSERT_TRUE(tree.try_insert(n).has_value());

  EXPECT_EQ(tree.lower_bound(20)->key, 20);
  EXPECT_EQ(tree.lower_bound(25)->key, 30);
  EXPECT_TRUE(tree.lower_bound(41) == tree.end());

  EXPECT_EQ(tree.upper_bound(20)->key, 30);
  EXPECT_EQ(tree.upper_bound(25)->key, 30);
  EXPECT_TRUE(tree.upper_bound(40) == tree.end());

  const tree_type &const_tree = tree;
  EXPECT_EQ(const_tree.lower_bound(20)->key, 20);
  EXPECT_EQ(const_tree.upper_bound(20)->key, 30);
}

TEST(IntrusiveRbtreeTest, BoundedRange) {
  tree_type tree;
  node nodes[] = {{10, "a", {}}, {20, "b", {}}, {30, "c", {}}, {40, "d", {}}};
  for (auto &n : nodes)
    ASSERT_TRUE(tree.try_insert(n).has_value());

  {
    auto [first, last] = tree.bounded_range(20, 40);
    std::vector<int> keys;
    for (auto it = first; it != last; ++it)
      keys.push_back(it->key);
    EXPECT_EQ(keys, (std::vector<int>{20, 30}));
  }
  {
    auto [first, last] = tree.bounded_range(20, 40, true, true);
    std::vector<int> keys;
    for (auto it = first; it != last; ++it)
      keys.push_back(it->key);
    EXPECT_EQ(keys, (std::vector<int>{20, 30, 40}));
  }
  {
    auto [first, last] = tree.bounded_range(20, 40, false, false);
    std::vector<int> keys;
    for (auto it = first; it != last; ++it)
      keys.push_back(it->key);
    EXPECT_EQ(keys, (std::vector<int>{30}));
  }
  {
    auto [first, last] = tree.bounded_range(100, 1);
    EXPECT_TRUE(first == tree.end());
    EXPECT_TRUE(last == tree.end());
  }
}

TEST(IntrusiveRbtreeTest, RangeErase) {
  tree_type tree;
  node nodes[] = {{10, "a", {}}, {20, "b", {}}, {30, "c", {}}, {40, "d", {}}, {50, "e", {}}};
  for (auto &n : nodes)
    ASSERT_TRUE(tree.try_insert(n).has_value());

  auto first = tree.lower_bound(20);
  auto last = tree.lower_bound(40);
  auto next = tree.erase(first, last);
  ASSERT_TRUE(next != tree.end());
  EXPECT_EQ(next->key, 40);
  EXPECT_EQ(tree.size(), 3);
  EXPECT_TRUE(tree.contains(10));
  EXPECT_FALSE(tree.contains(20));
  EXPECT_FALSE(tree.contains(30));
  EXPECT_TRUE(tree.contains(40));
  EXPECT_TRUE(tree.contains(50));

  // An empty range (first == last) erases nothing.
  auto same = tree.lower_bound(40);
  auto unchanged = tree.erase(same, same);
  EXPECT_EQ(unchanged->key, 40);
  EXPECT_EQ(tree.size(), 3);

  // The full range erases everything.
  EXPECT_TRUE(tree.erase(tree.begin(), tree.end()) == tree.end());
  EXPECT_TRUE(tree.empty());
}

TEST(IntrusiveRbtreeTest, RangeEraseAndDispose) {
  tree_type tree;
  node nodes[] = {{10, "a", {}}, {20, "b", {}}, {30, "c", {}}, {40, "d", {}}};
  for (auto &n : nodes)
    ASSERT_TRUE(tree.try_insert(n).has_value());

  std::vector<int> disposed;
  auto first = tree.lower_bound(20);
  auto last = tree.lower_bound(40);
  auto next = tree.erase_and_dispose(first, last, [&](node &n) { disposed.push_back(n.key); });
  std::sort(disposed.begin(), disposed.end());
  EXPECT_EQ(disposed, (std::vector<int>{20, 30}));
  ASSERT_TRUE(next != tree.end());
  EXPECT_EQ(next->key, 40);
  EXPECT_EQ(tree.size(), 2);
  EXPECT_FALSE(nodes[1].hook.is_linked());
  EXPECT_FALSE(nodes[2].hook.is_linked());
}

TEST(IntrusiveRbtreeTest, EraseAndDispose) {
  tree_type tree;
  node a{1, "one", {}};
  node b{2, "two", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());
  ASSERT_TRUE(tree.try_insert(b).has_value());

  std::vector<int> disposed;
  auto it = tree.iterator_to(a);
  auto next = tree.erase_and_dispose(it, [&](node &n) { disposed.push_back(n.key); });
  EXPECT_EQ(disposed, (std::vector<int>{1}));
  EXPECT_FALSE(a.hook.is_linked());
  ASSERT_TRUE(next != tree.end());
  EXPECT_EQ(next->key, 2);
  EXPECT_EQ(tree.size(), 1);
}

TEST(IntrusiveRbtreeTest, RemoveAndDispose) {
  tree_type tree;
  node a{1, "one", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());

  std::vector<int> disposed;
  tree.remove_and_dispose(a, [&](node &n) { disposed.push_back(n.key); });
  EXPECT_EQ(disposed, (std::vector<int>{1}));
  EXPECT_FALSE(a.hook.is_linked());
  EXPECT_TRUE(tree.empty());
}

TEST(IntrusiveRbtreeTest, TryRemoveAndDispose) {
  tree_type tree;
  node a{1, "one", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());

  std::vector<int> disposed;
  EXPECT_TRUE(tree.try_remove_and_dispose(1, [&](node &n) { disposed.push_back(n.key); }).has_value());
  EXPECT_EQ(disposed, (std::vector<int>{1}));
  EXPECT_TRUE(tree.try_remove_and_dispose(1, [&](node &) {}).error() == reloco::error::not_found);
}

TEST(IntrusiveRbtreeTest, ClearAndDispose) {
  tree_type tree;
  node nodes[] = {{1, "a", {}}, {2, "b", {}}, {3, "c", {}}};
  for (auto &n : nodes)
    ASSERT_TRUE(tree.try_insert(n).has_value());

  std::vector<int> disposed;
  tree.clear_and_dispose([&](node &n) { disposed.push_back(n.key); });
  std::sort(disposed.begin(), disposed.end());
  EXPECT_EQ(disposed, (std::vector<int>{1, 2, 3}));
  EXPECT_TRUE(tree.empty());
  for (auto &n : nodes)
    EXPECT_FALSE(n.hook.is_linked());
}

TEST(IntrusiveRbtreeTest, SpliceRetainsConflictsInSource) {
  tree_type source;
  tree_type target;
  node src_nodes[] = {{10, "s10", {}}, {20, "s20", {}}, {30, "s30", {}}};
  node tgt_nodes[] = {{20, "t20", {}}, {40, "t40", {}}};
  for (auto &n : src_nodes)
    ASSERT_TRUE(source.try_insert(n).has_value());
  for (auto &n : tgt_nodes)
    ASSERT_TRUE(target.try_insert(n).has_value());

  auto moved = target.splice(source, source.begin(), source.end());
  EXPECT_EQ(moved, 2); // 10 and 30 move; 20 conflicts and stays in source

  EXPECT_EQ(source.size(), 1);
  ASSERT_TRUE(source.contains(20));
  auto found_20 = source.try_find(20);
  ASSERT_TRUE(found_20.has_value());
  EXPECT_EQ(found_20->get().value, "s20");

  EXPECT_EQ(target.size(), 4);
  EXPECT_TRUE(target.contains(10));
  EXPECT_TRUE(target.contains(30));
  EXPECT_TRUE(target.contains(40));
  ASSERT_TRUE(target.contains(20));
  auto found_20_target = target.try_find(20);
  ASSERT_TRUE(found_20_target.has_value());
  EXPECT_EQ(found_20_target->get().value, "t20"); // target's own node wins, untouched
}

TEST(IntrusiveRbtreeTest, SpliceWholeTreeConvenienceOverload) {
  tree_type source;
  tree_type target;
  node src_nodes[] = {{1, "a", {}}, {2, "b", {}}};
  for (auto &n : src_nodes)
    ASSERT_TRUE(source.try_insert(n).has_value());

  auto moved = target.splice(source);
  EXPECT_EQ(moved, 2);
  EXPECT_TRUE(source.empty());
  EXPECT_EQ(target.size(), 2);
}

TEST(IntrusiveRbtreeTest, SpliceReplaceEvictsConflictingTargetNode) {
  tree_type source;
  tree_type target;
  node src_nodes[] = {{10, "s10", {}}, {20, "s20", {}}};
  node tgt_nodes[] = {{20, "t20", {}}, {40, "t40", {}}};
  for (auto &n : src_nodes)
    ASSERT_TRUE(source.try_insert(n).has_value());
  for (auto &n : tgt_nodes)
    ASSERT_TRUE(target.try_insert(n).has_value());

  std::vector<std::string> disposed;
  auto moved = target.splice_replace(source, source.begin(), source.end(),
                                      [&](node &n) { disposed.push_back(n.value); });
  EXPECT_EQ(moved, 2); // every source node moves, none left behind
  EXPECT_TRUE(source.empty());
  EXPECT_EQ(disposed, (std::vector<std::string>{"t20"})); // the evicted target node

  EXPECT_EQ(target.size(), 3);
  ASSERT_TRUE(target.contains(20));
  auto found_20 = target.try_find(20);
  ASSERT_TRUE(found_20.has_value());
  EXPECT_EQ(found_20->get().value, "s20"); // source's node won
  EXPECT_TRUE(target.contains(10));
  EXPECT_TRUE(target.contains(40));
  EXPECT_FALSE(tgt_nodes[0].hook.is_linked()); // the evicted node is unlinked
}

TEST(IntrusiveRbtreeTest, SpliceDiscardKeepsExistingTargetNode) {
  tree_type source;
  tree_type target;
  node src_nodes[] = {{10, "s10", {}}, {20, "s20", {}}};
  node tgt_nodes[] = {{20, "t20", {}}, {40, "t40", {}}};
  for (auto &n : src_nodes)
    ASSERT_TRUE(source.try_insert(n).has_value());
  for (auto &n : tgt_nodes)
    ASSERT_TRUE(target.try_insert(n).has_value());

  std::vector<std::string> disposed;
  auto moved = target.splice_discard(source, source.begin(), source.end(),
                                      [&](node &n) { disposed.push_back(n.value); });
  EXPECT_EQ(moved, 1); // only 10 moves; 20 is discarded
  EXPECT_TRUE(source.empty());
  EXPECT_EQ(disposed, (std::vector<std::string>{"s20"})); // the discarded source node

  EXPECT_EQ(target.size(), 3);
  ASSERT_TRUE(target.contains(20));
  auto found_20 = target.try_find(20);
  ASSERT_TRUE(found_20.has_value());
  EXPECT_EQ(found_20->get().value, "t20"); // target's own node is retained
  EXPECT_TRUE(target.contains(10));
  EXPECT_TRUE(target.contains(40));
  EXPECT_FALSE(src_nodes[1].hook.is_linked()); // the discarded node is unlinked
}
