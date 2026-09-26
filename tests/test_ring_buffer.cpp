#include <gtest/gtest.h>
#include <reloco/array.hpp>
#include <reloco/ring_buffer.hpp>
#include <reloco/span.hpp>
#include <reloco/string_view.hpp>

using namespace reloco;

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {
TEST(RingBufferTest, BasicBulkReadWrite) {
  auto rb = ring_buffer<char>();
  ASSERT_TRUE(rb.try_reserve(10).has_value());

  std::string_view hw = "Hello";

  // Write 5 bytes
  ASSERT_TRUE(rb.try_write(span<const char>(hw.data(), hw.size())).has_value());
  EXPECT_EQ(rb.size(), 5);
  EXPECT_EQ(rb.free_space(), 5);

  // Read 3 bytes
  std::array<char, 5> buf{};
  EXPECT_EQ(rb.read(span<char>(buf.data(), 3)), 3);
  EXPECT_EQ(std::string_view(buf.data(), 3), "Hel");
  EXPECT_EQ(rb.size(), 2);

  // Buffer contains "lo", logically.
  std::string_view wr = "World!";
  ASSERT_TRUE(rb.try_write(span<const char>(wr.data(), wr.size())).has_value());
  EXPECT_EQ(rb.size(), 8);

  // Read remaining 8 bytes
  EXPECT_EQ(rb.read(span<char>(buf.data(), 8)), 8);
  EXPECT_EQ(std::string_view(buf.data(), 8), "loWorld!");
}

TEST(RingBufferTest, WriteOverwriteStreaming) {
  inline_ring_buffer<char, 5> stream;

  // Stream first 4 bytes
  stream.write_overwrite(span<const char>("1234", 4));
  EXPECT_EQ(stream.size(), 4);

  // Stream 3 more bytes. This overflows the 5-byte capacity by 2!
  // It should overwrite "1" and "2", leaving "34567"
  stream.write_overwrite(span<const char>("567", 3));
  EXPECT_EQ(stream.size(), 5);

  reloco::array<char, 5> out{};

  EXPECT_EQ(stream.read(span<char>(out)), 5);
  EXPECT_EQ(std::string_view(out.data(), 5), "34567");

  // Overwrite with a chunk LARGER than the whole buffer
  stream.write_overwrite(span<const char>("ABCDEFGHIJKLMNOP", 16));
  EXPECT_EQ(stream.size(), 5); // Must cap at 5

  // It should only retain the LAST 5 characters: "LMNOP"
  EXPECT_EQ(stream.read(span<char>(out)), 5);
  EXPECT_EQ(std::string_view(out.data(), 5), "LMNOP");
}

TEST(RingBufferTest, ScatterGatherIO) {
  sso_ring_buffer<char, 8> io_buf;

  // Simulate pushing some initial protocol header
  ASSERT_TRUE(io_buf.try_push_back('A').has_value());
  ASSERT_TRUE(io_buf.try_push_back('B').has_value());

  // Read 1 to offset the head, making a wrap-around highly likely later
  io_buf.consume(1);
  EXPECT_EQ(io_buf.size(), 1);

  // Simulate DMA/Socket receive into the free space slices
  auto slices = io_buf.write_slices();

  // Fill the first slice with 'X' and second with 'Y'
  for (char &c : slices.first)
    c = 'X';
  for (char &c : slices.second)
    c = 'Y';

  // Commit that we actually received exactly 5 bytes
  io_buf.commit(5);
  EXPECT_EQ(io_buf.size(), 6); // 1 original + 5 new

  // Verify
  reloco::array<char, 6> verify{};
  io_buf.read(reloco::span<char>(verify));

  // We expect 'B', followed by 5 'X's (or 'Y's if it wrapped immediately)
  EXPECT_EQ(verify[0], 'B');
  EXPECT_EQ(verify[1], 'X'); // Just ensuring we got the slice memory correctly mapped
}

struct PacketHeader {
  uint32_t magic;
  uint16_t length;
  uint8_t type;
  uint8_t flags;
};
} // namespace
// Ensure it's trivially copyable
namespace reloco {
template <> struct is_trivially_relocatable<PacketHeader> : std::true_type {};
} // namespace reloco

namespace {
TEST(RingBufferTest, ObjectSerialization) {
  inline_ring_buffer<char, 128> tx_buffer;

  PacketHeader out_hdr{0xDEADBEEF, 42, 1, 0xFF};

  // Serialize the struct into the byte buffer
  ASSERT_TRUE(tx_buffer.try_write_object(out_hdr).has_value());
  EXPECT_EQ(tx_buffer.size(), sizeof(PacketHeader));

  // Serialize a payload span of a different type
  uint32_t payload[] = {100, 200, 300};
  ASSERT_TRUE(tx_buffer.try_write_span(span<const uint32_t>(payload)).has_value());

  EXPECT_EQ(tx_buffer.size(), sizeof(PacketHeader) + sizeof(payload));

  // Deserialize the header back out safely
  auto in_hdr_res = tx_buffer.try_read_object<PacketHeader>();
  ASSERT_TRUE(in_hdr_res.has_value());

  PacketHeader in_hdr = *in_hdr_res;
  EXPECT_EQ(in_hdr.magic, 0xDEADBEEF);
  EXPECT_EQ(in_hdr.length, 42);

  // Remaining bytes are just the payload
  EXPECT_EQ(tx_buffer.size(), sizeof(payload));
}
} // namespace

// Standard header for our test codec
namespace {
struct TestHeader {
  uint32_t magic;
  uint32_t total_size; // Total frame size in bytes (header + payload)
};
} // namespace

namespace reloco {
template <> struct is_trivially_relocatable<TestHeader> : std::true_type {};
} // namespace reloco

class RingBufferCodecTest : public ::testing::Test {
protected:
  // Shared validator mimicking real-world networking checks
  auto get_validator() {
    return [](const TestHeader &hdr) -> std::pair<bool, std::size_t> {
      if (hdr.magic != 0x1337BEEF)
        return {false, 0};
      return {true, hdr.total_size};
    };
  }
};

TEST_F(RingBufferCodecTest, AtomicFrameInjectionAndCleanEviction) {
  inline_ring_buffer<char, 32> stream;
  auto val = get_validator();

  TestHeader pkt1{0x1337BEEF, sizeof(TestHeader) + 10};
  TestHeader pkt2{0x1337BEEF, sizeof(TestHeader) + 12};

  // Write Packet 1 (8 + 10 = 18 bytes)
  ASSERT_TRUE(stream.try_write_frame_evicting(pkt1, span<const char>("1111111111", 10), val).has_value());
  EXPECT_EQ(stream.size(), 18);

  // Write Packet 2 (8 + 12 = 20 bytes).
  // Free space is 14. 20 > 14, so it cleanly evicts the 18-byte pkt1.
  ASSERT_TRUE(stream.try_write_frame_evicting(pkt2, span<const char>("222222222222", 12), val).has_value());

  // Size should be exactly the size of pkt2, no shredded garbage left behind.
  EXPECT_EQ(stream.size(), 20);

  // Verify contents
  int processed = 0;
  auto res =
      stream.try_consume_frame<TestHeader>(val, [&](const TestHeader &, span<const char> c1, span<const char> c2) {
        processed++;
        std::string payload;
        payload.append(c1.data(), c1.size());
        payload.append(c2.data(), c2.size());
        EXPECT_EQ(payload, "222222222222");
      });

  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(*res);
  EXPECT_EQ(processed, 1);
  EXPECT_EQ(stream.size(), 0);
}

TEST_F(RingBufferCodecTest, RejectsOversizedFrames) {
  inline_ring_buffer<char, 32> stream;
  TestHeader giant_pkt{0x1337BEEF, sizeof(TestHeader) + 40};

  // 48 bytes cannot fit into a 32-byte capacity ring buffer.
  auto res = stream.try_write_frame_evicting(giant_pkt, span<const char>("...40 bytes...", 40), get_validator());
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::capacity_exceeded);
  EXPECT_EQ(stream.size(), 0);
}

TEST_F(RingBufferCodecTest, ClearsBufferOnCorruptEviction) {
  inline_ring_buffer<char, 32> stream;
  auto val = get_validator();

  // Push a valid packet manually
  TestHeader pkt1{0x1337BEEF, sizeof(TestHeader) + 4};
  ASSERT_TRUE(stream.try_write_object(pkt1));
  ASSERT_TRUE(stream.try_write(span<const char>("ABCD", 4)));

  // Manually corrupt the magic bytes in memory
  stream.try_at(0).value()[0] = 0x00;

  // Write Packet 2 which forces an eviction check
  TestHeader pkt2{0x1337BEEF, sizeof(TestHeader) + 20};
  ASSERT_TRUE(stream.try_write_frame_evicting(pkt2, span<const char>("12345678901234567890", 20), val).has_value());

  // Because pkt1 was corrupt, the buffer should have self-healed by wiping itself
  // before writing pkt2. The size should be exactly pkt2's size.
  EXPECT_EQ(stream.size(), 28);
}

TEST_F(RingBufferCodecTest, FrameExtractionWaitAndProcess) {
  inline_ring_buffer<char, 128> stream;
  auto val = get_validator();

  TestHeader pkt{0x1337BEEF, sizeof(TestHeader) + 10};

  // Write header and HALF of the payload
  ASSERT_TRUE(stream.try_write_object(pkt));
  ASSERT_TRUE(stream.try_write(span<const char>("12345", 5)));

  int processed = 0;
  auto process_cb = [&](const TestHeader &, span<const char>, span<const char>) { processed++; };

  // Consume should return false (wait) because frame isn't fully downloaded
  auto res = stream.try_consume_frame<TestHeader>(val, process_cb);
  ASSERT_TRUE(res.has_value());
  EXPECT_FALSE(*res);
  EXPECT_EQ(processed, 0);

  // Write the rest
  ASSERT_TRUE(stream.try_write(span<const char>("67890", 5)));

  // Consume should now succeed
  res = stream.try_consume_frame<TestHeader>(val, process_cb);
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(*res);
  EXPECT_EQ(processed, 1);
  EXPECT_EQ(stream.size(), 0);
}

TEST_F(RingBufferCodecTest, ClearsBufferOnCorruptConsume) {
  inline_ring_buffer<char, 128> stream;

  // Write a packet with invalid magic bytes
  TestHeader bad_pkt{0xDEADDEAD, sizeof(TestHeader) + 10};
  ASSERT_TRUE(stream.try_write_object(bad_pkt));
  ASSERT_TRUE(stream.try_write(span<const char>("1234567890", 10)));

  EXPECT_EQ(stream.size(), 18);

  auto res = stream.try_consume_frame<TestHeader>(get_validator(),
                                                  [](const TestHeader &, span<const char>, span<const char>) {});

  // Must return an error and completely clear the buffer to stop cascading garbage
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::invalid_argument);
  EXPECT_EQ(stream.size(), 0);
}

TEST_F(RingBufferCodecTest, ClearsBufferOnImpossibleLengthConsume) {
  inline_ring_buffer<char, 128> stream;

  // Write a packet with valid magic, but malicious/impossible length (smaller than the header itself)
  TestHeader malicious_pkt{0x1337BEEF, sizeof(TestHeader) - 2};
  ASSERT_TRUE(stream.try_write_object(malicious_pkt));

  EXPECT_EQ(stream.size(), 8);

  auto res = stream.try_consume_frame<TestHeader>(get_validator(),
                                                  [](const TestHeader &, span<const char>, span<const char>) {});

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), error::invalid_argument);
  EXPECT_EQ(stream.size(), 0);
}

TEST_F(RingBufferCodecTest, PeekFrameDoesNotConsume) {
  inline_ring_buffer<char, 128> stream;
  auto val = get_validator();

  TestHeader pkt{0x1337BEEF, sizeof(TestHeader) + 5};
  ASSERT_TRUE(stream.try_write_object(pkt));
  ASSERT_TRUE(stream.try_write(span<const char>("HELLO", 5)));

  // Peek the frame
  int peek_count = 0;
  auto res = stream.try_peek_frame<TestHeader>(val, [&](const TestHeader &, span<const char> c1, span<const char>) {
    EXPECT_EQ(c1.size(), 5);
    EXPECT_EQ(std::string_view(c1.data(), c1.size()), "HELLO");
    peek_count++;
  });

  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(*res);
  EXPECT_EQ(peek_count, 1);

  // Verify it was NOT consumed! Size should remain unchanged.
  EXPECT_EQ(stream.size(), sizeof(TestHeader) + 5);

  // We can peek it again!
  res = stream.try_peek_frame<TestHeader>(
      val, [&](const TestHeader &, span<const char>, span<const char>) { peek_count++; });
  EXPECT_EQ(peek_count, 2);

  // Manually consume the exact size of the frame when finished
  stream.consume(sizeof(TestHeader) + 5);
  EXPECT_EQ(stream.size(), 0);
}

TEST(RingBufferTest, FindDelimiter) {
  inline_ring_buffer<char, 32> stream;

  // Create a wrapped layout: [ "world\n", free..., "Hello " ]
  ASSERT_TRUE(stream.try_reserve(10));
  ASSERT_TRUE(stream.try_write(span<const char>("XXXXX", 5)));
  stream.consume(5);
  ASSERT_TRUE(stream.try_write(span<const char>("Hello world\n", 12)));

  auto pos = stream.find('\n');
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(*pos, 11);

  // Verify we can find a substring/character starting from an offset
  auto l_pos = stream.find('l', 5); // Search for 'l' starting after "Hello"
  ASSERT_TRUE(l_pos.has_value());
  EXPECT_EQ(*l_pos, 9); // The 'l' in "world"
}

TEST(RingBufferTest, BulkPeek) {
  inline_ring_buffer<char, 32> stream;
  ASSERT_TRUE(stream.try_write(span<const char>("0123456789", 10)));

  std::string peek_buf(5, '\0');
  // Peek 5 bytes starting at index 2 ("23456")
  EXPECT_EQ(stream.peek(span<char>(peek_buf.data(), peek_buf.size()), 2), 5);
  EXPECT_EQ(peek_buf, "23456");

  // Ensure it wasn't consumed
  EXPECT_EQ(stream.size(), 10);
}

TEST(RingBufferTest, CascadingTransfer) {
  inline_ring_buffer<char, 16> src;
  inline_ring_buffer<char, 64> dest;

  ASSERT_TRUE(src.try_write(span<const char>("PacketData", 10)));

  // Transfer up to 100 bytes (will cap at 10)
  std::size_t moved = src.transfer_to(dest, 100);
  EXPECT_EQ(moved, 10);
  EXPECT_EQ(src.size(), 0);
  EXPECT_EQ(dest.size(), 10);

  // Verify destination data
  std::string out(10, '\0');
  dest.read(span<char>(out.data(), out.size()));
  EXPECT_EQ(out, "PacketData");
}

TEST_F(RingBufferCodecTest, AtomicFrameTransfer) {
  inline_ring_buffer<char, 128> src;
  inline_ring_buffer<char, 64> dest;
  auto val = get_validator();

  // Write Frame 1 (Full)
  TestHeader pkt1{0x1337BEEF, sizeof(TestHeader) + 5};
  ASSERT_TRUE(src.try_write_object(pkt1));
  ASSERT_TRUE(src.try_write(span<const char>("HELLO", 5)));

  // Write Frame 2 (Partial!)
  TestHeader pkt2{0x1337BEEF, sizeof(TestHeader) + 5};
  ASSERT_TRUE(src.try_write_object(pkt2));
  ASSERT_TRUE(src.try_write(span<const char>("WOR", 3))); // Missing 2 bytes

  // --- First Pump ---
  // Should transfer pkt1, but safely leave pkt2 in src
  auto res1 = src.try_transfer_frame_to<TestHeader>(dest, val);
  ASSERT_TRUE(res1.has_value());
  EXPECT_TRUE(*res1);

  EXPECT_EQ(src.size(), sizeof(TestHeader) + 3);  // pkt2 is left untouched
  EXPECT_EQ(dest.size(), sizeof(TestHeader) + 5); // pkt1 successfully moved

  // --- Second Pump ---
  // Should return false (waiting for data)
  auto res2 = src.try_transfer_frame_to<TestHeader>(dest, val);
  ASSERT_TRUE(res2.has_value());
  EXPECT_FALSE(*res2);

  // Finish downloading Frame 2
  ASSERT_TRUE(src.try_write(span<const char>("LD", 2)));

  // --- Third Pump ---
  // Should now transfer pkt2
  auto res3 = src.try_transfer_frame_to<TestHeader>(dest, val);
  ASSERT_TRUE(res3.has_value());
  EXPECT_TRUE(*res3);

  EXPECT_EQ(src.size(), 0);                             // Source is completely drained
  EXPECT_EQ(dest.size(), (sizeof(TestHeader) + 5) * 2); // Destination holds both full packets
}

TEST(RingBufferTest, AllocateAndCommit) {
  inline_ring_buffer<char, 32> stream;

  // Force a wrap-around layout
  ASSERT_TRUE(stream.try_reserve(20).has_value());
  ASSERT_TRUE(stream.try_write(span<const char>("123456789012345", 15)).has_value()); // Write 15
  stream.consume(10); // Consume 10. Head is at 10, Tail is at 15. Free space is 27 bytes total.

  // Allocate contiguous memory for a mock "socket read"
  // Even though 27 bytes are free, the tail is at 15, so the contiguous space
  // to the end of the 32-byte array is only 17 bytes!
  span<char> write_buf = stream.allocate_contiguous();
  EXPECT_EQ(write_buf.size(), 17);

  // Write 5 bytes directly into the span and commit it
  std::memcpy(write_buf.data(), "ABCDE", 5);
  stream.commit(5);

  EXPECT_EQ(stream.size(), 10); // 5 old bytes + 5 new bytes

  // Object Allocation
  // Allocate space directly for a struct without copying it later
  auto obj_res = stream.try_allocate_object<TestHeader>();
  ASSERT_TRUE(obj_res.has_value());

  TestHeader *hdr = *obj_res;
  hdr->magic = 0x1337BEEF;
  hdr->total_size = sizeof(TestHeader);

  stream.commit(sizeof(TestHeader));
  EXPECT_EQ(stream.size(), 18);

  // Resolving Free-Space Fragmentation!
  // We want to write 12 bytes. But we might be near the edge of the wrap.
  span<char> chunk = stream.allocate_contiguous(12);

  if (chunk.size() < 12) {
    // We hit the edge of the array! The free space is fragmented.
    // Fix it instantly with zero heap allocations:
    stream.make_contiguous();

    // Try again. Now the free space is guaranteed to be in one solid chunk!
    chunk = stream.allocate_contiguous(12);
    EXPECT_GE(chunk.size(), 12);
  }

  std::memcpy(chunk.data(), "FRAGMENT_FIX", 12);
  stream.commit(12);

  EXPECT_EQ(stream.size(), 30);
}

TEST(RingBufferTest, AllocateSlicesScatterGather) {
  inline_ring_buffer<char, 32> stream;

  // Force a wrap-around state
  ASSERT_TRUE(stream.try_reserve(32));
  // Write 24, consume 20.
  // Remaining data (4 bytes) is at index 20. Tail is at 24.
  // Free space: 8 bytes at the end [24-32], 20 bytes at the front [0-20].
  ASSERT_TRUE(stream.try_write(span<const char>("12345678901234567890ABCD", 24)));
  stream.consume(20);

  // Request 15 bytes of allocation
  auto [s1, s2] = stream.allocate_slices(15);

  // It gives us exactly the split we need!
  EXPECT_EQ(s1.size(), 8);
  EXPECT_EQ(s2.size(), 7);

  // Write directly into the allocated memory
  std::memcpy(s1.data(), "12345678", 8);
  std::memcpy(s2.data(), "90ABCDE", 7);

  // Commit the total number of bytes written
  stream.commit(15);

  EXPECT_EQ(stream.size(), 19); // 4 old bytes + 15 new bytes

  // Verify the data seamlessly crosses the boundary
  auto [r1, r2] = stream.read_slices();
  std::string full_read;
  full_read.append(r1.data(), r1.size());
  full_read.append(r2.data(), r2.size());

  EXPECT_EQ(full_read, "ABCD1234567890ABCDE");
}

TEST(RingBufferTest, ReadSlicesWithLimit) {
  inline_ring_buffer<char, 32> stream;

  // Force a wrapped state
  ASSERT_TRUE(stream.try_reserve(32));
  // Write 28, consume 24.
  // Remaining data (4 bytes) is at index 24. Free space is 28 bytes.
  ASSERT_TRUE(stream.try_write(span<const char>("12345678901234567890ABCDX_YZ", 28)));
  stream.consume(24);

  // Write 10 more bytes. Total size is 14.
  // They go from index 24 to 32 (8 bytes), then wrap to index 0 (6 bytes).
  ASSERT_TRUE(stream.try_write(span<const char>("1234567890", 10)));
  EXPECT_EQ(stream.size(), 14);

  // Read limitless (should give 8 and 6)
  auto [r1, r2] = stream.read_slices();
  EXPECT_EQ(r1.size(), 8);
  EXPECT_EQ(r2.size(), 6);

  // Read with limit fitting entirely in the first chunk
  auto [L1, L2] = stream.read_slices(5);
  EXPECT_EQ(L1.size(), 5);
  EXPECT_TRUE(L2.empty()); // No wrap needed

  // Read with limit precisely on the boundary
  auto [B1, B2] = stream.read_slices(8);
  EXPECT_EQ(B1.size(), 8);
  EXPECT_TRUE(B2.empty());

  // Read with limit crossing the boundary
  auto [W1, W2] = stream.read_slices(11);
  EXPECT_EQ(W1.size(), 8);
  EXPECT_EQ(W2.size(), 3);
}

TEST(RingBufferTest, IteratorSupport) {
  inline_ring_buffer<int, 10> stream;

  // Force a wrap around: write 8, consume 5, write 4
  ASSERT_TRUE(stream.try_write(span<const int>({1, 2, 3, 4, 5, 100, 200, 300})));
  stream.consume(5);                                                    // Remaining: 100, 200, 300
  ASSERT_TRUE(stream.try_write(span<const int>({400, 500, 600, 700}))); // Wraps!

  // Range-based for loop
  int sum = 0;
  for (const int val : stream) {
    sum += val;
  }
  EXPECT_EQ(sum, 100 + 200 + 300 + 400 + 500 + 600 + 700);

  // STL Algorithms (std::find works transparently across the wrap boundary!)
  auto it = std::find(stream.begin(), stream.end(), 500);
  ASSERT_NE(it, stream.end());
  EXPECT_EQ(*it, 500);

  // STL Mutation (std::fill)
  std::fill(stream.begin(), stream.end(), 42);
  for (int val : stream) {
    EXPECT_EQ(val, 42);
  }
}

TEST(RingBufferTest, RaiiWriteTransaction) {
  inline_ring_buffer<char, 32> stream;
  ASSERT_TRUE(stream.try_write(span<const char>("START", 5)));

  {
    // Start a transaction for 10 bytes
    auto tx = stream.begin_write(10);
    EXPECT_LE(tx.total_allocated(), 27); // Space available

    auto chunk1 = tx.chunk1();
    ASSERT_GE(chunk1.size(), 10);
    std::memcpy(chunk1.data(), "ROLLBACK_!", 10);

    // Notice we DO NOT call tx.commit() here!
    // The transaction falls out of scope, safely abandoning the written bytes.
  }

  EXPECT_EQ(stream.size(), 5); // The buffer size never changed!

  {
    // Try again, but this time we commit.
    auto tx = stream.begin_write(10);
    auto chunk1 = tx.chunk1();
    ASSERT_GE(chunk1.size(), 10);
    std::memcpy(chunk1.data(), "SUCCESS!!!", 10);
    tx.commit(10);
  }

  EXPECT_EQ(stream.size(), 15);

  // Ensure data is correct
  auto [r1, r2] = stream.read_slices();
  std::string out;
  out.append(r1.data(), r1.size());
  EXPECT_EQ(out, "STARTSUCCESS!!!");
}

TEST(RingBufferTest, FindSequenceAcrossBoundary) {
  inline_ring_buffer<char, 32> stream;
  ASSERT_TRUE(stream.try_reserve(28));
  ASSERT_TRUE(stream.try_write(span<const char>("12345678901234567890ABCD", 24)));
  stream.consume(20); // Force wrap around for next write

  // Write a magic word that perfectly splits across the end of the array
  ASSERT_TRUE(stream.try_write(span<const char>("START_MAGIC_END", 15)));

  std::string_view magic = "_MAGIC_";
  auto pos = stream.find_sequence(span<const char>(magic.data(), magic.size()));

  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(*pos, 9); // Index relative to current logical start
}

TEST(RingBufferTest, ConsumeUntil) {
  inline_ring_buffer<char, 32> stream;
  ASSERT_TRUE(stream.try_write(span<const char>("     { \"json\": true }", 21)).has_value());

  // Fast-forward past all the garbage whitespace
  std::size_t dropped = stream.consume_until([](char c) { return c == '{'; });

  EXPECT_EQ(dropped, 5);
  EXPECT_EQ(stream.size(), 16);
  EXPECT_EQ(*stream.begin(), '{');
}

TEST(RingBufferTest, BackInserterIsLossy) {
  inline_ring_buffer<int, 3> stream; // Capacity is 3
  std::vector<int> data = {1, 2, 3, 4, 5};

  // Push 5 elements into a 3-element buffer using STL
  std::copy(data.begin(), data.end(), std::back_inserter(stream));

  // The buffer should cap at size 3
  EXPECT_EQ(stream.size(), 3);

  // It should have overwritten '1' and '2', leaving '3', '4', '5'
  auto it = stream.begin();
  EXPECT_EQ(*it++, 3);
  EXPECT_EQ(*it++, 4);
  EXPECT_EQ(*it++, 5);
  EXPECT_EQ(it, stream.end());
}

TEST(RingBufferTest, LengthPrefixedFraming) {
  struct MyHeader {
    uint32_t magic;
    uint32_t payload_length;
  };

  inline_ring_buffer<char, 128> stream;

  // Simulate receiving a partial packet from the network
  MyHeader hdr{0xABCD, 10}; // 8 byte header + 10 byte payload = 18 bytes total
  ASSERT_TRUE(stream.try_write(span<const char>(reinterpret_cast<const char *>(&hdr), sizeof(MyHeader))));
  ASSERT_TRUE(stream.try_write(span<const char>("12345", 5))); // Only 5 bytes of payload arrived!

  EXPECT_EQ(stream.size(), 13);

  auto size_extractor = [](const MyHeader &h) { return sizeof(MyHeader) + h.payload_length; };

  // Try to read frame. It should fail because 13 < 18.
  auto frame1 = stream.try_read_frame<MyHeader>(size_extractor);
  EXPECT_FALSE(frame1.has_value());

  // The rest of the payload arrives over the network
  ASSERT_TRUE(stream.try_write(span<const char>("67890", 5)));

  // Try again. It succeeds, linearizes the memory, and returns the exact packet!
  auto frame2 = stream.try_read_frame<MyHeader>(size_extractor);
  ASSERT_TRUE(frame2.has_value());
  EXPECT_EQ(frame2->size(), 18);

  // Safely cast and process the zero-copy frame
  const MyHeader *zero_copy_hdr = reinterpret_cast<const MyHeader *>(frame2->data());
  EXPECT_EQ(zero_copy_hdr->payload_length, 10);

  // Advance the buffer
  stream.consume(frame2->size());
}

RELOCO_END_UNSAFE_BUFFER_USAGE

// ---------------------------------------------------------
// Heap Ring Buffer Smoke Test
// ---------------------------------------------------------
TEST(RingBufferSmokeTest, HeapBufferAllocatesAndGrows) {
  ring_buffer<char> buf;

  // Initially empty with 0 capacity
  EXPECT_EQ(buf.size(), 0);
  EXPECT_EQ(buf.capacity(), 0);

  // Reserve memory on the heap
  ASSERT_TRUE(buf.try_reserve(16));
  EXPECT_GE(buf.capacity(), 16);

  // Write data
  std::string_view msg = "HELLO";
  ASSERT_TRUE(buf.try_write(span<const char>(msg.data(), msg.size())));
  EXPECT_EQ(buf.size(), 5);

  // Read it back
  std::array<char, 5> out{};
  EXPECT_EQ(buf.read(span<char>(out)), 5);
  EXPECT_EQ(reloco::string_view(out.data(), 5), reloco::string_view("HELLO"));
  EXPECT_EQ(buf.size(), 0);
}

// ---------------------------------------------------------
// Inline (Stack) Ring Buffer Smoke Test
// ---------------------------------------------------------
TEST(RingBufferSmokeTest, InlineBufferStrictCapacity) {
  inline_ring_buffer<int, 3> buf;

  EXPECT_EQ(buf.capacity(), 3);

  // Push 3 elements successfully
  ASSERT_TRUE(buf.try_push_back(10));
  ASSERT_TRUE(buf.try_push_back(20));
  ASSERT_TRUE(buf.try_push_back(30));
  EXPECT_EQ(buf.size(), 3);

  // 4th element MUST fail because capacity is strictly locked to 3
  EXPECT_FALSE(buf.try_push_back(40));

  // Pop the front element
  auto val = buf.try_pop_front();
  ASSERT_TRUE(val);
  EXPECT_EQ(*val, 10);
  EXPECT_EQ(buf.size(), 2);
}

// ---------------------------------------------------------
// SSO (Small String Optimization) Ring Buffer Smoke Test
// ---------------------------------------------------------
TEST(RingBufferSmokeTest, SsoBufferSpillsToHeap) {
  sso_ring_buffer<int, 3> buf;

  // Initially uses inline storage
  EXPECT_EQ(buf.capacity(), 3);

  // Fill inline storage
  ASSERT_TRUE(buf.try_push_back(1).has_value());
  ASSERT_TRUE(buf.try_push_back(2).has_value());
  ASSERT_TRUE(buf.try_push_back(3).has_value());

  // Push 4th element. This should seamlessly allocate heap memory,
  // copy the existing 3 elements over, and push the 4th!
  ASSERT_TRUE(buf.try_push_back(4));

  EXPECT_GT(buf.capacity(), 3); // Proves it reallocated
  EXPECT_EQ(buf.size(), 4);

  // Verify data integrity after the move
  EXPECT_EQ(buf.try_pop_front().unwrap(), 1);
  EXPECT_EQ(buf.try_pop_front().unwrap(), 2);
  EXPECT_EQ(buf.try_pop_front().unwrap(), 3);
  EXPECT_EQ(buf.try_pop_front().unwrap(), 4);
}

// ---------------------------------------------------------
// 4. Outline (External Memory) Ring Buffer Smoke Test
// ---------------------------------------------------------
TEST(RingBufferSmokeTest, OutlineBufferCrossCastsAndWraps) {
  // We have an array of two 64-bit integers (16 bytes total)
  std::array<uint64_t, 2> raw_storage = {0, 0};

  // Construct a byte-oriented ring buffer over the uint64_t array.
  // The cross-casting span constructor should calculate 16 bytes of capacity!
  outline_ring_buffer<char> buf(reloco::span<uint64_t>{raw_storage});

  EXPECT_EQ(buf.capacity(), 16);

  // Write into the buffer
  std::string_view msg = "12345678";
  ASSERT_TRUE(buf.try_write(span<const char>(msg.data(), msg.size())));

  // Verify it actually mutated the underlying external memory!
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  char *raw_bytes = reinterpret_cast<char *>(raw_storage.data());
  EXPECT_EQ(raw_bytes[0], '1');
  EXPECT_EQ(raw_bytes[7], '8');
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

// ---------------------------------------------------------
// 5. Ring Buffer Ref (Type Erasure & Cursor) Smoke Test
// ---------------------------------------------------------

namespace {
// PARADIGM 1: The Mutating Reference
// Accepts the base class by reference. Any consumes here mutate the original buffer.
void process_network_packet(detail::unowned_ring_base<char> &stream) {
  if (stream.size() >= 4) {
    stream.consume(4);
  }
}

// PARADIGM 2: The Independent Cursor
// Accepts our new explicit cursor by value. Modifying it leaves the original untouched.
std::size_t lookahead_scan(ring_buffer_ref<char> cursor) {
  cursor.consume(2); // Modifies the local cursor's length, NOT the original!
  return cursor.size();
}
} // namespace

TEST(RingBufferSmokeTest, RingBufferRefTypeErasure) {
  inline_ring_buffer<char, 32> fast_buf;
  std::string_view msg = "PACKET_DATA";

  // Write data and verify
  ASSERT_TRUE(fast_buf.try_write(span<const char>(msg.data(), msg.size())).has_value());
  EXPECT_EQ(fast_buf.size(), 11);

  // 1. Pass by Reference (Modifies original buffer)
  // Implicitly upcasts perfectly to detail::unowned_ring_base<char>&
  process_network_packet(fast_buf);
  EXPECT_EQ(fast_buf.size(), 7); // 11 - 4 = 7

  // 2. Create an Independent Cursor
  // Because the constructor is `explicit`, this compile-time safety is enforced.
  // lookahead_scan(fast_buf) would fail to compile! We MUST explicitly borrow it:
  std::size_t remaining_in_cursor = lookahead_scan(ring_buffer_ref<char>(fast_buf));

  EXPECT_EQ(remaining_in_cursor, 5); // 7 - 2 = 5 inside the cursor
  EXPECT_EQ(fast_buf.size(), 7);     // Original is UNTOUCHED!
}
