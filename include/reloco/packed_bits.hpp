// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <reloco/detail/assert.hpp>
#include <reloco/error.hpp>
#include <type_traits>

namespace reloco {

/**
 * @brief Descriptor for a specific field within a bit-packed integer.
 * @tparam Offset The starting bit index (0 is the least significant bit).
 * @tparam Bits The length of the bitfield.
 * @tparam IsSigned Whether the field represents a two's complement signed integer.
 */
template <std::size_t Offset, std::size_t Bits, bool IsSigned = false> struct bitfield {
  static_assert(Bits > 0, "Bitfield must have at least 1 bit");
  static constexpr std::size_t offset = Offset;
  static constexpr std::size_t bits = Bits;
  static constexpr bool is_signed = IsSigned;
};

/** @brief Convenience alias for a signed bitfield. */
template <std::size_t Offset, std::size_t Bits> using signed_bitfield = bitfield<Offset, Bits, true>;

namespace detail {

// Safely generates a bitmask without triggering Undefined Behavior if Bits == sizeof(T)*8
template <std::size_t Bits, typename T> [[nodiscard]] constexpr T generate_mask() noexcept {
  if constexpr (Bits >= sizeof(T) * 8) {
    return ~T(0);
  } else {
    return (T(1) << Bits) - T(1);
  }
}

} // namespace detail

/**
 * @brief A strictly typed, UB-free bitfield packer for hardware descriptors.
 * @tparam T The underlying unsigned integer type (e.g., uint32_t, uint64_t).
 */
template <typename T> class packed_bits {
  static_assert(std::is_unsigned_v<T>, "packed_bits requires an unsigned integer type");

public:
  using value_type = T;

  constexpr packed_bits() noexcept = default;
  constexpr explicit packed_bits(T val) noexcept : value_(val) {}

  /** @brief Returns the raw packed integer. */
  [[nodiscard]] constexpr T value() const noexcept { return value_; }

  /**
   * @brief Extracts a bitfield. Automatically sign-extends if BitField::is_signed is true.
   * Returns std::make_signed_t<T> for signed fields, and T for unsigned fields.
   */
  template <typename BitField> [[nodiscard]] constexpr auto get() const noexcept {
    constexpr std::size_t Offset = BitField::offset;
    constexpr std::size_t Bits = BitField::bits;
    static_assert(Offset + Bits <= sizeof(T) * 8, "Bitfield extraction out of bounds");

    constexpr T mask = detail::generate_mask<Bits, T>();
    T raw = (value_ >> Offset) & mask;

    if constexpr (BitField::is_signed) {
      using signed_T = std::make_signed_t<T>;
      constexpr T sign_bit = T(1) << (Bits - 1);

      // If the sign bit is set, extend the 1s all the way to the MSB
      if (raw & sign_bit) {
        raw |= ~mask;
      }
      return static_cast<signed_T>(raw);
    } else {
      return raw;
    }
  }

  /**
   * @brief Fallible insertion.
   * Returns error::out_of_range if the value would be truncated or loses its sign.
   */
  template <typename BitField, typename V>
  [[nodiscard]] RELOCO_CONSTEXPR20 result<void> try_set(V field_value) noexcept {
    constexpr std::size_t Offset = BitField::offset;
    constexpr std::size_t Bits = BitField::bits;
    static_assert(Offset + Bits <= sizeof(T) * 8, "Bitfield insertion out of bounds");

    constexpr T mask = detail::generate_mask<Bits, T>();
    T raw_insert = static_cast<T>(field_value) & mask;

    if constexpr (BitField::is_signed) {
      using signed_T = std::make_signed_t<T>;
      constexpr T sign_bit = T(1) << (Bits - 1);

      T extended = raw_insert;
      if (extended & sign_bit) {
        extended |= ~mask;
      }

      if (static_cast<signed_T>(extended) != static_cast<signed_T>(field_value)) {
        return unexpected(error::out_of_range);
      }
    } else {
      if constexpr (std::is_signed_v<V>) {
        if (field_value < 0)
          return unexpected(error::out_of_range);
      }
      if ((static_cast<T>(field_value) & ~mask) != 0) {
        return unexpected(error::out_of_range);
      }
    }

    value_ &= ~(mask << Offset);
    value_ |= (raw_insert << Offset);
    return {};
  }

  /**
   * @brief Infallible insertion.
   * Panics via RELOCO_ASSERT (fatal in release) if the value is truncated.
   */
  template <typename BitField, typename V> RELOCO_CONSTEXPR20 void set(V field_value) noexcept {
    auto res = try_set<BitField>(field_value);
    // Upgraded to a fatal RELOCO_ASSERT. Silent truncation in a hardware
    // descriptor is a catastrophic logic bug (and potential VM escape vector).
    RELOCO_ASSERT(res.has_value(), "Value truncated during bitfield insertion (exceeds capacity)");
  }

  /**
   * @brief Saturating insertion.
   * Clamps the value to the minimum/maximum representable range of the bitfield.
   */
  template <typename BitField, typename V> constexpr void saturating_set(V field_value) noexcept {
    constexpr std::size_t Offset = BitField::offset;
    constexpr std::size_t Bits = BitField::bits;
    static_assert(Offset + Bits <= sizeof(T) * 8, "Bitfield insertion out of bounds");

    constexpr T mask = detail::generate_mask<Bits, T>();
    T raw_insert = 0;

    if constexpr (BitField::is_signed) {
      using signed_T = std::make_signed_t<T>;
      signed_T min_val, max_val;
      if constexpr (Bits >= sizeof(T) * 8) {
        min_val = std::numeric_limits<signed_T>::min();
        max_val = std::numeric_limits<signed_T>::max();
      } else {
        max_val = (signed_T(1) << (Bits - 1)) - 1;
        min_val = -(signed_T(1) << (Bits - 1));
      }

      auto val = static_cast<signed_T>(field_value);
      if (val > max_val) {
        val = max_val;
      } else if (val < min_val) {
        val = min_val;
      }
      raw_insert = static_cast<T>(val) & mask;
    } else {
      T max_val = mask;
      T val = 0;
      if constexpr (std::is_signed_v<V>) {
        if (field_value < 0) {
          val = 0;
        } else {
          auto unsigned_val = static_cast<std::make_unsigned_t<V>>(field_value);
          val = (unsigned_val > max_val) ? max_val : static_cast<T>(unsigned_val);
        }
      } else {
        val = (field_value > max_val) ? max_val : static_cast<T>(field_value);
      }
      raw_insert = val & mask;
    }

    value_ &= ~(mask << Offset);
    value_ |= (raw_insert << Offset);
  }

  /**
   * @brief Truncating insertion.
   * Silently masks and fits the value into the bitfield width without assertions.
   */
  template <typename BitField, typename V> constexpr void truncating_set(V field_value) noexcept {
    constexpr std::size_t Offset = BitField::offset;
    constexpr std::size_t Bits = BitField::bits;
    static_assert(Offset + Bits <= sizeof(T) * 8, "Bitfield insertion out of bounds");

    constexpr T mask = detail::generate_mask<Bits, T>();
    T raw_insert = static_cast<T>(field_value) & mask;

    value_ &= ~(mask << Offset);
    value_ |= (raw_insert << Offset);
  }

  /** @brief Explicitly clear the packed structure. */
  constexpr void clear() noexcept { value_ = 0; }

private:
  T value_{0};
};

} // namespace reloco