// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file inplace_function.hpp
 * @brief Zero-allocation, fixed-capacity callable wrapper.
 *
 * `reloco::inplace_function` provides a deterministic alternative to `std::function`.
 * It guarantees zero heap allocations by storing the type-erased callable entirely
 * on the stack within a fixed-size byte buffer.
 *
 * If a functor exceeds the `Capacity` or alignment requirements, the compilation
 * fails via `static_assert`, preventing silent fallback to dynamic allocation.
 *
 * Following the reloco architecture:
 * 1. **Checked Tier:** `operator()` safely traps via `RELOCO_ASSERT` if empty.
 * 2. **Unsafe Tier:** `unsafe_invoke()` skips the empty check for hot paths,
 *    gated by `RELOCO_UNSAFE_BUFFER_USAGE`.
 */

#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename Signature, size_t Capacity = 32> class inplace_function; // Primary template undefined

template <typename R, typename... Args, size_t Capacity>
class RELOCO_CONSUMABLE(unconsumed) inplace_function<R(Args...), Capacity> {
private:
  struct vtable {
    R (*invoke)(void *, Args &&...);
    void (*relocate)(void *dest, void *src) noexcept;
    void (*destroy)(void *) noexcept;
  };

  template <typename Functor>
  static constexpr vtable vtable_for = {// Invoke
                                        [](void *storage, Args &&...args) -> R {
                                          return std::invoke(*static_cast<Functor *>(storage),
                                                             std::forward<Args>(args)...);
                                        },

                                        // Relocate (Move-construct and destroy)
                                        [](void *dest, void *src) noexcept {
                                          Functor *src_ptr = static_cast<Functor *>(src);

                                          // If the underlying type supports trivial relocation, we can skip the
                                          // move-constructor and destructor overhead entirely.
                                          if constexpr (is_trivially_relocatable<Functor>::value) {
                                            memcpy(dest, src, sizeof(Functor));
                                          } else {
                                            new (dest) Functor(std::move(*src_ptr));
                                            src_ptr->~Functor();
                                          }
                                        },

                                        // Destroy
                                        [](void *storage) noexcept {
                                          if constexpr (!std::is_trivially_destructible_v<Functor>) {
                                            static_cast<Functor *>(storage)->~Functor();
                                          }
                                        }};

  const vtable *vptr_{nullptr};
  alignas(std::max_align_t) std::byte storage_[Capacity];

public:
  using result_type = R;

  constexpr inplace_function() noexcept RELOCO_RETURN_TYPESTATE(consumed) = default;
  constexpr inplace_function(std::nullptr_t) noexcept RELOCO_RETURN_TYPESTATE(consumed) {}

  template <typename F, typename Decayed = std::decay_t<F>,
            typename =
                std::enable_if_t<!std::is_same_v<Decayed, inplace_function> && std::is_invocable_r_v<R, F &&, Args...>>>
  inplace_function(F &&f) noexcept(std::is_nothrow_constructible_v<Decayed, F &&>) RELOCO_RETURN_TYPESTATE(unconsumed) {

    static_assert(sizeof(Decayed) <= Capacity, "Functor size exceeds inplace_function Capacity. "
                                               "Increase Capacity or capture fewer variables by value.");

    static_assert(alignof(Decayed) <= alignof(std::max_align_t), "Functor alignment exceeds std::max_align_t.");

    new (storage_) Decayed(std::forward<F>(f));
    vptr_ = &vtable_for<Decayed>;
  }

  inplace_function(const inplace_function &) = delete;
  inplace_function &operator=(const inplace_function &) = delete;

  inplace_function(inplace_function &&other) noexcept RELOCO_RETURN_TYPESTATE(unknown) {
    if (other.vptr_) {
      other.vptr_->relocate(storage_, other.storage_);
      vptr_ = other.vptr_;
      other.vptr_ = nullptr;
    }
  }

  ~inplace_function() {
    if (vptr_) {
      vptr_->destroy(storage_);
    }
  }

  inplace_function &operator=(std::nullptr_t) noexcept RELOCO_SET_TYPESTATE(consumed) {
    reset();
    return *this;
  }

  inplace_function &operator=(inplace_function &&other) noexcept RELOCO_SET_TYPESTATE(unknown) {
    if (this != &other) {
      reset();
      if (other.vptr_) {
        other.vptr_->relocate(storage_, other.storage_);
        vptr_ = other.vptr_;
        other.vptr_ = nullptr;
      }
    }
    return *this;
  }

  template <typename F, typename Decayed = std::decay_t<F>,
            typename =
                std::enable_if_t<!std::is_same_v<Decayed, inplace_function> && std::is_invocable_r_v<R, F &&, Args...>>>
  inplace_function &operator=(F &&f) noexcept(std::is_nothrow_constructible_v<Decayed, F &&>)
      RELOCO_SET_TYPESTATE(unconsumed) {

    static_assert(sizeof(Decayed) <= Capacity, "Functor size exceeds Capacity.");
    static_assert(alignof(Decayed) <= alignof(std::max_align_t), "Functor over-aligned.");

    reset();
    new (storage_) Decayed(std::forward<F>(f));
    vptr_ = &vtable_for<Decayed>;
    return *this;
  }

  [[nodiscard]] constexpr bool has_value() const noexcept { return vptr_ != nullptr; }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vptr_ != nullptr; }

  /**
   * @brief Checked tier: Safely traps if the function is empty.
   */
  R operator()(Args... args) RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_ASSERT(vptr_ != nullptr, "inplace_function: invoked while empty");
    return vptr_->invoke(storage_, std::forward<Args>(args)...);
  }

  /**
   * @brief Unsafe tier: Bypasses the empty check for hot paths.
   */
  RELOCO_UNSAFE_BUFFER_USAGE R unsafe_invoke(Args... args) RELOCO_CALLABLE_WHEN("unconsumed") {
    RELOCO_DEBUG_ASSERT(vptr_ != nullptr, "inplace_function: unsafe_invoke while empty");
    return vptr_->invoke(storage_, std::forward<Args>(args)...);
  }

  RELOCO_REINITIALIZES void reset() noexcept RELOCO_SET_TYPESTATE(consumed) {
    if (vptr_) {
      vptr_->destroy(storage_);
      vptr_ = nullptr;
    }
  }

  [[nodiscard]] inplace_function &as_known() & noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(vptr_ != nullptr, "inplace_function: as_known() called on empty object");
    return *this;
  }

  [[nodiscard]] const inplace_function &as_known() const & noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(vptr_ != nullptr, "inplace_function: as_known() called on empty object");
    return *this;
  }

  void swap(inplace_function &other) noexcept {
    if (this == &other)
      return;

    if (vptr_ && other.vptr_) {
      alignas(std::max_align_t) std::byte temp[Capacity];
      vptr_->relocate(temp, storage_);
      other.vptr_->relocate(storage_, other.storage_);
      vptr_->relocate(other.storage_, temp);
      std::swap(vptr_, other.vptr_);
    } else if (vptr_) {
      vptr_->relocate(other.storage_, storage_);
      other.vptr_ = vptr_;
      vptr_ = nullptr;
    } else if (other.vptr_) {
      other.vptr_->relocate(storage_, other.storage_);
      vptr_ = other.vptr_;
      other.vptr_ = nullptr;
    }
  }
};

template <typename Signature, size_t Capacity>
void swap(inplace_function<Signature, Capacity> &lhs, inplace_function<Signature, Capacity> &rhs) noexcept {
  lhs.swap(rhs);
}

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
