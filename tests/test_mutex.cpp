#include <gtest/gtest.h>
#include <reloco/mutex.hpp>

class MutexTest : public ::testing::Test {};

TEST_F(MutexTest, BasicLockUnlock) {
  reloco::mutex m;
  auto res = m.lock();
  EXPECT_TRUE(res.has_value());

  EXPECT_FALSE(m.try_lock()); // Should fail, already locked

  res = m.unlock();
  EXPECT_TRUE(res.has_value());
  EXPECT_TRUE(m.try_lock()); // Should succeed now
  std::ignore = m.unlock();
}

TEST_F(MutexTest, ErrorCheckingDeadlock) {
  reloco::error_checking_mutex m;
  std::ignore = m.lock();

  auto res = m.lock(); // Double lock on error-checking mutex
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), reloco::error::deadlock);

  std::ignore = m.unlock();
}

TEST_F(MutexTest, RecursiveLocking) {
  reloco::recursive_mutex m;
  EXPECT_TRUE(m.lock().has_value());
  EXPECT_TRUE(m.lock().has_value()); // Should succeed

  EXPECT_TRUE(m.unlock().has_value());
  EXPECT_TRUE(m.unlock().has_value());
}

TEST_F(MutexTest, SharedMutexMultipleReaders) {
  reloco::shared_mutex sm;

  auto res1 = sm.lock_shared();
  auto res2 = sm.lock_shared();

  EXPECT_TRUE(res1.has_value());
  EXPECT_TRUE(res2.has_value()); // Both should hold read locks

  EXPECT_FALSE(sm.try_lock()); // Writer should fail

  std::ignore = sm.unlock_shared();
  std::ignore = sm.unlock_shared();
}

TEST_F(MutexTest, SharedMutexWriterExclusion) {
  reloco::shared_mutex sm;
  std::ignore = sm.lock(); // Exclusive lock

  EXPECT_FALSE(sm.try_lock_shared());
  EXPECT_FALSE(sm.try_lock());

  std::ignore = sm.unlock();
}

TEST_F(MutexTest, ConditionVariableNotify) {
  reloco::mutex m;
  reloco::condition_variable cv;
  bool ready = false;
  bool processed = false;

  std::thread worker([&]() {
    std::unique_lock<reloco::mutex> lk(m);
    std::ignore = cv.wait(lk, [&] { return ready; }); // Wait for main thread
    processed = true;
    lk.unlock();
    cv.notify_one();
  });

  {
    std::lock_guard<reloco::mutex> lk(m);
    ready = true;
  }
  cv.notify_one();

  worker.join();
  EXPECT_TRUE(processed);
}
