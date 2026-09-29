// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file fdt_format.hpp
 * @brief Shared Flattened Device Tree (DTB, `/dts-v1/`) binary-format
 * constants and big-endian codec primitives used by both
 * `reloco/fdt_writer.hpp` and `reloco/fdt_reader.hpp`, so the two never
 * drift apart on token IDs, header layout, or byte order.
 *
 * All multi-byte fields are big-endian, matching the DTB spec. No
 * floating point is used anywhere in this file.
 */

#include "../lifetime.hpp"
#include <cstddef>
#include <cstdint>

namespace reloco::fdt {

// This header writes/reads big-endian fields through raw std::byte*
// pointer arithmetic throughout; treated as a single checked boundary
// like reloco/bytes.hpp and reloco/string_view.hpp (see fdt_writer.hpp
// and fdt_reader.hpp, which both wrap their own callers of these
// functions in the same pragma pair -- redundant but harmless nesting).
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

inline constexpr uint32_t magic = 0xd00dfeedu;
inline constexpr uint32_t version = 17u;
inline constexpr uint32_t last_comp_version = 16u;

namespace detail {

inline constexpr uint32_t token_begin_node = 1u;
inline constexpr uint32_t token_end_node = 2u;
inline constexpr uint32_t token_prop = 3u;
inline constexpr uint32_t token_nop = 4u;
inline constexpr uint32_t token_end = 9u;

inline constexpr std::size_t header_size = 40; // 10 big-endian uint32 fields.

[[nodiscard]] inline constexpr std::size_t align4(std::size_t n) noexcept { return (n + 3u) & ~static_cast<std::size_t>(3u); }

inline void store_be32(std::byte *p, uint32_t v) noexcept {
  p[0] = static_cast<std::byte>((v >> 24) & 0xffu);
  p[1] = static_cast<std::byte>((v >> 16) & 0xffu);
  p[2] = static_cast<std::byte>((v >> 8) & 0xffu);
  p[3] = static_cast<std::byte>(v & 0xffu);
}

inline void store_be64(std::byte *p, uint64_t v) noexcept {
  for (std::size_t i = 0; i < 8; ++i)
    p[i] = static_cast<std::byte>((v >> (8u * (7u - i))) & 0xffu);
}

[[nodiscard]] inline uint32_t load_be32(const std::byte *p) noexcept {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

[[nodiscard]] inline uint64_t load_be64(const std::byte *p) noexcept {
  uint64_t v = 0;
  for (std::size_t i = 0; i < 8; ++i)
    v = (v << 8) | static_cast<uint64_t>(p[i]);
  return v;
}

} // namespace detail

RELOCO_END_UNSAFE_BUFFER_USAGE

} // namespace reloco::fdt
