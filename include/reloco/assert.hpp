#pragma once
#include <cstdio>
#include <cstdlib>

#if defined(__clang__) || defined(__GNUC__)
#define RELOCO_TRAP() __builtin_trap()
#elif defined(_MSC_VER)
#define RELOCO_TRAP() __debugbreak()
#else
#define RELOCO_TRAP() std::abort()
#endif

namespace reloco {
// A hook for the user to provide their own failure logic (e.g., logging to a
// file)
using assert_handler_t = void (*)(const char *expression, const char *file,
                                  int line, const char *message);

namespace detail {
// Internal default handler
inline void default_assert_handler(const char *expr, const char *file, int line,
                                   const char *msg) {
  std::fprintf(stderr, "[RELOCO ASSERT] Failure: %s\nAt: %s:%d\nMessage: %s\n",
               expr, file, line, msg);
}

// Global pointer to the current handler
inline assert_handler_t &get_handler_ptr() {
  static assert_handler_t handler = default_assert_handler;
  return handler;
}
} // namespace detail

// API to change the behavior at runtime
inline void set_assert_handler(assert_handler_t new_handler) {
  detail::get_handler_ptr() = new_handler;
}
} // namespace reloco

#if defined(RELOCO_DISABLE_ASSERT)
#if defined(__clang__) || defined(__GNUC__)
#define RELOCO_ASSERT(cond, ...)                                               \
  do {                                                                         \
    if (!(cond))                                                               \
      __builtin_unreachable();                                                 \
  } while (0)
#else
#define RELOCO_ASSERT(cond, ...) (void)0
#endif
#else
#define RELOCO_ASSERT(cond, ...)                                               \
  do {                                                                         \
    if (!(cond)) [[unlikely]] {                                                \
      ::reloco::detail::get_handler_ptr()(#cond, __FILE__, __LINE__,           \
                                          "" __VA_ARGS__);                     \
      RELOCO_TRAP();                                                           \
    }                                                                          \
  } while (0)
#endif