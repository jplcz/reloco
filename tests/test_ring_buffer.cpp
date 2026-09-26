#include <gtest/gtest.h>
#include <reloco/array.hpp>
#include <reloco/ring_buffer.hpp>
#include <reloco/span.hpp>

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
  io_buf.commit_written(5);
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

RELOCO_END_UNSAFE_BUFFER_USAGE