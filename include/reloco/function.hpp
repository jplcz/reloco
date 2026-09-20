// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file function.hpp
 * @brief Type-erased, allocator-backed callable wrapper with fallible
 * construction.
 *
 * `function<R(Args...)>` is reloco's counterpart to `std::function`: it
 * erases any callable convertible to `R(Args...)` behind a small, fixed
 * vtable, choosing the cheapest storage strategy available at construction
 * time, without ever throwing:
 *
 * - **Plain function pointer** (or a captureless lambda convertible to
 *   one): stored directly in the handle, no allocation at all.
 * - **Small object optimization**: a callable that fits within
 *   `soo_capacity` bytes (and whose alignment does not exceed
 *   `alignof(std::max_align_t)`) is placement-newed directly into an inline
 *   buffer inside the handle.
 * - **Heap fallback**: anything larger is placement-newed into a single
 *   allocator block, exactly like `unique_ptr::try_allocate`.
 *
 * The vtable itself is a plain struct of function pointers (no virtual
 * dispatch, no RTTI), the same customization-point shape `allocator.hpp`
 * uses for `allocator_ref`'s own backend dispatch -- one `static constexpr
 * vtable` instance per storage strategy and captured type, selected once at
 * construction and never branched on again.
 *
 * Every fallible entry point returns `reloco::result<T>` (see `error.hpp`).
 * Move-only by default: use `try_clone()` for an explicit deep copy, which
 * fails with `error::unsupported_operation` if the captured callable is not
 * `std::is_nothrow_copy_constructible_v`.
 *
 * Like `allocator.hpp`/`unique_ptr.hpp`, this file's `namespace reloco` body
 * is wrapped in `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/
 * `RELOCO_END_UNSAFE_BUFFER_USAGE`: the vtable functions placement-new/
 * placement-destroy directly into raw storage (the inline buffer or an
 * allocator block), which has no bounds-tracked alternative. The public API
 * itself never exposes a raw pointer or caller-supplied storage.
 */

#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename Sig> class function;

namespace detail {

template <typename T> struct is_result : std::false_type {};
template <typename T> struct is_result<result<T>> : std::true_type {};
template <typename T> inline constexpr bool is_result_v = is_result<T>::value;

} // namespace detail

/**
 * @brief Type-erased, allocator-backed callable wrapper.
 *
 * Never copyable: use `try_clone()` explicitly for a deep copy. Empty
 * (default-constructed or moved-from) functions may not be invoked via
 * `operator()` (`RELOCO_ASSERT`s); use `try_call` for a checked invocation
 * that reports `error::container_empty` instead.
 */
template <typename R, typename... Args> class [[nodiscard]] RELOCO_OWNER function<R(Args...)> {
public:
  /** @brief Inline capacity available to the small-object-optimization
   * storage tier, in bytes. */
  static constexpr std::size_t soo_capacity = 4 * sizeof(void *);

  /**
   * @brief Wraps `func` using the given allocator, choosing the cheapest
   * storage strategy available for its (decayed) type: a bare function
   * pointer, the inline SOO buffer, or a single heap allocation.
   */
  template <typename F>
  [[nodiscard]] static result<function> try_allocate(allocator_ref alloc, F &&func) noexcept {
    using decayed_f = std::decay_t<F>;
    static_assert(std::is_invocable_r_v<R, decayed_f &, Args...>,
                  "function<R(Args...)>: F must be invocable as R(Args...)");

    function f;
    f.alloc_ = alloc;

    if constexpr (std::is_convertible_v<F, R (*)(Args...)>) {
      f.storage_.func_ptr = reinterpret_cast<void *>(static_cast<R (*)(Args...)>(func));
      f.vtable_ = &c_pointer_vtable_factory<decayed_f>::instance;
    } else if constexpr (sizeof(decayed_f) <= soo_capacity &&
                         alignof(decayed_f) <= alignof(std::max_align_t)) {
      new (&f.storage_.buffer) decayed_f(std::forward<F>(func));
      f.vtable_ = &object_vtable_factory<decayed_f>::soo_instance;
    } else {
      auto block = alloc.allocate(sizeof(decayed_f), alignof(decayed_f));
      if (!block)
        return unexpected(block.error());
      new (block->ptr) decayed_f(std::forward<F>(func));
      f.storage_.heap_ptr = block->ptr;
      f.vtable_ = &object_vtable_factory<decayed_f>::heap_instance;
    }
    return f;
  }

  /**
   * @brief `try_allocate` using the process-wide default allocator (see
   * `default_allocator()`).
   */
  template <typename F> [[nodiscard]] static result<function> try_create(F &&func) noexcept {
    return try_allocate(default_allocator(), std::forward<F>(func));
  }

  constexpr function() noexcept = default;
  constexpr function(std::nullptr_t) noexcept {}

  function(const function &) = delete;
  function &operator=(const function &) = delete;

  function(function &&other) noexcept : vtable_(other.vtable_), alloc_(other.alloc_) {
    if (vtable_)
      vtable_->move_and_destroy(&other.storage_, &storage_);
    other.vtable_ = nullptr;
  }

  function &operator=(function &&other) noexcept {
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

  ~function() { reset(); }

  /** @brief Invokes the wrapped callable. Asserts non-empty (see
   * `try_call` for a checked alternative, or `unsafe_call` to skip the
   * check entirely). */
  R operator()(Args... args) const {
    RELOCO_ASSERT(vtable_ != nullptr, "function: call to an empty function");
    return vtable_->invoke(&storage_, std::forward<Args>(args)...);
  }

  /**
   * @brief Invokes the wrapped callable without the always-on non-empty
   * check that `operator()` performs.
   *
   * Explicitly-unsafe tier: only a `RELOCO_DEBUG_ASSERT`, so it is compiled
   * out under `NDEBUG` (unless `RELOCO_DEBUG` is also defined). Use only
   * once non-empty has already been established via `operator bool()`.
   */
  RELOCO_UNSAFE_BUFFER_USAGE R unsafe_call(Args... args) const {
    RELOCO_DEBUG_ASSERT(vtable_ != nullptr, "function: call to an empty function");
    return vtable_->invoke(&storage_, std::forward<Args>(args)...);
  }

  /**
   * @brief Checked invocation: fails with `error::container_empty` on an
   * empty function instead of asserting.
   *
   * If `R` is itself a `reloco::result<U>`, the call's own result is
   * returned as-is (flattened); otherwise the call's return value is
   * wrapped in `result<R>`.
   */
  template <typename... CallArgs> auto try_call(CallArgs &&...args) const noexcept {
    if constexpr (detail::is_result_v<R>) {
      if (!vtable_) RELOCO_UNLIKELY { return R(unexpected(error::container_empty)); }
      return vtable_->invoke(&storage_, std::forward<CallArgs>(args)...);
    } else {
      if (!vtable_) RELOCO_UNLIKELY { return result<R>(unexpected(error::container_empty)); }
      return result<R>(vtable_->invoke(&storage_, std::forward<CallArgs>(args)...));
    }
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtable_ != nullptr; }

  /** @brief Destroys the wrapped callable (releasing any heap allocation)
   * and resets this wrapper to the empty state. */
  void reset() noexcept {
    if (vtable_) {
      vtable_->destroy(&storage_, alloc_);
      vtable_ = nullptr;
    }
  }

  /**
   * @brief Fallible deep copy. Cloning an empty function always succeeds,
   * yielding another empty function; otherwise fails with
   * `error::unsupported_operation` if the captured callable is not
   * `std::is_nothrow_copy_constructible_v`.
   */
  [[nodiscard]] result<function> try_clone() const noexcept {
    if (!vtable_)
      return function();

    auto cloned_storage = vtable_->try_clone(&storage_, alloc_);
    if (!cloned_storage)
      return unexpected(cloned_storage.error());

    function cloned;
    cloned.alloc_ = alloc_;
    cloned.vtable_ = vtable_;
    if (cloned_storage.value() == nullptr) {
      // Sentinel: no allocation was needed (function pointer or SOO copy);
      // vtable_->copy_soo performs the actual bitwise/placement-new copy.
      RELOCO_ASSERT(vtable_->copy_soo != nullptr, "function: internal logic error");
      vtable_->copy_soo(&storage_, &cloned.storage_);
    } else {
      cloned.storage_.heap_ptr = cloned_storage.value();
    }
    return cloned;
  }

private:
  union storage {
    alignas(std::max_align_t) std::byte buffer[soo_capacity];
    void *heap_ptr;
    void *func_ptr;
  };

  struct vtable {
    R (*invoke)(const storage *, Args...);
    void (*destroy)(storage *, allocator_ref);
    void (*move_and_destroy)(storage *src, storage *dest);
    // Returns nullptr as a sentinel meaning "no allocation was needed; call
    // copy_soo instead", or a non-null heap pointer already holding the
    // cloned callable.
    result<void *> (*try_clone)(const storage *, allocator_ref);
    void (*copy_soo)(const storage *src, storage *dest);
  };

  template <typename F> struct c_pointer_vtable_factory {
    static constexpr vtable instance = {
        [](const storage *s, Args... args) -> R {
          auto fp = reinterpret_cast<R (*)(Args...)>(s->func_ptr);
          return fp(std::forward<Args>(args)...);
        },
        [](storage *, allocator_ref) noexcept {
          // No-op: a bare function pointer owns nothing.
        },
        [](storage *src, storage *dest) noexcept { dest->func_ptr = src->func_ptr; },
        [](const storage *, allocator_ref) noexcept -> result<void *> {
          return result<void *>(nullptr); // No allocation needed.
        },
        [](const storage *src, storage *dest) noexcept { dest->func_ptr = src->func_ptr; },
    };
  };

  template <typename F> struct object_vtable_factory {
    static constexpr vtable soo_instance = {
        [](const storage *s, Args... args) -> R {
          return (*reinterpret_cast<const F *>(s->buffer))(std::forward<Args>(args)...);
        },
        [](storage *s, allocator_ref) noexcept { reinterpret_cast<F *>(s->buffer)->~F(); },
        [](storage *src, storage *dest) noexcept {
          new (dest->buffer) F(std::move(*reinterpret_cast<F *>(src->buffer)));
          reinterpret_cast<F *>(src->buffer)->~F();
        },
        [](const storage *, allocator_ref) noexcept -> result<void *> {
          if constexpr (std::is_nothrow_copy_constructible_v<F>) {
            return result<void *>(nullptr); // SOO: copy_soo does the work.
          } else {
            return unexpected(error::unsupported_operation);
          }
        },
        [](const storage *src, storage *dest) noexcept {
          if constexpr (std::is_nothrow_copy_constructible_v<F>) {
            new (dest->buffer) F(*reinterpret_cast<const F *>(src->buffer));
          } else {
            RELOCO_ASSERT(false, "function: copy_soo called on a non-copyable captured type");
          }
        },
    };

    static constexpr vtable heap_instance = {
        [](const storage *s, Args... args) -> R {
          return (*static_cast<const F *>(s->heap_ptr))(std::forward<Args>(args)...);
        },
        [](storage *s, allocator_ref alloc) noexcept {
          static_cast<F *>(s->heap_ptr)->~F();
          alloc.deallocate(s->heap_ptr, sizeof(F));
        },
        [](storage *src, storage *dest) noexcept {
          dest->heap_ptr = src->heap_ptr;
          src->heap_ptr = nullptr;
        },
        [](const storage *s, allocator_ref alloc) noexcept -> result<void *> {
          if constexpr (std::is_nothrow_copy_constructible_v<F>) {
            const F &original = *static_cast<const F *>(s->heap_ptr);
            auto block = alloc.allocate(sizeof(F), alignof(F));
            if (!block)
              return unexpected(block.error());
            return result<void *>(new (block->ptr) F(original));
          } else {
            return unexpected(error::unsupported_operation);
          }
        },
        nullptr, // Never invoked: try_clone above never returns the sentinel.
    };
  };

  storage storage_{};
  const vtable *vtable_{nullptr};
  allocator_ref alloc_{};
};

/**
 * @brief `function<R(Args...)>` may hold an arbitrary captured callable
 * inline in its small-object-optimization buffer; relocating it by copying
 * bytes would skip that callable's own move/destroy semantics, which is
 * only safe if the captured type is itself relocatable. Since the captured
 * type is erased, this is conservatively `false` -- the same answer the
 * default (`is_trivially_copyable_v<function<R(Args...)>>`, always `false`
 * due to the user-declared destructor) already gives. Declared explicitly
 * for documentation.
 */
template <typename R, typename... Args>
struct is_trivially_relocatable<function<R(Args...)>> : std::false_type {};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
