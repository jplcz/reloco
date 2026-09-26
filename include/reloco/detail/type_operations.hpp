// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file type_operations.hpp
 * @brief `type_operations`: a per-`T` table of *single-element*
 * construct/clone/destroy/relocate primitives, type-erased behind `void*` +
 * `type_metadata` exactly like `vector_base.hpp`'s `vector_operations`, but
 * deliberately scoped to one element at a time rather than a contiguous
 * range.
 *
 * `vector_operations` (in `vector_base.hpp`) is *array-shaped*: its
 * `move_range`/`move_range_up` assume a contiguous buffer being shifted
 * left/right, which only makes sense for `vector`/`inline_vector`/
 * `sso_vector`/`outline_vector` and the `flat_set`/`flat_map` family built
 * on top of them. A node- or bucket-based container (a future type-erased
 * map/list engine) never shifts a contiguous range, but still needs the
 * exact same *per-element* facts: how to destroy one `T` in place, how to
 * clone one `T` into uninitialized storage, how to default/copy-construct
 * one `T`, how to relocate one `T` from one address to another. This
 * header factors those four single-element primitives out of
 * `vector_base.hpp` so both engines resolve them the same way, once per
 * `T`, instead of each hand-rolling its own tiered `construction_helpers`
 * dispatch loop.
 *
 * `vector_operations`'s own range operations remain exactly where they are
 * -- this header does not replace `vector_operations`, it underlies it:
 * `vector_base.hpp`'s non-trivial range resolvers loop over
 * `get_type_operations_for<T>()`'s single-element function pointers
 * instead of duplicating the per-element tiered dispatch inline, while
 * `vector_operations`'s *trivial* range fast path (`trivial_operator_set`,
 * whole-range `memcpy`/`memset`) is untouched: looping a single-element
 * table entry once per index would be strictly slower than one `memcpy`
 * for trivial `T`, so that fast path is deliberately not rebuilt on top of
 * this header.
 *
 * Like `vector_operations`, resolution avoids per-`T` template pollution
 * for the common case: `get_type_operations_for<T>()` returns the single
 * shared `operations_for_trivial_element` table (rather than
 * instantiating a fresh `type_operations_for<T>`) whenever `T` is
 * trivially destructible/relocatable/copyable enough, and a
 * `std::pair<T1, T2>` partial specialization extends that sharing to pairs
 * of independently-trivial types.
 */

#include "../construction_helpers.hpp"
#include "../error.hpp"
#include "../reloco_extern.hpp"
#include "type_metadata.hpp"

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

namespace reloco::detail {

/**
 * @brief Per-`T` table of type-erased *single-element* operations, shared
 * by any type-erased container engine (contiguous-array or node-based)
 * that needs to construct/clone/destroy/relocate one `T` at a time without
 * a template parameter of its own; resolved once at compile time by
 * `get_type_operations_for<T>()`.
 */
struct RELOCO_EXPORT type_operations {
  /// @brief Destroy a single, already-constructed element. `nullptr` (a
  /// no-op) when `T` is trivially destructible.
  void (*destroy_one)(const type_metadata &type, void *data) noexcept;
  /// @brief Clone a single already-constructed @p src element into
  /// uninitialized @p dest storage, using fallible clone.
  result<void> (*clone_one)(const type_metadata &type, allocator_ref alloc, void *dest, const void *src) noexcept;
  /// @brief Copy-construct (if @p value_ptr is not `nullptr`) or
  /// default-construct a single element into uninitialized @p dest
  /// storage.
  result<void> (*copy_construct_one)(const type_metadata &type, allocator_ref alloc, void *dest,
                                     const void *value_ptr) noexcept;
  /// @brief Move-construct the element at @p from into uninitialized
  /// @p to storage, then destroy the element at @p from -- relocating one
  /// element from one address to another (addresses must not overlap).
  void (*relocate_one)(const type_metadata &type, void *to, const void *from) noexcept;
};

/**
 * @brief `memcpy`-based `type_operations` bodies shared by every trivially
 * destructible/relocatable/copyable `T`, avoiding a per-element call
 * through `construction_helpers` entirely.
 */

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

struct RELOCO_EXPORT trivial_type_operations {
  static result<void> clone_one(const type_metadata &type, allocator_ref, void *dest, const void *src) noexcept {
    std::memcpy(dest, src, type.element_size);
    return {};
  } // namespace reloco::detail

  static result<void> copy_construct_one(const type_metadata &type, allocator_ref, void *dest,
                                         const void *value_ptr) noexcept {
    if (!value_ptr) {
      std::memset(dest, 0, type.element_size);
    } else {
      std::memcpy(dest, value_ptr, type.element_size);
    }
    return {};
  }

  static void relocate_one(const type_metadata &type, void *to, const void *from) noexcept {
    std::memcpy(to, from, type.element_size);
  }
};

RELOCO_END_UNSAFE_BUFFER_USAGE

inline constexpr type_operations operations_for_trivial_element = {
    nullptr,
    &trivial_type_operations::clone_one,
    &trivial_type_operations::copy_construct_one,
    &trivial_type_operations::relocate_one,
};

/**
 * @brief `true` when `T` qualifies for the single shared
 * `operations_for_trivial_element` table instead of a per-`T`
 * `type_operations_for<T>` instantiation. Also reused by `vector_base.hpp`
 * as `has_trivial_vector_ops<T>`, since a range of `T` can use the whole-
 * range `memcpy`/`memset` fast path under exactly the same condition.
 */
template <typename T>
constexpr inline bool has_trivial_type_ops =
    std::is_trivially_destructible_v<T> && is_trivially_relocatable_v<T> && std::is_trivially_copyable_v<T>;

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

template <typename T, typename = void> struct destroy_one_resolver {
  static constexpr auto get() noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      return [](const type_metadata &, void *data) noexcept { static_cast<T *>(data)->~T(); };
    } else {
      return nullptr;
    }
  }
};

template <typename T, typename = void> struct clone_one_resolver {
  static constexpr auto get() noexcept {
    if constexpr (std::is_trivially_copyable_v<T>) {
      return &trivial_type_operations::clone_one;
    } else if constexpr (is_try_cloneable_v<T>) {
      return [](const type_metadata &, allocator_ref alloc, void *dest, const void *src) noexcept -> result<void> {
        return construction_helpers::try_clone_at(alloc, static_cast<T *>(dest), *static_cast<const T *>(src));
      };
    } else {
      return static_cast<decltype(&trivial_type_operations::clone_one)>(nullptr);
    }
  }
};

template <typename T> struct copy_construct_one_resolver {
  static constexpr auto get() noexcept {
    if constexpr (std::is_trivially_copyable_v<T> && std::is_trivially_default_constructible_v<T>) {
      return &trivial_type_operations::copy_construct_one;
    } else {
      return
          [](const type_metadata &, allocator_ref alloc, void *dest, const void *value_ptr) noexcept -> result<void> {
            T *ptr = static_cast<T *>(dest);
            if (!value_ptr) {
              // --- Path A: Default construction ---
              if constexpr (!std::is_default_constructible_v<T>) {
                return unexpected(error::invalid_argument);
              } else {
                return construction_helpers::try_construct<T>(alloc, ptr);
              }
            } else {
              // --- Path B: Copy construction ---
              if constexpr (!is_try_constructible_v<T, const T &> && !std::is_copy_constructible_v<T>) {
                return unexpected(error::unsupported_operation);
              } else {
                return construction_helpers::try_construct<T>(alloc, ptr, *static_cast<const T *>(value_ptr));
              }
            }
          };
    }
  }
};

template <typename T> struct relocate_one_resolver {
  static constexpr auto get() noexcept {
    if constexpr (is_trivially_relocatable_v<T>) {
      return &trivial_type_operations::relocate_one;
    } else {
      return [](const type_metadata &, void *to, const void *from) noexcept {
        T *d = static_cast<T *>(to);
        T *s = const_cast<T *>(static_cast<const T *>(from));
        new (d) T(std::move(*s));
        if constexpr (!std::is_trivially_destructible_v<T>) {
          s->~T();
        }
      };
    }
  }
};

template <typename T>
inline constexpr type_operations type_operations_for = {
    destroy_one_resolver<T>::get(),
    clone_one_resolver<T>::get(),
    copy_construct_one_resolver<T>::get(),
    relocate_one_resolver<T>::get(),
};

template <typename T, typename = void> struct type_operations_maker {
  static constexpr const type_operations *make() noexcept {
    if constexpr (has_trivial_type_ops<T>) {
      return &operations_for_trivial_element;
    } else {
      return &type_operations_for<T>;
    }
  }
};

template <typename T1, typename T2>
struct type_operations_maker<std::pair<T1, T2>,
                             std::enable_if_t<(has_trivial_type_ops<T1> && has_trivial_type_ops<T2>), void>> {
  static constexpr const type_operations *make() noexcept { return &operations_for_trivial_element; }
};

/**
 * @brief Master dispatcher returning `operations_for_trivial_element` for
 * fully trivial types, or `type_operations_for<T>` with selective trivial
 * backend routing (per function pointer) for hybrid types.
 */
template <typename T> inline constexpr const type_operations *get_type_operations_for() noexcept {
  return type_operations_maker<T>::make();
}

RELOCO_END_UNSAFE_BUFFER_USAGE

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "type_operations.ipp"
#endif

} // namespace reloco::detail
