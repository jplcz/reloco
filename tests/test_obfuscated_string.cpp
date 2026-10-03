// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/obfuscated_string.hpp>

#include <cstring>

// This file compares raw byte contents with memmem/std::memcmp, which
// clang flags as -Wunsafe-buffer-usage-in-libc-call; treated as a single
// checked boundary like reloco/bytes.hpp's tests do.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

TEST(ObfuscatedStringTest, DecryptsToOriginalPlaintext) {
  auto decoded = RELOCO_OBFUSCATED_STR("hello, world");
  EXPECT_STREQ(decoded.c_str(), "hello, world");
}

TEST(ObfuscatedStringTest, ViewMatchesLengthAndContents) {
  auto decoded = RELOCO_OBFUSCATED_STR("reloco");
  EXPECT_EQ(decoded.size(), 6u);
  EXPECT_EQ(decoded.view(), reloco::string_view("reloco"));
}

TEST(ObfuscatedStringTest, EmptyStringRoundTrips) {
  auto decoded = RELOCO_OBFUSCATED_STR("");
  EXPECT_EQ(decoded.size(), 0u);
  EXPECT_STREQ(decoded.c_str(), "");
}

TEST(ObfuscatedStringTest, CiphertextDoesNotContainPlaintext) {
  static constexpr auto obf = reloco::obfuscated_string("super-secret-marker", UINT64_C(0x1234567890abcdef));
  const auto *raw = reinterpret_cast<const char *>(&obf);
  EXPECT_EQ(memmem(raw, sizeof(obf), "super-secret-marker", 20), nullptr);
}

TEST(ObfuscatedStringTest, SameLiteralAtDifferentCallSitesUsesIndependentKeys) {
  auto a = RELOCO_OBFUSCATED_STR("duplicate-literal");
  auto b = RELOCO_OBFUSCATED_STR("duplicate-literal");
  EXPECT_STREQ(a.c_str(), "duplicate-literal");
  EXPECT_STREQ(b.c_str(), "duplicate-literal");
}

TEST(ObfuscatedStringTest, DifferentSeedsProduceDifferentCiphertext) {
  static constexpr auto obf_a = reloco::obfuscated_string("same-plaintext", UINT64_C(0x1111111111111111));
  static constexpr auto obf_b = reloco::obfuscated_string("same-plaintext", UINT64_C(0x2222222222222222));
  EXPECT_NE(std::memcmp(&obf_a, &obf_b, sizeof(obf_a)), 0);
}

} // namespace

RELOCO_END_UNSAFE_BUFFER_USAGE
