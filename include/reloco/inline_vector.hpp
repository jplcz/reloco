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

template <typename T, std::size_t Capacity> class RELOCO_OWNER inline_vector {
  static_assert(Capacity > 0, "inline_vector requires a positive Capacity; there is no zero-capacity specialization "
                              "(see array<T, 0> for that shape).");

public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T &;
  using const_reference = const T &;
  using pointer = T *;
  using const_pointer = const T *;
  using iterator = T *;
  using const_iterator = const T *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  constexpr inline_vector() noexcept = default;

  inline_vector(const inline_vector &) = delete;
  inline_vector &operator=(const inline_vector &) = delete;

  inline_vector(inline_vector &&other) noexcept { relocate_from(other); }

  inline_vector &operator=(inline_vector &&other) noexcept {
    if (this != &other) {
      clear();
      relocate_from(other);
    }
    return *this;
  }

  ~inline_vector() noexcept { clear(); }

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
    if constexpr (!has_try_clone_v<T> && std::is_trivially_copyable_v<T>) {
      if (size_ > 0)
        std::memcpy(clone.slot(0), slot(0), size_ * sizeof(T));
      clone.size_ = size_;
    } else {
      for (size_type i = 0; i < size_; ++i) {
        auto elem_res = construction_helpers::try_clone_at<T>(alloc, clone.slot(i), *slot(i));
        if (!elem_res) {
          if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_type j = 0; j < clone.size_; ++j)
              clone.slot(j)->~T();
          }
          clone.size_ = 0;
          return unexpected(elem_res.error());
        }
        ++clone.size_;
      }
    }
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
    auto vec_res = vector<T>::try_allocate(alloc, size_);
    if (!vec_res)
      return unexpected(vec_res.error());
    vector<T> vec = std::move(*vec_res);
    for (size_type i = 0; i < size_; ++i) {
      auto clone_res = construction_helpers::try_clone<T>(alloc, *slot(i));
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
    auto vec_res = vector<T>::try_allocate(alloc, size_);
    if (!vec_res) {
      clear();
      return unexpected(vec_res.error());
    }
    vector<T> vec = std::move(*vec_res);
    for (size_type i = 0; i < size_; ++i) {
      auto push_res = vec.try_push_back(std::move(*slot(i)));
      if (!push_res) {
        clear();
        return unexpected(push_res.error());
      }
    }
    clear();
    return vec;
  }

  /**
   * @brief Same as `try_to_vector(allocator_ref) &&`, using the
   * process-wide default allocator (see `default_allocator()`).
   */
  [[nodiscard]] result<vector<T>> try_to_vector() && noexcept {
    return std::move(*this).try_to_vector(default_allocator());
  }

  // ---- capacity ----

  [[nodiscard]] static constexpr size_type capacity() noexcept { return Capacity; }
  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr bool full() const noexcept { return size_ == Capacity; }

  // ---- mutation ----

  /**
   * @brief Constructs a new element in place at the end of the vector.
   * Fails with `error::capacity_exceeded` if `size() == Capacity`.
   */
  template <typename... Args>
  [[nodiscard]] result<std::reference_wrapper<T>> try_emplace_back(Args &&...args) & noexcept RELOCO_LIFETIMEBOUND {
    if (size_ == Capacity)
      return unexpected(error::capacity_exceeded);
    T *ptr = slot(size_);
    auto res = construction_helpers::try_construct<T>(default_allocator(), ptr, std::forward<Args>(args)...);
    if (!res)
      return unexpected(res.error());
    ++size_;
    return std::ref(*ptr);
  }

  /**
   * @brief Move-appends @p value to the end of the vector. Fails with
   * `error::capacity_exceeded` if `size() == Capacity`.
   */
  [[nodiscard]] result<std::reference_wrapper<T>> try_push_back(T value) & noexcept RELOCO_LIFETIMEBOUND {
    return try_emplace_back(std::move(value));
  }

  /**
   * @brief Removes the last element. Fails with `error::container_empty` if
   * the vector is empty.
   */
  [[nodiscard]] result<void> try_pop_back() & noexcept {
    if (size_ == 0)
      return unexpected(error::container_empty);
    --size_;
    if constexpr (!std::is_trivially_destructible_v<T>)
      slot(size_)->~T();
    return {};
  }

  /**
   * @brief Destroys every element and resets size to zero.
   */
  void clear() noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      for (size_type i = 0; i < size_; ++i)
        slot(i)->~T();
    }
    size_ = 0;
  }

  /**
   * @brief Removes the element at @p index, shifting subsequent elements
   * down by one.
   */
  [[nodiscard]] result<void> try_erase_at(size_type index) & noexcept {
    if (index >= size_)
      return unexpected(error::out_of_bounds);

    if constexpr (!std::is_trivially_destructible_v<T>)
      slot(index)->~T();

    const size_type move_count = size_ - index - 1;
    if (move_count > 0) {
      if constexpr (is_trivially_relocatable_v<T>) {
        std::memmove(slot(index), slot(index + 1), move_count * sizeof(T));
      } else {
        static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
        for (size_type i = index; i + 1 < size_; ++i) {
          new (slot(i)) T(std::move(*slot(i + 1)));
          if constexpr (!std::is_trivially_destructible_v<T>)
            slot(i + 1)->~T();
        }
      }
    }
    --size_;
    return {};
  }

  /**
   * @brief Constructs a new element in place at @p index, shifting
   * subsequent elements up by one. Fails with `error::capacity_exceeded`
   * if `size() == Capacity`.
   *
   * The new element is fully constructed off to the side (via
   * `construction_helpers::try_allocate`) before any existing element is
   * moved, so a construction failure leaves the vector completely
   * unmodified.
   */
  template <typename... Args>
  [[nodiscard]] result<std::reference_wrapper<T>> try_insert_at(size_type index,
                                                                Args &&...args) & noexcept RELOCO_LIFETIMEBOUND {
    if (index > size_)
      return unexpected(error::out_of_bounds);
    if (size_ == Capacity)
      return unexpected(error::capacity_exceeded);

    auto built = construction_helpers::try_allocate<T>(default_allocator(), std::forward<Args>(args)...);
    if (!built)
      return unexpected(built.error());

    const size_type move_count = size_ - index;
    if (move_count > 0) {
      if constexpr (is_trivially_relocatable_v<T>) {
        std::memmove(slot(index + 1), slot(index), move_count * sizeof(T));
      } else {
        static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
        for (size_type i = size_; i > index; --i) {
          new (slot(i)) T(std::move(*slot(i - 1)));
          if constexpr (!std::is_trivially_destructible_v<T>)
            slot(i - 1)->~T();
        }
      }
    }

    static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
    T *ptr = new (slot(index)) T(std::move(*built));
    ++size_;
    return std::ref(*ptr);
  }

  // ---- element access ----

  [[nodiscard]] result<std::reference_wrapper<T>> try_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= size_)
      return unexpected(error::out_of_bounds);
    return std::ref(*slot(index));
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= size_)
      return unexpected(error::out_of_bounds);
    return std::cref(*slot(index));
  }

  [[nodiscard]] T &operator[](size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < size_, "inline_vector index out of bounds");
    return *slot(index);
  }

  [[nodiscard]] const T &operator[](size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < size_, "inline_vector index out of bounds");
    return *slot(index);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < size_, "inline_vector index out of bounds");
    return *slot(index);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < size_, "inline_vector index out of bounds");
    return *slot(index);
  }

  [[nodiscard]] T &front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "inline_vector is empty");
    return *slot(0);
  }

  [[nodiscard]] const T &front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "inline_vector is empty");
    return *slot(0);
  }

  [[nodiscard]] T &back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "inline_vector is empty");
    return *slot(size_ - 1);
  }

  [[nodiscard]] const T &back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "inline_vector is empty");
    return *slot(size_ - 1);
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_front() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::ref(*slot(0));
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_front() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::cref(*slot(0));
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_back() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::ref(*slot(size_ - 1));
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_back() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::cref(*slot(size_ - 1));
  }

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) T *data() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "inline_vector is empty");
    return slot(0);
  }

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) const T *data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "inline_vector is empty");
    return slot(0);
  }

  [[nodiscard]] result<T *> try_data() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return slot(0);
  }

  [[nodiscard]] result<const T *> try_data() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return slot(0);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE
  RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) T *unsafe_data() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "inline_vector has no data");
    return slot(0);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE
  RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) const T *unsafe_data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "inline_vector has no data");
    return slot(0);
  }

  // ---- iteration ----

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) iterator begin() & noexcept RELOCO_LIFETIMEBOUND {
    return slot(0);
  }
  [[nodiscard]] iterator end() & noexcept RELOCO_LIFETIMEBOUND { return slot(size_); }
  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) const_iterator
      begin() const & noexcept RELOCO_LIFETIMEBOUND {
    return slot(0);
  }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return slot(size_); }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return slot(0); }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return slot(size_); }

  [[nodiscard]] reverse_iterator rbegin() & noexcept RELOCO_LIFETIMEBOUND { return reverse_iterator(end()); }
  [[nodiscard]] reverse_iterator rend() & noexcept RELOCO_LIFETIMEBOUND { return reverse_iterator(begin()); }
  [[nodiscard]] const_reverse_iterator rbegin() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(end());
  }
  [[nodiscard]] const_reverse_iterator rend() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(begin());
  }
  [[nodiscard]] const_reverse_iterator crbegin() const & noexcept RELOCO_LIFETIMEBOUND { return rbegin(); }
  [[nodiscard]] const_reverse_iterator crend() const & noexcept RELOCO_LIFETIMEBOUND { return rend(); }

private:
  [[nodiscard]] T *slot(size_type index) noexcept { return reinterpret_cast<T *>(storage_) + index; }
  [[nodiscard]] const T *slot(size_type index) const noexcept { return reinterpret_cast<const T *>(storage_) + index; }

  void relocate_from(inline_vector &other) noexcept {
    if constexpr (is_trivially_relocatable_v<T>) {
      if (other.size_ > 0)
        std::memcpy(storage_, other.storage_, other.size_ * sizeof(T));
    } else {
      static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
      for (size_type i = 0; i < other.size_; ++i) {
        new (slot(i)) T(std::move(*other.slot(i)));
        if constexpr (!std::is_trivially_destructible_v<T>)
          other.slot(i)->~T();
      }
    }
    size_ = other.size_;
    other.size_ = 0;
  }

  alignas(effective_alignment_v<T>) std::byte storage_[sizeof(T) * Capacity];
  size_type size_ = 0;
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
