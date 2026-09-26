#include <gtest/gtest.h>
#include <reloco/array.hpp>
#include <reloco/ring_buffer.hpp>
#include <reloco/span.hpp>

using namespace reloco;

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

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
RELOCO_END_UNSAFE_BUFFER_USAGE