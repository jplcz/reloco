#include <gtest/gtest.h>
#include <reloco/atomic_ring_buffer.hpp>
#include <reloco/string_view.hpp>
#include <thread>

using namespace reloco;

// =========================================================================
// SMOKE TESTS (Storage Policies & Capacity Math)
// =========================================================================

TEST(SpscSmokeTest, HeapBufferRoundsUpCapacity) {
  heap_spsc_ring_buffer<char> buf;

  // Initialize with 10. Should round UP to nearest power of 2 (16).
  ASSERT_TRUE(buf.try_initialize(10).has_value());
  EXPECT_EQ(buf.capacity(), 16);

  ASSERT_TRUE(buf.try_push('A').has_value());
  auto val = buf.try_pop();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 'A');
}

TEST(SpscSmokeTest, InlineBufferStrictCapacity) {
  // Compile-time guaranteed capacity of 16.
  inline_spsc_ring_buffer<char, 16> buf;
  EXPECT_EQ(buf.capacity(), 16);

  ASSERT_TRUE(buf.try_push('B').has_value());
  EXPECT_EQ(buf.try_pop().unwrap(), 'B');
}

TEST(SpscSmokeTest, OutlineBufferRoundsDownCapacity) {
  // 5 uint32_t elements = 20 bytes total.
  std::array<uint32_t, 5> external_mem{};

  // Wrapping 20 bytes. Must round DOWN to nearest power of 2 to guarantee
  // memory safety with the bitwise-AND masking. Should become 16.
  outline_spsc_ring_buffer<char> buf{span<uint32_t>(external_mem)};

  EXPECT_EQ(buf.capacity(), 16);

  ASSERT_TRUE(buf.try_push('C').has_value());
  EXPECT_EQ(buf.try_pop().unwrap(), 'C');
}

// =========================================================================
// CORE UNIT TESTS (Algorithmic Logic using Inline Buffer)
// =========================================================================

class SpscRingBufferTest : public ::testing::Test {
protected:
  inline_spsc_ring_buffer<char, 16> buf;
};

TEST_F(SpscRingBufferTest, SingleElementPushPop) {
  EXPECT_FALSE(buf.try_pop().has_value()); // Empty

  for (int i = 0; i < 16; ++i) {
    ASSERT_TRUE(buf.try_push(static_cast<char>('a' + i)).has_value());
  }

  // Buffer is full (capacity 16)
  EXPECT_FALSE(buf.try_push('z').has_value());

  for (int i = 0; i < 16; ++i) {
    auto val = buf.try_pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, static_cast<char>('a' + i));
  }

  EXPECT_FALSE(buf.try_pop().has_value()); // Empty again
}

TEST_F(SpscRingBufferTest, BulkWrapAroundMechanics) {
  std::string_view p1 = "1234567890"; // 10 bytes
  ASSERT_TRUE(buf.try_write(span<const char>(p1.data(), p1.size())).has_value());

  std::array<char, 8> out{};
  EXPECT_EQ(buf.read(span<char>(out.data(), 8)), 8);
  EXPECT_EQ(reloco::string_view(out.data(), 8), "12345678");

  // Head is at 8, 2 bytes remaining. Free space is 14.
  // Writing 10 more bytes will physically wrap around the array!
  std::string_view p2 = "ABCDEFGHIJ";
  ASSERT_TRUE(buf.try_write(span<const char>(p2.data(), p2.size())).has_value());

  // Read the remaining 2 old bytes + 10 new bytes = 12 bytes
  std::array<char, 12> out2{};
  EXPECT_EQ(buf.read(span<char>(out2.data(), 12)), 12);
  EXPECT_EQ(reloco::string_view(out2.data(), 12), reloco::string_view("90ABCDEFGHIJ"));
}

TEST_F(SpscRingBufferTest, StructIoAndEmplacement) {
  struct alignas(4) Point {
    uint32_t x;
    uint32_t y;
  };

  Point p1{100, 200};
  ASSERT_TRUE(buf.try_write_object(p1).has_value());

  // Emplace directly into the ring buffer zero-copy
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  auto p2_res = buf.try_emplace<Point>(300u, 400u);
  RELOCO_END_UNSAFE_BUFFER_USAGE;
  ASSERT_TRUE(p2_res.has_value());
  EXPECT_EQ(p2_res.value()->x, 300);

  // Read them back
  auto r1 = buf.try_read_object<Point>();
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(r1->x, 100);
  EXPECT_EQ(r1->y, 200);

  auto r2 = buf.try_read_object<Point>();
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(r2->x, 300);
  EXPECT_EQ(r2->y, 400);
}

TEST_F(SpscRingBufferTest, TransactionRollback) {
  // Write Transaction Rollback
  {
    auto tx = buf.begin_write();
    ASSERT_TRUE(tx);
    {
      auto chunk1 = tx.chunk1();
      chunk1[0] = 'X';
    }
    // We DO NOT call tx.commit(). It goes out of scope.
  }
  EXPECT_FALSE(buf.try_pop().has_value()); // Queue should still be empty!

  // Read Transaction Rollback
  ASSERT_TRUE(buf.try_push('Y').has_value());
  {
    auto tx = buf.begin_read();
    ASSERT_TRUE(tx);
    auto chunk1 = tx.chunk1();
    EXPECT_EQ(chunk1[0], 'Y');
    // We DO NOT call tx.consume(). It goes out of scope.
  }

  // Data is still there!
  auto val = buf.try_pop();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 'Y');
}

TEST_F(SpscRingBufferTest, FrameParsing) {
  struct PacketHeader {
    uint16_t magic;
    uint16_t payload_len;
  };

  auto validator = [](const PacketHeader &h) -> std::pair<bool, std::size_t> {
    if (h.magic != 0xBEEF)
      return {false, 0};
    return {true, sizeof(PacketHeader) + h.payload_len};
  };

  // Write incomplete frame
  PacketHeader hdr{0xBEEF, 4};
  ASSERT_TRUE(buf.try_write_object(hdr).has_value());

  bool processed = false;
  auto processor = [&](const PacketHeader &, span<const char>, span<const char>) { processed = true; };

  // Should return false (waiting for payload)
  auto res = buf.try_consume_frame<PacketHeader>(validator, processor);
  ASSERT_TRUE(res.has_value());
  EXPECT_FALSE(*res);
  EXPECT_FALSE(processed);

  // Write the payload
  std::string_view payload = "ABCD";
  ASSERT_TRUE(buf.try_write(span<const char>(payload.data(), payload.size())).has_value());

  // Should now succeed and consume
  res = buf.try_consume_frame<PacketHeader>(validator, processor);
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(*res);
  EXPECT_TRUE(processed);

  // Queue should now be empty
  EXPECT_FALSE(buf.try_pop().has_value());
}

TEST_F(SpscRingBufferTest, ClosuresAndAlgorithms) {
  // write_with
  buf.write_with([](span<char> c1, span<char>) -> std::size_t {
    if (c1.size() >= 3) {
      c1[0] = '0';
      c1[1] = '0';
      c1[2] = 'A';
      return 3;
    }
    return 0;
  });

  // consume_while
  std::size_t dropped = buf.consume_while([](char c) { return c == '0'; });
  EXPECT_EQ(dropped, 2);

  // read_with
  char out = 0;
  buf.read_with([&](span<const char> c1, span<const char>) -> std::size_t {
    if (!c1.empty())
      out = c1[0];
    return 1;
  });

  EXPECT_EQ(out, 'A');
}

// =========================================================================
// MULTI-THREADED STRESS TEST (Proves Atomics & Padding Work)
// =========================================================================

TEST(SpscConcurrencyTest, ProducerConsumerPingPong) {
  heap_spsc_ring_buffer<int> queue;

  // 64KB lock-free queue
  ASSERT_TRUE(queue.try_initialize(65536).has_value());

  std::atomic<bool> producer_done{false};
  constexpr int TARGET = 1'000'000;

  std::thread producer([&]() {
    for (int i = 0; i < TARGET; ++i) {
      // Spin-wait until there is space
      while (!queue.try_push(i).has_value()) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  std::thread consumer([&]() {
    int expected = 0;
    while (true) {
      auto val = queue.try_pop();
      if (val.has_value()) {
        EXPECT_EQ(*val, expected);
        expected++;
      } else {
        if (producer_done.load(std::memory_order_acquire)) {
          // Try one last time in case of a race
          if (!queue.try_pop().has_value()) {
            break;
          }
        }
        std::this_thread::yield();
      }
    }
    EXPECT_EQ(expected, TARGET);
  });

  producer.join();
  consumer.join();
}