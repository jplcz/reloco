#include "reloco/epoch.hpp" // Your header
#include <gtest/gtest.h>

namespace {

// ============================================================================
// Helper Mock: A dummy RTOS object that resides in a memory pool
// ============================================================================
struct RtosTask : public reloco::epoch_trackable {
  int id;
  int priority;

  // Normal constructor
  explicit RtosTask(const int task_id) : reloco::epoch_trackable(1), id(task_id), priority(0) {}

  // Placement-new recycler constructor (preserves the previous epoch)
  RtosTask(const int task_id, const reloco::epoch_t preserved_epoch)
      : reloco::epoch_trackable(preserved_epoch), id(task_id), priority(0) {}
};

} // anonymous namespace

// ============================================================================
// Runtime Behavioral Tests
// ============================================================================

TEST(EpochTest, InitialStateAndBump) {
  RtosTask task(42);

  // Default initial epoch should be 1
  EXPECT_EQ(task.current_epoch(), 1);

  task.bump_epoch();
  EXPECT_EQ(task.current_epoch(), 2);

  task.bump_epoch();
  EXPECT_EQ(task.current_epoch(), 3);
}

TEST(EpochTest, SuccessfulLock) {
  RtosTask task(100);
  task.priority = 5;

  // Create a safe handle targeting generation 1
  const reloco::epoch_handle<RtosTask> handle(task);
  EXPECT_FALSE(handle.is_empty());

  // Lock the handle
  auto guard = handle.lock();

  // The memory has not been recycled, so it must be alive
  EXPECT_TRUE(guard.is_alive());

  if (guard.is_alive()) {
    EXPECT_EQ(guard.get().id, 100);
    EXPECT_EQ(guard->priority, 5); // Test arrow operator syntactic sugar
  }
}

TEST(EpochTest, StaleLockPreventionABA) {
  RtosTask task(999);

  // Snapshot the handle (Epoch is 1)
  const reloco::epoch_handle<RtosTask> handle(task);

  // Simulate the memory pool recycling the object
  // In a real system, uninit<T> would destroy the old task and placement-new
  // a fresh one, incrementing the epoch.
  task.bump_epoch(); // Epoch is now 2

  // Attempt to lock the old handle
  const auto guard = handle.lock();

  // The handle MUST be rejected because the generation changed!
  EXPECT_FALSE(guard.is_alive());
}

TEST(EpochTest, WrapAroundSkipsZero) {
  // Force the task to the absolute maximum 32-bit integer limit
  RtosTask task(1, 0xFFFFFFFF);
  EXPECT_EQ(task.current_epoch(), 0xFFFFFFFF);

  // Bump it. It should overflow to 0, but the CAS loop must catch it and set it to 1.
  task.bump_epoch();

  EXPECT_EQ(task.current_epoch(), 1);
}

TEST(EpochTest, EmptyHandleResolution) {
  constexpr reloco::epoch_handle<RtosTask> empty;
  EXPECT_TRUE(empty.is_empty());

  const auto guard = empty.lock();
  EXPECT_FALSE(guard.is_alive());
}

TEST(EpochTest, HandleReset) {
  RtosTask task(7);
  reloco::epoch_handle<RtosTask> handle(task);

  EXPECT_FALSE(handle.is_empty());
  handle.reset();
  EXPECT_TRUE(handle.is_empty());

  const auto guard = handle.lock();
  EXPECT_FALSE(guard.is_alive());
}

// ============================================================================
// Compile-Time Typestate & Lifetime Tests (Negative Testing)
// ============================================================================
/*
#ifdef RELOCO_TEST_COMPILATION_FAILURES

void test_epoch_typestate_enforcement() {
    RtosTask task(1);
    reloco::epoch_handle<RtosTask> handle(task);
    auto guard = handle.lock();

    // 1. FORGOTTEN CHECK (Caught by RELOCO_CALLABLE_WHEN(verified))
    // ❌ ERROR: invalid invocation of method 'get' on object 'guard'
    // while it is in the 'unverified' state.
    int id = guard.get().id;

    // 2. INCORRECT BRANCH (Caught by test_typestate(verified))
    if (!guard.is_alive()) {
        // ❌ ERROR: guard is still 'unverified' in the false branch!
        guard.get().id = 5;
    }

    // 3. DANGLING REFERENCE ESCAPE (Caught by RELOCO_LIFETIMEBOUND)
    // ❌ WARNING: returning reference to local temporary object
    RtosTask& escaped_ref = [&]() -> RtosTask& {
        auto local_guard = handle.lock();
        if (local_guard.is_alive()) {
            return local_guard.get(); // Cannot escape the guard's lifetime!
        }
        static RtosTask fallback(0);
        return fallback;
    }();
}

#endif
*/