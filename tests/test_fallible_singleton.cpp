#include <gtest/gtest.h>
#include <mutex>
#include <reloco/fallible_singleton.hpp>
#include <reloco/mutex.hpp>
#include <thread>
#include <vector>

namespace {

class SpyResource {
public:
  using reloco_fallible_t = void;
  static inline int constructor_calls = 0;
  static inline int init_calls = 0;

  SpyResource(reloco::detail::constructor_key<SpyResource>) {
    constructor_calls++;
  }

  reloco::result<void> try_init(reloco::detail::constructor_key<SpyResource>) {
    init_calls++;
    return {};
  }

  void do_work() { value++; }
  int value = 0;

  static void reset() {
    constructor_calls = 0;
    init_calls = 0;
  }
};

using SimpleSingleton = reloco::fallible_singleton<SpyResource>;

struct InternalMutexTrait {
  using lock_type = reloco::mutex;
  static inline lock_type mtx;

  static void lock(lock_type &l) { l.lock(); }
  static void unlock(lock_type &l) { l.unlock(); }
};

} // namespace

TEST(SingletonTest, LazyInitialization) {
  SpyResource::reset();

  // Before calling instance, nothing should be initialized
  EXPECT_EQ(SpyResource::constructor_calls, 0);

  // First call triggers init
  auto res1 = SimpleSingleton::instance();
  ASSERT_TRUE(res1.has_value());
  EXPECT_EQ(SpyResource::constructor_calls, 1);
  EXPECT_EQ(SpyResource::init_calls, 1);

  // Second call returns the same pointer without re-initializing
  auto res2 = SimpleSingleton::instance();
  EXPECT_EQ(*res1, *res2);
  EXPECT_EQ(SpyResource::init_calls, 1);
}

using TestSingleton =
    reloco::atomic_fallible_singleton<SpyResource, InternalMutexTrait>;

TEST(AtomicSingletonTest, ConcurrentInitialization) {
  SpyResource::reset();
  const int thread_count = 32;
  std::vector<std::thread> threads;
  std::vector<SpyResource *> results(thread_count, nullptr);

  // Launch threads to race for initialization
  for (int i = 0; i < thread_count; ++i) {
    threads.emplace_back([&results, i]() {
      auto res = TestSingleton::instance(InternalMutexTrait::mtx);
      if (res)
        results[i] = *res;
    });
  }

  for (auto &t : threads)
    t.join();

  // Verification
  ASSERT_NE(results[0], nullptr);
  for (auto *ptr : results) {
    EXPECT_EQ(ptr, results[0])
        << "Threads observed different memory addresses!";
  }

  EXPECT_EQ(SpyResource::constructor_calls, 1)
      << "Constructor called multiple times!";
  EXPECT_EQ(SpyResource::init_calls, 1) << "try_init called multiple times!";
}

struct PoisonedTrait : public InternalMutexTrait {
  static inline bool initialized = false;
  static void lock(lock_type &l) {
    if (initialized) {
      ADD_FAILURE()
          << "Slow path taken after singleton was already initialized!";
    }
    InternalMutexTrait::lock(l);
  }
};

TEST(AtomicSingletonTest, FastPathDoesNotLock) {
  using FastSingleton =
      reloco::atomic_fallible_singleton<SpyResource, PoisonedTrait>;

  auto res1 = FastSingleton::instance(PoisonedTrait::mtx);
  ASSERT_TRUE(res1.has_value());
  PoisonedTrait::initialized = true;

  for (int i = 0; i < 100; ++i) {
    auto res2 = FastSingleton::instance(PoisonedTrait::mtx);
    EXPECT_EQ(*res1, *res2);
  }
}
