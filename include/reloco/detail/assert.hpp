// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../lifetime.hpp"
#include "compat.hpp"

#if defined(RELOCO_KERNEL) && !defined(RELOCO_KERNEL_PANIC)
#error                                                                                                                 \
    "RELOCO_KERNEL requires RELOCO_KERNEL_PANIC(expression, file, line, message) to be defined before the first inclusion of assert.hpp. Implement it against the target's panic/log facility, e.g. a printf-like kernel panic function, or, in the worst case, a sequence of raw string writes."
#endif

#if !defined(RELOCO_KERNEL) && !defined(RELOCO_DISABLE_ASSERT_STDIO)
#include <cstdio>
#endif

namespace reloco {

#if !defined(RELOCO_KERNEL)

using assert_handler_t = void (*)(const char *expression, const char *file, int line, const char *message);

namespace detail {

inline void default_assert_handler(const char *expr, const char *file, int line, const char *msg) {
#if defined(RELOCO_DISABLE_ASSERT_STDIO)
  (void)expr;
  (void)file;
  (void)line;
  (void)msg;
#else
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  std::fprintf(stderr, "[RELOCO ASSERT] Failure: %s\nAt: %s:%d\nMessage: %s\n", expr, file, line, msg);
  RELOCO_END_UNSAFE_BUFFER_USAGE
#endif
}

inline assert_handler_t &get_handler_ptr() {
  static assert_handler_t handler = default_assert_handler;
  return handler;
}

} // namespace detail

inline void set_assert_handler(assert_handler_t new_handler) { detail::get_handler_ptr() = new_handler; }

#endif // !RELOCO_KERNEL

} // namespace reloco

// In kernel/freestanding builds (RELOCO_KERNEL defined), assert failures
// call the port-supplied RELOCO_KERNEL_PANIC(expression, file, line,
// message) macro directly instead of going through the runtime,
// function-pointer-based assert_handler_t indirection used on hosted
// platforms. This keeps the failure path free of runtime dispatch and lets
// the port route straight into its own panic/log facility.
#if defined(RELOCO_KERNEL)
#define RELOCO_DETAIL_ASSERT_FAIL(cond, ...) RELOCO_KERNEL_PANIC(#cond, __FILE__, __LINE__, "" __VA_ARGS__)
#define RELOCO_DETAIL_ASSERT_FAIL_MSG(cond, msg_expr) RELOCO_KERNEL_PANIC(#cond, __FILE__, __LINE__, (msg_expr))
#define RELOCO_DETAIL_ASSERT_FAIL_LOC(cond, file, line, ...) RELOCO_KERNEL_PANIC(#cond, (file), (line), "" __VA_ARGS__)
#define RELOCO_DETAIL_ASSERT_FAIL_MSG_LOC(cond, file, line, msg_expr)                                                  \
  RELOCO_KERNEL_PANIC(#cond, (file), (line), (msg_expr))
#else
#define RELOCO_DETAIL_ASSERT_FAIL(cond, ...)                                                                           \
  ::reloco::detail::get_handler_ptr()(#cond, __FILE__, __LINE__, "" __VA_ARGS__)
#define RELOCO_DETAIL_ASSERT_FAIL_MSG(cond, msg_expr)                                                                  \
  ::reloco::detail::get_handler_ptr()(#cond, __FILE__, __LINE__, (msg_expr))
#define RELOCO_DETAIL_ASSERT_FAIL_LOC(cond, file, line, ...)                                                           \
  ::reloco::detail::get_handler_ptr()(#cond, (file), (line), "" __VA_ARGS__)
#define RELOCO_DETAIL_ASSERT_FAIL_MSG_LOC(cond, file, line, msg_expr)                                                  \
  ::reloco::detail::get_handler_ptr()(#cond, (file), (line), (msg_expr))
#endif

#if defined(RELOCO_DISABLE_ASSERT)
#if RELOCO_HAS_UNREACHABLE
#define RELOCO_ASSERT(cond, ...)                                                                                       \
  do {                                                                                                                 \
    if (!(cond))                                                                                                       \
      RELOCO_UNREACHABLE();                                                                                            \
  } while (0)
#else
#define RELOCO_ASSERT(cond, ...) (void)0
#endif
#else
#define RELOCO_ASSERT(cond, ...)                                                                                       \
  do {                                                                                                                 \
    if (!(cond))                                                                                                       \
      RELOCO_UNLIKELY {                                                                                                \
        RELOCO_DETAIL_ASSERT_FAIL(cond, __VA_ARGS__);                                                                  \
        RELOCO_TRAP();                                                                                                 \
      }                                                                                                                \
  } while (0)
#endif

// Like `RELOCO_ASSERT`, but @p msg_expr is an arbitrary runtime
// `const char *` expression (e.g. a caller-supplied `expect(msg)`
// argument) rather than a string literal folded in at the call site via
// `""`-concatenation -- use this instead of `RELOCO_ASSERT(cond, msg)`
// whenever the message isn't known until runtime.
#if defined(RELOCO_DISABLE_ASSERT)
#if RELOCO_HAS_UNREACHABLE
#define RELOCO_ASSERT_MSG(cond, msg_expr)                                                                              \
  do {                                                                                                                 \
    (void)(msg_expr);                                                                                                  \
    if (!(cond))                                                                                                       \
      RELOCO_UNREACHABLE();                                                                                            \
  } while (0)
#else
#define RELOCO_ASSERT_MSG(cond, msg_expr) ((void)(msg_expr))
#endif
#else
#define RELOCO_ASSERT_MSG(cond, msg_expr)                                                                              \
  do {                                                                                                                 \
    if (!(cond))                                                                                                       \
      RELOCO_UNLIKELY {                                                                                                \
        RELOCO_DETAIL_ASSERT_FAIL_MSG(cond, msg_expr);                                                                 \
        RELOCO_TRAP();                                                                                                 \
      }                                                                                                                \
  } while (0)
#endif

// Like `RELOCO_ASSERT`, but @p file/@p line are supplied explicitly
// instead of being taken from `__FILE__`/`__LINE__` at the macro's own
// expansion site -- for a caller-location-aware wrapper (e.g.
// `unique_lock`/`shared_lock`, see `call_location.hpp`) that wants a
// failing precondition attributed to *its own caller's* site rather
// than a line inside the wrapper itself. Pass `nullptr` for @p file
// when no real location is available (e.g. a release-mode
// `release_call_location_ref` call site); the failure is then reported
// exactly like a plain `RELOCO_ASSERT` would, since every assert
// handler (`default_assert_handler`, `RELOCO_KERNEL_PANIC`) already
// treats a null `file` as "no location".
#if defined(RELOCO_DISABLE_ASSERT)
#if RELOCO_HAS_UNREACHABLE
#define RELOCO_ASSERT_LOC(cond, file, line, ...)                                                                       \
  do {                                                                                                                 \
    (void)(file);                                                                                                      \
    (void)(line);                                                                                                      \
    if (!(cond))                                                                                                       \
      RELOCO_UNREACHABLE();                                                                                            \
  } while (0)
#else
#define RELOCO_ASSERT_LOC(cond, file, line, ...) ((void)(file), (void)(line), (void)0)
#endif
#else
#define RELOCO_ASSERT_LOC(cond, file, line, ...)                                                                       \
  do {                                                                                                                 \
    if (!(cond))                                                                                                       \
      RELOCO_UNLIKELY {                                                                                                \
        RELOCO_DETAIL_ASSERT_FAIL_LOC(cond, (file), (line), __VA_ARGS__);                                              \
        RELOCO_TRAP();                                                                                                 \
      }                                                                                                                \
  } while (0)
#endif

// Like `RELOCO_ASSERT_MSG`, but with an explicit @p file/@p line pair --
// see `RELOCO_ASSERT_LOC` above for why/when to use this instead of
// `RELOCO_ASSERT_MSG`.
#if defined(RELOCO_DISABLE_ASSERT)
#if RELOCO_HAS_UNREACHABLE
#define RELOCO_ASSERT_MSG_LOC(cond, file, line, msg_expr)                                                              \
  do {                                                                                                                 \
    (void)(file);                                                                                                      \
    (void)(line);                                                                                                      \
    (void)(msg_expr);                                                                                                  \
    if (!(cond))                                                                                                       \
      RELOCO_UNREACHABLE();                                                                                            \
  } while (0)
#else
#define RELOCO_ASSERT_MSG_LOC(cond, file, line, msg_expr) ((void)(file), (void)(line), (void)(msg_expr))
#endif
#else
#define RELOCO_ASSERT_MSG_LOC(cond, file, line, msg_expr)                                                              \
  do {                                                                                                                 \
    if (!(cond))                                                                                                       \
      RELOCO_UNLIKELY {                                                                                                \
        RELOCO_DETAIL_ASSERT_FAIL_MSG_LOC(cond, (file), (line), msg_expr);                                             \
        RELOCO_TRAP();                                                                                                 \
      }                                                                                                                \
  } while (0)
#endif

#if defined(NDEBUG) && !defined(RELOCO_DEBUG)
#if RELOCO_HAS_UNREACHABLE
#define RELOCO_DEBUG_ASSERT(cond, ...)                                                                                 \
  do {                                                                                                                 \
    if (!(cond))                                                                                                       \
      RELOCO_UNREACHABLE();                                                                                            \
  } while (0)
#else
#define RELOCO_DEBUG_ASSERT(cond, ...) (void)0
#endif
#else
#define RELOCO_DEBUG_ASSERT(cond, ...)                                                                                 \
  do {                                                                                                                 \
    if (!(cond))                                                                                                       \
      RELOCO_UNLIKELY {                                                                                                \
        RELOCO_DETAIL_ASSERT_FAIL(cond, __VA_ARGS__);                                                                  \
        RELOCO_TRAP();                                                                                                 \
      }                                                                                                                \
  } while (0)
#endif
