// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/array.hpp>
#include <reloco/byte_cursor.hpp>
#include <reloco/endian.hpp>

#include <cstdint>
#include <initializer_list>

namespace {

using reloco::byte_order;

template <std::size_t N> struct buffer {
  reloco::array<std::byte, N> storage{};

  [[nodiscard]] reloco::span<std::byte> mut() noexcept { return reloco::span<std::byte>(storage.data(), N); }
  [[nodiscard]] reloco::span<const std::byte> view() const noexcept {
    return reloco::span<const std::byte>(storage.data(), N);
  }
};

reloco::span<const std::byte> bytes_of(const std::initializer_list<unsigned char> &list,
                                       reloco::array<std::byte, 16> &store) {
  std::size_t i = 0;
  for (unsigned char c : list)
    store[i++] = static_cast<std::byte>(c);
  return reloco::span<const std::byte>(store.data(), list.size());
}

enum class opcode : std::uint16_t { push = 0x0102, pop = 0x0304 };

} // namespace

static_assert(reloco::byteswap<std::uint16_t>(0x1234U) == 0x3412U);
static_assert(reloco::byteswap<std::uint32_t>(0x01020304U) == 0x04030201U);
static_assert(reloco::byteswap<std::int64_t>(0x0102030405060708) == 0x0807060504030201);
static_assert(reloco::byteswap<std::uint8_t>(0xABU) == 0xABU);
static_assert(reloco::to_byte_order<reloco::native_byte_order>(std::uint32_t{7}) == 7U);
static_assert(reloco::is_wire_integer_v<opcode>);
static_assert(!reloco::is_wire_integer_v<bool>);
static_assert(!reloco::is_wire_integer_v<double>);

TEST(EndianTest, LoadStoreRoundTripBothOrders) {
  buffer<8> buf;
  ASSERT_TRUE(reloco::try_store<byte_order::big>(buf.mut(), std::uint32_t{0x01020304}).has_value());
  EXPECT_EQ(buf.storage[0], std::byte{1});
  EXPECT_EQ(buf.storage[3], std::byte{4});
  EXPECT_EQ((reloco::try_load<std::uint32_t, byte_order::big>(buf.view()).value()), 0x01020304U);
  EXPECT_EQ((reloco::try_load<std::uint32_t, byte_order::little>(buf.view()).value()), 0x04030201U);

  ASSERT_TRUE(reloco::try_store<byte_order::little>(buf.mut(), std::int64_t{-2}).has_value());
  EXPECT_EQ((reloco::try_load<std::int64_t, byte_order::little>(buf.view()).value()), -2);
}

TEST(EndianTest, ShortBufferFails) {
  buffer<3> buf;
  auto loaded = reloco::try_load<std::uint32_t, byte_order::big>(buf.view());
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error(), reloco::error::out_of_bounds);
  EXPECT_FALSE(reloco::try_store<byte_order::big>(buf.mut(), std::uint32_t{1}).has_value());
}

TEST(ByteReaderTest, ReadsIntegersInRequestedOrder) {
  reloco::array<std::byte, 16> store{};
  reloco::byte_reader r(bytes_of({0x01, 0x02, 0x01, 0x02, 0x03, 0x04, 0xFF}, store));
  EXPECT_EQ(r.try_read_be<std::uint16_t>().value(), 0x0102U);
  EXPECT_EQ(r.position(), 2U);
  EXPECT_EQ(r.try_read_le<std::uint32_t>().value(), 0x04030201U);
  EXPECT_EQ(r.try_read_u8().value(), 0xFFU);
  EXPECT_TRUE(r.at_end());
  auto past = r.try_read_u8();
  ASSERT_FALSE(past.has_value());
  EXPECT_EQ(past.error(), reloco::error::out_of_bounds);
}

TEST(ByteReaderTest, FailureLeavesPositionUnchanged) {
  reloco::array<std::byte, 16> store{};
  reloco::byte_reader r(bytes_of({1, 2, 3}, store));
  EXPECT_FALSE(r.try_read_be<std::uint32_t>().has_value());
  EXPECT_EQ(r.position(), 0U);
  EXPECT_FALSE(r.try_skip(4).has_value());
  EXPECT_FALSE(r.try_read_bytes(4).has_value());
  EXPECT_EQ(r.position(), 0U);
  EXPECT_EQ(r.remaining(), 3U);
}

TEST(ByteReaderTest, PeekDoesNotConsume) {
  reloco::array<std::byte, 16> store{};
  reloco::byte_reader r(bytes_of({0x12, 0x34}, store));
  EXPECT_EQ((r.try_peek<std::uint16_t, byte_order::big>().value()), 0x1234U);
  EXPECT_EQ(r.position(), 0U);
}

TEST(ByteReaderTest, ReadsEnums) {
  reloco::array<std::byte, 16> store{};
  reloco::byte_reader r(bytes_of({0x01, 0x02, 0x04, 0x03}, store));
  EXPECT_EQ(r.try_read_be<opcode>().value(), opcode::push);
  EXPECT_EQ(r.try_read_le<opcode>().value(), opcode::pop);
}

TEST(ByteReaderTest, SeekSkipAndAlign) {
  reloco::array<std::byte, 16> store{};
  reloco::byte_reader r(bytes_of({0, 1, 2, 3, 4, 5, 6, 7}, store));
  ASSERT_TRUE(r.try_skip(1).has_value());
  ASSERT_TRUE(r.try_align(4).has_value());
  EXPECT_EQ(r.position(), 4U);
  ASSERT_TRUE(r.try_align(4).has_value());
  EXPECT_EQ(r.position(), 4U);
  EXPECT_EQ(r.try_align(3).error(), reloco::error::invalid_argument);
  EXPECT_EQ(r.try_align(0).error(), reloco::error::invalid_argument);
  ASSERT_TRUE(r.try_seek(8).has_value());
  EXPECT_FALSE(r.try_seek(9).has_value());
  ASSERT_TRUE(r.try_seek(0).has_value());
  EXPECT_EQ(r.remaining(), 8U);
}

TEST(ByteReaderTest, ReadBytesSplitAndInto) {
  reloco::array<std::byte, 16> store{};
  reloco::byte_reader r(bytes_of({9, 8, 7, 6, 5, 4}, store));
  auto head = r.try_read_bytes(2).value();
  EXPECT_EQ(head.size(), 2U);
  EXPECT_EQ(head[0], std::byte{9});

  auto inner = r.try_split(3).value();
  EXPECT_EQ(inner.size(), 3U);
  EXPECT_EQ(r.remaining(), 1U);
  EXPECT_EQ(inner.try_read_u8().value(), 7U);
  EXPECT_FALSE(inner.try_read_be<std::uint32_t>().has_value());
  EXPECT_FALSE(r.try_split(2).has_value());

  buffer<1> out;
  ASSERT_TRUE(r.try_read_into(out.mut()).has_value());
  EXPECT_EQ(out.storage[0], std::byte{4});
}

TEST(ByteReaderTest, ReadUntilDelimiter) {
  reloco::array<std::byte, 16> store{};
  reloco::byte_reader r(bytes_of({'h', 'i', 0, 'x'}, store));
  auto text = r.try_read_until(std::byte{0}).value();
  EXPECT_EQ(text.size(), 2U);
  EXPECT_EQ(r.position(), 3U);
  auto missing = r.try_read_until(std::byte{0});
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), reloco::error::not_found);
  EXPECT_EQ(r.position(), 3U);
}

TEST(ByteReaderTest, Leb128KnownVectors) {
  reloco::array<std::byte, 16> store{};
  reloco::byte_reader u(bytes_of({0xE5, 0x8E, 0x26}, store));
  EXPECT_EQ(u.try_read_uleb128().value(), 624485U);

  reloco::array<std::byte, 16> store2{};
  reloco::byte_reader s(bytes_of({0xC0, 0xBB, 0x78}, store2));
  EXPECT_EQ(s.try_read_sleb128().value(), -123456);
}

TEST(ByteReaderTest, Leb128TruncatedAndOverflow) {
  reloco::array<std::byte, 16> store{};
  reloco::byte_reader truncated(bytes_of({0x80, 0x80}, store));
  EXPECT_EQ(truncated.try_read_uleb128().error(), reloco::error::out_of_bounds);
  EXPECT_EQ(truncated.position(), 0U);

  reloco::array<std::byte, 16> store2{};
  reloco::byte_reader too_big(bytes_of({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F}, store2));
  EXPECT_EQ(too_big.try_read_uleb128().error(), reloco::error::integer_overflow);

  reloco::array<std::byte, 16> store3{};
  reloco::byte_reader too_long(bytes_of({0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01}, store3));
  EXPECT_EQ(too_long.try_read_uleb128().error(), reloco::error::integer_overflow);
}

TEST(ByteReaderTest, UnsafeReadAfterCheck) {
  reloco::array<std::byte, 16> store{};
  reloco::byte_reader r(bytes_of({0, 0, 1, 0}, store));
  ASSERT_GE(r.remaining(), 4U);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ((r.unsafe_read<std::uint32_t, byte_order::big>()), 256U);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

TEST(ByteWriterTest, WritesAndReadsBack) {
  buffer<32> buf;
  reloco::byte_writer w(buf.mut());
  ASSERT_TRUE(w.try_write_u8(0xAA).has_value());
  ASSERT_TRUE(w.try_write_be(std::uint16_t{0x0102}).has_value());
  ASSERT_TRUE(w.try_write_le(std::int32_t{-1}).has_value());
  ASSERT_TRUE(w.try_write_be(opcode::pop).has_value());
  EXPECT_EQ(w.position(), 9U);
  EXPECT_EQ(w.remaining(), 23U);

  reloco::byte_reader r(w.written());
  EXPECT_EQ(r.try_read_u8().value(), 0xAAU);
  EXPECT_EQ(r.try_read_be<std::uint16_t>().value(), 0x0102U);
  EXPECT_EQ(r.try_read_le<std::int32_t>().value(), -1);
  EXPECT_EQ(r.try_read_be<opcode>().value(), opcode::pop);
  EXPECT_TRUE(r.at_end());
}

TEST(ByteWriterTest, OverflowIsAtomic) {
  buffer<5> buf;
  reloco::byte_writer w(buf.mut());
  ASSERT_TRUE(w.try_write_be(std::uint32_t{0x01020304}).has_value());
  auto fail = w.try_write_be(std::uint16_t{0xFFFF});
  ASSERT_FALSE(fail.has_value());
  EXPECT_EQ(fail.error(), reloco::error::out_of_bounds);
  EXPECT_EQ(w.position(), 4U);
  EXPECT_EQ(buf.storage[4], std::byte{0});
  EXPECT_FALSE(w.try_write_zeros(2).has_value());
  EXPECT_EQ(buf.storage[4], std::byte{0});
}

TEST(ByteWriterTest, BytesFillAlignAndReset) {
  buffer<16> buf;
  reloco::byte_writer w(buf.mut());
  buffer<3> src;
  src.storage[0] = std::byte{1};
  src.storage[1] = std::byte{2};
  src.storage[2] = std::byte{3};
  ASSERT_TRUE(w.try_write_bytes(src.view()).has_value());
  ASSERT_TRUE(w.try_align(4).has_value());
  EXPECT_EQ(w.position(), 4U);
  EXPECT_EQ(buf.storage[3], std::byte{0});
  ASSERT_TRUE(w.try_fill(std::byte{0x7F}, 2).has_value());
  EXPECT_EQ(buf.storage[5], std::byte{0x7F});
  EXPECT_EQ(w.try_align(6).error(), reloco::error::invalid_argument);
  w.reset();
  EXPECT_EQ(w.position(), 0U);
  EXPECT_TRUE(w.written().empty());
}

TEST(ByteWriterTest, ReserveAndPatchLengthPrefix) {
  buffer<16> buf;
  reloco::byte_writer w(buf.mut());
  const std::size_t prefix = w.try_reserve(2).value();
  ASSERT_TRUE(w.try_write_u8(1).has_value());
  ASSERT_TRUE(w.try_write_u8(2).has_value());
  ASSERT_TRUE(w.try_write_u8(3).has_value());
  ASSERT_TRUE(w.try_patch<byte_order::big>(prefix, static_cast<std::uint16_t>(w.position() - 2)).has_value());
  EXPECT_EQ(w.position(), 5U);

  reloco::byte_reader r(w.written());
  auto body = r.try_split(r.try_read_be<std::uint16_t>().value()).value();
  EXPECT_EQ(body.size(), 3U);

  EXPECT_FALSE((w.try_patch<byte_order::big>(4, std::uint16_t{1})).has_value());
  EXPECT_FALSE((w.try_patch<byte_order::big>(99, std::uint8_t{1})).has_value());
}

TEST(ByteWriterTest, Leb128RoundTrip) {
  buffer<64> buf;
  reloco::byte_writer w(buf.mut());
  const std::uint64_t unsigned_values[] = {0, 1, 127, 128, 624485, 0xFFFFFFFFFFFFFFFFULL};
  const std::int64_t signed_values[] = {0, 1, -1, 63, 64, -64, -65, -123456, INT64_MAX, INT64_MIN};
  for (auto v : unsigned_values)
    ASSERT_TRUE(w.try_write_uleb128(v).has_value());
  for (auto v : signed_values)
    ASSERT_TRUE(w.try_write_sleb128(v).has_value());

  reloco::byte_reader r(w.written());
  for (auto v : unsigned_values)
    EXPECT_EQ(r.try_read_uleb128().value(), v);
  for (auto v : signed_values)
    EXPECT_EQ(r.try_read_sleb128().value(), v);
  EXPECT_TRUE(r.at_end());
}

TEST(ByteWriterTest, Leb128KnownEncoding) {
  buffer<8> buf;
  reloco::byte_writer w(buf.mut());
  ASSERT_TRUE(w.try_write_uleb128(624485).has_value());
  ASSERT_EQ(w.position(), 3U);
  EXPECT_EQ(buf.storage[0], std::byte{0xE5});
  EXPECT_EQ(buf.storage[1], std::byte{0x8E});
  EXPECT_EQ(buf.storage[2], std::byte{0x26});
}

TEST(ByteWriterTest, Leb128TooLargeForBufferFails) {
  buffer<1> buf;
  reloco::byte_writer w(buf.mut());
  EXPECT_FALSE(w.try_write_uleb128(300).has_value());
  EXPECT_EQ(w.position(), 0U);
}
