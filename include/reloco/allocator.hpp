#include <reloco/posix_allocator.hpp>

namespace reloco {

using core_allocator = posix_allocator;

inline core_allocator &get_default_allocator() noexcept {
  static core_allocator instance;
  return instance;
}

} // namespace reloco
