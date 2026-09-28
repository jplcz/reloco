// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file masked_pointer.hpp
 * @brief `masked_pointer<T, AuthPolicy>`: a raw pointer wrapper that never
 * stores the plaintext pointer value at rest, mitigating passive memory
 * scanning/heap scraping and (depending on @c AuthPolicy) detecting
 * in-memory tampering of the stored value before it is ever dereferenced.
 *
 * Three `AuthPolicy` traits are provided, selected automatically by
 * `security::default_auth_traits` (hardware PAC where available, else the
 * signed software cookie):
 *
 * - `security::sw_cookie_traits`: XORs the pointer with a single
 *   process-wide secret cookie. Obfuscation only -- a corrupted/garbage
 *   encoded value decodes to a corrupted/garbage pointer with no
 *   detection at all, so this trait must never be used for a pointer an
 *   attacker can influence via a buffer overflow.
 * - `security::sw_signed_cookie_traits` (the software default): XORs the
 *   pointer with one cookie and stores a second, independently-keyed copy
 *   XORed with a different cookie; `auth()` compares the two decoded
 *   copies and treats any mismatch as **fatal, unconditionally, in every
 *   build configuration** -- see "Security is not negotiable" below.
 * - `security::hw_pac_traits`: ARMv8.3+ Pointer Authentication
 *   (`ptrauth_sign_unauthenticated`/`ptrauth_auth_data`). The hardware
 *   itself traps on an invalid signature; nothing in this header can
 *   weaken that guarantee. Only available when the compiler defines
 *   Clang's `ptrauth_calls` feature (guarded via `RELOCO_HAS_FEATURE`, see
 *   `detail/compat.hpp` -- this header never references the bare
 *   `__has_feature` macro directly, since GCC/MSVC don't define it and a
 *   naked `#if __has_feature(...)` is a hard compile error there).
 *
 * ## Security is not negotiable
 *
 * A tamper check that can be silently compiled away is not a tamper
 * check. `sw_signed_cookie_traits::auth()` therefore does **not** use
 * `RELOCO_ASSERT` -- `RELOCO_ASSERT` is a debuggability convenience that a
 * caller may globally disable via `RELOCO_DISABLE_ASSERT` for hot-path
 * performance elsewhere in a program, which would silently turn a
 * corruption-detection security boundary into undefined behavior (a
 * `RELOCO_UNREACHABLE()` hint fed a false condition). Instead it fails
 * through a small always-on trap (`RELOCO_DETAIL_ASSERT_FAIL` +
 * `RELOCO_TRAP()`, the exact same failure path `RELOCO_ASSERT` itself
 * expands to when *not* disabled) that ignores `RELOCO_DISABLE_ASSERT`
 * entirely. In a `RELOCO_KERNEL` build this still routes into the port's
 * own `RELOCO_KERNEL_PANIC`, exactly like every other reloco assertion.
 *
 * ## Threat model and honesty about "weak" entropy
 *
 * Both software traits seed their process-wide cookie(s) from
 * `security::detail::generate_weak_process_cookie()`: a SplitMix64-style
 * avalanche of a couple of ASLR-influenced addresses (this header's own
 * code address and a stack address), lazily computed once via
 * `once_lock<uintptr_t>` (never a function-local "magic" static, matching
 * every other lazily-initialized global in reloco, see `once_lock.hpp`).
 * This is **obfuscation and corruption detection, not cryptographic
 * secrecy**: an attacker who can read arbitrary process memory more than
 * once can recover both the cookie(s) and the pointer they protect, same
 * as any other in-process secret. What this header *does* guarantee,
 * unconditionally, is that a single stray write (a buffer overflow, a
 * use-after-free scribble, a wild pointer store) that corrupts a
 * `masked_pointer<T, sw_signed_cookie_traits>`'s at-rest bytes is caught
 * at `get()`/`operator->`/`operator*` time, before the corrupted address
 * is ever dereferenced, rather than silently followed. A caller that
 * needs an actually cryptographically-strong cookie (e.g. seeded from a
 * hardware RNG, or `mprotect()`-ed read-only after initialization) should
 * write its own `AuthPolicy` matching this file's four static members
 * (`internal_type`, `sign()`, `auth()`); nothing about `masked_pointer`
 * itself is tied to the cookie-generation strategy above.
 *
 * ## Rust-like operators
 *
 * `masked_pointer<T>` is `Copy` (like a raw `*mut T`/`*const T` pointer in
 * Rust would be), so its "Rust-like" surface mirrors `Option<*mut T>`
 * rather than `checked_value.hpp`'s move-only `Option<T>` typestate
 * tracking: `take()` (matching `Option::take`: returns the current raw
 * pointer and resets `*this` to null) and `replace(T *)` (matching
 * `Option::replace`/`std::mem::replace`: stores a new raw pointer and
 * returns the pointer it replaced), plus `is_null()` as a non-`explicit`
 * convenience alongside the existing `explicit operator bool()`.
 */

#include "detail/assert.hpp"
#include "lifetime.hpp"
#include "once_lock.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>

#if RELOCO_HAS_FEATURE(ptrauth_calls)
#include <ptrauth.h>
#endif

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {
namespace security {

namespace detail {

/**
 * @brief SplitMix64-style avalanche: scrambles @p x so that every output
 * bit depends on every input bit. Shared by every weak-entropy source in
 * this header.
 */
[[nodiscard]] inline uintptr_t avalanche_mix(uintptr_t x) noexcept {
  x ^= (x >> 30);
  x *= static_cast<uintptr_t>(0xbf58476d1ce4e5b9ULL);
  x ^= (x >> 27);
  x *= static_cast<uintptr_t>(0x94d049bb133111ebULL);
  x ^= (x >> 31);
  return x;
}

/**
 * @brief Weak, best-effort process-wide entropy source: mixes this
 * function's own (ASLR-influenced) code address with a stack address
 * through `avalanche_mix`. See the file-level "Threat model" section
 * above for exactly what this is (and is not) a defense against.
 */
[[nodiscard]] inline uintptr_t generate_weak_process_cookie() noexcept {
  uintptr_t stack_addr = reinterpret_cast<uintptr_t>(&stack_addr);
  uintptr_t code_addr = reinterpret_cast<uintptr_t>(&generate_weak_process_cookie);
  return avalanche_mix(stack_addr ^ avalanche_mix(code_addr));
}

} // namespace detail

/* =========================================================================
 * TRAIT 1: Software XOR Cookie (Obfuscation Only)
 * ========================================================================= */

/**
 * @brief Obfuscates a pointer by XOR-ing it with a single process-wide
 * secret cookie. No integrity check: a corrupted encoded value silently
 * decodes to a corrupted pointer. Only appropriate when the wrapped
 * pointer can never be influenced by attacker-controlled memory
 * corruption (e.g. it lives in read-only memory once set) -- otherwise
 * prefer `sw_signed_cookie_traits`.
 */
struct sw_cookie_traits {
  using internal_type = uintptr_t;

  template <typename T> [[nodiscard]] static internal_type sign(T *ptr) noexcept {
    return reinterpret_cast<uintptr_t>(ptr) ^ cookie();
  }

  template <typename T> [[nodiscard]] static T *auth(internal_type val) noexcept {
    return reinterpret_cast<T *>(val ^ cookie());
  }

private:
#if !defined(RELOCO_KERNEL)
  [[nodiscard]] static uintptr_t cookie() noexcept {
    return g_cookie_.get_or_init(detail::generate_weak_process_cookie);
  }

  static inline once_lock<uintptr_t> g_cookie_{};
#else
  [[nodiscard]] static uintptr_t cookie() noexcept;
#endif
};

/* =========================================================================
 * TRAIT 2: Software Signed Cookie (Obfuscation + Integrity)
 * ========================================================================= */

/**
 * @brief Obfuscates a pointer with one secret cookie and stores a second,
 * independently-keyed copy XORed with a different cookie; `auth()`
 * compares the two decoded copies and treats any mismatch as fatal,
 * unconditionally -- see the file-level "Security is not negotiable"
 * section above. This is `security::default_auth_traits` on every target
 * without hardware pointer authentication.
 */
struct sw_signed_cookie_traits {
  struct internal_type {
    uintptr_t x[2];

    [[nodiscard]] bool operator==(const internal_type &other) const noexcept {
      return x[0] == other.x[0] && x[1] == other.x[1];
    }
    [[nodiscard]] bool operator!=(const internal_type &other) const noexcept { return !(*this == other); }
  };

  template <typename T> [[nodiscard]] static internal_type sign(T *ptr) noexcept {
    uintptr_t raw = reinterpret_cast<uintptr_t>(ptr);
    return {raw ^ obfuscation_cookie(), raw ^ sign_cookie()};
  }

  template <typename T> [[nodiscard]] static T *auth(internal_type val) noexcept {
    bool intact = (val.x[0] ^ obfuscation_cookie()) == (val.x[1] ^ sign_cookie());
    // Deliberately not RELOCO_ASSERT -- see "Security is not negotiable"
    // in the file-level documentation. This check must survive
    // RELOCO_DISABLE_ASSERT.
    if (!intact)
      RELOCO_UNLIKELY {
        RELOCO_DETAIL_ASSERT_FAIL(intact, "reloco::security::sw_signed_cookie_traits::auth: pointer integrity "
                                          "check failed -- possible memory corruption or tampering");
        RELOCO_TRAP();
      }
    return reinterpret_cast<T *>(val.x[0] ^ obfuscation_cookie());
  }

private:
#if !defined(RELOCO_KERNEL)
  [[nodiscard]] static uintptr_t obfuscation_cookie() noexcept {
    return g_obfuscation_cookie_.get_or_init(detail::generate_weak_process_cookie);
  }

  [[nodiscard]] static uintptr_t sign_cookie() noexcept {
    return g_sign_cookie_.get_or_init([]() noexcept {
      return detail::avalanche_mix(detail::generate_weak_process_cookie() ^ 0x5c5c5c5c5c5c5c5cULL);
    });
  }

  static inline once_lock<uintptr_t> g_obfuscation_cookie_{};
  static inline once_lock<uintptr_t> g_sign_cookie_{};
#else
  [[nodiscard]] static uintptr_t obfuscation_cookie() noexcept;
  [[nodiscard]] static uintptr_t sign_cookie() noexcept;
#endif
};

/* =========================================================================
 * TRAIT 3: Hardware Pointer Authentication (ARMv8.3+ PAC)
 * ========================================================================= */

#if RELOCO_HAS_FEATURE(ptrauth_calls)
/**
 * @brief ARMv8.3+ Pointer Authentication (PAC). The hardware itself signs
 * and authenticates the pointer and traps on mismatch -- this trait is a
 * thin wrapper, not an independent implementation of the integrity check.
 */
struct hw_pac_traits {
  using internal_type = void *;

  template <typename T> [[nodiscard]] static internal_type sign(T *ptr) noexcept {
    // ptrauth_key_asda = Data Key A.
    return ptrauth_sign_unauthenticated(reinterpret_cast<void *>(ptr), ptrauth_key_asda, 0);
  }

  template <typename T> [[nodiscard]] static T *auth(internal_type val) noexcept {
    // Hardware validates the signature and traps immediately if invalid.
    return reinterpret_cast<T *>(ptrauth_auth_data(val, ptrauth_key_asda, 0));
  }
};
#endif

/* =========================================================================
 * DEFAULT TRAIT RESOLUTION
 * ========================================================================= */

#if RELOCO_HAS_FEATURE(ptrauth_calls) && !defined(RELOCO_FORCE_SW_MASKING)
using default_auth_traits = hw_pac_traits;
#else
// Defaulting to the signed software trait for integrity detection on
// x86/RISC-V/older ARM, where no hardware pointer-authentication exists.
using default_auth_traits = sw_signed_cookie_traits;
#endif

} // namespace security

/* =========================================================================
 * MASKED POINTER
 * ========================================================================= */

/**
 * @brief A zero-overhead pointer wrapper that never keeps the plaintext
 * pointer value at rest. See the file-level documentation above for the
 * available `AuthPolicy` traits and this header's threat model.
 */
template <typename T, typename AuthPolicy = security::default_auth_traits> class masked_pointer {
  static_assert(std::is_object_v<T> || std::is_void_v<T>, "masked_pointer must point to an object or void");

  typename AuthPolicy::internal_type encoded_val_;

public:
  using element_type = T;
  using pointer = T *;

  // ---- Constructors ----

  masked_pointer() noexcept : encoded_val_(AuthPolicy::template sign<T>(nullptr)) {}

  masked_pointer(std::nullptr_t) noexcept : encoded_val_(AuthPolicy::template sign<T>(nullptr)) {}

  explicit masked_pointer(T *ptr) noexcept : encoded_val_(AuthPolicy::template sign<T>(ptr)) {}

  // Copy semantics -- masked_pointer is Copy, like a raw pointer.
  masked_pointer(const masked_pointer &other) noexcept = default;
  masked_pointer &operator=(const masked_pointer &other) noexcept = default;

  // Move semantics: matches Rust's `Option::take` behavior of leaving the
  // moved-from side null rather than merely copying bits, so a
  // moved-from masked_pointer is never mistaken for a still-live alias.
  masked_pointer(masked_pointer &&other) noexcept : encoded_val_(other.encoded_val_) { other.reset(); }

  masked_pointer &operator=(masked_pointer &&other) noexcept {
    encoded_val_ = other.encoded_val_;
    other.reset();
    return *this;
  }

  // ---- Core Accessors ----

  [[nodiscard]] T *get() const noexcept RELOCO_LIFETIMEBOUND { return AuthPolicy::template auth<T>(encoded_val_); }

  // Enable ONLY if U (which is T) is not void
  template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
  [[nodiscard]] U *operator->() const noexcept RELOCO_LIFETIMEBOUND {
    U *result = static_cast<U *>(get());
    RELOCO_ASSERT(result != nullptr, "Dereferencing null pointer");
    return result; // Optimized: don't call get() twice
  }

  // Enable ONLY if U (which is T) is not void
  template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
  [[nodiscard]] U &operator*() const noexcept RELOCO_LIFETIMEBOUND {
    U *result = static_cast<U *>(get());
    RELOCO_ASSERT(result != nullptr, "Dereferencing null pointer");
    return *result; // Optimized: don't call get() twice
  }

  // ---- Modifiers ----

  void reset(T *ptr = nullptr) noexcept { encoded_val_ = AuthPolicy::template sign<T>(ptr); }

  void swap(masked_pointer &other) noexcept { std::swap(encoded_val_, other.encoded_val_); }

  masked_pointer &operator=(T *ptr) noexcept {
    reset(ptr);
    return *this;
  }

  masked_pointer &operator=(std::nullptr_t) noexcept {
    reset(nullptr);
    return *this;
  }

  // ---- Rust-like operators ----

  /**
   * @brief Matches Rust's `Option::take`: returns the current raw
   * pointer and resets `*this` to null, in one step.
   */
  [[nodiscard]] T *take() noexcept {
    T *old = get();
    reset();
    return old;
  }

  /**
   * @brief Matches Rust's `Option::replace`/`std::mem::replace`: stores
   * @p ptr and returns the raw pointer it replaced.
   */
  T *replace(T *ptr) noexcept {
    T *old = get();
    reset(ptr);
    return old;
  }

  // ---- Observers ----

  [[nodiscard]] explicit operator bool() const noexcept { return get() != nullptr; }

  [[nodiscard]] bool is_null() const noexcept { return get() == nullptr; }

  // ---- Comparisons ----
  // We compare encoded_val_ directly. Since the cookie(s) are constant
  // for the lifetime of the process, equal raw pointers produce equal
  // encoded values.

  [[nodiscard]] friend bool operator==(const masked_pointer &lhs, const masked_pointer &rhs) noexcept {
    return lhs.encoded_val_ == rhs.encoded_val_;
  }

  [[nodiscard]] friend bool operator!=(const masked_pointer &lhs, const masked_pointer &rhs) noexcept {
    return lhs.encoded_val_ != rhs.encoded_val_;
  }

  [[nodiscard]] friend bool operator==(const masked_pointer &lhs, std::nullptr_t) noexcept {
    return lhs.get() == nullptr;
  }

  [[nodiscard]] friend bool operator!=(const masked_pointer &lhs, std::nullptr_t) noexcept {
    return lhs.get() != nullptr;
  }

  [[nodiscard]] friend bool operator==(std::nullptr_t, const masked_pointer &rhs) noexcept {
    return rhs.get() == nullptr;
  }

  [[nodiscard]] friend bool operator!=(std::nullptr_t, const masked_pointer &rhs) noexcept {
    return rhs.get() != nullptr;
  }
};

// Free function swap for ADL
template <typename T, typename AuthPolicy>
void swap(masked_pointer<T, AuthPolicy> &lhs, masked_pointer<T, AuthPolicy> &rhs) noexcept {
  lhs.swap(rhs);
}

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
