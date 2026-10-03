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
 * therefore every ciphertext -- changes on every rebuild, the same "weak
 * but at least not static" entropy tradeoff `masked_pointer.hpp`'s
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
 *
 * ## Type erasure: declaring these in headers, without `N`
 *
 * `obfuscated_string<N>` cannot be `extern`-declared in a header without
 * committing to a specific `N` at the declaration site, which defeats the
 * point of a header-visible global. `obfuscated_string_ref` is a
 * non-template, type-erased handle (ciphertext pointer + encoded size +
 * key) for exactly this case -- a single, concrete type that can be
 * forward-declared anywhere, `extern`-declared in a header, and defined
 * once (still backed by an internal-linkage `obfuscated_string<N>`
 * constant, still emitted into `.rodata`):
 *
 * ```cpp
 * // some_header.hpp
 * RELOCO_DECLARE_OBFUSCATED_STR(g_license_marker);
 *
 * // some_header.cpp
 * RELOCO_DEFINE_OBFUSCATED_STR(g_license_marker, "super-secret-marker");
 * ```
 *
 * Every other translation unit including the header sees only
 * `g_license_marker`'s type, `obfuscated_string_ref` -- never the
 * template, never `N` -- so no per-literal template instantiation leaks
 * across translation units; it happens exactly once, where the macro is
 * defined. See `obfuscated_string_ref::for_each_byte()` for how to read
 * it back without ever storing the plaintext contiguously.
 */

#include "detail/assert.hpp"
#include "lifetime.hpp"
#include "string_view.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

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

template <size_t N> class obfuscated_string;
class obfuscated_string_ref;

/**
 * @brief RAII handle to a plaintext copy of an `obfuscated_string<N>`,
 * decoded into an inline stack buffer and volatile-wiped on destruction --
 * matching `masked_byte_region::wipe()`'s compiler-proof clearing
 * technique. Neither copyable nor movable, so a plaintext copy can never
 * silently outlive the scope that requested it.
 *
 * A top-level (not nested-in-`obfuscated_string<N>`) class template
 * specifically so consumers (e.g. `microfmt::formatter<obfuscated_decrypted_view<N>>`)
 * can partial-specialize templates over it: a nested-name-specifier
 * dependent on a template parameter (`obfuscated_string<N>::decrypted_view`)
 * is a non-deduced context ([temp.deduct.type]), so `N` could never be
 * recovered from that spelling alone. `obfuscated_string<N>::decrypted_view`
 * remains a member-typedef alias of this type for source compatibility.
 */
template <size_t N> class obfuscated_decrypted_view {
  char plain_[N]{};

  friend class obfuscated_string<N>;

  explicit obfuscated_decrypted_view(const obfuscated_string<N> &src) noexcept;

public:
  obfuscated_decrypted_view(const obfuscated_decrypted_view &) = delete;
  obfuscated_decrypted_view &operator=(const obfuscated_decrypted_view &) = delete;
  obfuscated_decrypted_view(obfuscated_decrypted_view &&) = delete;
  obfuscated_decrypted_view &operator=(obfuscated_decrypted_view &&) = delete;

  ~obfuscated_decrypted_view() noexcept {
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
 * @brief Non-template, type-erased reference to an `obfuscated_string<N>`'s
 * ciphertext and key -- exactly the cases a template parameter `N` can't
 * reach: an `extern`-declared global in a header (see
 * `RELOCO_DECLARE_OBFUSCATED_STR`/`RELOCO_DEFINE_OBFUSCATED_STR` below), a
 * homogeneous array/container of differently-sized obfuscated strings, or
 * an API boundary that should not be templated at all.
 *
 * `for_each_byte()` is the "no storage" decode entry point: it decodes and
 * visits one plaintext byte at a time, computed on the fly through the
 * same volatile-read technique `obfuscated_decrypted_view`'s constructor
 * uses, and never materializes the plaintext as a contiguous buffer
 * anywhere -- see `microfmt::formatter<reloco::obfuscated_string_ref>` in
 * the microfmt project for the motivating use case (streaming straight to
 * a format sink, one byte at a time).
 *
 * Like `string_view`, this type only ever holds a *pointer* to the
 * ciphertext, so the referenced `obfuscated_string<N>` must outlive it --
 * in practice, a `static`/namespace-scope constant, never a local
 * temporary.
 */
class obfuscated_string_ref {
  const uint8_t *data_{};
  size_t encoded_size_{};
  uint64_t seed_{};

public:
  constexpr obfuscated_string_ref() noexcept = default;

  /** @brief Type-erases @p src. Defined out-of-line below, once `obfuscated_string<N>` is complete. */
  template <size_t N> constexpr obfuscated_string_ref(const obfuscated_string<N> &src) noexcept;

  /** @brief Length of the decoded plaintext, excluding the trailing NUL. */
  [[nodiscard]] constexpr size_t size() const noexcept { return encoded_size_ == 0 ? 0 : encoded_size_ - 1; }

  /**
   * @brief Decodes and visits each plaintext byte in turn, as `visit(char)`,
   * without ever storing more than one byte at a time -- see the class
   * documentation above. The trailing NUL is not visited.
   */
  template <typename Visitor> void for_each_byte(Visitor &&visit) const noexcept {
    // Same "defeat the optimizer" rationale as
    // `obfuscated_decrypted_view`'s constructor below: a volatile access is
    // an observable side effect the compiler may not presume a value for
    // or elide, forcing a genuine runtime load-XOR-visit every time.
    const volatile uint8_t *ciphertext = data_;
    const size_t n = size();
    for (size_t i = 0; i < n; ++i)
      visit(static_cast<char>(ciphertext[i] ^ security::detail::obfuscation_key_byte(seed_, i)));
  }
};

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

  friend class obfuscated_decrypted_view<N>;

public:
  /** @brief Member alias kept for source compatibility -- see `obfuscated_decrypted_view<N>`. */
  using decrypted_view = obfuscated_decrypted_view<N>;

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

  /**
   * @brief Ciphertext bytes (including the trailing NUL). Exposed only
   * for `obfuscated_string_ref` type erasure, not otherwise part of the
   * public API -- this is still ciphertext, not plaintext, so exposing
   * the pointer discloses nothing beyond what `decrypt()` already does.
   */
  [[nodiscard]] constexpr const uint8_t *data() const noexcept { return data_; }

  /**
   * @brief The per-literal XOR key. Exposed only for
   * `obfuscated_string_ref` type erasure, not otherwise part of the
   * public API.
   */
  [[nodiscard]] constexpr uint64_t key_seed() const noexcept { return seed_; }

  /** @brief Type-erases this object -- see `obfuscated_string_ref`. */
  [[nodiscard]] constexpr obfuscated_string_ref as_ref() const noexcept { return obfuscated_string_ref(*this); }

  /**
   * @brief Streaming, no-storage decode -- see
   * `obfuscated_string_ref::for_each_byte()`.
   */
  template <typename Visitor> void for_each_byte(Visitor &&visit) const noexcept {
    as_ref().for_each_byte(std::forward<Visitor>(visit));
  }
};

template <size_t N>
constexpr obfuscated_string_ref::obfuscated_string_ref(const obfuscated_string<N> &src) noexcept
    : data_(src.data()), encoded_size_(N), seed_(src.key_seed()) {}

template <size_t N>
inline obfuscated_decrypted_view<N>::obfuscated_decrypted_view(const obfuscated_string<N> &src) noexcept {
  // Reads `src.data_` through a volatile-qualified pointer -- exactly the
  // same "defeat the optimizer" trick `masked_byte_region::wipe()` uses
  // for its stores, applied here to a load instead. Without it, an
  // aggressive/LTO-enabled optimizer is free to see straight through
  // `decrypt()`: every input (`src.data_`, `src.seed_`, the loop bounds)
  // is a compile-time constant once `src` is a `static constexpr` local,
  // so a fully constant-propagating compiler could legally precompute
  // this entire loop and bake the *plaintext* into `plain_`'s initializer
  // -- right back into `.rodata`/`.data`, defeating the whole point of
  // this header. A volatile access is defined to be an observable side
  // effect the compiler may not presume a value for or elide, forcing a
  // genuine runtime load-XOR-store sequence every time, regardless of
  // optimization level.
  const volatile uint8_t *ciphertext = src.data_;
  for (size_t i = 0; i < N; ++i)
    plain_[i] = static_cast<char>(ciphertext[i] ^ security::detail::obfuscation_key_byte(src.seed_, i));
}

template <size_t N> obfuscated_string(const char (&)[N], uint64_t) -> obfuscated_string<N>;

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE

/**
 * @def RELOCO_DETAIL_OBFUSCATED_SEED()
 * @brief The `__FILE__`/`__LINE__`/`__TIME__` key-derivation expression
 * shared by `RELOCO_OBFUSCATED_STR` and `RELOCO_DEFINE_OBFUSCATED_STR` --
 * not for direct use.
 */
#define RELOCO_DETAIL_OBFUSCATED_SEED()                                                                                \
  (::reloco::security::detail::obfuscation_fnv1a(__FILE__, sizeof(__FILE__) - 1, static_cast<uint64_t>(__LINE__)) ^    \
   ::reloco::security::detail::obfuscation_fnv1a(__TIME__, sizeof(__TIME__) - 1, 0))

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
    static constexpr auto reloco_obfuscated_literal_ =                                                                 \
        ::reloco::obfuscated_string(str_literal, RELOCO_DETAIL_OBFUSCATED_SEED());                                     \
    return reloco_obfuscated_literal_.decrypt();                                                                       \
  }())

/**
 * @def RELOCO_DECLARE_OBFUSCATED_STR(name)
 * @brief Forward-declares @p name as an `extern const
 * reloco::obfuscated_string_ref` -- the type-erased handle, usable in a
 * header without ever naming an `N`. Pair with
 * `RELOCO_DEFINE_OBFUSCATED_STR` in exactly one translation unit. See the
 * file-level "Type erasure" section above.
 */
#define RELOCO_DECLARE_OBFUSCATED_STR(name) extern const ::reloco::obfuscated_string_ref name

/**
 * @def RELOCO_DEFINE_OBFUSCATED_STR(name, str_literal)
 * @brief Defines @p name, previously forward-declared with
 * `RELOCO_DECLARE_OBFUSCATED_STR(name)`, as a type-erased reference to an
 * internal-linkage `obfuscated_string<N>` constant encoding @p str_literal
 * -- still a compile-time-only encode, still emitted into `.rodata`, now
 * addressable from any translation unit through a single, non-template
 * type.
 */
#define RELOCO_DEFINE_OBFUSCATED_STR(name, str_literal)                                                                \
  namespace {                                                                                                          \
  constexpr auto reloco_obfuscated_storage_##name##_ =                                                                 \
      ::reloco::obfuscated_string(str_literal, RELOCO_DETAIL_OBFUSCATED_SEED());                                       \
  } /* namespace */                                                                                                    \
  const ::reloco::obfuscated_string_ref name(reloco_obfuscated_storage_##name##_)
