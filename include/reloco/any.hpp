// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file any.hpp
 * @brief Type-erased, allocator-backed single-value container with fallible
 * construction and no dependency on RTTI.
 *
 * `reloco::any` is reloco's counterpart to `std::any`: it erases a value of
 * any decayed, move-constructible type behind a small, fixed vtable,
 * choosing the cheapest storage strategy available at construction time,
 * without ever throwing:
 *
 * - **Small object optimization**: a value that fits within `soo_capacity`
 *   bytes (and whose alignment does not exceed `alignof(std::max_align_t)`)
 *   is placement-newed directly into an inline buffer inside the handle.
 * - **Heap fallback**: anything larger is placement-newed into a single
 *   allocator block, exactly like `function::try_allocate`/
 *   `unique_ptr::try_allocate`.
 *
 * The vtable itself is a plain struct of function pointers and one
 * `reloco::type_id` field (no virtual dispatch, no `<typeinfo>`, no
 * `dynamic_cast`, no `typeid`) -- the same customization-point shape
 * `allocator.hpp`/`function.hpp` use for their own backend dispatch. Type
 * identity is established without RTTI via `reloco::type_id` (see
 * `type_id.hpp`), matching Rust's `std::any::TypeId`. One `static constexpr
 * vtable` instance per storage strategy and stored type is selected once
 * at construction and never branched on again.
 *
 * Every fallible entry point returns `reloco::result<T>` (see `error.hpp`).
 * Move-only by default: use `try_clone()` for an explicit deep copy, which
 * dispatches through `construction_helpers::try_clone_at` (see
 * `construction_helpers.hpp`) -- the stored type's own `try_clone`/
 * `try_allocate`/`try_create` if it implements one, falling back to plain
 * nothrow copy-construction -- and fails with `error::unsupported_operation`
 * if none of those apply.
 *
 * Access follows reloco's usual tri-tier convention: `get<T>()` asserts
 * (via `RELOCO_ASSERT`) that the stored value is present and holds exactly
 * `T`; `try_get<T>()` is the checked alternative, returning
 * `result<std::reference_wrapper<T>>` (failing with `error::container_empty`
 * if empty, or `error::invalid_argument` on a type mismatch); `unsafe_get<T>()`
 * skips the check entirely (only a `RELOCO_DEBUG_ASSERT`).
 *
 * A parallel, Rust-flavored surface mirrors Rust's `std::any::Any` trait
 * directly on top of the same underlying dispatch: `type_id()` (Rust's
 * `Any::type_id`) returns the held value's `reloco::type_id` (the "no
 * type" sentinel, `type_id{}`, if empty) -- `type_id().name()` additionally
 * gives an optional, game-engine-style debug name if one was registered
 * for the held type via `RELOCO_TYPE_ID_NAME` (see `type_id.hpp`),
 * `nullptr` otherwise; `downcast_ref<T>()`/
 * `downcast_mut<T>()` (Rust's `Any::downcast_ref`/`downcast_mut`) return a
 * nullable `const T *`/`T *` instead of asserting, reloco's usual analog
 * of Rust's `Option<&T>`/`Option<&mut T>` for a checked-but-non-asserting
 * accessor (see e.g. `flat_map`'s `find`); and `downcast<T>() &&` (Rust's
 * `Any::downcast`, consuming) moves the held `T` out into a `result<T>` --
 * `error::container_empty`/`error::invalid_argument` on failure, rather
 * than Rust's `Result<Box<T>, Box<dyn Any>>`, since every fallible reloco
 * operation returns `reloco::result<T>` (see `error.hpp`) and handing back
 * the original, differently-typed `any` would require a second error type.
 *
 * Like `function.hpp`/`unique_ptr.hpp`, this file's `namespace reloco` body
 * is wrapped in `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/
 * `RELOCO_END_UNSAFE_BUFFER_USAGE`: the vtable functions placement-new/
 * placement-destroy directly into raw storage (the inline buffer or an
 * allocator block), which has no bounds-tracked alternative. The public API
 * itself never exposes a raw pointer or caller-supplied storage.
 */

#include "construction_helpers.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "type_id.hpp"

#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief Type-erased, allocator-backed single-value container.
 *
 * Never copyable: use `try_clone()` explicitly for a deep copy. Empty
 * (default-constructed or moved-from) instances report `is<T>()` false for
 * every `T`; `get<T>`/`unsafe_get<T>` on an empty or mismatched instance
 * `RELOCO_ASSERT`s (or `RELOCO_DEBUG_ASSERT`s), use `try_get<T>` for a
 * checked alternative that reports an error instead.
 */
class RELOCO_EXPORT RELOCO_OWNER any {
public:
  /** @brief Inline capacity available to the small-object-optimization
   * storage tier, in bytes. */
  static constexpr std::size_t soo_capacity = 4 * sizeof(void *);

  /**
   * @brief Wraps a copy/move of `value` using the given allocator, choosing
   * the cheapest storage strategy available for its (decayed) type: the
   * inline SOO buffer, or a single heap allocation.
   */
  template <typename T, typename Decayed = std::decay_t<T>,
            std::enable_if_t<!std::is_same_v<Decayed, any>, int> = 0>
  [[nodiscard]] static result<any> try_allocate(allocator_ref alloc, T &&value) noexcept {
    return try_allocate_impl<Decayed>(alloc, std::forward<T>(value));
  }

  /**
   * @brief Constructs a `Decayed = T` value in place from `args...` using
   * the given allocator, avoiding the extra move/copy `try_allocate(alloc,
   * value)` requires.
   */
  template <typename T, typename... Args>
  [[nodiscard]] static result<any> try_allocate(allocator_ref alloc, std::in_place_type_t<T>,
                                                Args &&...args) noexcept {
    return try_allocate_impl<T>(alloc, std::forward<Args>(args)...);
  }

  /**
   * @brief `try_allocate` using the process-wide default allocator (see
   * `default_allocator()`).
   */
  template <typename T, typename Decayed = std::decay_t<T>,
            std::enable_if_t<!std::is_same_v<Decayed, any>, int> = 0>
  [[nodiscard]] static result<any> try_create(T &&value) noexcept {
    return try_allocate(default_allocator(), std::forward<T>(value));
  }

  /**
   * @brief In-place `try_allocate` using the process-wide default allocator.
   */
  template <typename T, typename... Args>
  [[nodiscard]] static result<any> try_create(std::in_place_type_t<T>, Args &&...args) noexcept {
    return try_allocate(default_allocator(), std::in_place_type<T>, std::forward<Args>(args)...);
  }

  constexpr any() noexcept = default;
  constexpr any(std::nullptr_t) noexcept {}

  any(const any &) = delete;
  any &operator=(const any &) = delete;

  any(any &&other) noexcept : vtable_(other.vtable_), alloc_(other.alloc_) {
    if (vtable_)
      vtable_->move_and_destroy(&other.storage_, &storage_);
    other.vtable_ = nullptr;
  }

  any &operator=(any &&other) noexcept {
    if (this != &other) {
      reset();
      vtable_ = other.vtable_;
      alloc_ = other.alloc_;
      if (vtable_)
        vtable_->move_and_destroy(&other.storage_, &storage_);
      other.vtable_ = nullptr;
    }
    return *this;
  }

  ~any() { reset(); }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtable_ != nullptr; }

  /** @brief `std::any`-compatible alias for `operator bool()`. */
  [[nodiscard]] constexpr bool has_value() const noexcept { return vtable_ != nullptr; }

  /**
   * @brief Reports whether this instance is non-empty and holds exactly
   * `std::decay_t<T>`, established without RTTI (see `type_id.hpp`).
   */
  template <typename T> [[nodiscard]] constexpr bool is() const noexcept {
    return vtable_ != nullptr && vtable_->type_id == reloco::type_id::of<std::decay_t<T>>();
  }

  /**
   * @brief Rust `Any::type_id` equivalent: the held value's `reloco::type_id`
   * (see `type_id.hpp`), or the "no type" sentinel (`type_id{}`, `!bool()`)
   * if this instance is empty.
   */
  [[nodiscard]] constexpr reloco::type_id type_id() const noexcept {
    return vtable_ != nullptr ? vtable_->type_id : reloco::type_id();
  }

  /**
   * @brief Rust `Any::downcast_mut` equivalent: a mutable pointer to the
   * held `std::decay_t<T>`, or `nullptr` if this instance is empty or
   * holds a different type -- reloco's usual nullable-pointer analog of
   * Rust's `Option<&mut T>` for a checked, non-asserting accessor (see
   * e.g. `flat_map::find`). See `get<T>()`/`try_get<T>()` for the
   * asserted/`result`-returning tiers.
   */
  template <typename T> [[nodiscard]] T *downcast_mut() noexcept RELOCO_LIFETIMEBOUND {
    using decayed = std::decay_t<T>;
    return is<decayed>() ? static_cast<decayed *>(vtable_->data(&storage_)) : nullptr;
  }

  /**
   * @brief Rust `Any::downcast_ref` equivalent: a `const` pointer to the
   * held `std::decay_t<T>`, or `nullptr` if this instance is empty or
   * holds a different type. See @ref downcast_mut for details.
   */
  template <typename T> [[nodiscard]] const T *downcast_ref() const noexcept RELOCO_LIFETIMEBOUND {
    using decayed = std::decay_t<T>;
    return is<decayed>() ? static_cast<const decayed *>(vtable_->data(const_cast<storage *>(&storage_))) : nullptr;
  }

  /**
   * @brief Rust `Any::downcast` equivalent (consuming): moves the held
   * `std::decay_t<T>` out, failing with `error::container_empty` on an
   * empty instance or `error::invalid_argument` on a type mismatch,
   * instead of Rust's `Result<Box<T>, Box<dyn Any>>` -- see the file-level
   * docs for why the mismatch case cannot also hand back the original
   * `any`. Equivalent to a checked `std::move(*this).get<T>()`.
   */
  template <typename T> [[nodiscard]] result<std::decay_t<T>> downcast() && noexcept {
    using decayed = std::decay_t<T>;
    if (!vtable_)
      RELOCO_UNLIKELY { return unexpected(error::container_empty); }
    if (!is<decayed>())
      RELOCO_UNLIKELY { return unexpected(error::invalid_argument); }
    return result<decayed>(std::move(*static_cast<decayed *>(vtable_->data(&storage_))));
  }

  /** @brief Destroys the held value (releasing any heap allocation) and
   * resets this instance to the empty state. */
  void reset() noexcept {
    if (vtable_) {
      vtable_->destroy(&storage_, alloc_);
      vtable_ = nullptr;
    }
  }

  /**
   * @brief Fallible deep copy. Cloning an empty instance always succeeds,
   * yielding another empty instance; otherwise resolves the held type's
   * best cloning strategy via `construction_helpers::try_clone_at`
   * (a custom `try_clone`/`try_allocate`/`try_create` the held type
   * implements, falling back to plain nothrow copy-construction -- see
   * `construction_helpers.hpp`), failing with `error::unsupported_operation`
   * if none of those apply.
   */
  [[nodiscard]] result<any> try_clone() const noexcept {
    if (!vtable_)
      return any();

    any cloned;
    cloned.alloc_ = alloc_;
    auto res = vtable_->clone_into(&storage_, &cloned.storage_, alloc_);
    if (!res)
      return unexpected(res.error());
    cloned.vtable_ = vtable_; // Only marked valid once construction has succeeded.
    return cloned;
  }

  /** @brief Asserted access to the held `T`. See the class docs for the
   * checked (`try_get`) and unsafe (`unsafe_get`) alternatives. */
  template <typename T> [[nodiscard]] T &get() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(is<T>(), "any: get<T>() called with an empty instance or the wrong T");
    return *static_cast<T *>(vtable_->data(&storage_));
  }

  /** @brief `const`-qualified overload of @ref get. */
  template <typename T> [[nodiscard]] const T &get() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(is<T>(), "any: get<T>() called with an empty instance or the wrong T");
    return *static_cast<const T *>(vtable_->data(const_cast<storage *>(&storage_)));
  }

  /** @brief Rvalue overload of @ref get, moving the held `T` out. */
  template <typename T> [[nodiscard]] T &&get() && noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(is<T>(), "any: get<T>() called with an empty instance or the wrong T");
    return std::move(*static_cast<T *>(vtable_->data(&storage_)));
  }

  /**
   * @brief Checked access: fails with `error::container_empty` on an empty
   * instance, or `error::invalid_argument` if the held type is not
   * `std::decay_t<T>`, instead of asserting.
   */
  template <typename T> [[nodiscard]] result<std::reference_wrapper<T>> try_get() & noexcept RELOCO_LIFETIMEBOUND {
    if (!vtable_)
      RELOCO_UNLIKELY { return unexpected(error::container_empty); }
    if (!is<T>())
      RELOCO_UNLIKELY { return unexpected(error::invalid_argument); }
    return result<std::reference_wrapper<T>>(std::ref(*static_cast<T *>(vtable_->data(&storage_))));
  }

  /** @brief `const`-qualified overload of @ref try_get. */
  template <typename T>
  [[nodiscard]] result<std::reference_wrapper<const T>> try_get() const & noexcept RELOCO_LIFETIMEBOUND {
    if (!vtable_)
      RELOCO_UNLIKELY { return unexpected(error::container_empty); }
    if (!is<T>())
      RELOCO_UNLIKELY { return unexpected(error::invalid_argument); }
    return result<std::reference_wrapper<const T>>(
        std::cref(*static_cast<const T *>(vtable_->data(const_cast<storage *>(&storage_)))));
  }

  /**
   * @brief Accesses the held `T` without the always-on empty/type check
   * that `get()` performs.
   *
   * Explicitly-unsafe tier: only a `RELOCO_DEBUG_ASSERT`, so it is compiled
   * out under `NDEBUG` (unless `RELOCO_DEBUG` is also defined). Use only
   * once `is<T>()` has already been established.
   */
  template <typename T> [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_get() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(is<T>(), "any: unsafe_get<T>() called with an empty instance or the wrong T");
    return *static_cast<T *>(vtable_->data(&storage_));
  }

  /** @brief `const`-qualified overload of @ref unsafe_get. */
  template <typename T>
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_get() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(is<T>(), "any: unsafe_get<T>() called with an empty instance or the wrong T");
    return *static_cast<const T *>(vtable_->data(const_cast<storage *>(&storage_)));
  }

private:
  union storage {
    alignas(std::max_align_t) std::byte buffer[soo_capacity];
    void *heap_ptr;
  };

  struct vtable {
    reloco::type_id type_id;
    void (*destroy)(storage *, allocator_ref);
    void (*move_and_destroy)(storage *src, storage *dest);
    // Constructs a clone of `src`'s held value directly into uninitialized
    // `dest` storage of the same tier (SOO or heap), using whichever
    // cloning strategy `construction_helpers::try_clone_at` resolves for
    // the held type; see `try_clone()` above.
    result<void> (*clone_into)(const storage *src, storage *dest, allocator_ref alloc);
    void *(*data)(storage *);
  };

  template <typename T, typename... Args>
  [[nodiscard]] static result<any> try_allocate_impl(allocator_ref alloc, Args &&...args) noexcept {
    static_assert(!std::is_void_v<T>, "any: cannot store void");
    static_assert(std::is_constructible_v<T, Args &&...>, "any: T must be constructible from the given arguments");

    any a;
    a.alloc_ = alloc;

    if constexpr (sizeof(T) <= soo_capacity && alignof(T) <= alignof(std::max_align_t)) {
      new (&a.storage_.buffer) T(std::forward<Args>(args)...);
      a.vtable_ = &object_vtable_factory<T>::soo_instance;
    } else {
      auto block = alloc.allocate(sizeof(T), alignof(T));
      if (!block)
        return unexpected(block.error());
      new (block->ptr) T(std::forward<Args>(args)...);
      a.storage_.heap_ptr = block->ptr;
      a.vtable_ = &object_vtable_factory<T>::heap_instance;
    }
    return a;
  }

  template <typename T> struct object_vtable_factory {
    // Whether construction_helpers::try_clone_at<T> can be evaluated without
    // hitting its "T must be nothrow copy constructible" fallback
    // static_assert: true if T implements any fallible clone protocol
    // (concepts.hpp), or is plain nothrow copy constructible. Gating on this
    // keeps a genuinely non-cloneable stored type (e.g. a move-only type with
    // no try_clone) a *runtime* `error::unsupported_operation` from
    // `try_clone()`, rather than a hard compile error merely from storing it
    // in an `any`.
    static constexpr bool cloneable = has_try_clone_v<T> || has_try_allocate_v<T, const T &> ||
                                      has_try_create_v<T, const T &> || std::is_nothrow_copy_constructible_v<T>;

    static constexpr vtable soo_instance = {
        reloco::type_id::of<T>(),
        [](storage *s, allocator_ref) noexcept { reinterpret_cast<T *>(s->buffer)->~T(); },
        [](storage *src, storage *dest) noexcept {
          new (dest->buffer) T(std::move(*reinterpret_cast<T *>(src->buffer)));
          reinterpret_cast<T *>(src->buffer)->~T();
        },
        [](const storage *src, storage *dest, allocator_ref alloc) noexcept -> result<void> {
          if constexpr (cloneable) {
            return construction_helpers::try_clone_at<T>(alloc, reinterpret_cast<T *>(dest->buffer),
                                                          *reinterpret_cast<const T *>(src->buffer));
          } else {
            return unexpected(error::unsupported_operation);
          }
        },
        [](storage *s) noexcept -> void * { return reinterpret_cast<void *>(s->buffer); },
    };

    static constexpr vtable heap_instance = {
        reloco::type_id::of<T>(),
        [](storage *s, allocator_ref alloc) noexcept {
          static_cast<T *>(s->heap_ptr)->~T();
          alloc.deallocate(s->heap_ptr, sizeof(T));
        },
        [](storage *src, storage *dest) noexcept {
          dest->heap_ptr = src->heap_ptr;
          src->heap_ptr = nullptr;
        },
        [](const storage *src, storage *dest, allocator_ref alloc) noexcept -> result<void> {
          if constexpr (cloneable) {
            auto block = alloc.allocate(sizeof(T), alignof(T));
            if (!block)
              return unexpected(block.error());
            auto res = construction_helpers::try_clone_at<T>(alloc, static_cast<T *>(block->ptr),
                                                              *static_cast<const T *>(src->heap_ptr));
            if (!res) {
              alloc.deallocate(block->ptr, sizeof(T));
              return unexpected(res.error());
            }
            dest->heap_ptr = block->ptr;
            return {};
          } else {
            return unexpected(error::unsupported_operation);
          }
        },
        [](storage *s) noexcept -> void * { return s->heap_ptr; },
    };
  };

  storage storage_{};
  const vtable *vtable_{nullptr};
  allocator_ref alloc_{};
};

/**
 * @brief `any` may hold an arbitrary stored type inline in its
 * small-object-optimization buffer; relocating it by copying bytes would
 * skip that type's own move/destroy semantics, which is only safe if the
 * stored type is itself relocatable. Since the stored type is erased, this
 * is conservatively `false` -- the same answer the default
 * (`is_trivially_copyable_v<any>`, always `false` due to the user-declared
 * destructor) already gives. Declared explicitly for documentation.
 */
template <> struct is_trivially_relocatable<any> : std::false_type {};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
