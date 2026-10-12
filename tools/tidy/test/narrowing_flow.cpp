// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-narrowing-flow
#include <cstdint>

std::uint8_t ok_mask(std::uint32_t x) noexcept { return static_cast<std::uint8_t>(x & 0xFFu); }
std::uint8_t ok_mod(std::uint32_t x) noexcept { return static_cast<std::uint8_t>(x % 256u); }
std::uint8_t ok_shift(std::uint32_t x) noexcept { return static_cast<std::uint8_t>(x >> 24); }
std::uint8_t ok_constant() noexcept { return static_cast<std::uint8_t>(200); }
std::uint64_t ok_widen(std::uint32_t x) noexcept { return static_cast<std::uint64_t>(x); }
std::uint32_t ok_small(std::uint8_t a, std::uint8_t b) noexcept { return static_cast<std::uint32_t>(a + b); }
std::uint8_t ok_bytes(char c) noexcept { return static_cast<std::uint8_t>(c); }
std::int16_t ok_clamped(std::int32_t x) noexcept { return static_cast<std::int16_t>(x % 1000); }

std::uint8_t bad_truncate(std::uint32_t x) noexcept { return static_cast<std::uint8_t>(x); }  // expect: reloco-narrowing-flow
std::uint32_t bad_sign(std::int32_t x) noexcept { return static_cast<std::uint32_t>(x); }  // expect: reloco-narrowing-flow
std::int32_t bad_unsigned_big(std::uint64_t x) noexcept { return static_cast<std::int32_t>(x >> 8); }  // expect: reloco-narrowing-flow
std::uint8_t bad_arith(std::uint8_t a, std::uint8_t b) noexcept { return static_cast<std::uint8_t>(a + b); }  // expect: reloco-narrowing-flow
std::uint16_t suppressed(std::uint32_t x) noexcept { return static_cast<std::uint16_t>(x); }  // NOLINT(reloco-narrowing-flow)
// An all-ones constant is a deliberate mask, not a narrowing.
std::uint32_t all_ones() noexcept { return static_cast<std::uint32_t>(-1); }
