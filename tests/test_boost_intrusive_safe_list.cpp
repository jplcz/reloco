// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "reloco/boost_intrusive_safe_list.hpp"
#include "reloco/boost_intrusive_safe_list_hook.hpp"
#include <gtest/gtest.h>

namespace {

struct task : public reloco::boost_intrusive::safe_list_hook<> {
  int priority;
  bool is_blocked;
  int ticks_left;

  explicit task(int p, bool blocked = false, int ticks = 1) : priority(p), is_blocked(blocked), ticks_left(ticks) {}

  // Returns true once the task has finished (no more ticks left).
  bool tick() noexcept { return --ticks_left <= 0; }
};

using task_list = reloco::boost_intrusive::safe_list<task>;

} // namespace

TEST(BoostIntrusiveSafeListTest, UsableAsOrdinaryBoostIntrusiveList) {
  task t1(1), t2(2), t3(3);
  task_list list;
  list.push_back(t1);
  list.push_back(t2);
  list.push_back(t3);
  EXPECT_EQ(list.size(), 3u);
  list.erase(list.iterator_to(t1));
  list.erase(list.iterator_to(t2));
  list.erase(list.iterator_to(t3));
  EXPECT_TRUE(list.empty());
}

TEST(BoostIntrusiveSafeListTest, IterOverlayMatchesFreeFunctionIter) {
  task t1(5), t2(7), t3(11);
  task_list list;
  list.push_back(t1);
  list.push_back(t2);
  list.push_back(t3);

  int member_total = list.iter().map([](task &t) { return t.priority; }).sum();
  int free_function_total = reloco::iter(list).map([](task &t) { return t.priority; }).sum();
  EXPECT_EQ(member_total, 23);
  EXPECT_EQ(member_total, free_function_total);

  list.erase(list.iterator_to(t1));
  list.erase(list.iterator_to(t2));
  list.erase(list.iterator_to(t3));
}

TEST(BoostIntrusiveSafeListTest, ConstIterOverlayWorksOnConstList) {
  task t1(2), t2(4);
  task_list list;
  list.push_back(t1);
  list.push_back(t2);

  const task_list &const_list = list;
  int total = const_list.iter().map([](const task &t) { return t.priority; }).sum();
  EXPECT_EQ(total, 6);

  list.erase(list.iterator_to(t1));
  list.erase(list.iterator_to(t2));
}

TEST(BoostIntrusiveSafeListTest, ExtractIfOverlayRelinksBetweenTwoSafeLists) {
  task t1(1, true), t2(2, false), t3(3, true);
  task_list active;
  task_list blocked;
  active.push_back(t1);
  active.push_back(t2);
  active.push_back(t3);

  active.extract_if([](task &t) { return t.is_blocked; }).for_each([&](auto &&tx_obj) {
    auto &tx = tx_obj.as_known();
    tx.relink_to(blocked, reloco::boost_intrusive::push_back_inserter());
  });

  ASSERT_EQ(active.size(), 1u);
  EXPECT_EQ(&active.front(), &t2);
  ASSERT_EQ(blocked.size(), 2u);
  EXPECT_EQ(blocked.front().priority, 1);
  EXPECT_EQ(blocked.back().priority, 3);

  blocked.erase(blocked.iterator_to(t1));
  blocked.erase(blocked.iterator_to(t3));
  active.erase(active.iterator_to(t2));
}

TEST(BoostIntrusiveSafeListTest, ExtractIfOverlayDisposesFinishedTasks) {
  auto *finished = new task(1, false, /*ticks=*/1);
  auto *pending = new task(2, false, /*ticks=*/5);
  task_list queue;
  queue.push_back(*finished);
  queue.push_back(*pending);

  queue.extract_if([](task &t) { return t.tick(); }).for_each([&](auto &&tx_obj) {
    auto &tx = tx_obj.as_known();
    tx.release_to(reloco::boost_intrusive::default_delete_disposer<task>());
  });

  ASSERT_EQ(queue.size(), 1u);
  EXPECT_EQ(&queue.front(), pending);
  queue.erase(queue.iterator_to(*pending));
  delete pending;
}

TEST(BoostIntrusiveSafeListTest, MoveConstructionTransfersOwnershipOfLinkedNodes) {
  task t1(9), t2(10);
  task_list source;
  source.push_back(t1);
  source.push_back(t2);

  task_list moved(static_cast<task_list &&>(source));
  EXPECT_TRUE(source.empty());
  EXPECT_EQ(moved.size(), 2u);

  moved.erase(moved.iterator_to(t1));
  moved.erase(moved.iterator_to(t2));
}
