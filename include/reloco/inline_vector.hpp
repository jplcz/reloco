// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file inline_vector.hpp
 * @brief Fixed-capacity, allocator-free growable array with fallible
 * mutation.
 *
 * `inline_vector<T, Capacity>` is `vector<T>`'s (see `vector.hpp`)
 * fixed-capacity counterpart: instead of an `allocator_ref`-backed heap
 * allocation, its elements live directly inside the object, in a raw
 * `alignas(reloco::effective_alignment_v<T>) std::byte` buffer sized for
 * exactly `Capacity` elements (see `alignment.hpp`) -- the same "raw
 * storage + placement-new" pattern
 * `inplace_function<Signature, Capacity>` (see `inplace_function.hpp`)
 * already uses for its type-erased callable, just without the vtable. This
 * makes `inline_vector<T, Capacity>` useful wherever a bounded, small
 * number of elements is known at compile time and heap allocation (or
 * even the possibility of it) must be avoided entirely -- e.g. as
 * `inline_flat_set`/`inline_flat_map`'s backing storage (see
 * `flat_map.hpp`).
 *
 * Unlike `array<T, N>` (see `array.hpp`), which is a plain aggregate
 * `T data_[N]` and therefore requires `T` to be default-constructible (and
 * always holds exactly `N` live elements), `inline_vector<T, Capacity>`
 * tracks its own `size()` separately from `Capacity` and only constructs
 * elements as they are pushed/inserted, exactly like `vector<T>`; `T` needs
 * no default constructor.
 *
 * `Capacity` must be greater than zero (`static_assert`ed below) -- unlike
 * `array<T, 0>`, there is no zero-capacity specialization; a zero-capacity
 * `inline_vector` would never be able to hold anything, so it isn't worth
 * the extra surface.
 *
 * Mutation follows the same fallible `result<T>`-returning shape as
 * `vector<T>`: `try_push_back`/`try_emplace_back`/`try_insert_at` fail with
 * `error::capacity_exceeded` (see `error.hpp`) once `size() == Capacity`,
 * since -- unlike `vector<T>` -- there is no allocator to grow into.
 * Element construction/cloning go through `construction_helpers` (see
 * `construction_helpers.hpp`) exactly like `vector<T>`, passing
 * `default_allocator()` as the allocator argument those helpers require --
 * `inline_vector<T, Capacity>` itself never allocates, but a nested `T`
 * that implements its own fallible construction protocol still needs an
 * `allocator_ref` to hand to its own allocation. Read-only/mutating access
 * to already-owned elements follows the same checked/`try_*`/`unsafe_*`
 * tri-tier convention as `vector`/`array`/`span` (see
 * `docs/hardened-containers.md`).
 *
 * `inline_vector<T, Capacity>` can also be "upgraded" to a heap-backed
 * `vector<T>` via `try_to_vector()` -- an `&&`-qualified overload that
 * moves each element out (consuming `*this`, leaving it empty) and a
 * `const &`-qualified overload that clones each element instead (leaving
 * `*this` untouched) -- for callers that reach `inline_vector`'s fixed
 * `Capacity` but need to keep growing.
 *
 * `is_trivially_relocatable<inline_vector<T, Capacity>>` is specialized
 * below to `is_trivially_relocatable<T>` (conditional, unlike `vector<T>`'s
 * unconditional specialization): `inline_vector`'s storage is embedded
 * directly in the object rather than behind a heap pointer, so relocating
 * it via `memcpy` is only safe when every contained `T` is itself
 * trivially relocatable.
 *
 * Like `vector.hpp`/`array.hpp`/`inplace_function.hpp`, this file performs
 * raw pointer arithmetic and placement-new/-delete into its own byte
 * buffer with no bounds-tracked alternative, so its `namespace reloco` body
 * is wrapped in
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
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"
#include "vector.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

namespace detail {

template <typename T, std::size_t Capacity> struct inline_vector_storage {
  alignas(effective_alignment_v<T>) std::byte storage_bytes_[sizeof(T) * Capacity];
};

} // namespace detail

template <typename T, std::size_t Capacity>
class RELOCO_OWNER inline_vector : detail::inline_vector_storage<T, Capacity>,
                                   public detail::typed_vector_base<T, detail::inline_vector_base> {
  static_assert(Capacity > 0, "inline_vector requires a positive Capacity; there is no zero-capacity specialization "
                              "(see array<T, 0> for that shape).");

  using base_t = detail::typed_vector_base<T, detail::inline_vector_base>;

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

  constexpr inline_vector() noexcept : base_t(this->storage_bytes_, Capacity) {}

  inline_vector(const inline_vector &) = delete;
  inline_vector &operator=(const inline_vector &) = delete;

  inline_vector(inline_vector &&other) noexcept : base_t(this->storage_bytes_, Capacity) {
    base_t::move_construct_from_base(detail::get_operations_for<T>(), detail::metadata_for<T>, std::move(other));
  }

  inline_vector &operator=(inline_vector &&other) noexcept {
    if (this != &other) {
      this->clear();
      base_t::move_assign_from_base(detail::get_operations_for<T>(), detail::metadata_for<T>, std::move(other));
    }
    return *this;
  }

  ~inline_vector() noexcept { this->destroy_elements(detail::get_operations_for<T>(), detail::metadata_for<T>); }

  // ---- fallible cloning (see concepts.hpp) ----

  /**
   * @brief Fallible deep copy using a caller-chosen allocator for any
   * nested fallible-allocation `T` (`inline_vector` itself never
   * allocates).
   *
   * Uses a single `std::memcpy` when `T` is trivially copyable and does
   * not implement its own `try_clone`/`try_clone_at`; otherwise clones
   * each element in turn through `construction_helpers::try_clone_at`,
   * rolling back (destroying) already-cloned elements if a later one
   * fails.
   */
  [[nodiscard]] result<inline_vector> try_clone(allocator_ref alloc) const noexcept {
    inline_vector clone;
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
   * @brief Fallible deep copy using the process-wide default allocator
   * (see `default_allocator()`) for any nested fallible-allocation `T`.
   */
  [[nodiscard]] result<inline_vector> try_clone() const noexcept { return try_clone(default_allocator()); }

  /**
   * @brief Fallible deep copy directly into uninitialized storage.
   */
  [[nodiscard]] static result<void> try_clone_at(allocator_ref alloc, inline_vector *storage,
                                                 const inline_vector &source) noexcept {
    auto res = source.try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    new (storage) inline_vector(std::move(*res));
    return {};
  }

  // ---- upgrading to a heap-backed vector<T> ----

  /**
   * @brief Clones every element into a newly heap-allocated `vector<T>`,
   * leaving `*this` untouched. Useful when the fixed `Capacity` has been
   * reached (or is about to be) but the caller still wants a growable
   * container.
   */
  [[nodiscard]] result<vector<T>> try_to_vector(allocator_ref alloc) const & noexcept {
    auto vec_res = vector<T>::try_allocate(alloc, this->size());
    if (!vec_res)
      return unexpected(vec_res.error());
    vector<T> vec = std::move(*vec_res);
    for (size_type i = 0; i < this->size(); ++i) {
      auto clone_res = construction_helpers::try_clone<T>(alloc, (*this)[i]);
      if (!clone_res)
        return unexpected(clone_res.error());
      auto push_res = vec.try_push_back(std::move(*clone_res));
      if (!push_res)
        return unexpected(push_res.error());
    }
    return vec;
  }

  /**
   * @brief Same as `try_to_vector(allocator_ref)`, using the process-wide
   * default allocator (see `default_allocator()`).
   */
  [[nodiscard]] result<vector<T>> try_to_vector() const & noexcept { return try_to_vector(default_allocator()); }

  /**
   * @brief Moves every element out into a newly heap-allocated `vector<T>`,
   * consuming `*this` (which is left empty regardless of success or
   * failure).
   */
  [[nodiscard]] result<vector<T>> try_to_vector(allocator_ref alloc) && noexcept {
    auto vec_res = vector<T>::try_allocate(alloc, this->size());
    if (!vec_res) {
      this->clear();
      return unexpected(vec_res.error());
    }
    vector<T> vec = std::move(*vec_res);
    for (size_type i = 0; i < this->size(); ++i) {
      auto push_res = vec.try_push_back(std::move((*this)[i]));
      if (!push_res) {
        this->clear();
        return unexpected(push_res.error());
      }
    }
    this->clear();
    return vec;
  }

  /**
   * @brief Same as `try_to_vector(allocator_ref) &&`, using the
   * process-wide default allocator (see `default_allocator()`).
   */
  [[nodiscard]] result<vector<T>> try_to_vector() && noexcept {
    return std::move(*this).try_to_vector(default_allocator());
  }

  [[nodiscard]] static constexpr size_type capacity() noexcept { return Capacity; }
  [[nodiscard]] constexpr bool full() const noexcept { return this->size() == Capacity; }
};

/**
 * @brief `inline_vector<T, Capacity>` is trivially relocatable exactly when
 * `T` is: its storage is embedded directly in the object (unlike
 * `vector<T>`'s heap pointer, which is unconditionally relocatable), so
 * relocating it via `memcpy` is only safe when every contained `T` also
 * has no internal/external self-reference.
 */
template <typename T, std::size_t Capacity>
struct is_trivially_relocatable<inline_vector<T, Capacity>> : is_trivially_relocatable<T> {};

/**
 * @brief Adapts `inline_vector<T, Capacity>` for the collection views.
 */
template <typename T, std::size_t Capacity> struct collection_view_traits<reloco::inline_vector<T, Capacity>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = true;

  static std::size_t size(const reloco::inline_vector<T, Capacity> &c) noexcept { return c.size(); }
  static bool empty(const reloco::inline_vector<T, Capacity> &c) noexcept { return c.empty(); }
  static T &at(reloco::inline_vector<T, Capacity> &c, std::size_t index) noexcept { return c[index]; }
  static const T &at(const reloco::inline_vector<T, Capacity> &c, std::size_t index) noexcept { return c[index]; }
  static T *data(reloco::inline_vector<T, Capacity> &c) noexcept { return c.data(); }
  static const T *data(const reloco::inline_vector<T, Capacity> &c) noexcept { return c.data(); }
};

/**
 * @brief Adapts `inline_vector<T, Capacity>` for `mutable_container_ref`.
 */
template <typename T, std::size_t Capacity> struct container_ref_traits<reloco::inline_vector<T, Capacity>> {
  using element_type = T;
  static constexpr bool is_associative = false;

  static std::size_t size(const reloco::inline_vector<T, Capacity> &c) noexcept { return c.size(); }
  static bool empty(const reloco::inline_vector<T, Capacity> &c) noexcept { return c.empty(); }
  static void clear(reloco::inline_vector<T, Capacity> &c) noexcept { c.clear(); }
  static T &at(reloco::inline_vector<T, Capacity> &c, std::size_t index) noexcept { return c[index]; }

  static result<void> try_push_back(reloco::inline_vector<T, Capacity> &c, T value) noexcept {
    auto res = c.try_push_back(std::move(value));
    if (!res)
      return unexpected(res.error());
    return {};
  }

  static result<void> try_push_front(reloco::inline_vector<T, Capacity> &c, T value) noexcept {
    auto res = c.try_insert_at(0, std::move(value));
    if (!res)
      return unexpected(res.error());
    return {};
  }

  static result<void> try_insert_at(reloco::inline_vector<T, Capacity> &c, std::size_t index, T value) noexcept {
    auto res = c.try_insert_at(index, std::move(value));
    if (!res)
      return unexpected(res.error());
    return {};
  }

  static result<void> try_erase_at(reloco::inline_vector<T, Capacity> &c, std::size_t index) noexcept {
    return c.try_erase_at(index);
  }
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
