// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file byte_cursor.hpp
 * @brief Bounds-checked sequential binary parser (`byte_reader`) and
 * serializer (`byte_writer`) over a caller-owned byte buffer.
 *
 * Both are tiny views (a span plus a position) with explicit byte order on
 * every integer access; nothing allocates and nothing is ever read or
 * written out of bounds. Every operation is all-or-nothing: on failure the
 * position is unchanged and (for the writer) the buffer is untouched.
 *
 * Failure reporting:
 *  - `error::out_of_bounds`: not enough bytes left (reader) / not enough room
 *    (writer), or a seek past the end.
 *  - `error::invalid_argument`: bad alignment (not a power of two).
 *  - `error::not_found`: `try_read_until` found no delimiter.
 *  - `error::integer_overflow`: a LEB128 value does not fit 64 bits.
 *
 * See `docs/byte-cursor.md` for a guide with examples, and `endian.hpp` for
 * the underlying conversions.
 */

#include "array.hpp"
#include "detail/assert.hpp"
#include "endian.hpp"
#include "error.hpp"
#include "lifetime.hpp"
#include "span.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

namespace detail {

constexpr bool is_power_of_two(std::size_t value) noexcept { return value != 0 && (value & (value - 1)) == 0; }

// Bytes needed to bring `position` up to a multiple of `alignment` (a power of two).
constexpr std::size_t alignment_padding(std::size_t position, std::size_t alignment) noexcept {
  return (alignment - (position & (alignment - 1))) & (alignment - 1);
}

} // namespace detail

/**
 * @brief Sequential reader over a read-only byte buffer.
 *
 * Does not own the bytes: the viewed buffer must outlive the reader and every
 * span it hands out. Copying a reader copies the position, which makes
 * speculative parsing trivial: copy, try, and keep the copy only on success.
 */
class RELOCO_POINTER byte_reader {
public:
  constexpr byte_reader() noexcept = default;

  /** @brief Reads from the start of `data`. */
  constexpr explicit byte_reader(span<const std::byte> data) noexcept : data_(data) {}

  /** @brief Total size of the viewed buffer. */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return data_.size(); }

  /** @brief Number of bytes consumed so far. */
  [[nodiscard]] constexpr std::size_t position() const noexcept { return pos_; }

  /** @brief Number of bytes left to read. */
  [[nodiscard]] constexpr std::size_t remaining() const noexcept { return data_.size() - pos_; }

  /** @brief True when every byte has been consumed. */
  [[nodiscard]] constexpr bool at_end() const noexcept { return pos_ == data_.size(); }

  /** @brief View of the bytes not yet consumed (the reader is not advanced). */
  [[nodiscard]] span<const std::byte> remaining_bytes() const noexcept { return tail(0, remaining()); }

  /** @brief View of the bytes already consumed. */
  [[nodiscard]] span<const std::byte> consumed_bytes() const noexcept {
    return span<const std::byte>(data_.data(), pos_);
  }

  /** @brief Moves to the absolute offset `offset` (`offset == size()` is allowed). */
  [[nodiscard]] result<void> try_seek(std::size_t offset) noexcept {
    if (offset > data_.size())
      return unexpected(error::out_of_bounds);
    pos_ = offset;
    return {};
  }

  /** @brief Skips `count` bytes. */
  [[nodiscard]] result<void> try_skip(std::size_t count) noexcept {
    if (count > remaining())
      return unexpected(error::out_of_bounds);
    pos_ += count;
    return {};
  }

  /**
   * @brief Skips to the next multiple of `alignment` (a power of two),
   * measured from the start of the buffer.
   */
  [[nodiscard]] result<void> try_align(std::size_t alignment) noexcept {
    if (!detail::is_power_of_two(alignment))
      return unexpected(error::invalid_argument);
    return try_skip(detail::alignment_padding(pos_, alignment));
  }

  /** @brief Reads an integer/enum in `Order` without consuming it. */
  template <typename T, byte_order Order> [[nodiscard]] result<T> try_peek() const noexcept {
    static_assert(is_wire_integer_v<T>, "byte_reader reads integers and enums only");
    if (remaining() < sizeof(T))
      return unexpected(error::out_of_bounds);
    return unsafe_load<T, Order>(tail(0, sizeof(T)));
  }

  /** @brief Reads and consumes an integer/enum in `Order`. */
  template <typename T, byte_order Order> [[nodiscard]] result<T> try_read() noexcept {
    auto value = try_peek<T, Order>();
    if (value)
      pos_ += sizeof(T);
    return value;
  }

  /** @brief `try_read<T, byte_order::little>()`. */
  template <typename T> [[nodiscard]] result<T> try_read_le() noexcept { return try_read<T, byte_order::little>(); }

  /** @brief `try_read<T, byte_order::big>()`. */
  template <typename T> [[nodiscard]] result<T> try_read_be() noexcept { return try_read<T, byte_order::big>(); }

  /** @brief `try_read<T, native_byte_order>()`: only for data that never leaves this machine. */
  template <typename T> [[nodiscard]] result<T> try_read_ne() noexcept { return try_read<T, native_byte_order>(); }

  /** @brief Reads one byte as `std::uint8_t`. */
  [[nodiscard]] result<std::uint8_t> try_read_u8() noexcept { return try_read<std::uint8_t, native_byte_order>(); }

  /**
   * @brief Reads without a bounds check beyond a debug assertion. The caller
   * must have verified `remaining() >= sizeof(T)`.
   */
  template <typename T, byte_order Order> [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T unsafe_read() noexcept {
    RELOCO_DEBUG_ASSERT(remaining() >= sizeof(T), "byte_reader::unsafe_read: not enough bytes");
    const T value = unsafe_load<T, Order>(tail(0, sizeof(T)));
    pos_ += sizeof(T);
    return value;
  }

  /** @brief Returns a view of the next `count` bytes and consumes them. */
  [[nodiscard]] result<span<const std::byte>> try_read_bytes(std::size_t count) noexcept {
    auto bytes = try_peek_bytes(count);
    if (bytes)
      pos_ += count;
    return bytes;
  }

  /** @brief Returns a view of the next `count` bytes without consuming them. */
  [[nodiscard]] result<span<const std::byte>> try_peek_bytes(std::size_t count) const noexcept {
    if (count > remaining())
      return unexpected(error::out_of_bounds);
    return tail(0, count);
  }

  /** @brief Copies the next `out.size()` bytes into `out` and consumes them. */
  [[nodiscard]] result<void> try_read_into(span<std::byte> out) noexcept {
    if (out.size() > remaining())
      return unexpected(error::out_of_bounds);
    for (std::size_t i = 0; i < out.size(); ++i)
      out.unsafe_at(i) = data_.unsafe_at(pos_ + i);
    pos_ += out.size();
    return {};
  }

  /**
   * @brief Consumes the next `count` bytes and returns a reader limited to
   * them. Use it for length-prefixed records: the sub-reader cannot run past
   * its record, and this reader continues after it.
   */
  [[nodiscard]] result<byte_reader> try_split(std::size_t count) noexcept {
    auto bytes = try_read_bytes(count);
    if (!bytes)
      return unexpected(bytes.error());
    return byte_reader(*bytes);
  }

  /**
   * @brief Reads up to the first `delimiter` byte. Returns the bytes before
   * it and consumes the delimiter too. `error::not_found` (nothing consumed)
   * if it does not occur. With `std::byte{0}` this reads C strings.
   */
  [[nodiscard]] result<span<const std::byte>> try_read_until(std::byte delimiter) noexcept {
    for (std::size_t i = pos_; i < data_.size(); ++i) {
      if (data_.unsafe_at(i) == delimiter) {
        const span<const std::byte> found(data_.data() + pos_, i - pos_);
        pos_ = i + 1;
        return found;
      }
    }
    return unexpected(error::not_found);
  }

  /** @brief Reads an unsigned LEB128 (varint) value of at most 64 bits. */
  [[nodiscard]] result<std::uint64_t> try_read_uleb128() noexcept {
    std::size_t cursor = pos_;
    std::uint64_t value = 0;
    for (unsigned shift = 0;; shift += 7) {
      if (cursor >= data_.size())
        return unexpected(error::out_of_bounds);
      const auto byte = static_cast<std::uint64_t>(std::to_integer<unsigned>(data_.unsafe_at(cursor++)));
      const std::uint64_t payload = byte & 0x7FU;
      // The 10th byte may only carry the single remaining bit.
      if (shift >= 64 || (shift == 63 && payload > 1))
        return unexpected(error::integer_overflow);
      value |= payload << shift;
      if ((byte & 0x80U) == 0)
        break;
    }
    pos_ = cursor;
    return value;
  }

  /** @brief Reads a signed LEB128 value of at most 64 bits. */
  [[nodiscard]] result<std::int64_t> try_read_sleb128() noexcept {
    std::size_t cursor = pos_;
    std::uint64_t value = 0;
    unsigned shift = 0;
    std::uint64_t byte = 0;
    do {
      if (cursor >= data_.size())
        return unexpected(error::out_of_bounds);
      if (shift >= 64)
        return unexpected(error::integer_overflow);
      byte = static_cast<std::uint64_t>(std::to_integer<unsigned>(data_.unsafe_at(cursor++)));
      const std::uint64_t payload = byte & 0x7FU;
      // The 10th byte holds the sign bit and must be a plain sign extension (0x00 or 0x7F).
      if (shift == 63 && payload != 0 && payload != 0x7FU)
        return unexpected(error::integer_overflow);
      value |= payload << shift;
      shift += 7;
    } while ((byte & 0x80U) != 0);
    if (shift < 64 && (byte & 0x40U) != 0)
      value |= ~std::uint64_t{0} << shift;
    pos_ = cursor;
    // NOLINTNEXTLINE(reloco-narrowing-flow): two's-complement reinterpretation of the decoded bits.
    return static_cast<std::int64_t>(value);
  }

private:
  [[nodiscard]] span<const std::byte> tail(std::size_t skip, std::size_t count) const noexcept {
    return span<const std::byte>(data_.data() + pos_ + skip, count);
  }

  span<const std::byte> data_;
  std::size_t pos_{0};
};

/**
 * @brief Sequential writer into a caller-owned, fixed-size byte buffer.
 *
 * Nothing grows: a write that does not fit fails with `error::out_of_bounds`
 * and leaves both the position and the buffer unchanged. The buffer must
 * outlive the writer.
 */
class RELOCO_POINTER byte_writer {
public:
  constexpr byte_writer() noexcept = default;

  /** @brief Writes from the start of `buffer`. */
  constexpr explicit byte_writer(span<std::byte> buffer) noexcept : data_(buffer) {}

  /** @brief Size of the underlying buffer. */
  [[nodiscard]] constexpr std::size_t capacity() const noexcept { return data_.size(); }

  /** @brief Number of bytes written so far. */
  [[nodiscard]] constexpr std::size_t position() const noexcept { return pos_; }

  /** @brief Room left in the buffer. */
  [[nodiscard]] constexpr std::size_t remaining() const noexcept { return data_.size() - pos_; }

  /** @brief The bytes written so far. */
  [[nodiscard]] span<const std::byte> written() const noexcept { return span<const std::byte>(data_.data(), pos_); }

  /** @brief Forgets everything written (the buffer contents are not cleared). */
  constexpr void reset() noexcept { pos_ = 0; }

  /** @brief Writes an integer/enum in `Order`. */
  template <byte_order Order, typename T> [[nodiscard]] result<void> try_write(T value) noexcept {
    static_assert(is_wire_integer_v<T>, "byte_writer writes integers and enums only");
    if (remaining() < sizeof(T))
      return unexpected(error::out_of_bounds);
    unsafe_store<Order>(span<std::byte>(data_.data() + pos_, sizeof(T)), value);
    pos_ += sizeof(T);
    return {};
  }

  /** @brief `try_write<byte_order::little>(value)`. */
  template <typename T> [[nodiscard]] result<void> try_write_le(T value) noexcept {
    return try_write<byte_order::little>(value);
  }

  /** @brief `try_write<byte_order::big>(value)`. */
  template <typename T> [[nodiscard]] result<void> try_write_be(T value) noexcept {
    return try_write<byte_order::big>(value);
  }

  /** @brief `try_write<native_byte_order>(value)`: only for data that never leaves this machine. */
  template <typename T> [[nodiscard]] result<void> try_write_ne(T value) noexcept {
    return try_write<native_byte_order>(value);
  }

  /** @brief Writes one byte. */
  [[nodiscard]] result<void> try_write_u8(std::uint8_t value) noexcept { return try_write<native_byte_order>(value); }

  /** @brief Appends a copy of `bytes`. */
  [[nodiscard]] result<void> try_write_bytes(span<const std::byte> bytes) noexcept {
    if (bytes.size() > remaining())
      return unexpected(error::out_of_bounds);
    for (std::size_t i = 0; i < bytes.size(); ++i)
      data_.unsafe_at(pos_ + i) = bytes.unsafe_at(i);
    pos_ += bytes.size();
    return {};
  }

  /** @brief Appends `count` copies of `value`. */
  [[nodiscard]] result<void> try_fill(std::byte value, std::size_t count) noexcept {
    if (count > remaining())
      return unexpected(error::out_of_bounds);
    for (std::size_t i = 0; i < count; ++i)
      data_.unsafe_at(pos_ + i) = value;
    pos_ += count;
    return {};
  }

  /** @brief Appends `count` zero bytes. */
  [[nodiscard]] result<void> try_write_zeros(std::size_t count) noexcept { return try_fill(std::byte{0}, count); }

  /** @brief Pads with zeros up to the next multiple of `alignment` (a power of two), measured from the buffer start. */
  [[nodiscard]] result<void> try_align(std::size_t alignment) noexcept {
    if (!detail::is_power_of_two(alignment))
      return unexpected(error::invalid_argument);
    return try_write_zeros(detail::alignment_padding(pos_, alignment));
  }

  /**
   * @brief Reserves `count` zeroed bytes and returns their starting offset,
   * to be filled in later with `try_patch` (e.g. a length prefix that is only
   * known once the body has been written).
   */
  [[nodiscard]] result<std::size_t> try_reserve(std::size_t count) noexcept {
    const std::size_t offset = pos_;
    auto zeroed = try_write_zeros(count);
    if (!zeroed)
      return unexpected(zeroed.error());
    return offset;
  }

  /** @brief Overwrites already-written bytes at `offset` with an integer/enum in `Order`; the position is unchanged. */
  template <byte_order Order, typename T> [[nodiscard]] result<void> try_patch(std::size_t offset, T value) noexcept {
    static_assert(is_wire_integer_v<T>, "byte_writer writes integers and enums only");
    if (offset > pos_ || pos_ - offset < sizeof(T))
      return unexpected(error::out_of_bounds);
    unsafe_store<Order>(span<std::byte>(data_.data() + offset, sizeof(T)), value);
    return {};
  }

  /** @brief Appends `value` as an unsigned LEB128 (varint). */
  [[nodiscard]] result<void> try_write_uleb128(std::uint64_t value) noexcept {
    array<std::byte, 10> encoded{};
    std::size_t length = 0;
    do {
      auto chunk = static_cast<unsigned>(value & 0x7FU);
      value >>= 7;
      if (value != 0)
        chunk |= 0x80U;
      encoded[length++] = static_cast<std::byte>(chunk & 0xFFU);
    } while (value != 0);
    return try_write_bytes(span<const std::byte>(encoded.data(), length));
  }

  /** @brief Appends `value` as a signed LEB128. */
  [[nodiscard]] result<void> try_write_sleb128(std::int64_t value) noexcept {
    array<std::byte, 10> encoded{};
    std::size_t length = 0;
    bool more = true;
    while (more) {
      // NOLINTNEXTLINE(reloco-narrowing-flow): two's-complement reinterpretation to read the low bits.
      auto chunk = static_cast<unsigned>(static_cast<std::uint64_t>(value) & 0x7FU);
      value >>= 7; // arithmetic shift: keeps the sign
      const bool sign_bit_set = (chunk & 0x40U) != 0;
      more = !((value == 0 && !sign_bit_set) || (value == -1 && sign_bit_set));
      if (more)
        chunk |= 0x80U;
      encoded[length++] = static_cast<std::byte>(chunk & 0xFFU);
    }
    return try_write_bytes(span<const std::byte>(encoded.data(), length));
  }

private:
  span<std::byte> data_;
  std::size_t pos_{0};
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
