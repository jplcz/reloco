// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file vector.hpp
 * @brief Move-only, allocator-backed, growable dynamic array with fallible
 * construction and mutation.
 *
 * `vector<T>` owns a heap allocation obtained through a `reloco::allocator_ref`
 * (see `allocator.hpp`), stored by value rather than a reference to an
 * externally-owned allocator, exactly like `unique_ptr`/`basic_string` (see
 * `unique_ptr.hpp`, `string.hpp`). Every operation that can fail --
 * construction, growth, insertion, in-bounds access -- returns
 * `reloco::result<T>` (see `error.hpp`), the one error type every fallible
 * reloco operation returns; there is no scoped `vector_error` enum.
 *
 * Element construction and cloning go through `construction_helpers` (see
 * `construction_helpers.hpp`), which picks the most efficient strategy for
 * `T` at compile time based on `concepts.hpp`'s `has_try_*_v` traits --
 * `vector<T>` never hand-rolls per-tier construction logic itself.
 * `vector<T>` in turn implements the `try_create`/`try_allocate`/
 * `try_clone`/`try_clone_at` protocol itself, so it composes with those same
 * helpers when nested inside another fallible container (e.g.
 * `reloco::unique_ptr<reloco::vector<T>>::try_create(...)` resolves through
 * `has_try_create_v`). Deep copies are always explicit: `vector<T>` has no
 * copy constructor, matching `unique_ptr`/`basic_string`; use
 * `try_clone`/`try_clone_at` to opt into a fallible deep copy.
 *
 * Read-only/mutating access to already-owned elements follows the checked/
 * `try_*`/`unsafe_*` tri-tier convention used by `span`/`array`/`string`
 * (see `docs/hardened-containers.md`): `operator[]`/`front`/`back` assert on
 * misuse and stay active even when `NDEBUG` is defined, `try_at`/
 * `try_front`/`try_back` report failure through `reloco::result<...>`
 * instead of trapping, and `unsafe_at`/`unsafe_data` are
 * `RELOCO_UNSAFE_BUFFER_USAGE`-gated, debug-only-checked escape hatches.
 * There is no separately named `at()` -- `operator[]` itself is the
 * asserting/checked tier, matching `array<T,N>`/`span<T>`/`basic_string`.
 *
 * Growth (`try_reserve`) prefers `allocator_ref::expand_in_place` first
 * (never moves existing elements). If that fails, it either
 * `allocator_ref::reallocate`s the whole buffer in one byte-level move (when
 * `is_trivially_relocatable_v<T>`, see `relocatable.hpp` -- no per-element
 * constructor/destructor calls needed), or falls back to allocating a fresh
 * block and manually move-constructing + destroying each element in turn.
 * `is_trivially_relocatable<vector<T>>` is itself unconditionally
 * specialized to `true` at the end of this file: `vector<T>`'s own handle is
 * just an `allocator_ref` plus a pointer and two sizes, with no
 * self-reference into its own storage, regardless of `T`.
 *
 * `vector<T>` owns a heap allocation and performs raw pointer arithmetic to
 * manage it (`allocator_ref::allocate`/`deallocate`/`expand_in_place`/
 * `reallocate`, `std::memmove`/`memcpy` for relocatable `T`): raw memory
 * management with no bounds-tracked alternative, exactly like
 * `allocator_ref`'s own operations (see `docs/extending.md`). The file's
 * `namespace reloco` body is wrapped in
 * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE`,
 * matching `allocator.hpp`/`unique_ptr.hpp`/`array.hpp`/`string.hpp`.
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
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T> class RELOCO_OWNER vector : public detail::typed_vector_base<T, detail::heap_vector_base> {
  using base_t = detail::typed_vector_base<T, detail::heap_vector_base>;

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

  // -------------------------------------------------------------------------
  // Constructors & Destructor
  // -------------------------------------------------------------------------

  /**
   * @brief Constructs an empty vector using an optional allocator.
   */
  constexpr explicit vector(allocator_ref alloc = default_allocator()) noexcept : base_t(alloc) {}

  /**
   * @brief Destructor. Automatically destroys elements and deallocates heap storage.
   */
  ~vector() noexcept { this->destroy_elements(detail::metadata_for<T>); }

  // -------------------------------------------------------------------------
  // Move Semantics
  // -------------------------------------------------------------------------

  vector(vector &&other) noexcept : base_t(other.get_allocator()) {
    this->move_construct_from_base(detail::metadata_for<T>, std::move(other));
  }

  vector &operator=(vector &&other) noexcept {
    if (this != &other) {
      this->move_assign_from_base(detail::metadata_for<T>, std::move(other));
    }
    return *this;
  }

  // Delete copy constructors/assignments in favor of explicit fallible try_clone()
  vector(const vector &) = delete;
  vector &operator=(const vector &) = delete;

  // -------------------------------------------------------------------------
  // Factory Helpers (Fallible Creation)
  // -------------------------------------------------------------------------

  /**
   * @brief Fallible allocation and reservation factory.
   */
  [[nodiscard]] static result<vector> try_allocate(allocator_ref alloc, size_type initial_cap = 0) noexcept {
    vector vec(alloc);
    if (initial_cap > 0) {
      if (auto res = vec.try_reserve(initial_cap); !res) {
        return unexpected(res.error());
      }
    }
    return vec;
  }

  /**
   * @brief Fallible allocation using default allocator.
   */
  [[nodiscard]] static result<vector> try_create(size_type initial_cap = 0) noexcept {
    return try_allocate(default_allocator(), initial_cap);
  }

  /**
   * @brief Fallible deep copy clone using a given allocator.
   */
  [[nodiscard]] result<vector> try_clone(allocator_ref alloc) const noexcept {
    vector clone(alloc);
    if (this->size_ == 0)
      return clone;

    if (auto reserve_res = clone.try_reserve(this->size_); !reserve_res) {
      return unexpected(reserve_res.error());
    }

    if (this->operations_->clone_range) {
      if (auto clone_res =
              this->operations_->clone_range(detail::metadata_for<T>, this->data_, clone.data_, this->size_, alloc);
          !clone_res) {
        return unexpected(clone_res.error());
      }
    }

    clone.size_ = this->size_;
    return clone;
  }

  /**
   * @brief Fallible deep copy directly into uninitialized storage.
   */
  [[nodiscard]] static result<void> try_clone_at(allocator_ref alloc, vector *storage, const vector &source) noexcept {
    auto res = source.try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    new (storage) vector(std::move(*res));
    return {};
  }

  /**
   * @brief Fallible deep copy clone reusing this vector's allocator.
   */
  [[nodiscard]] result<vector> try_clone() const noexcept { return try_clone(this->get_allocator()); }
};

/**
 * @brief `vector<T>` is trivially relocatable regardless of `T`: its own
 * handle is just an `allocator_ref` plus a pointer and two sizes, with no
 * self-reference into its own storage.
 */
template <typename T> struct is_trivially_relocatable<vector<T>> : std::true_type {};

/**
 * @brief Adapts `vector<T, N>` for the collection views.
 */
template <typename T> struct collection_view_traits<reloco::vector<T>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = true;

  static std::size_t size(const reloco::vector<T> &c) noexcept { return c.size(); }
  static bool empty(const reloco::vector<T> &c) noexcept { return c.empty(); }
  static T &at(reloco::vector<T> &c, std::size_t index) noexcept { return c[index]; }
  static const T &at(const reloco::vector<T> &c, std::size_t index) noexcept { return c[index]; }
  static T *data(reloco::vector<T> &c) noexcept { return c.data(); }
  static const T *data(const reloco::vector<T> &c) noexcept { return c.data(); }
};

template <typename T> struct container_ref_traits<reloco::vector<T>> {
  using element_type = T;
  static constexpr bool is_associative = false;

  static std::size_t size(const reloco::vector<T> &c) noexcept { return c.size(); }
  static bool empty(const reloco::vector<T> &c) noexcept { return c.empty(); }
  static void clear(reloco::vector<T> &c) noexcept { c.clear(); }
  static T &at(reloco::vector<T> &c, std::size_t index) noexcept { return c[index]; }

  static result<void> try_push_back(reloco::vector<T> &c, T value) noexcept {
    auto res = c.try_push_back(std::move(value));
    if (!res)
      return unexpected(res.error());
    return {};
  }

  static result<void> try_push_front(reloco::vector<T> &c, T value) noexcept {
    auto res = c.try_insert_at(0, std::move(value));
    if (!res)
      return unexpected(res.error());
    return {};
  }

  static result<void> try_insert_at(reloco::vector<T> &c, std::size_t index, T value) noexcept {
    auto res = c.try_insert_at(index, std::move(value));
    if (!res)
      return unexpected(res.error());
    return {};
  }

  static result<void> try_erase_at(reloco::vector<T> &c, std::size_t index) noexcept { return c.try_erase_at(index); }
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
