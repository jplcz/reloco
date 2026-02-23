#pragma once
#if defined(_WIN32)
#include <reloco/win_allocator.hpp>
#else
#include <reloco/posix_allocator.hpp>
#endif

namespace reloco {

#if defined(_WIN32)
using core_allocator = win_allocator;
#else
using core_allocator = posix_allocator;
#endif

inline core_allocator &get_default_allocator() noexcept {
  static core_allocator instance;
  return instance;
}

} // namespace reloco
