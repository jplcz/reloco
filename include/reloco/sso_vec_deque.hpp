// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file sso_vec_deque.hpp
 * @brief Growable double-ended queue with an embedded small-size
 * optimization (SSO) buffer, and fallible mutation.
 *
 * `sso_vec_deque<T, InlineCapacity>` sits between `vec_deque<T>` (see
 * `vec_deque.hpp`) and `inline_vec_deque<T, Capacity>` (see
 * `inline_vec_deque.hpp`), exactly mirroring how `sso_vector<T,
 * InlineCapacity>` (see `sso_vector.hpp`) sits between `vector<T>` and
 * `inline_vector<T, Capacity>`: like `vec_deque<T>`, it is backed by an
 * `allocator_ref` (see `allocator.hpp`) and grows onto the heap without
 * bound; like `inline_vec_deque<T, Capacity>`, up to `InlineCapacity`
 * elements live directly inside the object, in a raw
 * `alignas(reloco::effective_alignment_v<T>) std::byte` ring buffer (see
 * `alignment.hpp`), reusing `inline_vector.hpp`'s
 * `detail::inline_vector_storage<T, InlineCapacity>` byte-buffer helper
 * as-is, so small instances never allocate at all.
 *
 * `InlineCapacity` must be greater than zero (`static_assert`ed below),
 * mirroring `sso_vector<T, InlineCapacity>`'s own restriction.
 *
 * A single `data_` pointer is always either the address of the object's
 * own inline ring buffer (`is_inline()` returns true) or a heap pointer
 * obtained through the bound allocator -- exactly the same discriminator
 * `sso_vector<T, InlineCapacity>` uses. `try_reserve` promotes from inline
 * to heap the first time growth exceeds `InlineCapacity`, linearizing the
 * (possibly wrapped) inline ring buffer into the fresh heap allocation;
 * once heap-backed, growth behavior is identical to `vec_deque<T>`.
 *
 * Because the object's inline buffer is a subobject of `*this`,
 * `is_trivially_relocatable<sso_vec_deque<T, InlineCapacity>>` is
 * unconditionally `false` regardless of `T`, for the same reason
 * `sso_vector<T, InlineCapacity>` documents for its own specialization.
 *
 * Because `data_` requires computing the address of the embedded buffer,
 * and `reinterpret_cast` cannot appear in a constant expression,
 * `sso_vec_deque`'s constructors are not `constexpr`, unlike
 * `vec_deque<T>`'s and `inline_vec_deque<T, Capacity>`'s.
 *
 * Like `sso_vector.hpp`, this file performs raw pointer arithmetic and
 * placement-new/-delete with no bounds-tracked alternative, so its
 * `namespace reloco` body is wrapped in
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
#include <functional>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T, std::size_t InlineCapacity>
class RELOCO_OWNER sso_vec_deque : detail::inline_vector_storage<T, InlineCapacity>,
                                   public detail::typed_deque_base<T, detail::mixed_deque_base> {
  static_assert(InlineCapacity > 0,
                "sso_vec_deque requires a positive InlineCapacity; a zero-capacity instance would never be able to "
                "stay inline (see vec_deque<T> for a purely heap-backed shape).");

  using base_t = detail::typed_deque_base<T, detail::mixed_deque_base>;

public:
  using typename base_t::allocator_type;
  using typename base_t::const_pointer;
  using typename base_t::const_reference;
  using typename base_t::difference_type;
  using typename base_t::pointer;
  using typename base_t::reference;
  using typename base_t::size_type;
  using typename base_t::value_type;

  sso_vec_deque() noexcept : sso_vec_deque(default_allocator()) {}

  explicit sso_vec_deque(allocator_ref alloc) noexcept : base_t(this->storage_bytes_, InlineCapacity, alloc) {}

  sso_vec_deque(const sso_vec_deque &) = delete;
  sso_vec_deque &operator=(const sso_vec_deque &) = delete;

  sso_vec_deque(sso_vec_deque &&other) noexcept : base_t(this->storage_bytes_, InlineCapacity, other.get_allocator()) {
    base_t::move_construct_from_base(detail::get_operations_for<T>(), detail::metadata_for<T>, std::move(other));
  }

  sso_vec_deque &operator=(sso_vec_deque &&other) noexcept {
    if (this != &other) {
      this->clear();
      base_t::move_assign_from_base(detail::get_operations_for<T>(), detail::metadata_for<T>, std::move(other));
    }
    return *this;
  }

  ~sso_vec_deque() noexcept { this->destroy_elements(detail::get_operations_for<T>(), detail::metadata_for<T>); }

  // ---- fallible construction / cloning (see concepts.hpp) ----

  /**
   * @brief Allocates and reserves storage for @p initial_cap elements,
   * using the given allocator. `initial_cap <= InlineCapacity` never
   * allocates.
   */
  [[nodiscard]] static result<sso_vec_deque> try_allocate(allocator_ref alloc, size_type initial_cap = 0) noexcept {
    sso_vec_deque deque(alloc);
    if (initial_cap > InlineCapacity) {
      auto res = deque.try_reserve(initial_cap);
      if (!res)
        return unexpected(res.error());
    }
    return deque;
  }

  /**
   * @brief Allocates and reserves storage for @p initial_cap elements,
   * using the process-wide default allocator (see `default_allocator()`).
   */
  [[nodiscard]] static result<sso_vec_deque> try_create(size_type initial_cap = 0) noexcept {
    return try_allocate(default_allocator(), initial_cap);
  }

  /**
   * @brief Fallible deep copy using a caller-chosen allocator. The deque
   * may be physically wrapped, so both chunks returned by `as_slices()`
   * are cloned in turn directly into the contiguous layout of the newly
   * allocated target.
   */
  [[nodiscard]] result<sso_vec_deque> try_clone(allocator_ref alloc) const noexcept {
    auto res = try_allocate(alloc, this->size());
    if (!res)
      return unexpected(res.error());
    sso_vec_deque clone = std::move(*res);

    if (this->size() == 0)
      return clone;

    const auto *ops = detail::get_operations_for<T>();
    const auto &type = detail::metadata_for<T>;
    if (!ops->clone_range)
      return unexpected(error::unsupported_operation);

    auto [s1, s2] = this->as_slices();

    if (!s1.empty()) {
      auto cr1 = ops->clone_range(type, s1.data(), clone.data_, s1.size(), alloc);
      if (!cr1)
        return unexpected(cr1.error());
      clone.len_ += s1.size();
    }

    if (!s2.empty()) {
      auto cr2 = ops->clone_range(type, s2.data(), static_cast<T *>(clone.data_) + s1.size(), s2.size(), alloc);
      if (!cr2)
        return unexpected(cr2.error());
      clone.len_ += s2.size();
    }

    return clone;
  }

  /**
   * @brief Fallible deep copy reusing this deque's own allocator.
   */
  [[nodiscard]] result<sso_vec_deque> try_clone() const noexcept { return try_clone(this->get_allocator()); }

  /**
   * @brief Fallible deep copy directly into uninitialized storage.
   */
  [[nodiscard]] static result<void> try_clone_at(allocator_ref alloc, sso_vec_deque *storage,
                                                 const sso_vec_deque &source) noexcept {
    auto res = source.try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    new (storage) sso_vec_deque(std::move(*res));
    return {};
  }
};

/**
 * @brief `sso_vec_deque<T, InlineCapacity>` is never trivially relocatable,
 * regardless of `T`: a small instance's `data_` points into its own
 * embedded inline buffer, so relocating the object via `memcpy` would
 * leave `data_` dangling into the old location (the same rationale
 * `sso_vector<T, InlineCapacity>` documents for its own specialization).
 */
template <typename T, std::size_t InlineCapacity>
struct is_trivially_relocatable<sso_vec_deque<T, InlineCapacity>> : std::false_type {};

/**
 * @brief Adapts `sso_vec_deque<T, InlineCapacity>` for collection views.
 * `has_data` is `false` because the queue might wrap around physically.
 */
template <typename T, std::size_t InlineCapacity>
struct collection_view_traits<reloco::sso_vec_deque<T, InlineCapacity>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = false;
  static constexpr bool is_mutable = true;

  static std::size_t size(const reloco::sso_vec_deque<T, InlineCapacity> &c) noexcept { return c.size(); }
  static bool empty(const reloco::sso_vec_deque<T, InlineCapacity> &c) noexcept { return c.empty(); }
  static T &at(reloco::sso_vec_deque<T, InlineCapacity> &c, std::size_t index) noexcept { return c[index]; }
  static const T &at(const reloco::sso_vec_deque<T, InlineCapacity> &c, std::size_t index) noexcept { return c[index]; }
};

/**
 * @brief Adapts `sso_vec_deque<T, InlineCapacity>` for
 * `mutable_container_ref`.
 */
template <typename T, std::size_t InlineCapacity>
struct container_ref_traits<reloco::sso_vec_deque<T, InlineCapacity>> {
  using element_type = std::decay_t<T>;
  static constexpr bool is_associative = false;

  static std::size_t size(const reloco::sso_vec_deque<T, InlineCapacity> &c) noexcept { return c.size(); }
  static bool empty(const reloco::sso_vec_deque<T, InlineCapacity> &c) noexcept { return c.empty(); }
  static void clear(reloco::sso_vec_deque<T, InlineCapacity> &c) noexcept { c.clear(); }
  static T &at(reloco::sso_vec_deque<T, InlineCapacity> &c, std::size_t index) noexcept { return c[index]; }

  static result<void> try_push_back(reloco::sso_vec_deque<T, InlineCapacity> &c, T value) noexcept {
    return c.try_push_back(std::move(value));
  }

  static result<void> try_push_front(reloco::sso_vec_deque<T, InlineCapacity> &c, T value) noexcept {
    return c.try_push_front(std::move(value));
  }

  static result<void> try_insert_at(reloco::sso_vec_deque<T, InlineCapacity> &c, std::size_t index, T value) noexcept {
    return c.try_insert_at(index, std::move(value));
  }

  static result<void> try_erase_at(reloco::sso_vec_deque<T, InlineCapacity> &c, std::size_t index) noexcept {
    return c.try_erase_at(index);
  }
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
