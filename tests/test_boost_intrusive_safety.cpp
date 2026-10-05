// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/boost_intrusive_safe_avl_set_hook.hpp"
#include "reloco/boost_intrusive_safe_bs_set_hook.hpp"
#include "reloco/boost_intrusive_safe_list_hook.hpp"
#include "reloco/boost_intrusive_safe_set_hook.hpp"
#include "reloco/boost_intrusive_safe_unordered_set_hook.hpp"
#include <array>
#include <boost/intrusive/avl_set.hpp>
#include <boost/intrusive/bs_set.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/splay_set.hpp>
#include <boost/intrusive/unordered_set.hpp>
#include <gtest/gtest.h>

// Every EXPECT_DEATH below re-execs this binary and expands to gtest's own
// internal fprintf() call sites (gtest-death-test-internal.h) -- not
// anything in reloco or this file -- which -Wunsafe-buffer-usage-in-libc-call
// otherwise flags; see RELOCO_BEGIN_UNSAFE_BUFFER_USAGE's own docs
// (lifetime.hpp) and tests/test_packed_bits.cpp for the same pattern.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

struct task : public reloco::boost_intrusive::safe_list_hook<> {
  int id;
  explicit task(int i) : id(i) {}
};
using task_list = boost::intrusive::list<task>;

struct ordered : public reloco::boost_intrusive::safe_set_hook<> {
  int value;
  explicit ordered(int v) : value(v) {}
  friend bool operator<(const ordered &a, const ordered &b) noexcept { return a.value < b.value; }
};
using ordered_set = boost::intrusive::set<ordered>;

struct hashed : public reloco::boost_intrusive::safe_unordered_set_hook<> {
  int key;
  explicit hashed(int k) : key(k) {}
  friend bool operator==(const hashed &a, const hashed &b) noexcept { return a.key == b.key; }
  friend std::size_t hash_value(const hashed &h) noexcept { return static_cast<std::size_t>(h.key); }
};
using hashed_set = boost::intrusive::unordered_set<hashed>;
// A plain alias avoids an unparenthesized template-argument comma
// (`std::array<hashed_set::bucket_type, 16>`) inside an `EXPECT_DEATH`
// macro argument below, which the preprocessor would otherwise
// misparse as two separate macro arguments.
using hashed_bucket_array = std::array<hashed_set::bucket_type, 16>;

struct member_task {
  reloco::boost_intrusive::safe_list_member_hook<> hook;
  int id;
  explicit member_task(int i) : id(i) {}
};
using member_task_list =
    boost::intrusive::list<member_task,
                            boost::intrusive::member_hook<member_task, reloco::boost_intrusive::safe_list_member_hook<>,
                                                           &member_task::hook>>;

struct member_ordered {
  reloco::boost_intrusive::safe_set_member_hook<> hook;
  int value;
  explicit member_ordered(int v) : value(v) {}
  friend bool operator<(const member_ordered &a, const member_ordered &b) noexcept { return a.value < b.value; }
};
using member_ordered_set = boost::intrusive::set<
    member_ordered,
    boost::intrusive::member_hook<member_ordered, reloco::boost_intrusive::safe_set_member_hook<>, &member_ordered::hook>>;

struct member_hashed {
  reloco::boost_intrusive::safe_unordered_set_member_hook<> hook;
  int key;
  explicit member_hashed(int k) : key(k) {}
  friend bool operator==(const member_hashed &a, const member_hashed &b) noexcept { return a.key == b.key; }
  friend std::size_t hash_value(const member_hashed &h) noexcept { return static_cast<std::size_t>(h.key); }
};
using member_hashed_set = boost::intrusive::unordered_set<
    member_hashed, boost::intrusive::member_hook<member_hashed, reloco::boost_intrusive::safe_unordered_set_member_hook<>,
                                                  &member_hashed::hook>>;
using member_hashed_bucket_array = std::array<member_hashed_set::bucket_type, 16>;

struct bs_node : public reloco::boost_intrusive::safe_bs_set_hook<> {
  int value;
  explicit bs_node(int v) : value(v) {}
  friend bool operator<(const bs_node &a, const bs_node &b) noexcept { return a.value < b.value; }
};
using bs_node_set = boost::intrusive::bs_set<bs_node>;

struct member_bs_node {
  reloco::boost_intrusive::safe_bs_set_member_hook<> hook;
  int value;
  explicit member_bs_node(int v) : value(v) {}
  friend bool operator<(const member_bs_node &a, const member_bs_node &b) noexcept { return a.value < b.value; }
};
using member_bs_splay_set = boost::intrusive::splay_set<
    member_bs_node,
    boost::intrusive::member_hook<member_bs_node, reloco::boost_intrusive::safe_bs_set_member_hook<>,
                                   &member_bs_node::hook>>;

struct avl_node : public reloco::boost_intrusive::safe_avl_set_hook<> {
  int value;
  explicit avl_node(int v) : value(v) {}
  friend bool operator<(const avl_node &a, const avl_node &b) noexcept { return a.value < b.value; }
};
using avl_node_set = boost::intrusive::avl_set<avl_node>;

struct member_avl_node {
  reloco::boost_intrusive::safe_avl_set_member_hook<> hook;
  int value;
  explicit member_avl_node(int v) : value(v) {}
  friend bool operator<(const member_avl_node &a, const member_avl_node &b) noexcept { return a.value < b.value; }
};
using member_avl_node_set = boost::intrusive::avl_set<
    member_avl_node,
    boost::intrusive::member_hook<member_avl_node, reloco::boost_intrusive::safe_avl_set_member_hook<>,
                                   &member_avl_node::hook>>;

} // namespace

TEST(BoostIntrusiveSafeListHookTest, UsableAsOrdinaryListBaseHook) {
  task t1(1), t2(2);
  task_list list;
  list.push_back(t1);
  list.push_back(t2);
  EXPECT_EQ(list.size(), 2u);
  list.erase(list.iterator_to(t1));
  list.erase(list.iterator_to(t2));
  EXPECT_TRUE(list.empty());
}

TEST(BoostIntrusiveSafeListHookTest, IsLinkedReflectsLinkState) {
  task t(1);
  task_list list;
  EXPECT_FALSE(t.is_linked());
  list.push_back(t);
  EXPECT_TRUE(t.is_linked());
  list.erase(list.iterator_to(t));
  EXPECT_FALSE(t.is_linked());
}

TEST(BoostIntrusiveSafeListHookDeathTest, DestroyingLinkedNodeTraps) {
  EXPECT_DEATH(
      {
        task_list list;
        task t(1);
        list.push_back(t);
        // `t` goes out of scope here while still linked -- the
        // RELOCO_ASSERT in safe_list_hook's destructor must fire before
        // Boost's own (now-unreachable) BOOST_ASSERT would.
      },
      "");
}

TEST(BoostIntrusiveSafeSetHookTest, UsableAsOrdinarySetBaseHook) {
  ordered a(3), b(1), c(2);
  ordered_set set;
  set.insert(a);
  set.insert(b);
  set.insert(c);
  auto it = set.begin();
  EXPECT_EQ((it++)->value, 1);
  EXPECT_EQ((it++)->value, 2);
  EXPECT_EQ((it++)->value, 3);
  set.erase(set.iterator_to(a));
  set.erase(set.iterator_to(b));
  set.erase(set.iterator_to(c));
}

TEST(BoostIntrusiveSafeSetHookDeathTest, DestroyingLinkedNodeTraps) {
  EXPECT_DEATH(
      {
        ordered_set set;
        ordered o(1);
        set.insert(o);
      },
      "");
}

TEST(BoostIntrusiveSafeUnorderedSetHookTest, UsableAsOrdinaryUnorderedSetBaseHook) {
  hashed_bucket_array buckets;
  hashed_set set(hashed_set::bucket_traits(buckets.data(), buckets.size()));
  hashed h1(7);
  set.insert(h1);
  EXPECT_EQ(set.size(), 1u);
  set.erase(set.iterator_to(h1));
  EXPECT_TRUE(set.empty());
}

TEST(BoostIntrusiveSafeUnorderedSetHookDeathTest, DestroyingLinkedNodeTraps) {
  EXPECT_DEATH(
      {
        hashed_bucket_array buckets;
        hashed_set set(hashed_set::bucket_traits(buckets.data(), buckets.size()));
        hashed h(7);
        set.insert(h);
      },
      "");
}

TEST(BoostIntrusiveSafeListMemberHookTest, UsableAsOrdinaryListMemberHook) {
  member_task t1(1), t2(2);
  member_task_list list;
  list.push_back(t1);
  list.push_back(t2);
  EXPECT_EQ(list.size(), 2u);
  list.erase(list.iterator_to(t1));
  list.erase(list.iterator_to(t2));
  EXPECT_TRUE(list.empty());
}

TEST(BoostIntrusiveSafeListMemberHookDeathTest, DestroyingLinkedNodeTraps) {
  EXPECT_DEATH(
      {
        member_task_list list;
        member_task t(1);
        list.push_back(t);
      },
      "");
}

TEST(BoostIntrusiveSafeSetMemberHookTest, UsableAsOrdinarySetMemberHook) {
  member_ordered a(3), b(1);
  member_ordered_set set;
  set.insert(a);
  set.insert(b);
  EXPECT_EQ(set.begin()->value, 1);
  set.erase(set.iterator_to(a));
  set.erase(set.iterator_to(b));
}

TEST(BoostIntrusiveSafeSetMemberHookDeathTest, DestroyingLinkedNodeTraps) {
  EXPECT_DEATH(
      {
        member_ordered_set set;
        member_ordered o(1);
        set.insert(o);
      },
      "");
}

TEST(BoostIntrusiveSafeUnorderedSetMemberHookTest, UsableAsOrdinaryUnorderedSetMemberHook) {
  member_hashed_bucket_array buckets;
  member_hashed_set set(member_hashed_set::bucket_traits(buckets.data(), buckets.size()));
  member_hashed h1(7);
  set.insert(h1);
  EXPECT_EQ(set.size(), 1u);
  set.erase(set.iterator_to(h1));
  EXPECT_TRUE(set.empty());
}

TEST(BoostIntrusiveSafeUnorderedSetMemberHookDeathTest, DestroyingLinkedNodeTraps) {
  EXPECT_DEATH(
      {
        member_hashed_bucket_array buckets;
        member_hashed_set set(member_hashed_set::bucket_traits(buckets.data(), buckets.size()));
        member_hashed h(7);
        set.insert(h);
      },
      "");
}

TEST(BoostIntrusiveSafeBsSetHookTest, UsableAsOrdinaryBsSetBaseHook) {
  bs_node a(3), b(1);
  bs_node_set set;
  set.insert(a);
  set.insert(b);
  EXPECT_EQ(set.begin()->value, 1);
  set.erase(set.iterator_to(a));
  set.erase(set.iterator_to(b));
}

TEST(BoostIntrusiveSafeBsSetHookDeathTest, DestroyingLinkedNodeTraps) {
  EXPECT_DEATH(
      {
        bs_node_set set;
        bs_node n(1);
        set.insert(n);
      },
      "");
}

TEST(BoostIntrusiveSafeBsSetMemberHookTest, UsableAsOrdinarySplaySetMemberHook) {
  member_bs_node a(3), b(1);
  member_bs_splay_set set;
  set.insert(a);
  set.insert(b);
  EXPECT_EQ(set.begin()->value, 1);
  set.erase(set.iterator_to(a));
  set.erase(set.iterator_to(b));
}

TEST(BoostIntrusiveSafeBsSetMemberHookDeathTest, DestroyingLinkedNodeTraps) {
  EXPECT_DEATH(
      {
        member_bs_splay_set set;
        member_bs_node n(1);
        set.insert(n);
      },
      "");
}

TEST(BoostIntrusiveSafeAvlSetHookTest, UsableAsOrdinaryAvlSetBaseHook) {
  avl_node a(3), b(1);
  avl_node_set set;
  set.insert(a);
  set.insert(b);
  EXPECT_EQ(set.begin()->value, 1);
  set.erase(set.iterator_to(a));
  set.erase(set.iterator_to(b));
}

TEST(BoostIntrusiveSafeAvlSetHookDeathTest, DestroyingLinkedNodeTraps) {
  EXPECT_DEATH(
      {
        avl_node_set set;
        avl_node n(1);
        set.insert(n);
      },
      "");
}

TEST(BoostIntrusiveSafeAvlSetMemberHookTest, UsableAsOrdinaryAvlSetMemberHook) {
  member_avl_node a(3), b(1);
  member_avl_node_set set;
  set.insert(a);
  set.insert(b);
  EXPECT_EQ(set.begin()->value, 1);
  set.erase(set.iterator_to(a));
  set.erase(set.iterator_to(b));
}

TEST(BoostIntrusiveSafeAvlSetMemberHookDeathTest, DestroyingLinkedNodeTraps) {
  EXPECT_DEATH(
      {
        member_avl_node_set set;
        member_avl_node n(1);
        set.insert(n);
      },
      "");
}

RELOCO_END_UNSAFE_BUFFER_USAGE
