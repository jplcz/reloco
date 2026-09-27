#include <gtest/gtest.h>
#include <reloco/uninit.hpp>
#include <string>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

// ============================================================================
// Helper Mock: Tracks exact constructor and destructor calls to prove
// reloco::uninit doesn't leak memory or double-free.
// ============================================================================
struct LifetimeTracker {
  static int constructions;
  static int destructions;
  int payload;

  explicit LifetimeTracker(int val) : payload(val) { constructions++; }

  // Track moves
  LifetimeTracker(LifetimeTracker &&other) noexcept : payload(other.payload) {
    other.payload = 0;
    constructions++;
  }

  // Banned copy to ensure we are strictly moving
  LifetimeTracker(const LifetimeTracker &) = delete;
  LifetimeTracker &operator=(const LifetimeTracker &) = delete;

  ~LifetimeTracker() { destructions++; }

  static void reset() {
    constructions = 0;
    destructions = 0;
  }
};

int LifetimeTracker::constructions = 0;
int LifetimeTracker::destructions = 0;

} // anonymous namespace

// ============================================================================
// Runtime Behavioral Tests
// ============================================================================

TEST(UninitTest, PrimitiveWriteAndRead) {
  reloco::uninit<int> u; // NOLINT(*-pro-type-member-init)

  // Write transitions state to 'unconsumed'
  std::ignore = u.write(42);

  EXPECT_EQ(u.get(), 42);

  // Mutate
  u.get_mut() = 100;
  EXPECT_EQ(u.get(), 100);

  // Must destroy to return to 'consumed' state before scope ends
  u.destroy();
}

TEST(UninitTest, ObjectLifetimeIsStrictlyManaged) {
  LifetimeTracker::reset();

  {
    reloco::uninit<LifetimeTracker> tracker; // NOLINT(*-pro-type-member-init)

    // Memory is allocated, but constructor NOT called yet.
    EXPECT_EQ(LifetimeTracker::constructions, 0);

    // In-place construction
    std::ignore = tracker.write(99);
    EXPECT_EQ(LifetimeTracker::constructions, 1);
    EXPECT_EQ(tracker.get().payload, 99);

    // Manual destruction (required by uninit)
    tracker.destroy();
    EXPECT_EQ(LifetimeTracker::destructions, 1);
  }

  // Ensure the default destructor of uninit didn't accidentally double-free
  EXPECT_EQ(LifetimeTracker::destructions, 1);
}

TEST(UninitTest, ExtractMovesValueAndDestroys) {
  LifetimeTracker::reset();

  reloco::uninit<LifetimeTracker> u; // NOLINT(*-pro-type-member-init)
  std::ignore = u.write(77);

  // Extract should physically move the value out and call destroy() internally
  LifetimeTracker extracted = u.extract();

  EXPECT_EQ(extracted.payload, 77);
  EXPECT_EQ(LifetimeTracker::constructions, 2); // 1 original + 1 move
  EXPECT_EQ(LifetimeTracker::destructions, 1);  // The one inside uninit was destroyed by extract()
}

TEST(UninitTest, AssumeInitForHardwareDMA) {
  reloco::uninit<uint32_t> dma_buffer; // NOLINT(*-pro-type-member-init)

  // Simulate hardware filling the buffer asynchronously (e.g., memcpy or DMA)
  // We bypass the class entirely for the write.
  auto *raw_ptr = reinterpret_cast<uint32_t *>(&dma_buffer);
  *raw_ptr = 0xDEADBEEF;

  // Tell the typestate analyzer: "Trust me, the hardware initialized this."
  const uint32_t *safe_ptr = dma_buffer.assume_init();

  EXPECT_EQ(*safe_ptr, 0xDEADBEEF);
  EXPECT_EQ(dma_buffer.get(), 0xDEADBEEF);

  dma_buffer.destroy();
}

// ============================================================================
// Compile-Time Typestate Tests (Negative Testing)
// ============================================================================
// #define RELOCO_TEST_COMPILATION_FAILURES
#ifdef RELOCO_TEST_COMPILATION_FAILURES

void test_typestate_enforcement() {

  reloco::uninit<int> u;

  // READ BEFORE WRITE (Caught by RELOCO_CALLABLE_WHEN(unconsumed))
  // error: invalid invocation of method 'get' on object 'u' while it is in the 'consumed' state
  [[maybe_unused]] int x = u.get();

  // DOUBLE INITIALIZATION (Caught by RELOCO_CALLABLE_WHEN(consumed))
  [[maybe_unused]] auto &u1 = u.write(1);
  // error: invalid invocation of method 'write' on object 'u' while it is in the 'unconsumed' state
  [[maybe_unused]] auto &u2 = u.write(2);

  // DANGLING REFERENCE (Caught by RELOCO_LIFETIMEBOUND)
  // warning: returning reference to local temporary object
  [[maybe_unused]] const int &dangling = []() -> const int & {
    reloco::uninit<int> temp;
    std::ignore = temp.write(5);
    return temp.get();
  }();

  u.destroy();
}

#endif

RELOCO_END_UNSAFE_BUFFER_USAGE
