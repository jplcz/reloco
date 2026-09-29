// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/scope_guard.hpp>

#include <functional>
#include <utility>

TEST(ScopeGuardTest, RunsOnScopeExitUnlessCanceled) {
  int calls = 0;
  {
    reloco::scope_guard guard([&] { ++calls; });
  }
  EXPECT_EQ(calls, 1);

  {
    reloco::scope_guard guard([&] { ++calls; });
    guard.cancel();
  }
  EXPECT_EQ(calls, 1);
}

TEST(ScopeGuardTest, DeferMacroRunsAtEndOfScope) {
  int calls = 0;
  {
    RELOCO_DEFER([&] { ++calls; });
    EXPECT_EQ(calls, 0);
  }
  EXPECT_EQ(calls, 1);
}

TEST(ScopeGuardTest, MoveConstructionTransfersResponsibility) {
  int calls = 0;
  {
    reloco::scope_guard source([&] { ++calls; });
    auto destination = std::move(source);
    EXPECT_EQ(calls, 0);
  }
  EXPECT_EQ(calls, 1);
}

TEST(ScopeGuardTest, MoveAssignmentTransfersResponsibility) {
  int source_calls = 0;
  int destination_calls = 0;
  using action = std::function<void()>;
  {
    reloco::scope_guard source(action([&] { ++source_calls; }));
    reloco::scope_guard destination(action([&] { ++destination_calls; }));
    destination.cancel();
    destination = std::move(source);
  }

  EXPECT_EQ(source_calls, 1);
  EXPECT_EQ(destination_calls, 0);
}
