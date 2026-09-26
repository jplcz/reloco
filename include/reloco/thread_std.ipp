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

namespace detail {

// Best-effort thread naming applied *after* creation, since std::thread
// itself offers no hook to run code before the entry callable starts.
// Only wired up where <pthread.h> is available and glibc/musl's
// std::thread native_handle is itself a pthread_t -- silently skipped
// (rather than reported as an error) everywhere else, matching the
// "best-effort" contract documented on thread::try_spawn_named.
inline void apply_thread_name_std(RELOCO_MAYBE_UNUSED thread &t, RELOCO_MAYBE_UNUSED const inline_string<15> &name) noexcept {
#if RELOCO_HAS_INCLUDE(<pthread.h>) && (defined(__linux__) || defined(__FreeBSD__))
  if (name.empty())
    return;
#if defined(__FreeBSD__)
  pthread_set_name_np(t.native_handle(), name.unsafe_c_str());
#else
  pthread_setname_np(t.native_handle(), name.unsafe_c_str());
#endif
#endif
}

} // namespace detail

RELOCO_API result<thread> thread::try_spawn_named(function<void()> &&entry, allocator_ref alloc,
                                                  optional<std::size_t> stack_size,
                                                  inline_string<15> name) noexcept {
  (void)alloc; // Unused: std::thread manages its own internal storage.
  if (stack_size.has_value())
    return unexpected(error::unsupported_operation);
#if RELOCO_HAS_EXCEPTIONS
  try {
    thread t;
    t.handle_ = std::thread(std::move(entry));
    detail::apply_thread_name_std(t, name);
    return t;
  } catch (const std::system_error &) {
    return unexpected(error::resource_exhausted);
  }
#else
  thread t;
  t.handle_ = std::thread(std::move(entry));
  detail::apply_thread_name_std(t, name);
  return t;
#endif
}
