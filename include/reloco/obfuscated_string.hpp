// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file obfuscated_string.hpp
 * @brief `RELOCO_OBFUSCATED_STR(literal)`: XOR-masks a string literal
 * entirely at compile time, so the plaintext never appears as a
 * contiguous byte sequence anywhere in the compiled binary -- only the
 * ciphertext (`obfuscated_string<N>`) is emitted into `.rodata`/`.data`,
 * and the plaintext is reconstituted into a short-lived, RAII-wiped
 * stack buffer (`obfuscated_string<N>::decrypted_view`) only at the
 * point of use.
 *
 * ## Threat model
 *
 * This is **obfuscation against static analysis (`strings(1)`, a
 * disassembler's string-xref view, a naive binary diff/signature scan),
 * not cryptographic secrecy**: the decode routine and the per-literal
 * XOR key both ship in the very same binary as the ciphertext, exactly
 * like `masked_byte_region.hpp`/`masked_pointer.hpp`'s "obfuscation at
 * rest" guarantees. An attacker willing to read the binary's code and
 * single-step the decode routine recovers the plaintext trivially; what
 * this header *does* guarantee is that the plaintext is never sitting in
 * a static data section for a mechanical scan to find, and that it is
 * zeroed out of the stack the moment `decrypted_view` goes out of scope
 * (see `wipe`-style volatile writes below, matching
 * `masked_byte_region::wipe()`).
 *
 * The per-literal key is derived from `__FILE__`/`__LINE__` (so two call
 * sites using the identical literal on different lines still get
 * independent ciphertexts) mixed with `__TIME__` (so the key -- and
 * therefore every
 * ciphertext -- changes on every rebuild, the same "weak but at least not
 * static" entropy tradeoff `masked_pointer.hpp`'s
 * `generate_weak_process_cookie()` makes). Unlike that process-wide
 * cookie, this key is a compile-time constant, not a runtime-generated
 * one: `obfuscated_string<N>` is a literal type with a `constexpr`
 * constructor, so `RELOCO_OBFUSCATED_STR` stores it in a function-local
 * `static constexpr` -- guaranteeing the encode step happens entirely in
 * the compiler's front end and never executes at runtime, and making
 * this header equally usable in a `RELOCO_KERNEL` build with no
 * once-per-process cookie initialization required at all.
 *
 * ## Usage and lifetime
 *
 * ```cpp
 * // Decrypts into a stack buffer; wiped at the end of the full
 * // expression, exactly like any other prvalue temporary's lifetime.
 * log_message(RELOCO_OBFUSCATED_STR("super-secret-marker").c_str());
 *
 * // Or keep the decoded view alive for a whole scope by naming it --
 * // never just its pointer/view, which would dangle once the unnamed
 * // temporary above is destroyed:
 * auto marker = RELOCO_OBFUSCATED_STR("super-secret-marker");
 * log_message(marker.c_str());
 * use_again(marker.view());
 * ```
 *
 * `c_str()`/`view()` are annotated `RELOCO_LIFETIMEBOUND`, so Clang's
 * `-Wdangling-gsl` flags the classic misuse of storing the *pointer*
 * returned from an unnamed `RELOCO_OBFUSCATED_STR(...)` temporary past
 * the end of its full expression -- the same lifetime-annotation
 * discipline every other reloco view type uses (see `lifetime.hpp`).
 */

#include "detail/assert.hpp"
#include "lifetime.hpp"
#include "string_view.hpp"

#include <cstddef>
#include <cstdint>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

namespace security {
namespace detail {

/**
 * @brief SplitMix64-style avalanche, identical in spirit to
 * `masked_pointer.hpp`'s `avalanche_mix`/`masked_byte_region.hpp`'s mask
 * mixing -- kept as an independent `constexpr` copy here so this header
 * remains a standalone, opt-in unit and so it can run at compile time.
 */
[[nodiscard]] constexpr uint64_t obfuscation_avalanche(uint64_t x) noexcept {
  x ^= (x >> 30);
  x *= UINT64_C(0xbf58476d1ce4e5b9);
  x ^= (x >> 27);
  x *= UINT64_C(0x94d049bb133111eb);
  x ^= (x >> 31);
  return x;
}

/**
 * @brief `constexpr` FNV-1a over a string, seeded with @p seed, used to
 * fold `__FILE__`/`__TIME__` into the per-literal compile-time key.
 */
[[nodiscard]] constexpr uint64_t obfuscation_fnv1a(const char *str, size_t len, uint64_t seed) noexcept {
  uint64_t hash = seed ^ UINT64_C(0xcbf29ce484222325);
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<unsigned char>(str[i]);
    hash *= UINT64_C(0x100000001b3);
  }
  return hash;
}

/**
 * @brief Derives the XOR mask for byte @p index of a literal encoded
 * under @p seed. Every index gets an independently-avalanched mask, so
 * identical plaintext bytes never produce identical ciphertext bytes.
 */
[[nodiscard]] constexpr uint8_t obfuscation_key_byte(uint64_t seed, size_t index) noexcept {
  uint64_t mixed = obfuscation_avalanche(seed ^ (static_cast<uint64_t>(index) * UINT64_C(0x9e3779b97f4a7c15)));
  return static_cast<uint8_t>(mixed & 0xFFU);
}

} // namespace detail
} // namespace security

/**
 * @brief Holds a `char[N]` string literal's contents (including the
 * trailing NUL) XOR-masked under a compile-time-only key. See the
 * file-level documentation above for this header's threat model; use
 * `RELOCO_OBFUSCATED_STR(...)` rather than naming this type directly.
 */
template <size_t N> class obfuscated_string {
  static_assert(N > 0, "obfuscated_string requires a NUL-terminated literal (char[N], N > 0)");

  uint8_t data_[N]{};
  uint64_t seed_;

public:
  /**
   * @brief RAII handle to a plaintext copy of an `obfuscated_string`,
   * decoded into an inline stack buffer and volatile-wiped on
   * destruction -- matching `masked_byte_region::wipe()`'s compiler-proof
   * clearing technique. Neither copyable nor movable, so a plaintext
   * copy can never silently outlive the scope that requested it.
   */
  class decrypted_view {
    char plain_[N]{};

    friend class obfuscated_string;

    explicit decrypted_view(const obfuscated_string &src) noexcept {
      for (size_t i = 0; i < N; ++i)
        plain_[i] = static_cast<char>(src.data_[i] ^ security::detail::obfuscation_key_byte(src.seed_, i));
    }

  public:
    decrypted_view(const decrypted_view &) = delete;
    decrypted_view &operator=(const decrypted_view &) = delete;
    decrypted_view(decrypted_view &&) = delete;
    decrypted_view &operator=(decrypted_view &&) = delete;

    ~decrypted_view() noexcept {
      volatile char *raw = plain_;
      for (size_t i = 0; i < N; ++i)
        raw[i] = '\0';
    }

    /** @brief NUL-terminated pointer to the decoded plaintext. */
    [[nodiscard]] const char *c_str() const noexcept RELOCO_LIFETIMEBOUND { return plain_; }

    /** @brief The decoded plaintext, excluding the trailing NUL. */
    [[nodiscard]] string_view view() const noexcept RELOCO_LIFETIMEBOUND { return string_view(plain_, N - 1); }

    /** @brief Length of the decoded plaintext, excluding the trailing NUL. */
    [[nodiscard]] static constexpr size_t size() noexcept { return N - 1; }
  };

  /**
   * @brief Encodes @p str under @p seed. Intended to run entirely at
   * compile time via a `static constexpr` local -- see
   * `RELOCO_OBFUSCATED_STR`.
   */
  constexpr obfuscated_string(const char (&str)[N], uint64_t seed) noexcept : seed_(seed) {
    for (size_t i = 0; i < N; ++i)
      data_[i] =
          static_cast<uint8_t>(static_cast<unsigned char>(str[i]) ^ security::detail::obfuscation_key_byte(seed, i));
  }

  /** @brief Decodes this literal into a freshly wiped stack buffer. */
  [[nodiscard]] decrypted_view decrypt() const noexcept { return decrypted_view(*this); }

  /** @brief Length of the encoded plaintext, excluding the trailing NUL. */
  [[nodiscard]] static constexpr size_t size() noexcept { return N - 1; }
};

template <size_t N> obfuscated_string(const char (&)[N], uint64_t) -> obfuscated_string<N>;

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE

/**
 * @def RELOCO_OBFUSCATED_STR(str_literal)
 * @brief Compile-time-encodes @p str_literal (a string literal) into an
 * `obfuscated_string<N>` and immediately decodes it, yielding a
 * `decrypted_view` prvalue. See the file-level documentation above for
 * the lifetime rules governing the result: use it directly within the
 * same full expression, or bind it to a named local to keep it alive for
 * a whole scope -- never just its `c_str()`/`view()` pointer, which
 * `RELOCO_LIFETIMEBOUND` will flag under `-Wdangling-gsl` if you try.
 */
#define RELOCO_OBFUSCATED_STR(str_literal)                                                                             \
  ([]() noexcept {                                                                                                     \
    static constexpr auto reloco_obfuscated_literal_ = ::reloco::obfuscated_string(                                    \
        str_literal, ::reloco::security::detail::obfuscation_fnv1a(__FILE__, sizeof(__FILE__) - 1,                     \
                                                                   static_cast<uint64_t>(__LINE__)) ^                  \
                         ::reloco::security::detail::obfuscation_fnv1a(__TIME__, sizeof(__TIME__) - 1, 0));            \
    return reloco_obfuscated_literal_.decrypt();                                                                       \
  }())
