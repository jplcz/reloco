// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file value_ref.hpp
 * @brief Safe non-null reference wrapper rejecting rvalues and temporaries. */

#include "lifetime.hpp"
#include "value_ptr.hpp"
#include <memory>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief Type-safe reference wrapper that rejects temporary objects.
 *
 * `value_ref<T>` preserves mutable access, while `value_ref<const T>` is
 * read-only. The referenced object must outlive the wrapper. Use
 * @ref value_ptr when the borrow may be null.
 */
template <typename T> class RELOCO_POINTER value_ref {
public:
  /**
   * @brief Constructs a reference wrapper from a persistent lvalue reference.
   * @param val Stable, non-temporary object.
   */
  template <typename U,
            typename =
                std::enable_if_t<std::is_convertible_v<U *, T *>>>
  constexpr explicit value_ref(
      U &val RELOCO_LIFETIMEBOUND
          RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_ptr(std::addressof(val)) {}

  /**
   * @brief Rejects rvalue and temporary object bindings.
   */
  template <typename U,
            std::enable_if_t<!std::is_lvalue_reference_v<U>, int> = 0>
  constexpr value_ref(U &&) = delete;

  [[nodiscard]] constexpr T *
  get() const noexcept RELOCO_LIFETIMEBOUND {
    return m_ptr.get();
  }

  [[nodiscard]] constexpr T &
  operator*() const noexcept RELOCO_LIFETIMEBOUND {
    // Bypass value_ptr's checked null-guard: a value_ref is only ever
    // constructed from a valid lvalue and never becomes null afterward, so
    // the check is provably redundant here and would be pure overhead. This
    // is exactly the verified use RELOCO_BEGIN/END_UNSAFE_BUFFER_USAGE
    // exists for.
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return m_ptr.unsafe_deref();
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  [[nodiscard]] constexpr T *
  operator->() const noexcept RELOCO_LIFETIMEBOUND {
    return m_ptr.get();
  }

  /**
   * @brief Returns the nullable pointer representation of this borrow.
   *
   * Useful when a required typed borrow crosses a type-erased boundary that
   * stores nullable state.
   */
  [[nodiscard]] constexpr value_ptr<T> pointer() const noexcept {
    return m_ptr;
  }

private:
  value_ptr<T> m_ptr;
};

template <typename T> value_ref(T &) -> value_ref<T>;

} // namespace reloco
