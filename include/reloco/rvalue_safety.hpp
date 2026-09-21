// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/assert.hpp"
#include <cstddef>

namespace reloco {

/**
 * @def RELOCO_BLOCK_RVALUE_ACCESS
 * @brief Hardens a container by explicitly deleting rvalue accessors.
 *
 * This macro prevents references, pointers, and iterators from being obtained
 * from temporary containers and becoming dangling at the end of the full
 * expression.
 *
 * @param Type The underlying element type.
 */
#define RELOCO_BLOCK_RVALUE_ACCESS(Type)                                                                               \
  Type &operator[](std::size_t) const && = delete;                                                                     \
  Type &operator[](std::size_t) && = delete;                                                                           \
  auto front() const && = delete;                                                                                      \
  auto back() const && = delete;                                                                                       \
  auto try_front() const && = delete;                                                                                  \
  auto try_back() const && = delete;                                                                                   \
  auto at(std::size_t) const && = delete;                                                                              \
  auto try_at(std::size_t) const && = delete;                                                                          \
  auto unsafe_at(std::size_t) const && = delete;                                                                       \
  auto data() const && = delete;                                                                                       \
  auto try_data() const && = delete;                                                                                   \
  auto unsafe_data() const && = delete;                                                                                \
  auto begin() const && = delete;                                                                                      \
  auto end() const && = delete;                                                                                        \
  auto rbegin() const && = delete;                                                                                     \
  auto rend() const && = delete;                                                                                       \
  auto cbegin() const && = delete;                                                                                     \
  auto cend() const && = delete;                                                                                       \
  auto crbegin() const && = delete;                                                                                    \
  auto crend() const && = delete;                                                                                      \
  auto operator*() const && = delete;                                                                                  \
  auto operator->() const && = delete;                                                                                 \
  auto get() const && = delete;                                                                                        \
  auto unsafe_get() const && = delete

} // namespace reloco
