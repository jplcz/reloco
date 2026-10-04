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
