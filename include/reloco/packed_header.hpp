// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file packed_header.hpp
 * @brief Bounds-checked byte writer/reader with LEB128 varints, the building blocks of a *packed frame header
 * codec* for the ring buffer's packed-frame API (`try_write_packed_frame_evicting`,
 * `try_consume_packed_frame`, `try_peek_packed_frame`).
 *
 * A trivially-copyable header struct (the plain frame API) always costs `sizeof(Header)` bytes, padding and
 * wide fields included. A packed header spends one byte on small values and still round-trips any 64-bit one.
 * The ring does not know the encoding: you describe it with a *codec*, a stateless type with:
 *
 * @code
 * struct my_codec {
 *   using header_type = my_header;                       // decoded, in-memory form (any type)
 *   static constexpr std::size_t max_header_size = 24;   // upper bound of the encoded size in bytes
 *
 *   // Writes the header. Fields are written in a fixed order; a failed write leaves w.ok() false.
 *   static void encode(const my_header &h, reloco::packed_writer &w) noexcept {
 *     w.put_u8(0xA5);            // sync byte, lets decode() reject garbage
 *     w.put_varint(h.payload_len);
 *     w.put_varint(h.id);
 *   }
 *
 *   // Reads the header back in the same order. Return false for an invalid header; running out of input is
 *   // tracked by the reader itself (r.underflow()), the ring uses it to tell "not all here yet" from "corrupt".
 *   static bool decode(reloco::packed_reader &r, my_header &h) noexcept {
 *     if (r.get_u8() != 0xA5) return false;
 *     h.payload_len = r.get_varint();
 *     h.id = r.get_varint();
 *     return r.ok();
 *   }
 *
 *   // Total frame size (encoded header + everything after it) for a header whose encoding took 'header_bytes'.
 *   // The ring uses it to skip whole frames when evicting and to know when a frame has fully arrived.
 *   static std::size_t frame_size(const my_header &h, std::size_t header_bytes) noexcept {
 *     return header_bytes + h.payload_len;
 *   }
 * };
 * @endcode
 */

#include "span.hpp"
#include <cstddef>
#include <cstdint>

namespace reloco {

/** @brief Appends bytes and LEB128 varints to a fixed span; never writes past its end. */
class RELOCO_POINTER packed_writer {
public:
  constexpr explicit packed_writer(span<char> out) noexcept : out_(out) {}

  /** @brief Appends one byte. */
  constexpr void put_u8(std::uint8_t v) noexcept {
    if (pos_ >= out_.size()) {
      ok_ = false;
      return;
    }
    // The check above bounds `pos_`; GCC 15 loses it after unrolling the varint loops and warns at -O3.
    RELOCO_BEGIN_SUPPRESS_GCC_BOUNDS_FALSE_POSITIVE;
    out_[pos_++] = static_cast<char>(v);
    RELOCO_END_SUPPRESS_GCC_BOUNDS_FALSE_POSITIVE;
  }

  /** @brief Appends @p v as an unsigned LEB128 varint: 1 byte below 128, up to 10 bytes for 64-bit values. */
  constexpr void put_varint(std::uint64_t v) noexcept {
    while (v >= 0x80U) {
      put_u8(static_cast<std::uint8_t>(0x80U + (v & 0x7FU)));
      v >>= 7;
    }
    put_u8(static_cast<std::uint8_t>(v & 0x7FU));
  }

  /** @brief Appends @p v zigzag-mapped, so small negative numbers stay short too. */
  constexpr void put_svarint(std::int64_t v) noexcept {
    // Two's-complement reinterpretation is the zigzag definition; no value is lost.
    // NOLINTNEXTLINE(reloco-narrowing-flow)
    put_varint((static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63));
  }

  /** @brief Bytes written so far. */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return pos_; }
  /** @brief False once a write did not fit. */
  [[nodiscard]] constexpr bool ok() const noexcept { return ok_; }

private:
  span<char> out_;
  std::size_t pos_{0};
  bool ok_{true};
};

/** @brief Reads bytes and LEB128 varints from a fixed span; never reads past its end. */
class RELOCO_POINTER packed_reader {
public:
  constexpr explicit packed_reader(span<const char> in) noexcept : in_(in) {}

  /** @brief Reads one byte (0 and `underflow()` if the input is exhausted). */
  [[nodiscard]] constexpr std::uint8_t get_u8() noexcept {
    if (pos_ >= in_.size()) {
      underflow_ = true;
      return 0;
    }
    return static_cast<std::uint8_t>(in_[pos_++]);
  }

  /** @brief Reads an unsigned LEB128 varint. Over-long (more than 10 byte) encodings set `invalid()`. */
  [[nodiscard]] constexpr std::uint64_t get_varint() noexcept {
    std::uint64_t v = 0;
    for (unsigned shift = 0; shift < 70; shift += 7) {
      const std::uint8_t b = get_u8();
      if (underflow_) {
        return 0;
      }
      v |= static_cast<std::uint64_t>(b & 0x7FU) << shift;
      if ((b & 0x80U) == 0) {
        return v;
      }
    }
    invalid_ = true;
    return 0;
  }

  /** @brief Reads a zigzag-mapped signed varint. */
  [[nodiscard]] constexpr std::int64_t get_svarint() noexcept {
    const std::uint64_t u = get_varint();
    return static_cast<std::int64_t>(u >> 1) ^ -static_cast<std::int64_t>(u & 1U);
  }

  /** @brief Marks the header invalid (e.g. a field out of range); `ok()` becomes false. */
  constexpr void set_invalid() noexcept { invalid_ = true; }

  /** @brief Bytes consumed so far. */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return pos_; }
  /** @brief True if a read ran past the end of the input (more bytes might complete the header). */
  [[nodiscard]] constexpr bool underflow() const noexcept { return underflow_; }
  /** @brief True if the bytes read so far are malformed. */
  [[nodiscard]] constexpr bool invalid() const noexcept { return invalid_; }
  /** @brief True while neither underflow nor invalid data occurred. */
  [[nodiscard]] constexpr bool ok() const noexcept { return !underflow_ && !invalid_; }

private:
  span<const char> in_;
  std::size_t pos_{0};
  bool underflow_{false};
  bool invalid_{false};
};

} // namespace reloco
