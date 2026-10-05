// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/boost_intrusive_adapter.hpp"
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <gtest/gtest.h>

using namespace boost::intrusive;

namespace {

struct item : public list_base_hook<link_mode<auto_unlink>>, public set_base_hook<link_mode<auto_unlink>> {
  int value;
  bool blocked = false;

  explicit item(int v, bool b = false) : value(v), blocked(b) {}

  // Order items by value, so `set_inserter`/`set<item>` can keep them sorted.
  friend bool operator<(const item &a, const item &b) noexcept { return a.value < b.value; }
};

using item_list = list<item, constant_time_size<false>, base_hook<list_base_hook<link_mode<auto_unlink>>>>;
using item_set = set<item, constant_time_size<false>, base_hook<set_base_hook<link_mode<auto_unlink>>>>;

} // namespace

// `reloco::iter()` (iterator.hpp) needs no adapter at all: a
// `boost::intrusive::list` already exposes a plain `begin()`/`end()`
// yielding `item&`.
TEST(BoostIntrusiveAdapterTest, IterWorksDirectlyOnBoostIntrusiveList) {
  item a(1), b(2), c(3);
  item_list list;
  list.push_back(a);
  list.push_back(b);
  list.push_back(c);

  int sum = 0;
  reloco::iter(list).for_each([&](item &it) { sum += it.value; });
  EXPECT_EQ(sum, 6);
}

TEST(BoostIntrusiveAdapterTest, ExtractIfRelinksWithPushBackInserter) {
  item a(1, true), b(2, false), c(3, true);
  item_list active;
  item_list blocked;
  active.push_back(a);
  active.push_back(b);
  active.push_back(c);

  reloco::extract_if(active, [](item &it) { return it.blocked; }).for_each([&](auto &&tx_obj) {
    auto &tx = tx_obj.as_known();
    tx.relink_to(blocked, reloco::boost_intrusive::push_back_inserter());
  });

  ASSERT_EQ(active.size(), 1u);
  EXPECT_EQ(&active.front(), &b);
  ASSERT_EQ(blocked.size(), 2u);
  EXPECT_EQ(blocked.front().value, 1);
  EXPECT_EQ(blocked.back().value, 3);
}

TEST(BoostIntrusiveAdapterTest, ExtractIfRelinksWithInsertInserterIntoAnotherSet) {
  item a(3), b(1), c(2);
  item_set active;
  item_set collected;
  active.insert(a);
  active.insert(b);
  active.insert(c);

  reloco::extract_if(active, [](item &) { return true; }).for_each([&](auto &&tx_obj) {
    auto &tx = tx_obj.as_known();
    tx.relink_to(collected, reloco::boost_intrusive::insert_inserter());
  });

  EXPECT_TRUE(active.empty());
  ASSERT_EQ(collected.size(), 3u);
  auto cur = collected.begin();
  EXPECT_EQ((cur++)->value, 1);
  EXPECT_EQ((cur++)->value, 2);
  EXPECT_EQ((cur++)->value, 3);
}

TEST(BoostIntrusiveAdapterTest, ExtractIfReleasesWithDefaultDeleteDisposer) {
  auto *a = new item(1, true);
  auto *b = new item(2, false);
  item_list active;
  active.push_back(*a);
  active.push_back(*b);

  reloco::extract_if(active, [](item &it) { return it.blocked; }).for_each([&](auto &&tx_obj) {
    auto &tx = tx_obj.as_known();
    tx.release_to(reloco::boost_intrusive::default_delete_disposer<item>());
  });

  ASSERT_EQ(active.size(), 1u);
  EXPECT_EQ(&active.front(), b);
  delete b;
}

// A foreign API that only ever hands back a `list_member_hook<>*` can
// recover the owning object via `container_of<Member>` -- there is
// nothing Boost-specific about it, see `container_of.hpp`.
TEST(BoostIntrusiveAdapterTest, ContainerOfRecoversOwnerFromMemberHook) {
  struct node {
    list_member_hook<> hook;
    int value;
    explicit node(int v) : value(v) {}
  };

  node n(42);
  list_member_hook<> *hook_ptr = &n.hook;
  node *owner = reloco::container_of<&node::hook>(hook_ptr);
  EXPECT_EQ(owner, &n);
  EXPECT_EQ(owner->value, 42);
}
