// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file value_ptr.hpp
 * @brief Nullable non-owning pointer wrapper with lifetime annotations. */

#include "lifetime.hpp"
#include "detail/assert.hpp"
#include <cstddef>
#include <type_traits>

namespace reloco {

/**
 * @brief Nullable, non-owning pointer to a caller-owned value.
 *
 * `value_ptr` documents retained pointer relationships without taking
 * ownership. The pointed object must outlive the wrapper and every pointer or
 * reference obtained from it.
 */
template <typename T> class RELOCO_POINTER value_ptr {
public:
  constexpr value_ptr() noexcept = default;
  constexpr value_ptr(std::nullptr_t) noexcept {}

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  constexpr value_ptr(
      U *ptr RELOCO_LIFETIMEBOUND
          RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ptr_(ptr) {}

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  constexpr value_ptr(value_ptr<U> other) noexcept : ptr_(other.ptr_) {}

  [[nodiscard]] constexpr T *
  get() const noexcept RELOCO_LIFETIMEBOUND {
    return ptr_;
  }

  template <typename U = T,
            std::enable_if_t<!std::is_void_v<U>, int> = 0>
  [[nodiscard]] constexpr U &
  operator*() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(ptr_ != nullptr, "value_ptr: dereferencing a null pointer");
    return *ptr_;
  }

  [[nodiscard]] constexpr T *
  operator->() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(ptr_ != nullptr, "value_ptr: dereferencing a null pointer");
    return ptr_;
  }

  /**
   * @brief Dereferences without the always-on null check.
   *
   * Explicitly-unsafe tier: only a `RELOCO_DEBUG_ASSERT`, so it is compiled
   * out under `NDEBUG` (unless `RELOCO_DEBUG` is also defined). Use only
   * once the caller has already established non-null via `operator bool()`
   * or `get()` and dereferences repeatedly on a hot path where the checked
   * `operator*`/`operator->` overhead is unacceptable.
   *
   * Marked `RELOCO_UNSAFE_BUFFER_USAGE`: under Clang's
   * `-Wunsafe-buffer-usage`, every call site must be wrapped in
   * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE`,
   * making the opt-out to the unsafe tier explicit and greppable at each use.
   */
  template <typename U = T,
            std::enable_if_t<!std::is_void_v<U>, int> = 0>
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr U &
  unsafe_deref() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(ptr_ != nullptr, "value_ptr: dereferencing a null pointer");
    return *ptr_;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return ptr_ != nullptr;
  }

private:
  template <typename> friend class value_ptr;

  T *ptr_{nullptr};
};

template <typename T> value_ptr(T *) -> value_ptr<T>;

} // namespace reloco
