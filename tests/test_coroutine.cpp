// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/coroutine.hpp>
#include <reloco/heap_allocator.hpp>

#include <cstddef>

#if RELOCO_HAS_COROUTINES

namespace {

// Allocator that counts live/total allocations and can be told to fail.
struct counting_context {
  int allocs = 0;
  int frees = 0;
  bool fail = false;
  std::size_t last_bytes = 0;
};

struct counting_tag {};

} // namespace

template <> struct reloco::allocator_traits<counting_tag> {
  using context_type = counting_context;

  static reloco::result<reloco::mem_block> allocate(reloco::value_ref<context_type> ctx, std::size_t bytes,
                                                    std::size_t alignment) noexcept {
    if (ctx->fail)
      return reloco::unexpected(reloco::error::allocation_failed);
    ++ctx->allocs;
    ctx->last_bytes = bytes;
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return reloco::allocator<reloco::heap_allocator_tag>::ref().allocate(bytes, alignment);
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  static void deallocate(reloco::value_ref<context_type> ctx, void *ptr, std::size_t bytes) noexcept {
    ++ctx->frees;
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    reloco::allocator<reloco::heap_allocator_tag>::ref().deallocate(ptr, bytes);
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }
};

namespace {

using reloco::allocator_arg;
using reloco::allocator_arg_t;
using reloco::allocator_ref;
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

task<int> sum_with_allocator(allocator_arg_t, allocator_ref, int a, int b) { co_return a + b; }

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

task<int> await_sum(allocator_ref alloc) {
  auto inner = co_await sum_with_allocator(allocator_arg, alloc, 1, 1);
  int v = co_await std::move(inner);
  co_return v;
}

task<void> suspend_with_probe(allocator_arg_t, allocator_ref, manual_event *e, int *c) {
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
  task<int> add(allocator_arg_t, allocator_ref, int v) { co_return base + v; }
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
  counting_context ctx;
  allocator_ref alloc = allocator_ref(counting_tag{}, ctx);
  manual_event ev;
  int destroyed = 0;
  {
    auto t = suspend_with_probe(allocator_arg, alloc, &ev, &destroyed);
    t.resume();
    EXPECT_FALSE(t.done());
  }
  EXPECT_EQ(destroyed, 1);
  EXPECT_EQ(ctx.allocs, 1);
  EXPECT_EQ(ctx.frees, 1);
}

TEST_F(CoroutineTest, FrameUsesSuppliedAllocator) {
  counting_context ctx;
  allocator_ref alloc = allocator_ref(counting_tag{}, ctx);
  {
    auto t = sum_with_allocator(allocator_arg, alloc, 2, 3);
    EXPECT_EQ(ctx.allocs, 1);
    EXPECT_EQ(ctx.frees, 0);
    t.resume();
    auto r = t.take();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 5);
  }
  EXPECT_EQ(ctx.frees, 1);
}

TEST_F(CoroutineTest, MemberCoroutineUsesSuppliedAllocator) {
  counting_context ctx;
  allocator_ref alloc = allocator_ref(counting_tag{}, ctx);
  object obj;
  {
    auto t = obj.add(allocator_arg, alloc, 5);
    EXPECT_EQ(ctx.allocs, 1);
    t.resume();
    EXPECT_EQ(t.take().value_or(0), 105);
  }
  EXPECT_EQ(ctx.frees, 1);
}

TEST_F(CoroutineTest, AllocationFailureYieldsError) {
  counting_context ctx;
  ctx.fail = true;
  allocator_ref alloc = allocator_ref(counting_tag{}, ctx);

  auto t = sum_with_allocator(allocator_arg, alloc, 1, 1);
  EXPECT_TRUE(t.done());
  t.resume(); // no-op
  auto r = t.take();
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), error::allocation_failed);

  // Awaiting a failed task propagates the failure without crashing.
  auto parent = await_sum(alloc);
  parent.resume();
  auto pr = parent.take();
  ASSERT_FALSE(pr.has_value());
  EXPECT_EQ(pr.error(), error::allocation_failed);
}

TEST_F(CoroutineTest, DefaultAllocatorIsUsedWithoutAllocatorArg) {
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
