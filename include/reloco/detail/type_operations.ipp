// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file type_operations.ipp
 * @brief Out-of-line bodies for `trivial_type_operations`, declared in
 * `type_operations.hpp`. Included from `type_operations.hpp` itself,
 * guarded on `RELOCO_SHARED_PROVIDE_DEFINITIONS` (see
 * `reloco/detail/compat.hpp`). Never included directly.
 */

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

RELOCO_API result<void> trivial_type_operations::clone_one(const type_metadata &type, allocator_ref, void *dest,
                                                           const void *src) noexcept {
  std::memcpy(dest, src, type.element_size);
  return {};
}

RELOCO_API result<void> trivial_type_operations::copy_construct_one(const type_metadata &type, allocator_ref,
                                                                    void *dest, const void *value_ptr) noexcept {
  if (!value_ptr) {
    std::memset(dest, 0, type.element_size);
  } else {
    std::memcpy(dest, value_ptr, type.element_size);
  }
  return {};
}

RELOCO_API void trivial_type_operations::relocate_one(const type_metadata &type, void *to, const void *from) noexcept {
  std::memcpy(to, from, type.element_size);
}

RELOCO_END_UNSAFE_BUFFER_USAGE
