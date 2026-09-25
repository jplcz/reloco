// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file vector_base.hpp
 * @brief Shared, type-erased implementation backing `vector<T>`,
 * `inline_vector<T, Capacity>`, `sso_vector<T, InlineCapacity>`, and
 * `outline_vector<T>`.
 *
 * `vector_base.hpp` factors out every storage-management primitive the
 * four vector flavors need -- growth, resize, insert/erase, retain/dedup,
 * move construction/assignment (where supported), destruction -- into a
 * single type-erased engine driven by a per-`T` `vector_operations`
 * function-pointer table (`get_operations_for<T>()`), so the four public
 * headers (`vector.hpp`, `inline_vector.hpp`, `sso_vector.hpp`,
 * `outline_vector.hpp`) only need to declare thin, strongly-typed wrappers
 * rather than re-implementing the same logic four times over.
 *
 * The design has two layers:
 *
 * - `unowned_vector_base`/`heap_vector_base`/`inline_vector_base`/
 *   `mixed_vector_base`/`outline_vector_base`: untyped storage policies
 *   holding a `void*`/`size_t size_`/`size_t cap_` triple (plus,
 *   respectively, nothing extra, an `allocator_ref`, an inline capacity,
 *   both, or a caller-owned capacity). Each policy knows only how to
 *   grow/shrink/move its *own* storage kind; all per-element logic is
 *   forwarded through the `vector_operations` table so these bases never
 *   need to know `T`.
 * - `typed_vector_base<T, Base>`: the strongly-typed layer derived classes
 *   (`vector<T>`, `inline_vector<T, Capacity>`, `sso_vector<T,
 *   InlineCapacity>`, `outline_vector<T>`) actually inherit from. It
 *   reintroduces `T`-aware member types (`iterator`, `reference`, ...) and
 *   the full public `try_*`/checked/`unsafe_*` mutation and access surface,
 *   delegating every operation straight through to the untyped `Base`
 *   policy plus `metadata_for<T>`.
 *
 * `vector_operations` resolves, per function pointer and per `T`, to either
 * `trivial_operator_set`'s `memcpy`/`memset`-based implementations (for
 * trivially destructible/relocatable/copyable `T`) or a compiler-generated
 * closure that loops element-by-element through `construction_helpers`
 * (see `construction_helpers.hpp`), exactly mirroring the tiered dispatch
 * `construction_helpers` itself uses for individual elements.
 *
 * `type_metadata`/`metadata_for<T>` -- the per-`T` size/alignment/
 * triviality facts `vector_operations`' bodies are parameterized over --
 * live in `type_metadata.hpp` rather than here: they describe `T` alone,
 * with nothing specific to *contiguous array* storage, so a future
 * type-erased engine for a node- or bucket-based container (map, list, ...)
 * can reuse `type_metadata` as-is instead of duplicating it.
 *
 * The *per-element* construct/clone/destroy/relocate dispatch itself --
 * tiered through `construction_helpers` for non-trivial `T` -- similarly
 * lives in its own header, `type_operations.hpp`, as `type_operations`/
 * `get_type_operations_for<T>()`: `vector_operations`'s non-trivial range
 * resolvers below loop over that single-element table rather than
 * duplicating the tiered dispatch inline, so the same logic is shared with
 * any future node- or bucket-based container engine instead of being
 * rewritten per engine. Only the *trivial* range fast path
 * (`trivial_operator_set`, whole-range `memcpy`/`memset`) stays specific
 * to this header -- looping a single-element operation once per index
 * would be strictly slower than one range-wide `memcpy` for trivial `T`.
 *
 * Like `flat_container_base.hpp`, this is deliberately not public API: it
 * lives in `reloco::detail` and is included only by the four vector
 * headers, guarded on `RELOCO_SHARED_PROVIDE_DEFINITIONS` for its
 * out-of-line `vector_base.ipp` bodies exactly like `heap_allocator.hpp`/
 * `error_std.hpp` guard theirs (see `docs/shared-library.md`).
 */

#include "../construction_helpers.hpp"
#include "../error.hpp"
#include "../function_ref.hpp"
#include "../reloco_extern.hpp"
#include "type_metadata.hpp"
#include "type_operations.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

namespace reloco {

namespace detail {

/**
 * @brief Per-`T` table of type-erased element operations the untyped
 * `*_base` classes call through instead of knowing `T` directly; resolved
 * once at compile time by `get_operations_for<T>()`.
 */
struct RELOCO_EXPORT vector_operations {
  /// @brief Destroy element range
  void (*destroy_range)(const type_metadata &type, void *data, std::size_t from, std::size_t to) noexcept;
  /// @brief Clone element range using fallible clone
  result<void> (*clone_range)(const type_metadata &type, const void *src, void *dest, std::size_t size,
                              allocator_ref alloc) noexcept;
  /// @brief Copy (if value is not nullptr) or default-construct range
  result<void> (*copy_construct_range)(const type_metadata &type, allocator_ref alloc, void *data, std::size_t from,
                                       std::size_t to, const void *value_ptr) noexcept;
  /// @brief Move elements where to < from
  void (*move_range)(const type_metadata &type, void *to, const void *from, std::size_t count);
  /// @brief Move elements where to > from
  void (*move_range_up)(const type_metadata &type, void *to, const void *from, std::size_t count);
};

/**
 * @brief `memcpy`/`memset`-based `vector_operations` bodies shared by every
 * trivially destructible/relocatable/copyable `T`, avoiding a
 * per-element loop entirely.
 */
struct RELOCO_EXPORT trivial_operator_set {
  RELOCO_API static result<void> clone_range(const type_metadata &type, const void *src, void *dest, std::size_t size,
                                             allocator_ref alloc) noexcept;
  RELOCO_API static result<void> copy_construct_range(const type_metadata &type, allocator_ref alloc, void *data,
                                                      std::size_t from, std::size_t to, const void *value_ptr) noexcept;
  RELOCO_API static void move_range(const type_metadata &type, void *to, const void *from, std::size_t count);
  RELOCO_API static void move_range_up(const type_metadata &type, void *to, const void *from, std::size_t count);
};

inline constexpr vector_operations operations_for_trivial = {
    nullptr,
    &trivial_operator_set::clone_range,
    &trivial_operator_set::copy_construct_range,
    &trivial_operator_set::move_range,
    &trivial_operator_set::move_range_up,
};

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

// The four non-trivial range resolvers below all loop over
// `get_type_operations_for<T>()`'s single-element function pointers rather
// than re-implementing per-element tiered construction/cloning/destruction
// dispatch inline: that dispatch is written exactly once, in
// `type_operations.hpp`, and shared with any other type-erased container
// engine (e.g. a future map/list) that needs the same per-element facts.
// The *trivial* range fast path (`trivial_operator_set`, below) is
// deliberately not rebuilt on top of `type_operations` -- one whole-range
// `memcpy`/`memset` is strictly cheaper than looping a single-element
// function pointer once per index.

template <typename T, typename = void> struct destroy_range_resolver {
  static constexpr auto get() noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      return [](const type_metadata &type, void *data, std::size_t from, std::size_t to) noexcept {
        constexpr const type_operations *ops = get_type_operations_for<T>();
        T *ptr = static_cast<T *>(data);
        for (std::size_t i = from; i < to; ++i) {
          ops->destroy_one(type, ptr + i);
        }
      };
    } else {
      return nullptr;
    }
  }
};

template <typename T, typename = void> struct clone_range_resolver {
  static constexpr auto get() noexcept {
    if constexpr (std::is_trivially_copyable_v<T>) {
      return &trivial_operator_set::clone_range;
    } else if constexpr (is_try_cloneable_v<T>) {
      return [](const type_metadata &type, const void *src, void *dest, std::size_t size,
                allocator_ref alloc) noexcept -> result<void> {
        constexpr const type_operations *ops = get_type_operations_for<T>();
        const T *s = static_cast<const T *>(src);
        T *d = static_cast<T *>(dest);
        std::size_t cloned = 0;
        for (std::size_t i = 0; i < size; ++i) {
          auto elem_res = ops->clone_one(type, alloc, d + i, s + i);
          if (!elem_res) {
            if (ops->destroy_one) {
              for (std::size_t j = 0; j < cloned; ++j) {
                ops->destroy_one(type, d + j);
              }
            }
            return unexpected(elem_res.error());
          }
          ++cloned;
        }
        return {};
      };
    } else {
      return static_cast<decltype(&trivial_operator_set::clone_range)>(nullptr);
    }
  }
};

template <typename T> struct copy_construct_range_resolver {
  static constexpr auto get() noexcept {
    if constexpr (std::is_trivially_copyable_v<T> && std::is_trivially_default_constructible_v<T>) {
      return &trivial_operator_set::copy_construct_range;
    } else {
      return [](const type_metadata &type, allocator_ref alloc, void *data, std::size_t from, std::size_t to,
                const void *value_ptr) noexcept -> result<void> {
        constexpr const type_operations *ops = get_type_operations_for<T>();
        T *ptr = static_cast<T *>(data);
        std::size_t constructed = from;
        for (std::size_t i = from; i < to; ++i) {
          auto ctor_res = ops->copy_construct_one(type, alloc, ptr + i, value_ptr);
          if (!ctor_res) {
            if (ops->destroy_one) {
              for (std::size_t j = from; j < constructed; ++j) {
                ops->destroy_one(type, ptr + j);
              }
            }
            return unexpected(ctor_res.error());
          }
          ++constructed;
        }
        return {};
      };
    }
  }
};

// Resolver for forward move/relocate range
template <typename T> struct move_range_resolver {
  static constexpr auto get() noexcept {
    if constexpr (is_trivially_relocatable_v<T>) {
      return &trivial_operator_set::move_range;
    } else {
      return [](const type_metadata &type, void *to, const void *from, std::size_t count) noexcept {
        constexpr const type_operations *ops = get_type_operations_for<T>();
        T *d = static_cast<T *>(to);
        const T *s = static_cast<const T *>(from);
        for (std::size_t i = 0; i < count; ++i) {
          ops->relocate_one(type, d + i, s + i);
        }
      };
    }
  }
};

// Resolver for backward move/relocate range (used when regions overlap upwards)
template <typename T> struct move_range_up_resolver {
  static constexpr auto get() noexcept {
    if constexpr (is_trivially_relocatable_v<T>) {
      return &trivial_operator_set::move_range_up;
    } else {
      return [](const type_metadata &type, void *to, const void *from, std::size_t count) noexcept {
        constexpr const type_operations *ops = get_type_operations_for<T>();
        T *d = static_cast<T *>(to);
        const T *s = static_cast<const T *>(from);
        for (std::size_t i = count; i-- > 0;) {
          ops->relocate_one(type, d + i, s + i);
        }
      };
    }
  }
};

template <typename T>
inline constexpr vector_operations vector_operations_for = {
    // 1. destroy_range: nullptr if trivially destructible, else loops get_type_operations_for<T>()->destroy_one
    destroy_range_resolver<T>::get(),

    // 2. clone_range: nullptr if not cloneable, trivial_operator_set if trivially copyable, else loops
    // get_type_operations_for<T>()->clone_one
    clone_range_resolver<T>::get(),

    // 3. copy_construct_range: trivial_operator_set if both copyable and default-constructible are trivial, else
    // loops get_type_operations_for<T>()->copy_construct_one
    copy_construct_range_resolver<T>::get(),

    // 4. move_range
    move_range_resolver<T>::get(),

    // 5. move_range_up
    move_range_up_resolver<T>::get(),
};

// Reused from `type_operations.hpp`: a range of `T` can take the whole-range
// `memcpy`/`memset` fast path under exactly the same condition that lets a
// single `T` take the shared `operations_for_trivial_element` table.
template <typename T> constexpr inline bool has_trivial_vector_ops = has_trivial_type_ops<T>;

template <typename T, typename = void> struct vector_operations_maker {
  static constexpr const vector_operations *make() noexcept {
    if constexpr (has_trivial_vector_ops<T>) {
      return &operations_for_trivial;
    } else {
      return &vector_operations_for<T>;
    }
  }
};

template <typename T1, typename T2>
struct vector_operations_maker<std::pair<T1, T2>,
                               std::enable_if_t<(has_trivial_vector_ops<T1> && has_trivial_vector_ops<T2>), void>> {
  static constexpr const vector_operations *make() noexcept { return &operations_for_trivial; }
};

/**
 * @brief Master dispatcher returning operations_for_trivial for fully POD types,
 * or vector_operations_for<T> with selective trivial backend routing for hybrid types.
 */
template <typename T> inline constexpr const vector_operations *get_operations_for() noexcept {
  return vector_operations_maker<T>::make();
}

/**
 * @brief Untyped core shared by every storage policy: a `void*`/`size_t
 * size_`/`size_t cap_` triple plus the `vector_operations` table, and every
 * `try_*_base` primitive (`try_reserve_base`, `try_resize_base`,
 * `try_insert_at_base`, `try_erase_at_base`, `retain_base`, `dedup_by_base`,
 * ...) that operates purely in terms of byte offsets and the operations
 * table, with no knowledge of `T` or of where the storage itself lives.
 */
class RELOCO_EXPORT unowned_vector_base {
protected:
  constexpr explicit unowned_vector_base(const vector_operations *ops) noexcept : operations_(ops) {}

  constexpr unowned_vector_base(const vector_operations *ops, void *data, std::size_t size, std::size_t cap) noexcept
      : data_(data), operations_(ops), size_(size), cap_(cap) {}

public:
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  ~unowned_vector_base() noexcept = default;

  unowned_vector_base(const unowned_vector_base &) = delete;
  unowned_vector_base &operator=(const unowned_vector_base &) = delete;
  unowned_vector_base(unowned_vector_base &&) = delete;
  unowned_vector_base &operator=(unowned_vector_base &&) = delete;

  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return cap_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

protected:
  // --- Base Engine Operations (Requiring explicit allocator_ref) ---

  void destroy_elements_base(const type_metadata &type) const noexcept {
    if (data_ && operations_) {
      if (operations_->destroy_range) {
        operations_->destroy_range(type, data_, 0, size_);
      }
    }
  }

  [[nodiscard]] RELOCO_API result<void> try_reserve_base(allocator_ref alloc, const type_metadata &type,
                                                         std::size_t new_cap, void *inline_storage,
                                                         std::size_t max_inline, std::size_t max_cap) noexcept;

  [[nodiscard]] RELOCO_API result<void> try_resize_base(allocator_ref alloc, const type_metadata &type,
                                                        std::size_t count, const void *value_ptr, void *inline_storage,
                                                        std::size_t max_inline, std::size_t max_cap) noexcept;

  [[nodiscard]] RELOCO_API result<void> shrink_to_fit_base(allocator_ref alloc, const type_metadata &type,
                                                           void *inline_storage, std::size_t max_inline) noexcept;

  RELOCO_API result<void> try_pop_back_base(const type_metadata &type) noexcept;

  [[nodiscard]] RELOCO_API result<void> try_erase_at_base(const type_metadata &type, std::size_t index) noexcept;

  RELOCO_API void retain_base(const type_metadata &type, function_ref<bool(const void *)> pred) noexcept;

  RELOCO_API void dedup_by_base(const type_metadata &type,
                                function_ref<bool(const void *, const void *)> same) noexcept;

  [[nodiscard]] RELOCO_API result<void *> try_insert_at_base(allocator_ref alloc, const type_metadata &type,
                                                             std::size_t index,
                                                             function_ref<result<void>(void *dest)> construct_fn,
                                                             void *inline_storage, std::size_t max_inline,
                                                             std::size_t max_cap) noexcept;

  void *data_ = nullptr;
  const vector_operations *operations_;
  std::size_t size_ = 0;
  std::size_t cap_ = 0;
};

/**
 * @brief Storage policy backing `vector<T>`: an `allocator_ref`-owned heap
 * allocation with no inline capacity, growable without bound (up to
 * `max_capacity`).
 */
class RELOCO_EXPORT heap_vector_base : public unowned_vector_base {
protected:
  constexpr explicit heap_vector_base(const vector_operations *ops, allocator_ref alloc = default_allocator()) noexcept
      : unowned_vector_base(ops), alloc_(alloc) {}

  ~heap_vector_base() noexcept = default;

  RELOCO_API void destroy_elements(const type_metadata &type) noexcept;

  constexpr void move_construct_from_base(const type_metadata &, heap_vector_base &&other) noexcept {
    operations_ = other.operations_;
    data_ = other.data_;
    size_ = other.size_;
    cap_ = other.cap_;
    alloc_ = other.alloc_;

    other.operations_ = nullptr;
    other.data_ = nullptr;
    other.size_ = 0;
    other.cap_ = 0;
  }

  RELOCO_API void move_assign_from_base(const type_metadata &type, heap_vector_base &&other) noexcept;

  [[nodiscard]] static constexpr void *get_inline_storage() noexcept { return nullptr; }

public:
  [[nodiscard]] constexpr static bool is_inline() noexcept { return false; }

  [[nodiscard]] allocator_ref get_allocator() const noexcept { return alloc_; }

  [[nodiscard]] static constexpr std::size_t inline_capacity() noexcept { return 0; }

  [[nodiscard]] static constexpr std::size_t max_capacity(const type_metadata &mt) noexcept {
    return std::numeric_limits<size_t>::max() / mt.element_size;
  }

private:
  allocator_ref alloc_;
};

/**
 * @brief Storage policy backing `inline_vector<T, Capacity>`: a
 * fixed-capacity buffer embedded directly in the object, with no allocator
 * and a hard `Capacity` ceiling that growth beyond fails against.
 */
class RELOCO_EXPORT inline_vector_base : public unowned_vector_base {
protected:
  constexpr inline_vector_base(const vector_operations *ops, void *storage, std::size_t capacity) noexcept
      : unowned_vector_base(ops, storage, 0, capacity) {}

  ~inline_vector_base() noexcept = default;

  RELOCO_API void destroy_elements(const type_metadata &type) noexcept;

  // Uses `data_` directly as the destination since unowned_vector_base already points it to local storage!
  RELOCO_API void move_construct_from_base(const type_metadata &type, inline_vector_base &&other) noexcept;

  RELOCO_API void move_assign_from_base(const type_metadata &type, inline_vector_base &&other) noexcept;

  [[nodiscard]] constexpr void *get_inline_storage() noexcept { return data_; }

public:
  [[nodiscard]] constexpr static bool is_inline() noexcept { return true; }

  // There's no real allocator with inline vector
  [[nodiscard]] static allocator_ref get_allocator() noexcept { return default_allocator(); }

  [[nodiscard]] std::size_t inline_capacity() const noexcept { return cap_; }

  [[nodiscard]] constexpr std::size_t max_capacity(const type_metadata &) noexcept { return cap_; }
};

/**
 * @brief Storage policy backing `outline_vector<T>`: elements live in a
 * caller-supplied, caller-owned byte span bound once at construction --
 * never rebound, reallocated, or grown past that span's capacity. Unlike
 * every other storage policy here, `outline_vector_base` supports no move
 * (nor, transitively, relocation) at all: there is no well-defined way to
 * "steal" a borrowed span out from under whoever actually owns it, the way
 * `heap_vector_base` steals an owned heap pointer or `inline_vector_base`/
 * `mixed_vector_base` relocate an embedded buffer's contents into another
 * object's own embedded buffer.
 */
class RELOCO_EXPORT outline_vector_base : public unowned_vector_base {
protected:
  constexpr outline_vector_base(const vector_operations *ops, void *storage, std::size_t capacity) noexcept
      : unowned_vector_base(ops, storage, 0, capacity) {}

  ~outline_vector_base() noexcept = default;

  RELOCO_API void destroy_elements(const type_metadata &type) noexcept;

  [[nodiscard]] constexpr void *get_inline_storage() noexcept { return data_; }

public:
  // The caller-supplied span is external, not embedded in *this.
  [[nodiscard]] constexpr static bool is_inline() noexcept { return false; }

  // There's no real allocator with an outline vector; nested fallible `T`
  // construction still needs one to forward to, exactly like inline_vector_base.
  [[nodiscard]] static allocator_ref get_allocator() noexcept { return default_allocator(); }

  [[nodiscard]] std::size_t inline_capacity() const noexcept { return cap_; }

  // The bound span's capacity is fixed for the lifetime of *this: growth past it
  // always fails with `error::capacity_exceeded`, never spills onto the heap.
  [[nodiscard]] constexpr std::size_t max_capacity(const type_metadata &) noexcept { return cap_; }
};

/**
 * @brief Storage policy backing `sso_vector<T, InlineCapacity>`: starts
 * using an embedded inline buffer like `inline_vector_base`, but falls back
 * to an `allocator_ref`-owned heap allocation once `InlineCapacity` is
 * exceeded, mirroring `basic_string`'s small-string optimization.
 */
class RELOCO_EXPORT mixed_vector_base : public unowned_vector_base {
private:
  void *inline_storage_;
  std::size_t inline_capacity_;
  allocator_ref alloc_;

protected:
  constexpr mixed_vector_base(const vector_operations *ops, void *inline_storage, std::size_t inline_capacity,
                              const allocator_ref alloc) noexcept
      : unowned_vector_base(ops, inline_storage, 0, inline_capacity), inline_storage_(inline_storage),
        inline_capacity_(inline_capacity), alloc_(alloc) {}

  ~mixed_vector_base() noexcept = default; // Deallocation handled by typed/derived layer or custom cleanup helper

  RELOCO_API void destroy_elements(const type_metadata &type) noexcept;

  RELOCO_API void move_construct_from_base(const type_metadata &type, mixed_vector_base &&other) noexcept;

  RELOCO_API void move_assign_from_base(const type_metadata &type, mixed_vector_base &&other) noexcept;

  [[nodiscard]] constexpr void *get_inline_storage() noexcept { return inline_storage_; }

public:
  [[nodiscard]] constexpr bool is_inline() const noexcept { return data_ == inline_storage_; }

  [[nodiscard]] allocator_ref get_allocator() const noexcept { return alloc_; }

  [[nodiscard]] std::size_t inline_capacity() const noexcept { return inline_capacity_; }

  [[nodiscard]] static constexpr std::size_t max_capacity(const type_metadata &mt) noexcept {
    return std::numeric_limits<size_t>::max() / mt.element_size;
  }
};

/**
 * @brief Strongly-typed layer `vector<T>`/`inline_vector<T, Capacity>`/
 * `sso_vector<T, InlineCapacity>`/`outline_vector<T>` actually derive from:
 * reintroduces `T`-aware member types and the full checked/`try_*`/
 * `unsafe_*` mutation and access surface (see
 * `docs/hardened-containers.md`), delegating every operation to the
 * untyped `Base` storage policy plus `metadata_for<T>`.
 */
template <typename T, typename Base> class RELOCO_EXPORT typed_vector_base : public Base {
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

protected:
  // Forward constructors to the underlying Base policy
  template <typename... Args>
  constexpr explicit typed_vector_base(Args &&...args) noexcept
      : Base(detail::get_operations_for<T>(), std::forward<Args>(args)...) {}

  ~typed_vector_base() noexcept = default;

public:
  /**
   * @brief Ensures storage for at least @p new_cap elements, growing the
   * backing allocation if needed.
   */
  [[nodiscard]] result<void> try_reserve(size_type new_cap) & noexcept {
    return Base::try_reserve_base(Base::get_allocator(), metadata_for<T>, new_cap, Base::get_inline_storage(),
                                  Base::inline_capacity(), Base::max_capacity(detail::metadata_for<T>));
  }

  /**
   * @brief Releases unused capacity back to the allocator, if supported.
   */
  [[nodiscard]] result<void> shrink_to_fit() & noexcept {
    return Base::shrink_to_fit_base(Base::get_allocator(), metadata_for<T>, Base::get_inline_storage(),
                                    Base::inline_capacity());
  }

  // ---- mutation ----

  /**
   * @brief Constructs a new element in place at the end of the vector,
   * growing storage first if needed.
   * Completely flattened: storage management and error safety are delegated
   * to the type-erased base engine via `try_insert_at_base`.
   */
  template <typename... Args>
  [[nodiscard]] result<std::reference_wrapper<T>> try_emplace_back(Args &&...args) & noexcept RELOCO_LIFETIMEBOUND {
    auto res = this->try_insert_at_base(
        this->get_allocator(), detail::metadata_for<T>, this->size_,
        [&args...](void *dest) noexcept -> result<void> {
          return construction_helpers::try_construct<T>(default_allocator(), static_cast<T *>(dest),
                                                        std::forward<Args>(args)...);
        },
        Base::get_inline_storage(), Base::inline_capacity(), Base::max_capacity(detail::metadata_for<T>));

    if (!res)
      return unexpected(res.error());

    // *res returns the raw void* pointer to the newly constructed element
    T *ptr = static_cast<T *>(*res);
    return std::ref(*ptr);
  }

  /**
   * @brief Appends a copy of @p value to the end of the vector.
   */
  [[nodiscard]] result<void> try_push_back(const T &value) & noexcept {
    auto res = try_emplace_back(value);
    return res ? result<void>{} : unexpected(res.error());
  }

  /**
   * @brief Appends a moved value to the end of the vector.
   */
  [[nodiscard]] result<void> try_push_back(T &&value) & noexcept {
    auto res = try_emplace_back(std::move(value));
    return res ? result<void>{} : unexpected(res.error());
  }

  /**
   * @brief Removes the last element. Fails with `error::container_empty` if
   * the vector is empty.
   */
  [[nodiscard]] result<void> try_pop_back() & noexcept { return Base::try_pop_back_base(metadata_for<T>); }

  // ---- capacity ----

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
    return Base::try_resize_base(Base::get_allocator(), metadata_for<T>, count, nullptr, Base::get_inline_storage(),
                                 Base::inline_capacity(), Base::max_capacity(detail::metadata_for<T>));
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
    static_assert(std::is_nothrow_copy_constructible_v<T>,
                  "try_resize(count) requires T to be default-constructible; use try_resize(count, value) instead.");
    return Base::try_resize_base(Base::get_allocator(), metadata_for<T>, count, &value, Base::get_inline_storage(),
                                 Base::inline_capacity(), Base::max_capacity(detail::metadata_for<T>));
  }

  /**
   * @brief Destroys every element and resets size to zero, keeping the
   * backing allocation (advising the allocator that large buffers are no
   * longer needed).
   * Completely flattened: delegates element destruction to the type-erased
   * base engine via `destroy_elements_base`.
   */
  void clear() noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      this->destroy_elements_base(detail::metadata_for<T>);
    }
    this->size_ = 0;
  }

  /**
   * @brief Passes memory usage hints to the underlying allocator for the entire backing buffer.
   * Only available for heap-backed or mixed vectors.
   */
  void advise(usage_hint hint) noexcept {
    if constexpr (!std::is_base_of_v<inline_vector_base, Base>) {
      if (this->cap_ > 0 && this->data_) {
        this->get_allocator().advise(this->data_, this->cap_ * sizeof(T), hint);
      }
    }
  }

  /**
   * @brief Surrenders the physical memory of the UNUSED capacity back to the OS,
   * while keeping virtual memory addresses intact.
   */
  void advise_unused(usage_hint hint = usage_hint::dont_need) noexcept {
    if constexpr (!std::is_base_of_v<inline_vector_base, Base>) {
      const size_type unused_elements = this->cap_ - this->size_;
      if (unused_elements > 0 && this->data_) {
        // Use strongly-typed data() accessor to safely perform pointer arithmetic
        pointer unused_ptr = this->data() + this->size_;
        this->get_allocator().advise(unused_ptr, unused_elements * sizeof(T), hint);
      }
    }
  }

  /**
   * @brief Removes the element at @p index, shifting subsequent elements
   * down by one.
   * Completely flattened: delegates bounds checking, vtable-driven destruction,
   * and safe shifting/relocation to the base engine via `try_erase_at_base`.
   */
  [[nodiscard]] result<void> try_erase_at(size_type index) & noexcept {
    return this->try_erase_at_base(detail::metadata_for<T>, index);
  }

  /**
   * @brief Removes the element at iterator @p pos.
   */
  [[nodiscard]] result<iterator> try_erase(const_iterator pos) & noexcept {
    const auto index = static_cast<size_type>(pos - this->begin());
    auto res = this->try_erase_at(index);
    if (!res)
      return unexpected(res.error());
    return this->begin() + index;
  }

  /**
   * @brief Rust `Vec::retain` equivalent: keeps only the elements for
   * which `pred(element)` returns `true`, destroying and compacting away
   * the rest in a single forward pass.
   * Flattened: delegates compaction, shifting, and destruction to `retain_base`.
   */
  template <typename Pred> void retain(Pred &&pred) & noexcept {
    // Type-erase the predicate into a function_ref taking `const void*`
    function_ref<bool(const void *)> erased_pred(
        [&pred](const void *elem_ptr) noexcept { return pred(*static_cast<const T *>(elem_ptr)); });

    this->retain_base(detail::metadata_for<T>, erased_pred);
  }

  /**
   * @brief Rust `Vec::dedup_by` equivalent: removes consecutive elements
   * for which `same(prev, current)` returns `true`.
   * Flattened: delegates to `dedup_by_base`.
   */
  template <typename BinPred> void dedup_by(BinPred &&same) & noexcept {
    if (this->size_ < 2)
      return;

    // Type-erase the binary predicate into a function_ref taking two `const void*` pointers
    function_ref<bool(const void *, const void *)> erased_same([&same](const void *a_ptr, const void *b_ptr) noexcept {
      return same(*static_cast<const T *>(a_ptr), *static_cast<const T *>(b_ptr));
    });

    this->dedup_by_base(detail::metadata_for<T>, erased_same);
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
   * Flattened: construction happens safely off-to-the-side first, and all
   * capacity management, shifting, and relocation are delegated to `try_insert_at_base`.
   */
  template <typename... Args>
  [[nodiscard]] result<std::reference_wrapper<T>> try_insert_at(size_type index,
                                                                Args &&...args) & noexcept RELOCO_LIFETIMEBOUND {
    if (index > this->size_)
      return unexpected(error::out_of_bounds);

    // Construct off to the side first (ensuring strong guarantee if construction fails)
    auto built = construction_helpers::try_allocate<T>(this->get_allocator(), std::forward<Args>(args)...);
    if (!built)
      return unexpected(built.error());

    // Delegate capacity reservation, shifting, and final placement to the base engine
    auto res = this->try_insert_at_base(
        this->get_allocator(), detail::metadata_for<T>, index,
        [&built](void *dest) noexcept -> result<void> {
          static_assert(std::is_nothrow_move_constructible_v<T>, "reloco requires noexcept move-construction.");
          new (dest) T(std::move(*built));
          return {};
        },
        Base::get_inline_storage(), Base::inline_capacity(), Base::max_capacity(detail::metadata_for<T>));

    if (!res)
      return unexpected(res.error());

    T *ptr = static_cast<T *>(*res);
    return std::ref(*ptr);
  }

  // ---- element access ----

  [[nodiscard]] result<std::reference_wrapper<T>> try_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= this->size_)
      return unexpected(error::out_of_bounds);
    return std::ref(static_cast<T *>(this->data_)[index]);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= this->size_)
      return unexpected(error::out_of_bounds);
    return std::cref(static_cast<const T *>(this->data_)[index]);
  }

  [[nodiscard]] T &operator[](size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < this->size_, "inline_vector index out of bounds");
    return static_cast<T *>(this->data_)[index];
  }

  [[nodiscard]] const T &operator[](size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < this->size_, "inline_vector index out of bounds");
    return static_cast<const T *>(this->data_)[index];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < this->size_, "vector index out of bounds");
    return static_cast<T *>(this->data_)[index];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_at(size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < this->size_, "vector index out of bounds");
    return static_cast<const T *>(this->data_)[index];
  }

  [[nodiscard]] T &front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!Base::empty(), "vector is empty");
    return static_cast<T *>(this->data_)[0];
  }

  [[nodiscard]] const T &front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!Base::empty(), "vector is empty");
    return static_cast<const T *>(this->data_)[0];
  }

  [[nodiscard]] T &back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!Base::empty(), "vector is empty");
    return static_cast<T *>(this->data_)[this->size_ - 1];
  }

  [[nodiscard]] const T &back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!Base::empty(), "vector is empty");
    return static_cast<const T *>(this->data_)[this->size_ - 1];
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_front() & noexcept RELOCO_LIFETIMEBOUND {
    if (Base::empty())
      return unexpected(error::container_empty);
    return std::ref(static_cast<T *>(this->data_)[0]);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_front() const & noexcept RELOCO_LIFETIMEBOUND {
    if (Base::empty())
      return unexpected(error::container_empty);
    return std::cref(static_cast<const T *>(this->data_)[0]);
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_back() & noexcept RELOCO_LIFETIMEBOUND {
    if (Base::empty())
      return unexpected(error::container_empty);
    return std::ref(static_cast<T *>(this->data_)[this->size_ - 1]);
  }

  [[nodiscard]] result<std::reference_wrapper<const T>> try_back() const & noexcept RELOCO_LIFETIMEBOUND {
    if (Base::empty())
      return unexpected(error::container_empty);
    return std::cref(static_cast<const T *>(this->data_)[this->size_ - 1]);
  }

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) T *data() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!Base::empty(), "vector is empty");
    return static_cast<T *>(this->data_);
  }

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) const T *data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!Base::empty(), "vector is empty");
    return static_cast<const T *>(this->data_);
  }

  [[nodiscard]] result<T *> try_data() & noexcept RELOCO_LIFETIMEBOUND {
    if (Base::empty())
      return unexpected(error::container_empty);
    return static_cast<T *>(this->data_);
  }

  [[nodiscard]] result<const T *> try_data() const & noexcept RELOCO_LIFETIMEBOUND {
    if (Base::empty())
      return unexpected(error::container_empty);
    return static_cast<const T *>(this->data_);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE
  RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) T *unsafe_data() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(this->data_ != nullptr, "vector data is null");
    return static_cast<T *>(this->data_);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE
  RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) const T *unsafe_data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(this->data_ != nullptr, "vector data is null");
    return static_cast<const T *>(this->data_);
  }

  // ---- iteration ----

  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) iterator begin() & noexcept RELOCO_LIFETIMEBOUND {
    return static_cast<T *>(this->data_);
  }
  [[nodiscard]] iterator end() & noexcept RELOCO_LIFETIMEBOUND { return static_cast<T *>(this->data_) + Base::size_; }
  [[nodiscard]] RELOCO_ASSUME_ALIGNED(effective_alignment_v<T>) const_iterator
      begin() const & noexcept RELOCO_LIFETIMEBOUND {
    return static_cast<const T *>(this->data_);
  }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND {
    return static_cast<const T *>(this->data_) + Base::size_;
  }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND {
    return static_cast<const T *>(this->data_);
  }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND {
    return static_cast<const T *>(this->data_) + Base::size_;
  }

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
};

RELOCO_END_UNSAFE_BUFFER_USAGE

#if RELOCO_SHARED_PROVIDE_DEFINITIONS
#include "vector_base.ipp"
#endif

} // namespace detail

} // namespace reloco
