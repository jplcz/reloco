// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file endian.hpp
 * @brief Byte order tags and portable integer <-> byte conversions.
 *
 * Integers are assembled from / split into individual bytes with shifts, so
 * there is no `memcpy`, no type punning and no alignment requirement; the
 * compilers turn the loops into single loads/stores (and `bswap`) at `-O1`
 * and above. Only integers and enums are supported (floating point is
 * deliberately not: convert the bit pattern yourself, reloco does no
 * floating point computation).
 *
 * See `docs/endian.md` and `byte_cursor.hpp` for the sequential reader/writer
 * built on these functions.
 */

#include "byte_like.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "span.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/** @brief Byte order of a multi-byte integer in memory or on the wire. */
enum class byte_order : unsigned char {
  little,
  big,
};

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && defined(__ORDER_LITTLE_ENDIAN__)
inline constexpr byte_order native_byte_order =
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ ? byte_order::big : byte_order::little;
#elif defined(_MSC_VER)
inline constexpr byte_order native_byte_order = byte_order::little;
#else
#error "reloco/endian.hpp: cannot determine the native byte order"
#endif

/** @brief Network byte order (big endian). */
inline constexpr byte_order network_byte_order = byte_order::big;

namespace detail {

template <typename T, bool = std::is_enum_v<T>> struct wire_unsigned {
  using type = std::make_unsigned_t<std::underlying_type_t<T>>;
};

template <typename T> struct wire_unsigned<T, false> {
  using type = std::make_unsigned_t<T>;
};

} // namespace detail

/**
 * @brief True for the types the endian helpers and the byte cursors can
 * encode: every integer type except `bool`, and enums (via their underlying
 * type).
 */
template <typename T>
inline constexpr bool is_wire_integer_v =
    (std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>) || std::is_enum_v<T>;

/** @brief Reverses the bytes of an integer (or enum). Constant-evaluable. */
template <typename T> [[nodiscard]] constexpr T byteswap(T value) noexcept {
  static_assert(is_wire_integer_v<T>, "byteswap requires an integer or enum type");
  using U = typename detail::wire_unsigned<T>::type;
  U in = static_cast<U>(value);
  U out = 0;
  for (std::size_t i = 0; i < sizeof(U); ++i) {
    out = static_cast<U>(static_cast<U>(out << 8) | static_cast<U>(in & 0xFFU));
    in = static_cast<U>(in >> 8);
  }
  return static_cast<T>(out);
}

/** @brief Converts a host-order value to `Order` (a no-op if they match). */
template <byte_order Order, typename T> [[nodiscard]] constexpr T to_byte_order(T value) noexcept {
  if constexpr (Order == native_byte_order)
    return value;
  else
    return byteswap(value);
}

/** @brief Converts a value that is in `Order` to host order. Same operation as `to_byte_order`. */
template <byte_order Order, typename T> [[nodiscard]] constexpr T from_byte_order(T value) noexcept {
  return to_byte_order<Order>(value);
}

namespace detail {

// Bit position of the byte stored at stream index `index` for `Order`.
template <byte_order Order, std::size_t Size> constexpr std::size_t wire_shift(std::size_t index) noexcept {
  return 8 * (Order == byte_order::little ? index : Size - 1 - index);
}

} // namespace detail

/**
 * @brief Reads a `T` from the first `sizeof(T)` bytes of `bytes`, interpreted
 * in `Order`. Debug-only size check: the caller must already have verified
 * `bytes.size() >= sizeof(T)` (prefer `try_load`).
 */
template <typename T, byte_order Order>
[[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T unsafe_load(span<const std::byte> bytes) noexcept {
  static_assert(is_wire_integer_v<T>, "unsafe_load requires an integer or enum type");
  RELOCO_DEBUG_ASSERT(bytes.size() >= sizeof(T), "unsafe_load: not enough bytes");
  using U = typename detail::wire_unsigned<T>::type;
  U value = 0;
  for (std::size_t i = 0; i < sizeof(U); ++i) {
    const U byte = static_cast<U>(std::to_integer<unsigned>(bytes.unsafe_at(i)));
    value = static_cast<U>(value | static_cast<U>(byte << detail::wire_shift<Order, sizeof(U)>(i)));
  }
  return static_cast<T>(value);
}

/**
 * @brief Writes `value` into the first `sizeof(T)` bytes of `bytes` in
 * `Order`. Debug-only size check (prefer `try_store`).
 */
template <byte_order Order, typename T>
RELOCO_UNSAFE_BUFFER_USAGE void unsafe_store(span<std::byte> bytes, T value) noexcept {
  static_assert(is_wire_integer_v<T>, "unsafe_store requires an integer or enum type");
  RELOCO_DEBUG_ASSERT(bytes.size() >= sizeof(T), "unsafe_store: not enough room");
  using U = typename detail::wire_unsigned<T>::type;
  const U raw = static_cast<U>(value);
  for (std::size_t i = 0; i < sizeof(U); ++i)
    bytes.unsafe_at(i) =
        static_cast<std::byte>(static_cast<unsigned char>((raw >> detail::wire_shift<Order, sizeof(U)>(i)) & 0xFFU));
}

/** @brief Checked `unsafe_load`: `error::out_of_bounds` if `bytes` is shorter than `sizeof(T)`. */
template <typename T, byte_order Order> [[nodiscard]] result<T> try_load(span<const std::byte> bytes) noexcept {
  if (bytes.size() < sizeof(T))
    return unexpected(error::out_of_bounds);
  return unsafe_load<T, Order>(bytes);
}

/** @brief Checked `unsafe_store`: `error::out_of_bounds` (and nothing written) if `bytes` is shorter than `sizeof(T)`.
 */
template <byte_order Order, typename T> [[nodiscard]] result<void> try_store(span<std::byte> bytes, T value) noexcept {
  if (bytes.size() < sizeof(T))
    return unexpected(error::out_of_bounds);
  unsafe_store<Order>(bytes, value);
  return {};
}

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
