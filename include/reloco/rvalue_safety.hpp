#pragma once
#include <reloco/assert.hpp>

namespace reloco {

/**
 * @def RELOCO_BLOCK_RVALUE_ACCESS
 * @brief Hardens a container by explicitly deleting rvalue accessors.
 * This macro mitigates dangling reference vulnerabilities. It deletes the
 * rvalue overloads for common accessors (operator[], front, back, etc.).
 * @details In kernel development, it is easy to accidentally write:
 * @code
 * auto& ref = (*helper.allocate_array<int>(5))[0]; // BUG: array is a
 * temporary!
 * @endcode
 * Without this macro, 'ref' would point to memory that is freed at the
 * end of the statement. With this macro, the compiler will generate an
 * error forcing the user to move the container to an lvalue first.
 * @param Type The underlying element type (e.g., T).
 */
#define RELOCO_BLOCK_RVALUE_ACCESS(Type)                                       \
  /** @name Rvalue Safety Guards */                                            \
  /** @{ */                                                                    \
  Type &operator[](size_t) const && = delete;                                  \
  auto front() const && = delete;                                              \
  auto back() const && = delete;                                               \
  auto try_front() const && = delete;                                          \
  auto try_back() const && = delete;                                           \
  auto at(size_t) const && = delete;                                           \
  auto try_at(size_t) const && = delete;                                       \
  auto unsafe_at(size_t) const && = delete;                                    \
  auto data() const && = delete;                                               \
  auto try_data() const && = delete;                                           \
  auto unsafe_data() const && = delete;                                        \
  auto begin() const && = delete;                                              \
  auto end() const && = delete;                                                \
  auto operator*() const && = delete;                                          \
  auto operator->() const && = delete;                                         \
  auto get() const && = delete;                                                \
  auto unsafe_get() const && = delete;                                         \
  /** @} */

} // namespace reloco
