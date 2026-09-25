// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file thread_std.ipp @brief Out-of-line body for the
 * RELOCO_THREAD_BACKEND_STD `thread::try_spawn` (see thread.hpp). Included
 * from thread.hpp itself, guarded on RELOCO_SHARED_PROVIDE_DEFINITIONS
 * (see reloco/detail/compat.hpp). Never included directly. */

// `std::thread`'s constructor throws `std::system_error` if the OS
// refuses to create a new thread (e.g. a resource limit). Any thrown
// `std::system_error` here indicates a normal, non-programming-error
// runtime condition (unlike mutex.hpp's own use of this pattern for
// lock/unlock misuse), so it is converted to `result<thread>` instead of
// asserting. When `RELOCO_HAS_EXCEPTIONS` is `0` (e.g. built with
// `-fno-exceptions`), the call is made unguarded: `try`/`catch` is not
// valid syntax in that mode, and the standard library's own throwing path
// becomes a terminating call anyway, so there is nothing left for reloco
// to translate.
RELOCO_API result<thread> thread::try_spawn(function<void()> &&entry, allocator_ref alloc) noexcept {
  (void)alloc; // Unused: std::thread manages its own internal storage.
#if RELOCO_HAS_EXCEPTIONS
  try {
    thread t;
    t.handle_ = std::thread(std::move(entry));
    return t;
  } catch (const std::system_error &) {
    return unexpected(error::resource_exhausted);
  }
#else
  thread t;
  t.handle_ = std::thread(std::move(entry));
  return t;
#endif
}
