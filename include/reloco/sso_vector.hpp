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
#include "error.hpp"
#include "expected.hpp"
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

template <typename T, std::size_t InlineCapacity> class RELOCO_OWNER sso_vector {
  static_assert(InlineCapacity > 0,
                "sso_vector requires a positive InlineCapacity; a zero-capacity instance would never be able to "
                "stay inline (see vector<T> for a purely heap-backed shape).");

public:
  using value_type = T;
  using allocator_type = allocator_ref;
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

  sso_vector() noexcept : sso_vector(default_allocator()) {}

  explicit sso_vector(allocator_ref alloc) noexcept : alloc_(alloc) {}

  sso_vector(const sso_vector &) = delete;
  sso_vector &operator=(const sso_vector &) = delete;

  sso_vector(sso_vector &&other) noexcept : alloc_(other.alloc_) { move_from(other); }

  sso_vector &operator=(sso_vector &&other) noexcept {
    if (this != &other) {
      release();
      alloc_ = other.alloc_;
      move_from(other);
    }
    return *this;
  }

  ~sso_vector() noexcept { release(); }

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
    if (size_ > InlineCapacity) {
      auto reserve_res = clone.try_reserve(size_);
      if (!reserve_res)
        return unexpected(reserve_res.error());
    }

    if constexpr (!has_try_clone_v<T> && std::is_trivially_copyable_v<T>) {
      if (size_ > 0)
        std::memcpy(clone.data_, data_, size_ * sizeof(T));
      clone.size_ = size_;
    } else {
      for (size_type i = 0; i < size_; ++i) {
        auto elem_res = construction_helpers::try_clone_at<T>(alloc, clone.data_ + i, data_[i]);
        if (!elem_res) {
          if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_type j = 0; j < clone.size_; ++j)
              clone.data_[j].~T();
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
   * @brief Fallible deep copy reusing this vector's own allocator.
   */
  [[nodiscard]] result<sso_vector> try_clone() const noexcept { return try_clone(alloc_); }

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

  // ---- capacity ----

  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return cap_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr allocator_ref get_allocator() const noexcept { return alloc_; }

  /**
   * @brief Returns whether `*this` currently holds its elements in the
   * embedded inline buffer rather than a heap allocation.
   */
  [[nodiscard]] bool is_inline() const noexcept { return data_ == inline_slot(0); }

  /**
   * @brief Ensures storage for at least @p new_cap elements, growing the
   * backing allocation if needed (promoting from the inline buffer to the
   * heap first, if not already heap-backed).
   */
  [[nodiscard]] result<void> try_reserve(size_type new_cap) & noexcept {
    if (new_cap <= cap_)
      return {};

    const size_type required_bytes = new_cap * sizeof(T);

    if (is_inline()) {
      // No existing heap allocation to expand-in-place or reallocate --
      // this is always a fresh allocation followed by relocating the
      // (at most InlineCapacity) inline elements across.
      auto res = alloc_.allocate(required_bytes, effective_alignment_v<T>);
      if (!res)
        return unexpected(res.error());
      T *new_data = static_cast<T *>(res->ptr);
      if constexpr (is_trivially_relocatable_v<T>) {
        if (size_ > 0)
          std::memmove(static_cast<void *>(new_data), static_cast<const void *>(data_), size_ * sizeof(T));
      } else {
        static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
        for (size_type i = 0; i < size_; ++i) {
          new (new_data + i) T(std::move(data_[i]));
          if constexpr (!std::is_trivially_destructible_v<T>)
            data_[i].~T();
        }
      }
      data_ = new_data;
      // Absorb any excess capacity returned by the allocator block
      cap_ = res->size / sizeof(T);
      return {};
    }

    if (auto res = alloc_.expand_in_place(data_, cap_ * sizeof(T), required_bytes); res) {
      // Absorb the actual expanded byte size returned by expand_in_place
      cap_ = *res / sizeof(T);
      return {};
    }

    if constexpr (is_trivially_relocatable_v<T>) {
      auto res = alloc_.reallocate(data_, cap_ * sizeof(T), required_bytes, effective_alignment_v<T>);
      if (!res)
        return unexpected(res.error());
      data_ = static_cast<T *>(res->ptr);
      // Absorb any excess capacity from the reallocated mem_block
      cap_ = res->size / sizeof(T);
    } else {
      auto res = alloc_.allocate(required_bytes, effective_alignment_v<T>);
      if (!res)
        return unexpected(res.error());

      static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
      T *new_data = static_cast<T *>(res->ptr);
      for (size_type i = 0; i < size_; ++i) {
        new (new_data + i) T(std::move(data_[i]));
        if constexpr (!std::is_trivially_destructible_v<T>)
          data_[i].~T();
      }
      alloc_.deallocate(data_, cap_ * sizeof(T));
      data_ = new_data;
      // Absorb any excess capacity from the fresh allocation block
      cap_ = res->size / sizeof(T);
    }
    return {};
  }

  /**
   * @brief Releases unused capacity, demoting back into the inline buffer
   * once `size()` fits within `InlineCapacity` again, or releasing unused
   * heap capacity otherwise. No-op while already inline.
   */
  [[nodiscard]] result<void> shrink_to_fit() & noexcept {
    if (is_inline())
      return {};

    if (size_ <= InlineCapacity) {
      T *old_data = data_;
      const size_type old_cap = cap_;
      if constexpr (is_trivially_relocatable_v<T>) {
        if (size_ > 0)
          std::memmove(static_cast<void *>(inline_slot(0)), static_cast<const void *>(old_data), size_ * sizeof(T));
      } else {
        static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
        for (size_type i = 0; i < size_; ++i) {
          new (inline_slot(i)) T(std::move(old_data[i]));
          if constexpr (!std::is_trivially_destructible_v<T>)
            old_data[i].~T();
        }
      }
      data_ = inline_slot(0);
      cap_ = InlineCapacity;
      alloc_.deallocate(old_data, old_cap * sizeof(T));
      return {};
    }

    if (cap_ <= size_)
      return {};

    if constexpr (is_trivially_relocatable_v<T>) {
      auto res = alloc_.reallocate(data_, cap_ * sizeof(T), size_ * sizeof(T), effective_alignment_v<T>);
      if (!res)
        return unexpected(res.error());
      data_ = static_cast<T *>(res->ptr);
      cap_ = size_;
    } else {
      auto res = alloc_.allocate(size_ * sizeof(T), effective_alignment_v<T>);
      if (!res)
        return unexpected(res.error());

      static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
      T *new_data = static_cast<T *>(res->ptr);
      for (size_type i = 0; i < size_; ++i) {
        new (new_data + i) T(std::move(data_[i]));
        if constexpr (!std::is_trivially_destructible_v<T>)
          data_[i].~T();
      }
      alloc_.deallocate(data_, cap_ * sizeof(T));
      data_ = new_data;
      cap_ = size_;
    }
    return {};
  }

  // ---- mutation ----

  /**
   * @brief Constructs a new element in place at the end of the vector,
   * growing storage first if needed.
   */
  template <typename... Args>
  [[nodiscard]] result<std::reference_wrapper<T>> try_emplace_back(Args &&...args) & noexcept RELOCO_LIFETIMEBOUND {
    if (size_ == cap_) {
      auto res = try_reserve(cap_ * 2);
      if (!res)
        return unexpected(res.error());
    }
    T *ptr = data_ + size_;
    auto res = construction_helpers::try_construct<T>(alloc_, ptr, std::forward<Args>(args)...);
    if (!res)
      return unexpected(res.error());
    ++size_;
    return std::ref(*ptr);
  }

  /**
   * @brief Move-appends @p value to the end of the vector.
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
      data_[size_].~T();
    return {};
  }

  /**
   * @brief Resizes the vector to contain @p count elements, growing storage
   * (and promoting from inline to heap if needed) first.
   *
   * If @p count < size(), the trailing elements are destroyed. If @p count >
   * size(), each new slot is default-constructed. Requires `T` to be
   * default-constructible; use `try_resize(count, value)` to fill new
   * elements with a copy of @p value instead.
   *
   * When `T` is trivially default-constructible, the newly added range is
   * bulk zero-filled with a single `std::memset` rather than looping a
   * placement-new per element.
   */
  [[nodiscard]] result<void> try_resize(size_type count) & noexcept {
    static_assert(std::is_default_constructible_v<T>,
                  "try_resize(count) requires T to be default-constructible; use try_resize(count, value) instead.");
    if (count <= size_) {
      destroy_range(count, size_);
      size_ = count;
      return {};
    }

    auto res = try_reserve(count);
    if (!res)
      return unexpected(res.error());

    if constexpr (std::is_trivially_default_constructible_v<T>) {
      std::memset(static_cast<void *>(data_ + size_), 0, (count - size_) * sizeof(T));
      size_ = count;
    } else {
      size_type i = size_;
      for (; i < count; ++i) {
        auto ctor_res = construction_helpers::try_construct<T>(alloc_, data_ + i);
        if (!ctor_res) {
          destroy_range(size_, i);
          return unexpected(ctor_res.error());
        }
      }
      size_ = count;
    }
    return {};
  }

  /**
   * @brief Resizes the vector to contain @p count elements, growing storage
   * (and promoting from inline to heap if needed) first and
   * copy-constructing @p value into any newly added slots.
   *
   * When `T` is trivially copyable, the newly added range is filled via a
   * plain assignment loop rather than going through the fallible
   * construction dispatcher per element.
   */
  [[nodiscard]] result<void> try_resize(size_type count, const T &value) & noexcept {
    if (count <= size_) {
      destroy_range(count, size_);
      size_ = count;
      return {};
    }

    auto res = try_reserve(count);
    if (!res)
      return unexpected(res.error());

    if constexpr (std::is_trivially_copyable_v<T>) {
      for (size_type i = size_; i < count; ++i)
        data_[i] = value;
      size_ = count;
    } else {
      size_type i = size_;
      for (; i < count; ++i) {
        auto ctor_res = construction_helpers::try_construct<T>(alloc_, data_ + i, value);
        if (!ctor_res) {
          destroy_range(size_, i);
          return unexpected(ctor_res.error());
        }
      }
      size_ = count;
    }
    return {};
  }

  /**
   * @brief Destroys every element and resets size to zero, keeping the
   * backing storage (whether inline or heap-allocated).
   */
  void clear() noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      for (size_type i = 0; i < size_; ++i)
        data_[i].~T();
    }
    size_ = 0;
  }

  /**
   * @brief Passes memory usage hints to the underlying allocator for the
   * entire backing buffer. No-op while inline (there is nothing to advise
   * the allocator about).
   */
  void advise(usage_hint hint) noexcept {
    if (!is_inline())
      alloc_.advise(data_, cap_ * sizeof(T), hint);
  }

  /**
   * @brief Surrenders the physical memory of the UNUSED heap capacity back
   * to the OS, while keeping the virtual memory addresses intact. No-op
   * while inline.
   */
  void advise_unused(usage_hint hint = usage_hint::dont_need) noexcept {
    if (is_inline())
      return;
    const size_type unused_elements = cap_ - size_;
    if (unused_elements > 0)
      alloc_.advise(data_ + size_, unused_elements * sizeof(T), hint);
  }

  /**
   * @brief Removes the element at @p index, shifting subsequent elements
   * down by one.
   */
  [[nodiscard]] result<void> try_erase_at(size_type index) & noexcept {
    if (index >= size_)
      return unexpected(error::out_of_bounds);

    if constexpr (!std::is_trivially_destructible_v<T>)
      data_[index].~T();

    const size_type move_count = size_ - index - 1;
    if (move_count > 0) {
      if constexpr (is_trivially_relocatable_v<T>) {
        std::memmove(static_cast<void *>(data_ + index), static_cast<const void *>(data_ + index + 1),
                     move_count * sizeof(T));
      } else {
        static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
        for (size_type i = index; i + 1 < size_; ++i) {
          new (data_ + i) T(std::move(data_[i + 1]));
          if constexpr (!std::is_trivially_destructible_v<T>)
            data_[i + 1].~T();
        }
      }
    }
    --size_;
    return {};
  }

  /**
   * @brief Rust `Vec::retain` equivalent: keeps only the elements for
   * which `pred(element)` returns `true`, destroying and compacting away
   * the rest in a single forward pass. Never allocates and cannot fail.
   */
  template <typename Pred> void retain(Pred &&pred) & noexcept {
    size_type write = 0;
    for (size_type read = 0; read < size_; ++read) {
      if (pred(std::as_const(data_[read]))) {
        if (write != read) {
          if constexpr (is_trivially_relocatable_v<T>) {
            std::memmove(static_cast<void *>(data_ + write), static_cast<const void *>(data_ + read), sizeof(T));
          } else {
            new (data_ + write) T(std::move(data_[read]));
            if constexpr (!std::is_trivially_destructible_v<T>)
              data_[read].~T();
          }
        }
        ++write;
      } else if constexpr (!std::is_trivially_destructible_v<T>) {
        data_[read].~T();
      }
    }
    size_ = write;
  }

  /**
   * @brief Rust `Vec::dedup_by` equivalent: removes consecutive elements
   * for which `same(prev, current)` returns `true`, keeping the first of
   * each run.
   */
  template <typename BinPred> void dedup_by(BinPred &&same) & noexcept {
    if (size_ < 2)
      return;
    size_type write = 1;
    for (size_type read = 1; read < size_; ++read) {
      if (same(std::as_const(data_[write - 1]), std::as_const(data_[read]))) {
        if constexpr (!std::is_trivially_destructible_v<T>)
          data_[read].~T();
        continue;
      }
      if (write != read) {
        if constexpr (is_trivially_relocatable_v<T>) {
          std::memmove(static_cast<void *>(data_ + write), static_cast<const void *>(data_ + read), sizeof(T));
        } else {
          new (data_ + write) T(std::move(data_[read]));
          if constexpr (!std::is_trivially_destructible_v<T>)
            data_[read].~T();
        }
      }
      ++write;
    }
    size_ = write;
  }

  /**
   * @brief Rust `Vec::dedup` equivalent: removes consecutive elements
   * that compare equal via `operator==`.
   */
  void dedup() & noexcept {
    dedup_by([](const T &a, const T &b) noexcept { return a == b; });
  }

  /**
   * @brief Constructs a new element in place at @p index, shifting
   * subsequent elements up by one and growing storage first if needed.
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

    auto built = construction_helpers::try_allocate<T>(alloc_, std::forward<Args>(args)...);
    if (!built)
      return unexpected(built.error());

    if (size_ == cap_) {
      auto res = try_reserve(cap_ * 2);
      if (!res)
        return unexpected(res.error());
    }

    const size_type move_count = size_ - index;
    if (move_count > 0) {
      if constexpr (is_trivially_relocatable_v<T>) {
        std::memmove(static_cast<void *>(data_ + index + 1), static_cast<const void *>(data_ + index),
                     move_count * sizeof(T));
      } else {
        static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
        for (size_type i = size_; i > index; --i) {
          new (data_ + i) T(std::move(data_[i - 1]));
          if constexpr (!std::is_trivially_destructible_v<T>)
            data_[i - 1].~T();
        }
      }
    }

    static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
    T *ptr = new (data_ + index) T(std::move(*built));
    ++size_;
    return std::ref(*ptr);
  }

  // ---- element access ----

  [[nodiscard]] result<std::reference_wrapper<T>> try_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= size_)
      return unexpected(error::out_of_bounds);
    return std::ref(data_[index]);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= size_)
      return unexpected(error::out_of_bounds);
    return std::cref(data_[index]);
  }

  [[nodiscard]] T &operator[](size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < size_, "sso_vector index out of bounds");
    return data_[index];
  }

  [[nodiscard]] const T &operator[](size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < size_, "sso_vector index out of bounds");
    return data_[index];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < size_, "sso_vector index out of bounds");
    return data_[index];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < size_, "sso_vector index out of bounds");
    return data_[index];
  }

  [[nodiscard]] T &front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "sso_vector is empty");
    return data_[0];
  }

  [[nodiscard]] const T &front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "sso_vector is empty");
    return data_[0];
  }

  [[nodiscard]] T &back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "sso_vector is empty");
    return data_[size_ - 1];
  }

  [[nodiscard]] const T &back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "sso_vector is empty");
    return data_[size_ - 1];
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_front() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::ref(data_[0]);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_front() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::cref(data_[0]);
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_back() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::ref(data_[size_ - 1]);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_back() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::cref(data_[size_ - 1]);
  }

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) T *data() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "sso_vector is empty");
    return data_;
  }

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) const T *data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "sso_vector is empty");
    return data_;
  }

  [[nodiscard]] result<T *> try_data() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return data_;
  }

  [[nodiscard]] result<const T *> try_data() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return data_;
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE
  RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) T *unsafe_data() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "sso_vector has no data");
    return data_;
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE
  RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) const T *unsafe_data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "sso_vector has no data");
    return data_;
  }

  // ---- iteration ----

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) iterator begin() & noexcept RELOCO_LIFETIMEBOUND {
    return data_;
  }
  [[nodiscard]] iterator end() & noexcept RELOCO_LIFETIMEBOUND { return data_ + size_; }
  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) const_iterator
      begin() const & noexcept RELOCO_LIFETIMEBOUND {
    return data_;
  }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return data_ + size_; }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return data_; }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return data_ + size_; }

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
  void destroy_range(size_type from, size_type to) noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      for (size_type i = from; i < to; ++i)
        data_[i].~T();
    }
  }

  // Cast the raw storage to `T *` once and then use ordinary `T *`
  // pointer arithmetic (implicitly scaled by `sizeof(T)`) instead of
  // manually multiplying `index * sizeof(T)` against a `std::byte *`.
  // This avoids both a `cpp/suspicious-pointer-scaling` false positive
  // (a `std::byte *` scaled as if it were `T *`) and a
  // `cpp/suspicious-add-sizeof` false positive (an explicit `sizeof(T)`
  // added to a pointer) from static analysis. Mirrors `inline_vector::slot()`.
  [[nodiscard]] T *inline_slot(size_type index) noexcept {
    return std::launder(static_cast<T *>(static_cast<void *>(inline_storage_)) + index);
  }
  [[nodiscard]] const T *inline_slot(size_type index) const noexcept {
    return std::launder(static_cast<const T *>(static_cast<const void *>(inline_storage_)) + index);
  }

  // Takes over `other`'s elements (by relocating/moving them into our own
  // inline buffer if `other` is inline, or by stealing its heap pointer
  // outright otherwise), then resets `other` to an empty, inline state.
  // Assumes `*this` currently holds no live elements of its own.
  void move_from(sso_vector &other) noexcept {
    if (other.is_inline()) {
      if constexpr (is_trivially_relocatable_v<T>) {
        if (other.size_ > 0)
          std::memmove(inline_storage_, other.inline_storage_, other.size_ * sizeof(T));
      } else {
        static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
        for (size_type i = 0; i < other.size_; ++i) {
          new (inline_slot(i)) T(std::move(*other.inline_slot(i)));
          if constexpr (!std::is_trivially_destructible_v<T>)
            other.inline_slot(i)->~T();
        }
      }
      data_ = inline_slot(0);
      size_ = other.size_;
      cap_ = InlineCapacity;
    } else {
      data_ = other.data_;
      size_ = other.size_;
      cap_ = other.cap_;
    }
    other.reset_to_empty_inline();
  }

  void reset_to_empty_inline() noexcept {
    data_ = inline_slot(0);
    size_ = 0;
    cap_ = InlineCapacity;
  }

  void release() noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      for (size_type i = 0; i < size_; ++i)
        data_[i].~T();
    }
    if (!is_inline())
      alloc_.deallocate(data_, cap_ * sizeof(T));
  }

  allocator_ref alloc_;
  size_type size_ = 0;
  size_type cap_ = InlineCapacity;
  alignas(effective_alignment_v<T>) std::byte inline_storage_[sizeof(T) * InlineCapacity];
  T *data_{inline_slot(0)};
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
