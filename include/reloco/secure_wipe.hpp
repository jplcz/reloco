// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file secure_wipe.hpp
 * @brief The one primitive reloco uses to destroy secret bytes so the
 * compiler cannot remove the write.
 *
 * A plain `memset` before a buffer dies is a dead store: nothing reads the
 * memory afterwards, so the optimizer deletes it and the secret stays in
 * memory. `secure_wipe` is the primitive that is *not* deleted. Every reloco
 * type that erases secrets (`secure_vector`, see `secure_vector.hpp`) goes
 * through it, so the policy -- what the bytes become, and how the write is
 * kept -- lives in exactly one place that a port can replace.
 *
 * ## What the bytes become
 *
 * Zeros by default. The contract is only that the previous contents are
 * destroyed; a port may substitute any destroying write (random bytes, a
 * pattern, a hardware crypto engine, a cryptographic erase) and callers never
 * read a wiped object back. Define `RELOCO_SECURE_WIPE_IMPL(ptr, bytes)`
 * before including any reloco header to replace the default. `ptr` is a
 * `void *` and `bytes` a `std::size_t`; the replacement must not be removable
 * by the compiler (an out-of-line function, `explicit_bzero`,
 * `memset_explicit`, a volatile loop).
 *
 * ## The default
 *
 * - GCC and Clang: `std::memset` followed by an empty `asm volatile` that
 *   takes the pointer as an input and clobbers memory, so the compiler must
 *   assume the zeroed bytes are observed and cannot drop the `memset`.
 *   `memset` may still be fused or vectorised; only its removal is forbidden.
 * - Other compilers: a loop of writes through a `volatile` byte pointer.
 *
 * ## C and C++ memory model
 *
 * The wipe writes through `unsigned char`, which both languages allow for the
 * object representation of any object. It must only be applied to storage
 * whose lifetime has not ended and never to an object that is `const`; the
 * object overload requires a trivially copyable type because byte-wise
 * overwriting a non-trivially-copyable object is undefined behaviour.
 *
 * See `docs/secure-wipe.md` for the guide, the rationale and examples.
 */

#include "lifetime.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>

#if defined(RELOCO_SECURE_WIPE_IMPL)
// A port-provided primitive replaces the default below.
#elif defined(__GNUC__) || defined(__clang__)
#define RELOCO_SECURE_WIPE_IMPL(ptr, bytes)                                                                            \
  do {                                                                                                                 \
    void *const reloco_wipe_ptr_ = (ptr);                                                                              \
    std::memset(reloco_wipe_ptr_, 0, (bytes));                                                                         \
    __asm__ volatile("" : : "r"(reloco_wipe_ptr_) : "memory");                                                         \
  } while (false)
#else
#define RELOCO_SECURE_WIPE_IMPL(ptr, bytes)                                                                            \
  do {                                                                                                                 \
    volatile unsigned char *reloco_wipe_p_ = static_cast<volatile unsigned char *>(ptr);                               \
    for (std::size_t reloco_wipe_i_ = 0; reloco_wipe_i_ < (bytes); ++reloco_wipe_i_) {                                \
      reloco_wipe_p_[reloco_wipe_i_] = 0;                                                                              \
    }                                                                                                                  \
  } while (false)
#endif

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

// Unsafe by design: raw byte writes that must never be optimised away.
RELOCO_BEGIN_UNSAFE_CODE

namespace reloco {

/**
 * @brief Destroys @p bytes bytes at @p ptr so the compiler cannot elide it.
 *
 * `RELOCO_UNSAFE_BUFFER_USAGE`: the caller guarantees that `[ptr, ptr+bytes)`
 * is live, writable storage (not a `const` object, not past its lifetime).
 * A null pointer or zero size is a no-op.
 */
RELOCO_UNSAFE_BUFFER_USAGE inline void secure_wipe(void *ptr, std::size_t bytes) noexcept {
  if (ptr == nullptr || bytes == 0) {
    return;
  }
  RELOCO_SECURE_WIPE_IMPL(ptr, bytes);
}

/**
 * @brief Destroys one trivially copyable object (or array of them).
 *
 * The object is dead after the call: do not read it back.
 */
template <typename T> void secure_wipe(T &object) noexcept {
  static_assert(!std::is_const_v<T>, "secure_wipe: a const object cannot be wiped (modifying it is undefined behaviour)");
  static_assert(std::is_trivially_copyable_v<T>,
                "secure_wipe: byte-wise overwrite of a non-trivially-copyable object is undefined behaviour");
  secure_wipe(static_cast<void *>(std::addressof(object)), sizeof(T));
}

} // namespace reloco

RELOCO_END_UNSAFE_CODE

RELOCO_END_UNSAFE_BUFFER_USAGE
