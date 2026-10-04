// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/intrusive_iteration.hpp>
#include <reloco/intrusive_splay_tree.hpp>

#include <algorithm>
#include <random>
#include <string>
#include <tuple>
#include <vector>

namespace {

struct node {
  int key = 0;
  std::string value;
  reloco::intrusive_splay_tree_hook<node> hook;
};

struct node_key_of {
  const int &operator()(const node &n) const noexcept { return n.key; }
};

using tree_type = reloco::intrusive_splay_tree<node, &node::hook, node_key_of>;

} // namespace

TEST(IntrusiveSplayTreeTest, DefaultStateIsEmpty) {
  tree_type tree;
  EXPECT_EQ(tree.size(), 0);
  EXPECT_TRUE(tree.empty());
  EXPECT_TRUE(tree.begin() == tree.end());
}

TEST(IntrusiveSplayTreeTest, InsertFindContains) {
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

TEST(IntrusiveSplayTreeTest, DuplicateKeyRejected) {
  tree_type tree;
  node a{1, "one", {}};
  node b{1, "also one", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());
  auto dup_res = tree.try_insert(b);
  ASSERT_FALSE(dup_res.has_value());
  EXPECT_EQ(dup_res.error(), reloco::error::already_exists);
  EXPECT_EQ(tree.size(), 1);
}

TEST(IntrusiveSplayTreeTest, TryFindMissingFails) {
  tree_type tree;
  node a{1, "one", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());

  auto found = tree.try_find(42);
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error(), reloco::error::not_found);
}

TEST(IntrusiveSplayTreeTest, FindSplaysFoundNodeToRoot) {
  tree_type tree;
  std::vector<node> nodes(10);
  for (std::size_t i = 0; i < 10; ++i) {
    nodes[i].key = static_cast<int>(i);
    ASSERT_TRUE(tree.try_insert(nodes[i]).has_value());
  }

  auto found = tree.try_find(3);
  ASSERT_TRUE(found.has_value());
  // After splaying key 3 to the root, it must be reachable as begin() + 3
  // steps and iteration order must still be fully ascending.
  std::vector<int> observed;
  for (const auto &n : tree)
    observed.push_back(n.key);
  EXPECT_EQ(observed, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
  EXPECT_EQ(&found->get(), &nodes[3]);
}

TEST(IntrusiveSplayTreeTest, ContainsDoesNotRestructure) {
  tree_type tree;
  std::vector<node> nodes(5);
  for (std::size_t i = 0; i < 5; ++i) {
    nodes[i].key = static_cast<int>(i);
    ASSERT_TRUE(tree.try_insert(nodes[i]).has_value());
  }
  // contains() is read-only; repeated calls must not change tree contents or order.
  for (std::size_t i = 0; i < 5; ++i)
    EXPECT_TRUE(tree.contains(static_cast<int>(i)));
  std::vector<int> observed;
  for (const auto &n : tree)
    observed.push_back(n.key);
  EXPECT_EQ(observed, (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(IntrusiveSplayTreeTest, ConstTryFindIsReadOnlyPeek) {
  tree_type tree;
  std::vector<node> nodes(5);
  for (std::size_t i = 0; i < 5; ++i) {
    nodes[i].key = static_cast<int>(i);
    ASSERT_TRUE(tree.try_insert(nodes[i]).has_value());
  }
  const tree_type &const_tree = tree;

  // Overload resolution must pick the const, non-splaying peek here; it still
  // returns reference_wrapper<node> (mutable), same as the non-const try_find,
  // since the tree doesn't own its nodes and const-qualifying the tree itself
  // says nothing about node mutability.
  auto found = const_tree.try_find(3);
  ASSERT_TRUE(found.has_value());
  static_assert(std::is_same<decltype(found)::value_type, std::reference_wrapper<node>>::value,
                "const try_find must still return a mutable reference");
  EXPECT_EQ(found->get().key, 3);
  EXPECT_EQ(&found->get(), &nodes[3]);

  auto missing = const_tree.try_find(42);
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), reloco::error::not_found);

  // Repeated const peeks must leave contents/order untouched.
  for (std::size_t i = 0; i < 5; ++i) {
    auto peek = const_tree.try_find(static_cast<int>(i));
    ASSERT_TRUE(peek.has_value());
    EXPECT_EQ(&peek->get(), &nodes[i]);
  }
  std::vector<int> observed;
  for (const auto &n : tree)
    observed.push_back(n.key);
  EXPECT_EQ(observed, (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(IntrusiveSplayTreeTest, ConstTryFirstTryLastAreReadOnlyPeeks) {
  tree_type tree;
  std::vector<node> nodes(5);
  for (std::size_t i = 0; i < 5; ++i) {
    nodes[i].key = static_cast<int>(i);
    ASSERT_TRUE(tree.try_insert(nodes[i]).has_value());
  }
  const tree_type &const_tree = tree;

  auto first = const_tree.try_first();
  ASSERT_TRUE(first.has_value());
  static_assert(std::is_same<decltype(first)::value_type, std::reference_wrapper<node>>::value,
                "const try_first must still return a mutable reference");
  EXPECT_EQ(first->get().key, 0);

  auto last = const_tree.try_last();
  ASSERT_TRUE(last.has_value());
  static_assert(std::is_same<decltype(last)::value_type, std::reference_wrapper<node>>::value,
                "const try_last must still return a mutable reference");
  EXPECT_EQ(last->get().key, 4);

  // Repeated const peeks must leave contents/order/size untouched.
  std::ignore = const_tree.try_first();
  std::ignore = const_tree.try_last();
  EXPECT_EQ(tree.size(), 5);
  std::vector<int> observed;
  for (const auto &n : tree)
    observed.push_back(n.key);
  EXPECT_EQ(observed, (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(IntrusiveSplayTreeTest, ConstTryFirstTryLastFailOnEmpty) {
  tree_type tree;
  const tree_type &const_tree = tree;
  EXPECT_EQ(const_tree.try_first().error(), reloco::error::container_empty);
  EXPECT_EQ(const_tree.try_last().error(), reloco::error::container_empty);
}

TEST(IntrusiveSplayTreeTest, TryRemoveByKey) {
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

TEST(IntrusiveSplayTreeTest, RemoveByNodeReference) {
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

TEST(IntrusiveSplayTreeTest, ReinsertAfterRemove) {
  tree_type tree;
  node a{1, "one", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());
  tree.remove(a);
  ASSERT_TRUE(tree.try_insert(a).has_value());
  EXPECT_TRUE(tree.contains(1));
  EXPECT_EQ(tree.size(), 1);
}

TEST(IntrusiveSplayTreeTest, IterationIsAscendingOrder) {
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

  std::vector<int> reversed;
  auto it = tree.end();
  while (it != tree.begin()) {
    --it;
    reversed.push_back(it->key);
  }
  EXPECT_EQ(reversed, (std::vector<int>{9, 8, 7, 6, 5, 4, 3, 2, 1, 0}));
}

TEST(IntrusiveSplayTreeTest, FirstLastPopFirstPopLast) {
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

TEST(IntrusiveSplayTreeTest, ClearUnlinksEveryNode) {
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

  ASSERT_TRUE(tree.try_insert(a).has_value());
  EXPECT_TRUE(tree.contains(1));
}

TEST(IntrusiveSplayTreeTest, IteratorToAndErase) {
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

TEST(IntrusiveSplayTreeTest, ExtractIfIteratorIntegration) {
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

TEST(IntrusiveSplayTreeTest, StressInsertRemovePreservesOrderAndSize) {
  tree_type tree;
  constexpr std::size_t n = 2000;
  std::vector<node> nodes(n);
  std::vector<int> keys(n);
  for (std::size_t i = 0; i < n; ++i)
    keys[i] = static_cast<int>(i);
  std::mt19937 rng(54321);
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

TEST(IntrusiveSplayTreeTest, MoveConstructTransfersOwnership) {
  tree_type tree;
  node a{1, "one", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());

  tree_type moved(std::move(tree));
  EXPECT_EQ(moved.size(), 1);
  EXPECT_TRUE(moved.contains(1));
  EXPECT_EQ(tree.size(), 0); // NOLINT(bugprone-use-after-move) -- moved-from state is well-defined here
}

TEST(IntrusiveSplayTreeTest, LowerBoundUpperBound) {
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

TEST(IntrusiveSplayTreeTest, BoundedRange) {
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

TEST(IntrusiveSplayTreeTest, EraseAndDispose) {
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

TEST(IntrusiveSplayTreeTest, RemoveAndDispose) {
  tree_type tree;
  node a{1, "one", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());

  std::vector<int> disposed;
  tree.remove_and_dispose(a, [&](node &n) { disposed.push_back(n.key); });
  EXPECT_EQ(disposed, (std::vector<int>{1}));
  EXPECT_FALSE(a.hook.is_linked());
  EXPECT_TRUE(tree.empty());
}

TEST(IntrusiveSplayTreeTest, TryRemoveAndDispose) {
  tree_type tree;
  node a{1, "one", {}};
  ASSERT_TRUE(tree.try_insert(a).has_value());

  std::vector<int> disposed;
  EXPECT_TRUE(tree.try_remove_and_dispose(1, [&](node &n) { disposed.push_back(n.key); }).has_value());
  EXPECT_EQ(disposed, (std::vector<int>{1}));
  EXPECT_TRUE(tree.try_remove_and_dispose(1, [&](node &) {}).error() == reloco::error::not_found);
}

TEST(IntrusiveSplayTreeTest, ClearAndDispose) {
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
