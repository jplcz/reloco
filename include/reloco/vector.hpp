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

template <typename T> class RELOCO_OWNER vector {
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

  constexpr vector() noexcept : vector(default_allocator()) {}

  constexpr explicit vector(allocator_ref alloc) noexcept : alloc_(alloc) {}

  vector(const vector &) = delete;
  vector &operator=(const vector &) = delete;

  vector(vector &&other) noexcept : alloc_(other.alloc_), data_(other.data_), size_(other.size_), cap_(other.cap_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.cap_ = 0;
  }

  vector &operator=(vector &&other) noexcept {
    if (this != &other) {
      release();
      alloc_ = other.alloc_;
      data_ = other.data_;
      size_ = other.size_;
      cap_ = other.cap_;
      other.data_ = nullptr;
      other.size_ = 0;
      other.cap_ = 0;
    }
    return *this;
  }

  ~vector() noexcept { release(); }

  // ---- fallible construction / cloning (see concepts.hpp) ----

  /**
   * @brief Allocates and reserves storage for @p initial_cap elements,
   * using the given allocator.
   */
  [[nodiscard]] static result<vector> try_allocate(allocator_ref alloc, size_type initial_cap = 0) noexcept {
    vector vec(alloc);
    if (initial_cap > 0) {
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
  [[nodiscard]] static result<vector> try_create(size_type initial_cap = 0) noexcept {
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
  [[nodiscard]] result<vector> try_clone(allocator_ref alloc) const noexcept {
    vector clone(alloc);
    auto reserve_res = clone.try_reserve(size_);
    if (!reserve_res)
      return unexpected(reserve_res.error());

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
  [[nodiscard]] result<vector> try_clone() const noexcept { return try_clone(alloc_); }

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

  // ---- capacity ----

  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return cap_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr allocator_ref get_allocator() const noexcept { return alloc_; }

  /**
   * @brief Ensures storage for at least @p new_cap elements, growing the
   * backing allocation if needed.
   */
  [[nodiscard]] result<void> try_reserve(size_type new_cap) & noexcept {
    if (new_cap <= cap_)
      return {};

    const size_type required_bytes = new_cap * sizeof(T);

    if (data_) {
      if (auto res = alloc_.expand_in_place(data_, cap_ * sizeof(T), required_bytes); res) {
        cap_ = new_cap;
        return {};
      }
    }

    if constexpr (is_trivially_relocatable_v<T>) {
      auto res = data_ ? alloc_.reallocate(data_, cap_ * sizeof(T), required_bytes, effective_alignment_v<T>)
                       : alloc_.allocate(required_bytes, effective_alignment_v<T>);
      if (!res)
        return unexpected(res.error());
      data_ = static_cast<T *>(res->ptr);
      cap_ = new_cap;
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
      if (data_)
        alloc_.deallocate(data_, cap_ * sizeof(T));
      data_ = new_data;
      cap_ = new_cap;
    }
    return {};
  }

  /**
   * @brief Releases unused capacity back to the allocator, if supported.
   */
  [[nodiscard]] result<void> shrink_to_fit() & noexcept {
    if (cap_ <= size_)
      return {};

    if (size_ == 0) {
      if (data_)
        alloc_.deallocate(data_, cap_ * sizeof(T));
      data_ = nullptr;
      cap_ = 0;
      return {};
    }

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
      auto res = try_reserve(cap_ == 0 ? size_type(8) : cap_ * 2);
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
   * first if needed.
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
   * first if needed and copy-constructing @p value into any newly added
   * slots.
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
   * backing allocation (advising the allocator that large buffers are no
   * longer needed).
   */
  void clear() noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      for (size_type i = 0; i < size_; ++i)
        data_[i].~T();
    }
    size_ = 0;
  }

  /**
   * @brief Passes memory usage hints to the underlying allocator for the entire backing buffer.
   */
  void advise(usage_hint hint) noexcept {
    if (cap_ > 0) {
      alloc_.advise(data_, cap_ * sizeof(T), hint);
    }
  }

  /**
   * @brief Surrenders the physical memory of the UNUSED capacity back to the OS,
   * while keeping the virtual memory addresses intact (avoiding reallocation).
   */
  void advise_unused(usage_hint hint = usage_hint::dont_need) noexcept {
    const std::size_t unused_elements = cap_ - size_;
    if (unused_elements > 0) {
      // Advance pointer past the active elements
      void *unused_ptr = data_ + size_;
      alloc_.advise(unused_ptr, unused_elements * sizeof(T), hint);
    }
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
        std::memmove(data_ + index, data_ + index + 1, move_count * sizeof(T));
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
            std::memmove(data_ + write, data_ + read, sizeof(T));
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
   * each run. Only adjacent duplicates are collapsed, matching
   * `std::unique`/Rust semantics (the vector should be sorted first if
   * every duplicate must be removed).
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
          std::memmove(data_ + write, data_ + read, sizeof(T));
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
      auto res = try_reserve(cap_ == 0 ? size_type(8) : cap_ * 2);
      if (!res)
        return unexpected(res.error());
    }

    const size_type move_count = size_ - index;
    if (move_count > 0) {
      if constexpr (is_trivially_relocatable_v<T>) {
        std::memmove(data_ + index + 1, data_ + index, move_count * sizeof(T));
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
    RELOCO_ASSERT(index < size_, "vector index out of bounds");
    return data_[index];
  }

  [[nodiscard]] const T &operator[](size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < size_, "vector index out of bounds");
    return data_[index];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < size_, "vector index out of bounds");
    return data_[index];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < size_, "vector index out of bounds");
    return data_[index];
  }

  [[nodiscard]] T &front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "vector is empty");
    return data_[0];
  }

  [[nodiscard]] const T &front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "vector is empty");
    return data_[0];
  }

  [[nodiscard]] T &back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "vector is empty");
    return data_[size_ - 1];
  }

  [[nodiscard]] const T &back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "vector is empty");
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
    RELOCO_ASSERT(!empty(), "vector is empty");
    return data_;
  }

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) const T *data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "vector is empty");
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
    RELOCO_DEBUG_ASSERT(!empty(), "vector has no data");
    return data_;
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE
  RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) const T *unsafe_data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "vector has no data");
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

  void release() noexcept {
    if (data_) {
      if constexpr (!std::is_trivially_destructible_v<T>) {
        for (size_type i = 0; i < size_; ++i)
          data_[i].~T();
      }
      alloc_.deallocate(data_, cap_ * sizeof(T));
    }
  }

  allocator_ref alloc_;
  T *data_ = nullptr;
  size_type size_ = 0;
  size_type cap_ = 0;
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
