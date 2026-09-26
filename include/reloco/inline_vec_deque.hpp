// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file inline_vec_deque.hpp
 * @brief Fixed-capacity, allocator-free double-ended queue with fallible
 * mutation.
 *
 * `inline_vec_deque<T, Capacity>` is `vec_deque<T>`'s (see `vec_deque.hpp`)
 * fixed-capacity counterpart, exactly mirroring how `inline_vector<T,
 * Capacity>` (see `inline_vector.hpp`) relates to `vector<T>`: instead of an
 * `allocator_ref`-backed heap allocation, its elements live directly inside
 * the object, in a raw `alignas(reloco::effective_alignment_v<T>)
 * std::byte` ring buffer sized for exactly `Capacity` elements (see
 * `alignment.hpp`), reusing `inline_vector.hpp`'s
 * `detail::inline_vector_storage<T, Capacity>` byte-buffer helper as-is --
 * that storage shape is just a raw aligned buffer, nothing vector-specific.
 * This is the shape needed wherever a bounded, small ring buffer is known
 * at compile time and heap allocation must be avoided entirely.
 *
 * `Capacity` must be greater than zero (`static_assert`ed below), mirroring
 * `inline_vector<T, Capacity>`'s own restriction.
 *
 * Mutation follows the same fallible `result<T>`-returning shape as
 * `vec_deque<T>`: `try_push_back`/`try_push_front`/`try_emplace_at`/
 * `try_insert_at` fail with `error::capacity_exceeded` once
 * `size() == Capacity`, since there is no allocator to grow into. Every
 * other operation -- `rotate_left`/`rotate_right`, `try_erase_at`,
 * `try_swap_remove_back`/`try_swap_remove_front`, `as_slices`,
 * `try_make_contiguous`, `contains` -- works identically to `vec_deque<T>`
 * and is inherited unchanged from `detail::typed_deque_base` (see
 * `docs/deque-containers.md`).
 *
 * `inline_vec_deque<T, Capacity>` can also be "upgraded" to a heap-backed
 * `vec_deque<T>` via `try_to_vec_deque()`, mirroring `inline_vector<T,
 * Capacity>::try_to_vector()`.
 *
 * `is_trivially_relocatable<inline_vec_deque<T, Capacity>>` is specialized
 * below to `is_trivially_relocatable<T>` (conditional, unlike
 * `vec_deque<T>`'s unconditional specialization), for the same reason as
 * `inline_vector<T, Capacity>`: its storage is embedded directly in the
 * object rather than behind a heap pointer.
 *
 * Like `inline_vector.hpp`, this file performs raw pointer arithmetic and
 * placement-new/-delete into its own byte buffer with no bounds-tracked
 * alternative, so its `namespace reloco` body is wrapped in
 * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE`.
 */

#include "alignment.hpp"
#include "collection_view.hpp"
#include "construction_helpers.hpp"
#include "container_ref.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "inline_vector.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"
#include "vec_deque.hpp"

#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T, std::size_t Capacity>
class RELOCO_OWNER inline_vec_deque : detail::inline_vector_storage<T, Capacity>,
                                      public detail::typed_deque_base<T, detail::inline_deque_base> {
  static_assert(Capacity > 0,
                "inline_vec_deque requires a positive Capacity; there is no zero-capacity specialization.");

  using base_t = detail::typed_deque_base<T, detail::inline_deque_base>;

public:
  using typename base_t::allocator_type;
  using typename base_t::const_pointer;
  using typename base_t::const_reference;
  using typename base_t::difference_type;
  using typename base_t::pointer;
  using typename base_t::reference;
  using typename base_t::size_type;
  using typename base_t::value_type;

  constexpr inline_vec_deque() noexcept : base_t(this->storage_bytes_, Capacity) {}

  inline_vec_deque(const inline_vec_deque &) = delete;
  inline_vec_deque &operator=(const inline_vec_deque &) = delete;

  inline_vec_deque(inline_vec_deque &&other) noexcept : base_t(this->storage_bytes_, Capacity) {
    base_t::move_construct_from_base(detail::get_operations_for<T>(), detail::metadata_for<T>, std::move(other));
  }

  inline_vec_deque &operator=(inline_vec_deque &&other) noexcept {
    if (this != &other) {
      this->clear();
      base_t::move_assign_from_base(detail::get_operations_for<T>(), detail::metadata_for<T>, std::move(other));
    }
    return *this;
  }

  ~inline_vec_deque() noexcept { this->destroy_elements(detail::get_operations_for<T>(), detail::metadata_for<T>); }

  // ---- fallible cloning (see concepts.hpp) ----

  /**
   * @brief Fallible deep copy using a caller-chosen allocator for any
   * nested fallible-allocation `T` (`inline_vec_deque` itself never
   * allocates). The deque may be physically wrapped, so both chunks
   * returned by `as_slices()` are cloned in turn.
   */
  [[nodiscard]] result<inline_vec_deque> try_clone(allocator_ref alloc) const noexcept {
    inline_vec_deque clone;
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
   * @brief Fallible deep copy using the process-wide default allocator
   * (see `default_allocator()`) for any nested fallible-allocation `T`.
   */
  [[nodiscard]] result<inline_vec_deque> try_clone() const noexcept { return try_clone(default_allocator()); }

  /**
   * @brief Fallible deep copy directly into uninitialized storage.
   */
  [[nodiscard]] static result<void> try_clone_at(allocator_ref alloc, inline_vec_deque *storage,
                                                 const inline_vec_deque &source) noexcept {
    auto res = source.try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    new (storage) inline_vec_deque(std::move(*res));
    return {};
  }

  // ---- upgrading to a heap-backed vec_deque<T> ----

  /**
   * @brief Clones every element into a newly heap-allocated `vec_deque<T>`,
   * leaving `*this` untouched. Useful when the fixed `Capacity` has been
   * reached (or is about to be) but the caller still wants a growable
   * deque.
   */
  [[nodiscard]] result<vec_deque<T>> try_to_vec_deque(allocator_ref alloc) const & noexcept {
    auto deque_res = vec_deque<T>::try_allocate(alloc, this->size());
    if (!deque_res)
      return unexpected(deque_res.error());
    vec_deque<T> deque = std::move(*deque_res);
    for (size_type i = 0; i < this->size(); ++i) {
      auto clone_res = construction_helpers::try_clone<T>(alloc, (*this)[i]);
      if (!clone_res)
        return unexpected(clone_res.error());
      auto push_res = deque.try_push_back(std::move(*clone_res));
      if (!push_res)
        return unexpected(push_res.error());
    }
    return deque;
  }

  /**
   * @brief Same as `try_to_vec_deque(allocator_ref)`, using the
   * process-wide default allocator (see `default_allocator()`).
   */
  [[nodiscard]] result<vec_deque<T>> try_to_vec_deque() const & noexcept {
    return try_to_vec_deque(default_allocator());
  }

  /**
   * @brief Moves every element out into a newly heap-allocated
   * `vec_deque<T>`, consuming `*this` (which is left empty regardless of
   * success or failure).
   */
  [[nodiscard]] result<vec_deque<T>> try_to_vec_deque(allocator_ref alloc) && noexcept {
    auto deque_res = vec_deque<T>::try_allocate(alloc, this->size());
    if (!deque_res) {
      this->clear();
      return unexpected(deque_res.error());
    }
    vec_deque<T> deque = std::move(*deque_res);
    while (!this->empty()) {
      auto push_res = deque.try_push_back(std::move(this->front()));
      if (!push_res) {
        this->clear();
        return unexpected(push_res.error());
      }
      std::ignore = this->try_pop_front();
    }
    return deque;
  }

  /**
   * @brief Same as `try_to_vec_deque(allocator_ref) &&`, using the
   * process-wide default allocator (see `default_allocator()`).
   */
  [[nodiscard]] result<vec_deque<T>> try_to_vec_deque() && noexcept {
    return std::move(*this).try_to_vec_deque(default_allocator());
  }

  [[nodiscard]] static constexpr size_type capacity() noexcept { return Capacity; }
  [[nodiscard]] constexpr bool full() const noexcept { return this->size() == Capacity; }
};

/**
 * @brief `inline_vec_deque<T, Capacity>` is trivially relocatable exactly
 * when `T` is: its storage is embedded directly in the object (unlike
 * `vec_deque<T>`'s heap pointer, which is unconditionally relocatable), so
 * relocating it via `memcpy` is only safe when every contained `T` also
 * has no internal/external self-reference.
 */
template <typename T, std::size_t Capacity>
struct is_trivially_relocatable<inline_vec_deque<T, Capacity>> : is_trivially_relocatable<T> {};

/**
 * @brief Adapts `inline_vec_deque<T, Capacity>` for collection views.
 * `has_data` is `false` because the queue might wrap around physically.
 */
template <typename T, std::size_t Capacity> struct collection_view_traits<reloco::inline_vec_deque<T, Capacity>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = false;
  static constexpr bool is_mutable = true;

  static std::size_t size(const reloco::inline_vec_deque<T, Capacity> &c) noexcept { return c.size(); }
  static bool empty(const reloco::inline_vec_deque<T, Capacity> &c) noexcept { return c.empty(); }
  static T &at(reloco::inline_vec_deque<T, Capacity> &c, std::size_t index) noexcept { return c[index]; }
  static const T &at(const reloco::inline_vec_deque<T, Capacity> &c, std::size_t index) noexcept { return c[index]; }
};

/**
 * @brief Adapts `inline_vec_deque<T, Capacity>` for `mutable_container_ref`.
 */
template <typename T, std::size_t Capacity> struct container_ref_traits<reloco::inline_vec_deque<T, Capacity>> {
  using element_type = std::decay_t<T>;
  static constexpr bool is_associative = false;

  static std::size_t size(const reloco::inline_vec_deque<T, Capacity> &c) noexcept { return c.size(); }
  static bool empty(const reloco::inline_vec_deque<T, Capacity> &c) noexcept { return c.empty(); }
  static void clear(reloco::inline_vec_deque<T, Capacity> &c) noexcept { c.clear(); }
  static T &at(reloco::inline_vec_deque<T, Capacity> &c, std::size_t index) noexcept { return c[index]; }

  static result<void> try_push_back(reloco::inline_vec_deque<T, Capacity> &c, T value) noexcept {
    return c.try_push_back(std::move(value));
  }

  static result<void> try_push_front(reloco::inline_vec_deque<T, Capacity> &c, T value) noexcept {
    return c.try_push_front(std::move(value));
  }

  static result<void> try_insert_at(reloco::inline_vec_deque<T, Capacity> &c, std::size_t index, T value) noexcept {
    return c.try_insert_at(index, std::move(value));
  }

  static result<void> try_erase_at(reloco::inline_vec_deque<T, Capacity> &c, std::size_t index) noexcept {
    return c.try_erase_at(index);
  }
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
