#pragma once
#include <cstddef>
#include <reloco/expected.hpp>
#include <system_error>

namespace reloco {

enum class error : int {
  allocation_failed = 1,
  in_place_growth_failed,
  unsupported_operation,
  out_of_range,
  invalid_argument,
  already_exists,
  empty_pointer,
  pointer_expired,
  no_owner,
  out_of_bounds,
  deadlock,
  invalid_owner,
  still_locked,
  not_locked,
  timed_out,
  try_again,
  not_initialized,
  container_empty,
  not_found
};

template <typename T> using result = expected<T, error>;

struct mem_block {
  void *ptr;
  std::size_t size;
};

enum class usage_hint : int {
  normal,     // Default behavior
  sequential, // Expecting to read from start to finish (e.g., big data
              // processing)
  random,     // No predictable pattern (disables aggressive prefetching)
  will_need,  // Load these pages into RAM now (async prefetch)
  dont_need,  // Can be reclaimed by OS if memory is tight (soft free)
  cold,       // This memory is unlikely to be touched soon (swap priority)
  huge_pages  // Attempt to back with transparent huge pages (THP)
};

class fallible_allocator {
public:
  virtual ~fallible_allocator() noexcept = default;

  [[nodiscard]] virtual result<mem_block>
  allocate(std::size_t bytes, std::size_t alignment) noexcept = 0;

  [[nodiscard]] virtual result<std::size_t>
  expand_in_place(void *ptr, std::size_t old_size,
                  std::size_t new_size) noexcept = 0;

  [[nodiscard]] virtual result<mem_block>
  reallocate(void *ptr, std::size_t old_size, std::size_t new_size,
             std::size_t alignment) noexcept = 0;

  virtual void deallocate(void *ptr, std::size_t bytes) noexcept = 0;

  virtual void advise(void *ptr, std::size_t bytes, usage_hint hint) noexcept {}
};

// By default, only trivially copyable types are bitwise relocatable.
// However, users can specialize this for types like std::unique_ptr.
template <typename T> struct is_relocatable : std::is_trivially_copyable<T> {};

} // namespace reloco
