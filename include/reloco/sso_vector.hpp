// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file sso_vector.hpp
 * @brief Growable dynamic array with an embedded small-size optimization
 * (SSO) buffer, and fallible mutation.
 *
 * `sso_vector<T, InlineCapacity>` sits between `vector<T>` (see
 * `vector.hpp`) and `inline_vector<T, Capacity>` (see `inline_vector.hpp`):
 * like `vector<T>`, it is backed by an `allocator_ref` (see `allocator.hpp`)
 * and grows onto the heap without bound; like `inline_vector<T, Capacity>`,
 * up to `InlineCapacity` elements live directly inside the object, in a raw
 * `alignas(reloco::effective_alignment_v<T>) std::byte` buffer (see
 * `alignment.hpp`), so small instances never allocate at all. This is the
 * same shape as LLVM's `SmallVector`, Boost's `small_vector`, and Abseil's
 * `InlinedVector` -- useful wherever most instances are expected to stay
 * small but an unbounded few legitimately need to grow, unlike
 * `inline_vector<T, Capacity>`, which fails outright past `Capacity`.
 *
 * `InlineCapacity` must be greater than zero (`static_assert`ed below),
 * mirroring `inline_vector<T, Capacity>`'s own restriction, and is a
 * required template parameter (there is no default) since a sensible
 * inline capacity depends on `T`, unlike `basic_sso_string`'s
 * `RELOCO_SSO_STRING_CAPACITY` macro (see `sso_string.hpp`), which is
 * shared across every `CharT`.
 *
 * A single `data_` pointer is always either the address of the object's
 * own inline buffer (`is_inline()` returns true) or a heap pointer obtained
 * through `alloc_` -- exactly the same discriminator `basic_sso_string`
 * uses for its `data_`/`sso_buf_` pair. `try_reserve` promotes from inline
 * to heap the first time growth exceeds `InlineCapacity` (there is no
 * existing heap allocation to `expand_in_place` into, so this always
 * allocates fresh and move/memcpy's the inline elements across);
 * `shrink_to_fit` demotes back from heap to inline once `size()` fits
 * within `InlineCapacity` again. Once heap-backed, growth/shrink behavior
 * is identical to `vector<T>`: `allocator_ref::expand_in_place` first, then
 * either a single `reallocate` (when `is_trivially_relocatable_v<T>`) or a
 * manual move-construct/destroy loop into a fresh block.
 *
 * Because the object's inline buffer is a subobject of `*this`,
 * `is_trivially_relocatable<sso_vector<T, InlineCapacity>>` is
 * unconditionally `false` regardless of `T` (see `relocatable.hpp`): a
 * small instance's `data_` points into its own storage, so relocating the
 * object via `memcpy` would leave `data_` dangling into the old location --
 * the same rationale `basic_sso_string` documents for its own
 * specialization.
 *
 * Because `data_` requires computing the address of the embedded buffer
 * (`reinterpret_cast`+`std::launder`, exactly like `inline_vector`'s
 * `slot()`), and `reinterpret_cast` cannot appear in a constant expression,
 * `sso_vector`'s constructors are not `constexpr`, unlike `vector<T>`'s and
 * `inline_vector<T, Capacity>`'s.
 *
 * Element construction and cloning go through `construction_helpers` (see
 * `construction_helpers.hpp`) exactly like `vector<T>`/`inline_vector<T,
 * Capacity>`. Read-only/mutating access to already-owned elements follows
 * the same checked/`try_*`/`unsafe_*` tri-tier convention as
 * `vector`/`inline_vector`/`span`/`array` (see
 * `docs/hardened-containers.md`).
 *
 * Like `vector.hpp`/`inline_vector.hpp`, this file performs raw pointer
 * arithmetic and placement-new/-delete with no bounds-tracked alternative,
 * so its `namespace reloco` body is wrapped in
 * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE`.
 */

#include "alignment.hpp"
#include "allocator.hpp"
#include "collection_view.hpp"
#include "construction_helpers.hpp"
#include "container_ref.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "detail/vector_base.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "inline_vector.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T, std::size_t InlineCapacity>
class RELOCO_OWNER sso_vector : detail::inline_vector_storage<T, InlineCapacity>,
                                public detail::typed_vector_base<T, detail::mixed_vector_base> {
  static_assert(InlineCapacity > 0,
                "sso_vector requires a positive InlineCapacity; a zero-capacity instance would never be able to "
                "stay inline (see vector<T> for a purely heap-backed shape).");

  using base_t = detail::typed_vector_base<T, detail::mixed_vector_base>;

public:
  using typename base_t::allocator_type;
  using typename base_t::const_iterator;
  using typename base_t::const_pointer;
  using typename base_t::const_reference;
  using typename base_t::difference_type;
  using typename base_t::iterator;
  using typename base_t::pointer;
  using typename base_t::reference;
  using typename base_t::size_type;
  using typename base_t::value_type;

  sso_vector() noexcept : sso_vector(default_allocator()) {}

  explicit sso_vector(allocator_ref alloc) noexcept : base_t(this->storage_bytes_, InlineCapacity, alloc) {}

  sso_vector(const sso_vector &) = delete;
  sso_vector &operator=(const sso_vector &) = delete;

  sso_vector(sso_vector &&other) noexcept : base_t(this->storage_bytes_, InlineCapacity, other.get_allocator()) {
    base_t::move_construct_from_base(detail::get_operations_for<T>(), detail::metadata_for<T>, std::move(other));
  }

  sso_vector &operator=(sso_vector &&other) noexcept {
    if (this != &other) {
      this->clear();
      base_t::move_assign_from_base(detail::get_operations_for<T>(), detail::metadata_for<T>, std::move(other));
    }
    return *this;
  }

  ~sso_vector() noexcept { this->destroy_elements(detail::get_operations_for<T>(), detail::metadata_for<T>); }

  // ---- fallible construction / cloning (see concepts.hpp) ----

  /**
   * @brief Allocates and reserves storage for @p initial_cap elements,
   * using the given allocator. `initial_cap <= InlineCapacity` never
   * allocates.
   */
  [[nodiscard]] static result<sso_vector> try_allocate(allocator_ref alloc, size_type initial_cap = 0) noexcept {
    sso_vector vec(alloc);
    if (initial_cap > InlineCapacity) {
      auto res = vec.try_reserve(initial_cap);
      if (!res)
        return unexpected(res.error());
    }
    return vec;
  }

  /**
   * @brief Allocates and reserves storage for @p initial_cap elements,
   * using the process-wide default allocator (see `default_allocator()`).
   */
  [[nodiscard]] static result<sso_vector> try_create(size_type initial_cap = 0) noexcept {
    return try_allocate(default_allocator(), initial_cap);
  }

  /**
   * @brief Fallible deep copy using a caller-chosen allocator.
   *
   * Uses a single `std::memcpy` when `T` is trivially copyable and does
   * not implement its own `try_clone`/`try_clone_at`; otherwise clones each
   * element in turn through `construction_helpers::try_clone_at`, rolling
   * back (destroying) already-cloned elements if a later one fails.
   */
  [[nodiscard]] result<sso_vector> try_clone(allocator_ref alloc) const noexcept {
    sso_vector clone(alloc);
    if (this->size_ == 0)
      return clone;

    if (auto reserve_res = clone.try_reserve(this->size_); !reserve_res) {
      return unexpected(reserve_res.error());
    }

    if (detail::get_operations_for<T>()->clone_range) {
      if (auto clone_res = detail::get_operations_for<T>()->clone_range(detail::metadata_for<T>, this->data_,
                                                                        clone.data_, this->size_, alloc);
          !clone_res) {
        return unexpected(clone_res.error());
      }
    }

    clone.size_ = this->size_;
    return clone;
  }

  /**
   * @brief Fallible deep copy reusing this vector's own allocator.
   */
  [[nodiscard]] result<sso_vector> try_clone() const noexcept { return try_clone(this->get_allocator()); }

  /**
   * @brief Fallible deep copy directly into uninitialized storage.
   */
  [[nodiscard]] static result<void> try_clone_at(allocator_ref alloc, sso_vector *storage,
                                                 const sso_vector &source) noexcept {
    auto res = source.try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    new (storage) sso_vector(std::move(*res));
    return {};
  }
};

/**
 * @brief `sso_vector<T, InlineCapacity>` is never trivially relocatable,
 * regardless of `T`: a small instance's `data_` points into its own
 * embedded inline buffer, so relocating the object via `memcpy` would
 * leave `data_` dangling into the old location (the same rationale
 * `basic_sso_string` documents for its own specialization).
 */
template <typename T, std::size_t InlineCapacity>
struct is_trivially_relocatable<sso_vector<T, InlineCapacity>> : std::false_type {};

/**
 * @brief Adapts `sso_vector<T, InlineCapacity>` for the collection views.
 */
template <typename T, std::size_t InlineCapacity> struct collection_view_traits<reloco::sso_vector<T, InlineCapacity>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = true;

  static std::size_t size(const reloco::sso_vector<T, InlineCapacity> &c) noexcept { return c.size(); }
  static bool empty(const reloco::sso_vector<T, InlineCapacity> &c) noexcept { return c.empty(); }
  static T &at(reloco::sso_vector<T, InlineCapacity> &c, std::size_t index) noexcept { return c[index]; }
  static const T &at(const reloco::sso_vector<T, InlineCapacity> &c, std::size_t index) noexcept { return c[index]; }
  static T *data(reloco::sso_vector<T, InlineCapacity> &c) noexcept { return c.data(); }
  static const T *data(const reloco::sso_vector<T, InlineCapacity> &c) noexcept { return c.data(); }
};

/**
 * @brief Adapts `sso_vector<T, InlineCapacity>` for `mutable_container_ref`.
 */
template <typename T, std::size_t InlineCapacity> struct container_ref_traits<reloco::sso_vector<T, InlineCapacity>> {
  using element_type = T;
  static constexpr bool is_associative = false;

  static std::size_t size(const reloco::sso_vector<T, InlineCapacity> &c) noexcept { return c.size(); }
  static bool empty(const reloco::sso_vector<T, InlineCapacity> &c) noexcept { return c.empty(); }
  static void clear(reloco::sso_vector<T, InlineCapacity> &c) noexcept { c.clear(); }
  static T &at(reloco::sso_vector<T, InlineCapacity> &c, std::size_t index) noexcept { return c[index]; }

  static result<void> try_push_back(reloco::sso_vector<T, InlineCapacity> &c, T value) noexcept {
    auto res = c.try_push_back(std::move(value));
    if (!res)
      return unexpected(res.error());
    return {};
  }

  static result<void> try_push_front(reloco::sso_vector<T, InlineCapacity> &c, T value) noexcept {
    auto res = c.try_insert_at(0, std::move(value));
    if (!res)
      return unexpected(res.error());
    return {};
  }

  static result<void> try_insert_at(reloco::sso_vector<T, InlineCapacity> &c, std::size_t index, T value) noexcept {
    auto res = c.try_insert_at(index, std::move(value));
    if (!res)
      return unexpected(res.error());
    return {};
  }

  static result<void> try_erase_at(reloco::sso_vector<T, InlineCapacity> &c, std::size_t index) noexcept {
    return c.try_erase_at(index);
  }
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
