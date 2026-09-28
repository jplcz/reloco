// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Windows RELOCO_FUTEX_BACKEND_WINDOWS implementation of reloco/futex.hpp's
// futex_wait/futex_wait_timeout/futex_wake_one/futex_wake_all, via
// WaitOnAddress/WakeByAddress (available since Windows 8). Included only from
// futex.hpp itself when RELOCO_FUTEX_BACKEND_WINDOWS is defined -- never
// include this file directly.

// Pre-emptively map MSVC architecture macros to Windows SDK architecture macros.
// This resolves the C1189 "#error: No Target Architecture" on newer Windows SDKs
// when using the conformant preprocessor (/Zc:preprocessor).
#if defined(_M_AMD64) && !defined(_AMD64_)
#define _AMD64_
#endif
#if defined(_M_IX86) && !defined(_X86_)
#define _X86_
#endif
#if defined(_M_ARM64) && !defined(_ARM64_)
#define _ARM64_
#endif
#if defined(_M_ARM) && !defined(_ARM_)
#define _ARM_
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

// clang-format off
#include <windows.h>
#include <synchapi.h>
// clang-format on

#include <cstdint>

// WaitOnAddress is exported from synchronization.lib
#pragma comment(lib, "synchronization.lib")

namespace reloco {

namespace detail {

// Win32 WaitOnAddress API family takes a `volatile VOID *` for the target
// address and `PVOID` for the expected value.
inline volatile void *raw_addr(const futex_word &word) noexcept {
  return static_cast<volatile void *>(const_cast<futex_word *>(&word));
}

} // namespace detail

RELOCO_API void futex_wait(const futex_word &word, std::uint32_t expected) noexcept {
  // WaitOnAddress(Address, CompareAddress, AddressSize, dwMilliseconds)
  // INFINITE maps to waiting forever. Spurious wakeups are tolerated exactly
  // as on Linux.
  WaitOnAddress(detail::raw_addr(word), &expected, sizeof(expected), INFINITE);
}

RELOCO_API bool futex_wait_timeout(const futex_word &word, std::uint32_t expected, duration timeout) noexcept {
  std::uint64_t raw_ms = timeout.as_millis();

  DWORD ms;
  // 1. Prevent 64-bit to 32-bit truncation bugs.
  // 2. Prevent the INFINITE (0xFFFFFFFF) trap where Windows waits forever.
  // We cap the maximum wait time to INFINITE - 1 (approx 49.7 days).
  if (raw_ms >= static_cast<std::uint64_t>(INFINITE)) {
    ms = INFINITE - 1;
  } else {
    ms = static_cast<DWORD>(raw_ms);
  }

  BOOL success = WaitOnAddress(detail::raw_addr(word), &expected, sizeof(expected), ms);

  // WaitOnAddress returns TRUE if the wait succeeded (a genuine wake up).
  // If it returns FALSE, GetLastError() indicates the reason.
  // We only return false if the timeout genuinely elapsed.
  if (!success && GetLastError() == ERROR_TIMEOUT) {
    return false;
  }

  return true;
}

RELOCO_API void futex_wake_one(futex_word &word) noexcept {
  WakeByAddressSingle(const_cast<void *>(static_cast<const void *>(&word)));
}

RELOCO_API void futex_wake_all(futex_word &word) noexcept {
  WakeByAddressAll(const_cast<void *>(static_cast<const void *>(&word)));
}

} // namespace reloco