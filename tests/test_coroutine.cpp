// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/coroutine.hpp>

#include <cstddef>

#if RELOCO_HAS_COROUTINES

namespace {

using reloco::error;
using reloco::result;
using reloco::task;
using reloco::unexpected;

// Manually completed event: awaiting it suspends until `fire()`.
struct manual_event {
  std::coroutine_handle<> waiter{};
  bool fired = false;

  bool await_ready() const noexcept { return fired; }
  void await_suspend(std::coroutine_handle<> h) noexcept { waiter = h; }
  void await_resume() const noexcept {}
  void fire() {
    fired = true;
    if (waiter)
      std::exchange(waiter, {}).resume();
  }
};

struct destructor_probe {
  int *counter;
  explicit destructor_probe(int *c) noexcept : counter(c) {}
  destructor_probe(const destructor_probe &) = delete;
  ~destructor_probe() { ++*counter; }
};

task<int> answer() { co_return 42; }
task<void> nothing() { co_return; }

result<int> parse(int v) {
  if (v < 0)
    return unexpected(error::invalid_argument);
  return v * 2;
}

task<int> double_it(int v) {
  int r = co_await parse(v);
  co_return r + 1;
}

task<int> chain(int v) {
  auto inner = co_await double_it(v);
  int x = co_await std::move(inner);
  co_return x * 10;
}

task<int> sum(int a, int b) { co_return a + b; }

task<int> direct_error() {
  co_return unexpected(error::busy);
}

task<void> void_fail() {
  co_await unexpected(error::io_error);
  co_return;
}

task<int> wait_then(manual_event &ev, int v) {
  co_await ev;
  co_return v;
}

// Coroutines are free functions, not lambdas: a temporary capturing lambda
// would be destroyed while its coroutine frame still refers to it.
task<int> wait_then_plus_one(manual_event &ev) {
  auto inner = co_await wait_then(ev, 7);
  int v = co_await std::move(inner);
  co_return v + 1;
}

task<void> suspend_with_probe(manual_event *e, int *c) {
  destructor_probe p(c);
  co_await *e;
}

task<int> set_flag_then_one(int *flag) {
  *flag = 1;
  co_return 1;
}

task<int> cleanup_on_error(int *counter) {
  destructor_probe p(counter);
  int r = co_await parse(-1);
  co_return r;
}

struct object {
  int base = 100;
  task<int> add(int v) { co_return base + v; }
};

class CoroutineTest : public ::testing::Test {};

TEST_F(CoroutineTest, LazyUntilResumed) {
  int ran = 0;
  auto t = set_flag_then_one(&ran);
  EXPECT_EQ(ran, 0);
  EXPECT_FALSE(t.done());
  t.resume();
  EXPECT_EQ(ran, 1);
  EXPECT_TRUE(t.done());
  auto r = t.take();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value(), 1);
}

TEST_F(CoroutineTest, ReturnsValueAndVoid) {
  auto t = answer();
  t.resume();
  auto r = t.take();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value(), 42);

  auto v = nothing();
  v.resume();
  EXPECT_TRUE(v.take().has_value());
}

TEST_F(CoroutineTest, TakeBeforeDoneIsInvalidState) {
  auto t = answer();
  auto r = t.take();
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), error::invalid_state);
}

TEST_F(CoroutineTest, AwaitedResultPropagatesErrorToParent) {
  auto ok = chain(4);
  ok.resume();
  auto r = ok.take();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value(), 90);

  auto bad = chain(-1);
  bad.resume();
  ASSERT_TRUE(bad.done());
  auto e = bad.take();
  ASSERT_FALSE(e.has_value());
  EXPECT_EQ(e.error(), error::invalid_argument);
}

TEST_F(CoroutineTest, CoReturnUnexpected) {
  auto t = direct_error();
  t.resume();
  auto r = t.take();
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), error::busy);
}

TEST_F(CoroutineTest, AwaitUnexpectedInVoidTask) {
  auto t = void_fail();
  t.resume();
  auto r = t.take();
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), error::io_error);
}

TEST_F(CoroutineTest, ErrorPathDestroysLocals) {
  int destroyed = 0;
  {
    auto t = cleanup_on_error(&destroyed);
    t.resume();
    EXPECT_TRUE(t.done());
    EXPECT_EQ(destroyed, 0); // frame still alive until the task is dropped
    auto r = t.take();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::invalid_argument);
  }
  EXPECT_EQ(destroyed, 1);
}

TEST_F(CoroutineTest, SuspendsOnExternalAwaitableAndResumes) {
  manual_event ev;
  auto outer = wait_then_plus_one(ev);
  outer.resume();
  EXPECT_FALSE(outer.done());
  ev.fire();
  ASSERT_TRUE(outer.done());
  auto r = outer.take();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value(), 8);
}

TEST_F(CoroutineTest, DroppingSuspendedTaskDestroysFrame) {
  manual_event ev;
  int destroyed = 0;
  {
    auto t = suspend_with_probe(&ev, &destroyed);
    t.resume();
    EXPECT_FALSE(t.done());
  }
  EXPECT_EQ(destroyed, 1);
}

TEST_F(CoroutineTest, CompletesWithValue) {
  auto t = sum(2, 3);
  t.resume();
  auto r = t.take();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value(), 5);
}

TEST_F(CoroutineTest, MemberCoroutineWorks) {
  object obj;
  auto t = obj.add(5);
  t.resume();
  EXPECT_EQ(t.take().value_or(0), 105);
}

TEST_F(CoroutineTest, FrameComesFromDefaultAllocator) {
  auto t = answer();
  t.resume();
  EXPECT_EQ(t.take().value_or(0), 42);
}

TEST_F(CoroutineTest, MoveTransfersOwnership) {
  auto a = answer();
  auto b = std::move(a);
  b.resume();
  EXPECT_EQ(b.take().value_or(0), 42);
}

} // namespace

#else

TEST(CoroutineTest, Unavailable) { GTEST_SKIP() << "coroutines require C++20"; }

#endif
