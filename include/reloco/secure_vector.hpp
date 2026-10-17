// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file secure_vector.hpp
 * @brief `vector<T>` for temporary secrets: every byte it releases is wiped.
 *
 * `secure_vector<T>` is a growable, allocator-backed buffer of *trivial*
 * elements (keys, plaintext, scratch blocks) that wipes the bytes of
 * everything it gives up, through `reloco::secure_wipe` (`secure_wipe.hpp`):
 *
 * | Operation | `vector<T>` | `secure_vector<T>` |
 * |-----------|-------------|--------------------|
 * | growth (`try_reserve`, `try_push_back`, `try_insert_at`, `try_resize`) | may `reallocate`, the old block is freed unwiped | allocate a new block, copy, **wipe the whole old block**, free it; never `reallocate` or expand in place |
 * | `try_pop_back`, `pop_back`, `clear`, `try_erase_at`, shrinking `try_resize` | length changes | the vacated element bytes are **wiped** |
 * | destructor, move assignment (the replaced buffer) | free | **wipe all `capacity()` elements** (spare capacity too), then free |
 * | `try_shrink_to_fit` | `reallocate` | copy into a smaller block, wipe the old one |
 *
 * It is a separate type, not a flag on `vector`: `vector<T>` is unchanged,
 * and a secret cannot reach an unwiped buffer by a silent conversion. There
 * is no conversion in either direction; `unwrap_unwiped()` is the explicit,
 * greppable way out and gives up the guarantee.
 *
 * The element type must be trivially copyable and trivially destructible
 * (`static_assert`): elements are copied bytewise and never destroyed, which
 * is also what makes a bytewise wipe valid in the C++ object model. An owning
 * or non-trivial element belongs in a struct that wipes itself, holding the
 * `secure_vector` as a member.
 *
 * Access follows the checked/`try_*`/`unsafe_*` tiers of
 * `docs/hardened-containers.md` and the contract of `docs/container-contract.md`
 * (`RELOCO_OWNER`, `RELOCO_BLOCK_RVALUE_ACCESS`, `RELOCO_LIFETIMEBOUND`).
 *
 * Limits (see `docs/secure-wipe.md`): the wipe erases this buffer, not copies
 * the caller or the compiler made elsewhere (a `T` passed by value, a
 * register, a spill slot). Values taken by value are wiped inside the call.
 *
 * Guide: `docs/secure-wipe.md`. Design: kif's `docs/kif-secure-wipe.md`
 * (rule W12), where this type is the C++ meaning of `secure_vector<T>`.
 */

#include "allocator.hpp"
#include "collection_view.hpp"
#include "container_ref.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"
#include "secure_wipe.hpp"
#include "vector.hpp"

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T> class RELOCO_OWNER secure_vector {
  static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>,
                "secure_vector<T> holds trivial elements only: they are copied bytewise and never destroyed. "
                "Put owning or non-trivial values in a struct that wipes itself and hold the secure_vector in it.");

  using storage_t = vector<T>;

public:
  using value_type = T;
  using allocator_type = allocator_ref;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T &;
  using const_reference = const T &;
  using pointer = T *;
  using const_pointer = const T *;
  using iterator = typename storage_t::iterator;
  using const_iterator = typename storage_t::const_iterator;

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  // -------------------------------------------------------------------------
  // Construction, destruction, moves
  // -------------------------------------------------------------------------

  /** @brief Constructs an empty secure vector using @p alloc. */
  constexpr explicit secure_vector(allocator_ref alloc = default_allocator()) noexcept : data_(alloc) {}

  /** @brief Wipes every byte of the buffer (all `capacity()` elements), then frees it. */
  ~secure_vector() noexcept { wipe_storage(); }

  secure_vector(secure_vector &&other) noexcept : data_(std::move(other.data_)) {}

  /** @brief Wipes the buffer being replaced, then takes @p other's buffer. */
  secure_vector &operator=(secure_vector &&other) noexcept {
    if (this != &other) {
      wipe_storage();
      data_ = std::move(other.data_);
    }
    return *this;
  }

  // Deep copies are explicit and fallible: see try_clone().
  secure_vector(const secure_vector &) = delete;
  secure_vector &operator=(const secure_vector &) = delete;

  /** @brief Fallible creation with room for @p initial_cap elements. */
  [[nodiscard]] static result<secure_vector> try_allocate(allocator_ref alloc, size_type initial_cap = 0) noexcept {
    secure_vector vec(alloc);
    if (initial_cap > 0) {
      if (auto res = vec.data_.try_reserve(initial_cap); !res) {
        return unexpected(res.error());
      }
    }
    return vec;
  }

  /** @brief Fallible creation using the default allocator. */
  [[nodiscard]] static result<secure_vector> try_create(size_type initial_cap = 0) noexcept {
    return try_allocate(default_allocator(), initial_cap);
  }

  /** @brief Fallible deep copy into a new secure buffer from @p alloc. */
  [[nodiscard]] result<secure_vector> try_clone(allocator_ref alloc) const noexcept {
    secure_vector clone(alloc);
    if (auto res = clone.copy_from(data_); !res) {
      return unexpected(res.error());
    }
    return clone;
  }

  /** @brief Fallible deep copy reusing this vector's allocator. */
  [[nodiscard]] result<secure_vector> try_clone() const noexcept { return try_clone(get_allocator()); }

  /** @brief Fallible deep copy directly into uninitialized storage. */
  [[nodiscard]] static result<void> try_clone_at(allocator_ref alloc, secure_vector *storage,
                                                 const secure_vector &source) noexcept {
    auto res = source.try_clone(alloc);
    if (!res) {
      return unexpected(res.error());
    }
    new (storage) secure_vector(std::move(*res));
    return {};
  }

  /**
   * @brief Hands the buffer over as a plain `vector<T>` and **gives up the
   * wipe guarantee** for it. Explicit and greppable by design.
   */
  [[nodiscard]] storage_t unwrap_unwiped() && noexcept { return std::move(data_); }

  // -------------------------------------------------------------------------
  // Observers
  // -------------------------------------------------------------------------

  [[nodiscard]] size_type size() const noexcept { return data_.size(); }
  [[nodiscard]] size_type capacity() const noexcept { return data_.capacity(); }
  [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
  [[nodiscard]] allocator_ref get_allocator() const & noexcept RELOCO_LIFETIMEBOUND { return data_.get_allocator(); }
  [[nodiscard]] allocator_ref get_allocator() const && = delete;

  // -------------------------------------------------------------------------
  // Wiping and mutation (fallible tier)
  // -------------------------------------------------------------------------

  /** @brief Wipes the whole buffer (all `capacity()` elements) and sets the size to zero, keeping the allocation. */
  void wipe() noexcept {
    wipe_storage();
    data_.clear();
  }

  /** @brief Wipes the live elements and sets the size to zero, keeping the allocation. */
  void clear() noexcept {
    wipe_range(0, data_.size());
    data_.clear();
  }

  /** @brief Ensures room for @p new_cap elements; growth copies to a new block and wipes the old one. */
  [[nodiscard]] result<void> try_reserve(size_type new_cap) & noexcept {
    if (new_cap <= data_.capacity()) {
      return {};
    }
    return relocate(new_cap);
  }

  /** @brief Reallocates to a block of exactly `size()` elements (at least), wiping the old block. */
  [[nodiscard]] result<void> try_shrink_to_fit() & noexcept {
    if (data_.size() == data_.capacity()) {
      return {};
    }
    return relocate(data_.size());
  }

  /** @brief Appends a copy of @p value, growing first if full. @p value is wiped after use. */
  [[nodiscard]] result<void> try_push_back(T value) & noexcept {
    auto res = grow_for_one();
    if (res) {
      res = data_.try_push_back(value);
    }
    secure_wipe(value);
    return res;
  }

  /** @brief Removes the last element and wipes its bytes. */
  [[nodiscard]] result<void> try_pop_back() & noexcept {
    auto res = data_.try_pop_back();
    if (res) {
      wipe_range(data_.size(), 1);
    }
    return res;
  }

  void pop_back() noexcept {
    RELOCO_ASSERT(!empty(), "Trying to pop back from empty container");
    std::ignore = try_pop_back();
  }

  /** @brief Resizes to @p count elements; new elements are default-constructed, removed ones wiped. */
  [[nodiscard]] result<void> try_resize(size_type count) & noexcept {
    return resize_with(count, [this](size_type n) noexcept { return data_.try_resize(n); });
  }

  /** @brief Resizes to @p count elements; new elements copy @p value, removed ones are wiped. */
  [[nodiscard]] result<void> try_resize(size_type count, const T &value) & noexcept {
    // @p value may alias an element of this buffer, which growth wipes.
    T local = value;
    auto res = resize_with(count, [this, &local](size_type n) noexcept { return data_.try_resize(n, local); });
    secure_wipe(local);
    return res;
  }

  /** @brief Inserts @p value at @p index, shifting later elements up; wipes @p value after use. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_insert_at(size_type index, T value) & noexcept
      RELOCO_LIFETIMEBOUND {
    if (index > data_.size()) {
      secure_wipe(value);
      return unexpected(error::out_of_bounds);
    }
    auto grown = grow_for_one();
    if (!grown) {
      secure_wipe(value);
      return unexpected(grown.error());
    }
    auto res = data_.try_insert_at(index, value);
    secure_wipe(value);
    return res;
  }

  /** @brief Removes the element at @p index, shifting later ones down; wipes the vacated tail slot. */
  [[nodiscard]] result<void> try_erase_at(size_type index) & noexcept {
    auto res = data_.try_erase_at(index);
    if (res) {
      wipe_range(data_.size(), 1);
    }
    return res;
  }

  // -------------------------------------------------------------------------
  // Element access: checked tier (RELOCO_ASSERT, active under NDEBUG)
  // -------------------------------------------------------------------------

  RELOCO_CHECKED [[nodiscard]] T &operator[](size_type index) & noexcept RELOCO_LIFETIMEBOUND { return data_[index]; }
  RELOCO_CHECKED [[nodiscard]] const T &operator[](size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    return data_[index];
  }

  [[nodiscard]] T &front() & noexcept RELOCO_LIFETIMEBOUND { return data_.front(); }
  [[nodiscard]] const T &front() const & noexcept RELOCO_LIFETIMEBOUND { return data_.front(); }
  [[nodiscard]] T &back() & noexcept RELOCO_LIFETIMEBOUND { return data_.back(); }
  [[nodiscard]] const T &back() const & noexcept RELOCO_LIFETIMEBOUND { return data_.back(); }

  [[nodiscard]] T *data() & noexcept RELOCO_LIFETIMEBOUND { return data_.data(); }
  [[nodiscard]] const T *data() const & noexcept RELOCO_LIFETIMEBOUND { return data_.data(); }

  // -------------------------------------------------------------------------
  // Element access: fallible tier (reloco::result instead of trapping)
  // -------------------------------------------------------------------------

  [[nodiscard]] result<std::reference_wrapper<T>> try_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    return data_.try_at(index);
  }
  [[nodiscard]] result<std::reference_wrapper<const T>> try_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    return data_.try_at(index);
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_front() & noexcept RELOCO_LIFETIMEBOUND {
    return data_.try_front();
  }
  [[nodiscard]] result<std::reference_wrapper<const T>> try_front() const & noexcept RELOCO_LIFETIMEBOUND {
    return data_.try_front();
  }
  [[nodiscard]] result<std::reference_wrapper<T>> try_back() & noexcept RELOCO_LIFETIMEBOUND {
    return data_.try_back();
  }
  [[nodiscard]] result<std::reference_wrapper<const T>> try_back() const & noexcept RELOCO_LIFETIMEBOUND {
    return data_.try_back();
  }

  [[nodiscard]] result<T *> try_data() & noexcept RELOCO_LIFETIMEBOUND { return data_.try_data(); }
  [[nodiscard]] result<const T *> try_data() const & noexcept RELOCO_LIFETIMEBOUND { return data_.try_data(); }

  // -------------------------------------------------------------------------
  // Element access: explicitly unsafe tier (debug-only checks)
  // -------------------------------------------------------------------------

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    return data_.unsafe_at(index);
  }
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    return data_.unsafe_at(index);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T *unsafe_data() & noexcept RELOCO_LIFETIMEBOUND {
    return data_.unsafe_data();
  }
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T *unsafe_data() const & noexcept RELOCO_LIFETIMEBOUND {
    return data_.unsafe_data();
  }

  // -------------------------------------------------------------------------
  // Iteration
  // -------------------------------------------------------------------------

  [[nodiscard]] iterator begin() & noexcept RELOCO_LIFETIMEBOUND { return data_.begin(); }
  [[nodiscard]] iterator end() & noexcept RELOCO_LIFETIMEBOUND { return data_.end(); }
  [[nodiscard]] const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND { return data_.begin(); }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return data_.end(); }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return data_.cbegin(); }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return data_.cend(); }

private:
  /** Wipes @p count element slots starting at slot @p from; the slots must lie inside the capacity. */
  void wipe_range(size_type from, size_type count) noexcept {
    if (count == 0 || data_.capacity() == 0) {
      return;
    }
    RELOCO_DEBUG_ASSERT(from + count <= data_.capacity(), "secure_vector wipe range past capacity");
    secure_wipe(static_cast<void *>(data_.unsafe_data() + from), count * sizeof(T));
  }

  /** Wipes the whole block, spare capacity included. */
  void wipe_storage() noexcept { wipe_range(0, data_.capacity()); }

  /**
   * Copies the live elements into a fresh block of @p new_cap elements, wipes
   * the old block, then adopts the new one. On failure the old block is
   * untouched and the partial copy is wiped by the temporary's destructor.
   */
  [[nodiscard]] result<void> relocate(size_type new_cap) noexcept {
    secure_vector fresh(get_allocator());
    if (auto res = fresh.data_.try_reserve(new_cap); !res) {
      return unexpected(res.error());
    }
    if (auto res = fresh.append_all(data_); !res) {
      return unexpected(res.error());
    }
    wipe_storage();
    data_ = std::move(fresh.data_);
    return {};
  }

  [[nodiscard]] result<void> append_all(const storage_t &source) noexcept {
    for (size_type i = 0; i < source.size(); ++i) {
      if (auto res = data_.try_push_back(source.unsafe_at(i)); !res) {
        return res;
      }
    }
    return {};
  }

  [[nodiscard]] result<void> copy_from(const storage_t &source) noexcept {
    if (source.size() == 0) {
      return {};
    }
    if (auto res = data_.try_reserve(source.size()); !res) {
      return res;
    }
    return append_all(source);
  }

  /** Grows (by relocation) so one more element fits. */
  [[nodiscard]] result<void> grow_for_one() noexcept {
    if (data_.size() < data_.capacity()) {
      return {};
    }
    const size_type cap = data_.capacity();
    if (cap > std::numeric_limits<size_type>::max() / sizeof(T) / 2) {
      return unexpected(error::integer_overflow);
    }
    return relocate(cap == 0 ? 4 : cap * 2);
  }

  /** Grows to @p count if needed, runs @p resize, then wipes any elements that were removed. */
  template <typename Resize> [[nodiscard]] result<void> resize_with(size_type count, Resize &&resize) noexcept {
    if (count > data_.capacity()) {
      if (auto res = relocate(count); !res) {
        return res;
      }
    }
    const size_type old_size = data_.size();
    auto res = resize(count);
    if (res && count < old_size) {
      wipe_range(count, old_size - count);
    }
    return res;
  }

  storage_t data_;
};

/** @brief `secure_vector<T>` is trivially relocatable: its handle is a `vector<T>` handle. */
template <typename T> struct is_trivially_relocatable<secure_vector<T>> : std::true_type {};

template <typename T> struct collection_view_traits<reloco::secure_vector<T>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = true;

  static std::size_t size(const reloco::secure_vector<T> &c) noexcept { return c.size(); }
  static bool empty(const reloco::secure_vector<T> &c) noexcept { return c.empty(); }
  static T &at(reloco::secure_vector<T> &c, std::size_t index) noexcept { return c[index]; }
  static const T &at(const reloco::secure_vector<T> &c, std::size_t index) noexcept { return c[index]; }
  static T *data(reloco::secure_vector<T> &c) noexcept { return c.data(); }
  static const T *data(const reloco::secure_vector<T> &c) noexcept { return c.data(); }
};

template <typename T> struct container_ref_traits<reloco::secure_vector<T>> {
  using element_type = T;
  static constexpr bool is_associative = false;

  static std::size_t size(const reloco::secure_vector<T> &c) noexcept { return c.size(); }
  static bool empty(const reloco::secure_vector<T> &c) noexcept { return c.empty(); }
  static void clear(reloco::secure_vector<T> &c) noexcept { c.clear(); }
  static T &at(reloco::secure_vector<T> &c, std::size_t index) noexcept { return c[index]; }

  static result<void> try_push_back(reloco::secure_vector<T> &c, T value) noexcept { return c.try_push_back(value); }

  static result<void> try_push_front(reloco::secure_vector<T> &c, T value) noexcept {
    auto res = c.try_insert_at(0, value);
    if (!res) {
      return unexpected(res.error());
    }
    return {};
  }

  static result<void> try_insert_at(reloco::secure_vector<T> &c, std::size_t index, T value) noexcept {
    auto res = c.try_insert_at(index, value);
    if (!res) {
      return unexpected(res.error());
    }
    return {};
  }

  static result<void> try_erase_at(reloco::secure_vector<T> &c, std::size_t index) noexcept {
    return c.try_erase_at(index);
  }
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
