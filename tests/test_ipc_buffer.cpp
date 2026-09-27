#include "reloco/lifetime.hpp"
#include <gtest/gtest.h>
#include <reloco/reloco_ipc_ring.hpp>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

// =========================================================================
// TEST HELPERS
// =========================================================================

// Simulates the Host/Secure-World formatting the raw shared memory page
// before passing the pointer to the guest/REE.
void format_shared_page(void *memory, uint32_t capacity, uint32_t elem_size) {
  auto *page = static_cast<reloco_ipc_spsc_page *>(memory);
  page->magic = RELOCO_IPC_MAGIC;
  page->version = RELOCO_IPC_VERSION;
  page->untrusted_elem_size = elem_size;
  page->untrusted_capacity = capacity;
  page->write_idx = 0;
  page->read_idx = 0;
}

// A 4096-byte aligned buffer acts as our fake "mapped physical memory page"
struct SharedMemorySim {
  alignas(RELOCO_IPC_CACHE_LINE) std::array<uint8_t, 4096> memory{};

  void *data() { return memory.data(); }
  reloco_ipc_spsc_page *get_page() { return reinterpret_cast<reloco_ipc_spsc_page *>(memory.data()); }
};

struct TestEvent {
  uint32_t id;
  float value;

  bool operator==(const TestEvent &other) const { return id == other.id && value == other.value; }
};

} // namespace

// =========================================================================
// TEST CASES
// =========================================================================

TEST(IpcRingBufferTest, ValidationAndMounting) {
  SharedMemorySim sim;
  format_shared_page(sim.memory.data(), 8, sizeof(TestEvent));
  auto *page = sim.get_page();

  // 1. Valid Mount
  EXPECT_EQ(0, reloco_ipc_validate_mount(page, sizeof(TestEvent), 8));

  // 2. Element Size Mismatch (ABI breakage)
  EXPECT_NE(0, reloco_ipc_validate_mount(page, sizeof(uint32_t), 8));

  // 3. Magic Mismatch (Memory Corruption)
  page->magic = 0xDEADBEEF;
  EXPECT_NE(0, reloco_ipc_validate_mount(page, sizeof(TestEvent), 8));
  page->magic = RELOCO_IPC_MAGIC; // Restore

  // 4. Invalid Capacity (Not power of 2)
  page->untrusted_capacity = 7;
  EXPECT_NE(0, reloco_ipc_validate_mount(page, sizeof(TestEvent), 8));
}

TEST(IpcRingBufferTest, BasicProduceConsume) {
  SharedMemorySim sim;
  format_shared_page(sim.memory.data(), 16, sizeof(int));

  reloco_ipc_producer p;
  reloco_ipc_consumer c;
  ASSERT_EQ(0, reloco_ipc_producer_init(&p, sim.get_page(), sizeof(int), 16));
  ASSERT_EQ(0, reloco_ipc_consumer_init(&c, sim.get_page(), sizeof(int), 16));

  int in_data[] = {10, 20, 30};

  // Write 3 elements
  EXPECT_EQ(3, reloco_ipc_try_write(&p, in_data, 3));

  // Read 3 elements
  int out_data[3] = {0};
  EXPECT_EQ(3, reloco_ipc_try_read(&c, out_data, 3));

  EXPECT_EQ(10, out_data[0]);
  EXPECT_EQ(20, out_data[1]);
  EXPECT_EQ(30, out_data[2]);
}

TEST(IpcRingBufferTest, FullAndEmptyBoundaries) {
  SharedMemorySim sim;
  // Create a tiny queue (capacity 4)
  format_shared_page(sim.memory.data(), 4, sizeof(int));

  reloco_ipc_producer p;
  reloco_ipc_consumer c;
  ASSERT_EQ(0, reloco_ipc_producer_init(&p, sim.get_page(), sizeof(int), 4));
  ASSERT_EQ(0, reloco_ipc_consumer_init(&c, sim.get_page(), sizeof(int), 4));

  int dummy_read = 0;
  int dummy_write = 42;

  // 1. Reading from empty queue returns 0
  EXPECT_EQ(0, reloco_ipc_try_read(&c, &dummy_read, 1));

  // 2. Fill the queue exactly to capacity
  int fill_data[] = {1, 2, 3, 4};
  EXPECT_EQ(4, reloco_ipc_try_write(&p, fill_data, 4));

  // 3. Writing to full queue fails (returns 0)
  EXPECT_EQ(0, reloco_ipc_try_write(&p, &dummy_write, 1));

  // 4. Read everything out
  int out_data[4] = {0};
  EXPECT_EQ(4, reloco_ipc_try_read(&c, out_data, 4));
  EXPECT_EQ(1, out_data[0]);
  EXPECT_EQ(4, out_data[3]);

  // 5. Verify it is empty again
  EXPECT_EQ(0, reloco_ipc_try_read(&c, &dummy_read, 1));
}

TEST(IpcRingBufferTest, MemoryWrapAround) {
  SharedMemorySim sim;
  // Capacity 4. Indices mask will be 3.
  format_shared_page(sim.memory.data(), 4, sizeof(int));

  reloco_ipc_producer p;
  reloco_ipc_consumer c;
  ASSERT_EQ(0, reloco_ipc_producer_init(&p, sim.get_page(), sizeof(int), 4));
  ASSERT_EQ(0, reloco_ipc_consumer_init(&c, sim.get_page(), sizeof(int), 4));

  int stream_in[] = {101, 102, 103, 104, 105, 106};
  int stream_out[6] = {0};

  // Step 1: Write 3 elements. Physical indices used: 0, 1, 2. (Next write is 3)
  EXPECT_EQ(3, reloco_ipc_try_write(&p, &stream_in[0], 3));

  // Step 2: Read 3 elements. Physical indices read: 0, 1, 2.
  EXPECT_EQ(3, reloco_ipc_try_read(&c, &stream_out[0], 3));

  // Step 3: Write 2 elements.
  // This will WRAP the physical buffer!
  // It writes to index 3, and then wraps around to write to index 0.
  EXPECT_EQ(2, reloco_ipc_try_write(&p, &stream_in[3], 2));

  // Step 4: Read 2 elements (wrapping read).
  EXPECT_EQ(2, reloco_ipc_try_read(&c, &stream_out[3], 2));

  // Verify the output exactly matches the input stream
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(stream_in[i], stream_out[i]) << "Mismatch at stream index " << i;
  }
}

TEST(IpcRingBufferTest, ComplexStructPayload) {
  SharedMemorySim sim;
  format_shared_page(sim.memory.data(), 8, sizeof(TestEvent));

  reloco_ipc_producer p;
  reloco_ipc_consumer c;
  ASSERT_EQ(0, reloco_ipc_producer_init(&p, sim.get_page(), sizeof(TestEvent), 8));
  ASSERT_EQ(0, reloco_ipc_consumer_init(&c, sim.get_page(), sizeof(TestEvent), 8));

  TestEvent e1 = {1, 3.14f};
  TestEvent e2 = {2, 6.28f};

  // Write array of structs
  TestEvent in_arr[2] = {e1, e2};
  EXPECT_EQ(2, reloco_ipc_try_write(&p, in_arr, 2));

  // Read back array of structs
  TestEvent out_arr[2] = {};
  EXPECT_EQ(2, reloco_ipc_try_read(&c, out_arr, 2));

  EXPECT_EQ(e1, out_arr[0]);
  EXPECT_EQ(e2, out_arr[1]);
}

// =========================================================================
// NEGATIVE TESTS / ATTACK SIMULATIONS
// =========================================================================

TEST(IpcRingBufferAttackTest, RejectHugeCountRequests) {
  SharedMemorySim sim;
  format_shared_page(sim.memory.data(), 8, sizeof(int));

  reloco_ipc_producer p;
  reloco_ipc_consumer c;
  ASSERT_EQ(0, reloco_ipc_producer_init(&p, sim.get_page(), sizeof(int), 8));
  ASSERT_EQ(0, reloco_ipc_consumer_init(&c, sim.get_page(), sizeof(int), 8));

  int dummy = 42;

  // ATTACK 1: Try to write UINT32_MAX elements (Integer overflow attempt)
  // The queue should cleanly reject this without executing a massive memcpy.
  EXPECT_EQ(0, reloco_ipc_try_write(&p, &dummy, UINT32_MAX));

  // ATTACK 2: Try to read UINT32_MAX elements
  EXPECT_EQ(0, reloco_ipc_try_read(&c, &dummy, UINT32_MAX));
}

TEST(IpcRingBufferAttackTest, MaliciousConsumerSpoofsReadIndex) {
  SharedMemorySim sim;
  format_shared_page(sim.memory.data(), 8, sizeof(int));
  auto *page = sim.get_page();

  reloco_ipc_producer p;
  ASSERT_EQ(0, reloco_ipc_producer_init(&p, page, sizeof(int), 8));

  int data = 42;
  EXPECT_EQ(1, reloco_ipc_try_write(&p, &data, 1));

  // ATTACK: The Consumer maliciously advances the read_idx.
  page->read_idx = 500;

  // The producer still has 7 slots in its local cache.
  // It will safely write them without checking the shared memory.
  int array[7] = {0};
  EXPECT_EQ(7, reloco_ipc_try_write(&p, array, 7));

  // NOW the local cache is exhausted (w = 8, cached_r = 0).
  // The next write forces a bus read (page->read_idx), loading the malicious 500.
  // Mathematical underflow triggers (8 - 500 = huge number > capacity).

  // RESULT: The trap snaps shut and returns 0.
  EXPECT_EQ(0, reloco_ipc_try_write(&p, &data, 1));
}

TEST(IpcRingBufferAttackTest, MaliciousProducerSpoofsWriteIndex_WayAhead) {
  SharedMemorySim sim;
  format_shared_page(sim.memory.data(), 8, sizeof(int));
  auto *page = sim.get_page();

  reloco_ipc_consumer c;
  ASSERT_EQ(0, reloco_ipc_consumer_init(&c, page, sizeof(int), 8));

  // ATTACK: The Producer maliciously advances the write_idx by 3x the capacity.
  // Without security checks, `available` would be 24, causing the Consumer
  // to memcpy 24 elements out of an 8-element payload array (Kernel Memory Leak).
  page->write_idx = 24;

  int out_data[24] = {0};

  // RESULT: The Consumer detects available > capacity and halts.
  EXPECT_EQ(0, reloco_ipc_try_read(&c, out_data, 24));
}

TEST(IpcRingBufferAttackTest, MaliciousProducerSpoofsWriteIndex_Backwards) {
  SharedMemorySim sim;
  format_shared_page(sim.memory.data(), 8, sizeof(int));
  auto *page = sim.get_page();

  reloco_ipc_consumer c;
  ASSERT_EQ(0, reloco_ipc_consumer_init(&c, page, sizeof(int), 8));

  // Set normal state: 4 elements written, 4 elements read
  page->write_idx = 4;
  page->read_idx = 4;

  // Synchronize the consumer's local cache
  int dummy = 0;
  reloco_ipc_try_read(&c, &dummy, 1);

  // ATTACK: The Producer moves the write_idx BACKWARDS (e.g., Integer Underflow).
  // w (3) - r (4) = 0xFFFFFFFFFFFFFFFF.
  page->write_idx = 3;

  // RESULT: Unsigned arithmetic evaluates w - r as huge, which triggers
  // the available > capacity security boundary.
  EXPECT_EQ(0, reloco_ipc_try_read(&c, &dummy, 1));
}

TEST(IpcRingBufferAttackTest, MountVersionDowngradeAttack) {
  SharedMemorySim sim;
  format_shared_page(sim.memory.data(), 8, sizeof(int));
  auto *page = sim.get_page();

  // ATTACK: The Host attempts to pass an older/unsupported version
  // of the struct to exploit a known legacy vulnerability.
  page->version = RELOCO_IPC_VERSION - 1;

  reloco_ipc_consumer c;

  // RESULT: The kernel strictly refuses to mount the memory.
  EXPECT_NE(0, reloco_ipc_consumer_init(&c, page, sizeof(int), 8));
}

TEST(IpcRingBufferCppTest, FatalMountValidation) {
  SharedMemorySim sim;
  format_shared_page(sim.data(), 16, sizeof(int));

  // Success case: The OS mapping exactly matches the trusted inputs
  EXPECT_NO_FATAL_FAILURE({
    reloco::ipc_producer<int> p(sim.data(), 16);
    reloco::ipc_consumer<int> c(sim.data(), 16);
  });

  // Fatal Failure: Expected capacity does not match untrusted_capacity
  EXPECT_DEATH({ reloco::ipc_producer<int> p(sim.data(), 32); }, "");

  // Fatal Failure: ABI element size mismatch
  EXPECT_DEATH(
      {
        // The page was formatted with sizeof(int), but we mount it as double
        reloco::ipc_consumer<double> c(sim.data(), 16);
      },
      "");
}

TEST(IpcRingBufferCppTest, BasicWriteAndRead) {
  SharedMemorySim sim;
  format_shared_page(sim.data(), 8, sizeof(int));
  reloco::ipc_producer<int> p(sim.data(), 8);
  reloco::ipc_consumer<int> c(sim.data(), 8);

  std::array<int, 3> in_data = {10, 20, 30};

  // try_write using reloco::span
  EXPECT_EQ(3, p.try_write(reloco::span<const int>(in_data.data(), in_data.size())));

  std::array<int, 5> out_data = {0};

  // try_read using reloco::span
  EXPECT_EQ(3, c.try_read(reloco::span<int>(out_data.data(), out_data.size())));

  EXPECT_EQ(10, out_data[0]);
  EXPECT_EQ(20, out_data[1]);
  EXPECT_EQ(30, out_data[2]);
  EXPECT_EQ(0, out_data[3]); // Unmodified
}

TEST(IpcRingBufferCppTest, ZeroCopyTransactions_Linear) {
  SharedMemorySim sim;
  format_shared_page(sim.data(), 8, sizeof(int));
  reloco::ipc_producer<int> p(sim.data(), 8);
  reloco::ipc_consumer<int> c(sim.data(), 8);

  // ---- PRODUCER: Write directly into shared memory ----
  {
    auto tx = p.begin_write(3);
    ASSERT_TRUE(static_cast<bool>(tx));

    // Since the queue is empty, chunk1 should have all 8 contiguous slots.
    EXPECT_EQ(8, tx.chunk1().size());
    EXPECT_EQ(0, tx.chunk2().size());

    auto chunk1 = tx.chunk1();
    chunk1[0] = 100;
    chunk1[1] = 200;
    chunk1[2] = 300;

    // Commits only the 3 elements we actually populated
    tx.commit(3);
  }

  // ---- CONSUMER: Read directly from shared memory ----
  {
    auto tx = c.begin_read(2);
    ASSERT_TRUE(static_cast<bool>(tx));

    auto chunk1 = tx.chunk1();
    EXPECT_EQ(3, chunk1.size()); // 3 elements available linearly

    EXPECT_EQ(100, chunk1[0]);
    EXPECT_EQ(200, chunk1[1]);

    // Consume only 2 elements, leaving the 3rd in the queue
    tx.consume(2);
  }
}

TEST(IpcRingBufferCppTest, ZeroCopyTransactions_WrapAroundSplit) {
  SharedMemorySim sim;
  format_shared_page(sim.data(), 4, sizeof(int));
  reloco::ipc_producer<int> p(sim.data(), 4);
  reloco::ipc_consumer<int> c(sim.data(), 4);

  // Step 1: Advance indices to near the end of the physical buffer.
  // write_idx = 3, read_idx = 3.
  int dummy[3] = {1, 2, 3};
  ASSERT_EQ(p.try_write(reloco::span<const int>(dummy, 3)), 3);
  ASSERT_EQ(c.try_read(reloco::span<int>(dummy, 3)), 3);

  // Step 2: The producer requests 2 slots.
  // Because physical index is 3 and capacity is 4, it MUST split.
  {
    auto tx = p.begin_write(2);
    ASSERT_TRUE(static_cast<bool>(tx));

    auto chunk1 = tx.chunk1();
    auto chunk2 = tx.chunk2();

    // Chunk1 hits the end of the buffer (index 3). Size = 1.
    ASSERT_EQ(1, chunk1.size());
    // Chunk2 wraps around to the beginning (index 0). Size = 3 (remaining free).
    ASSERT_EQ(3, chunk2.size());

    chunk1[0] = 88;
    chunk2[0] = 99; // Writing across the boundary!

    tx.commit(2);
  }

  // Step 3: The consumer reads 2 slots.
  // It should receive the exact same split chunks.
  {
    auto tx = c.begin_read(2);
    ASSERT_TRUE(static_cast<bool>(tx));

    auto chunk1 = tx.chunk1();
    auto chunk2 = tx.chunk2();

    ASSERT_EQ(1, chunk1.size());
    ASSERT_EQ(1, chunk2.size());

    EXPECT_EQ(88, chunk1[0]);
    EXPECT_EQ(99, chunk2[0]);

    tx.consume(2);
  }
}

TEST(IpcRingBufferCppTest, TransactionMoveSemantics) {
  SharedMemorySim sim;
  format_shared_page(sim.data(), 8, sizeof(int));
  reloco::ipc_producer<int> p(sim.data(), 8);

  // We start a transaction
  auto tx1 = p.begin_write(1);
  ASSERT_TRUE(static_cast<bool>(tx1));

  // Move it to a new owner
  auto tx2 = std::move(tx1);

  // The old transaction should be safely gutted (implicitly consumed state via Typestate)
  EXPECT_FALSE(static_cast<bool>(tx1));

  // The new transaction should hold the spans and commit perfectly
  ASSERT_TRUE(static_cast<bool>(tx2));

  auto chunk1 = tx2.chunk1();
  chunk1[0] = 42;
  tx2.commit(1);
}

TEST(IpcRingBufferCppTest, ReadTxDropsWhenQueueEmpty) {
  SharedMemorySim sim;
  format_shared_page(sim.data(), 8, sizeof(int));
  reloco::ipc_consumer<int> c(sim.data(), 8);

  // Queue is completely empty. Requesting 1 element should return a false tx.
  auto tx = c.begin_read(1);
  EXPECT_FALSE(static_cast<bool>(tx));
  EXPECT_TRUE(tx.chunk1().empty());
}

RELOCO_END_UNSAFE_BUFFER_USAGE
