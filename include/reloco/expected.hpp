// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/assert.hpp"
#include "lifetime.hpp"
#include "rvalue_safety.hpp"
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace reloco {

template <typename E> class unexpected {
  E m_error;

public:
  constexpr explicit unexpected(E e) noexcept : m_error(std::move(e)) {}
  constexpr E &value() & noexcept RELOCO_LIFETIMEBOUND { return m_error; }
  constexpr const E &value() const & noexcept RELOCO_LIFETIMEBOUND { return m_error; }
  constexpr E &&value() && noexcept RELOCO_LIFETIMEBOUND { return std::move(m_error); }
};

template <typename E> unexpected(E) -> unexpected<E>;

struct RELOCO_EXPORT expected_tag_t {};

template <typename T, typename E> class [[nodiscard]] expected : expected_tag_t {
  static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow move constructible");
  static_assert(std::is_nothrow_move_constructible_v<E>, "E must be nothrow move constructible");

  union {
    T m_value;
    E m_error;
  };
  bool m_has_value;

public:
  using value_type = T;
  using error_type = E;

  template <typename U = T, std::enable_if_t<std::is_nothrow_default_constructible_v<U>, int> = 0>
  constexpr expected() noexcept : m_has_value(true) {
    new (&m_value) T();
  }

  constexpr expected(T &&val) noexcept : m_value(std::move(val)), m_has_value(true) {}

  template <typename U, std::enable_if_t<std::is_constructible_v<T, U &&>, int> = 0>
  constexpr expected(U &&val) noexcept : m_has_value(true) {
    new (&m_value) T(std::forward<U>(val));
  }

  template <
      typename U, typename G,
      std::enable_if_t<std::is_nothrow_constructible_v<T, U &&> && std::is_nothrow_constructible_v<E, G &&>, int> = 0>
  constexpr expected(expected<U, G> &&other) noexcept : m_has_value(other.has_value()) {
    if (m_has_value) {
      new (&m_value) T(std::move(other.value()));
    } else {
      new (&m_error) E(std::move(other.error()));
    }
  }

  template <typename G, std::enable_if_t<std::is_constructible_v<E, G &&>, int> = 0>
  constexpr expected(unexpected<G> &&err) noexcept : m_has_value(false) {
    new (&m_error) E(std::move(err.value()));
  }

  constexpr expected(unexpected<E> &&err) noexcept : m_error(std::move(err.value())), m_has_value(false) {}
  constexpr expected(const unexpected<E> &err) noexcept : m_error(err.value()), m_has_value(false) {}

  ~expected() noexcept {
    if (m_has_value)
      m_value.~T();
    else
      m_error.~E();
  }

  constexpr bool has_value() const noexcept { return m_has_value; }
  constexpr explicit operator bool() const noexcept { return m_has_value; }

  constexpr T &value() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(m_has_value, "Result does not contain a value");
    return m_value;
  }

  constexpr T &&value() && noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(m_has_value, "Result does not contain a value");
    return std::move(m_value);
  }

  constexpr const T &value() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(m_has_value, "Result does not contain a value");
    return m_value;
  }

  constexpr const T &&value() const && noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(m_has_value, "Result does not contain a value");
    return std::move(m_value);
  }

  constexpr E &error() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!m_has_value, "Result does not contain an error");
    return m_error;
  }

  constexpr const E &error() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!m_has_value, "Result does not contain an error");
    return m_error;
  }

  constexpr E &&error() && noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!m_has_value, "Result does not contain an error");
    return std::move(m_error);
  }

  constexpr const E &&error() const && noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!m_has_value, "Result does not contain an error");
    return std::move(m_error);
  }

  template <typename F> auto transform(F &&f) const noexcept {
    using NewValue = decltype(f(value()));
    if (m_has_value)
      return expected<NewValue, E>(f(value()));
    return expected<NewValue, E>(unexpected(m_error));
  }

  template <typename F> auto and_then(F &&f) const noexcept {
    if (m_has_value)
      return f(value());
    return decltype(f(value()))(unexpected(m_error));
  }

  /**
   * @brief Rust `Result::map` alias: identical to `transform`, applying
   * @p f to the contained value and leaving the error untouched.
   */
  template <typename F> auto map(F &&f) const noexcept { return transform(std::forward<F>(f)); }

  /**
   * @brief Rust `Result::map_err` equivalent: applies @p f to the
   * contained error, leaving a present value untouched.
   */
  template <typename F> auto map_err(F &&f) const noexcept {
    using NewError = decltype(f(error()));
    if (m_has_value)
      return expected<T, NewError>(m_value);
    return expected<T, NewError>(unexpected(f(error())));
  }

  /**
   * @brief Rust `Result::or_else` equivalent: if this holds a value,
   * returns it unchanged (wrapped in @p f's `expected` return type);
   * otherwise invokes @p f with the error and returns its result.
   */
  template <typename F> auto or_else(F &&f) const noexcept {
    using Ret = decltype(f(error()));
    if (m_has_value)
      return Ret(m_value);
    return f(error());
  }

  /**
   * @brief Rust `Result::unwrap_or_else` equivalent: returns the value if
   * present, otherwise invokes @p f with the error and returns its result.
   */
  template <typename F> T unwrap_or_else(F &&f) const noexcept {
    if (m_has_value)
      return m_value;
    return f(error());
  }

  constexpr T value_or(T &&fallback) const noexcept { return m_has_value ? m_value : std::move(fallback); }

  constexpr T *operator->() & noexcept RELOCO_LIFETIMEBOUND { return &value(); }
  constexpr const T *operator->() const & noexcept RELOCO_LIFETIMEBOUND { return &value(); }

  constexpr T &operator*() & noexcept RELOCO_LIFETIMEBOUND { return value(); }
  constexpr const T &operator*() const & noexcept RELOCO_LIFETIMEBOUND { return value(); }

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  constexpr bool operator==(const expected &other) const noexcept {
    if (m_has_value != other.m_has_value)
      return false;
    if (m_has_value)
      return m_value == other.m_value;
    return m_error == other.m_error;
  }

  constexpr bool operator!=(const expected &other) const noexcept { return !(*this == other); }
};

template <typename E> class [[nodiscard]] expected<void, E> {
  static_assert(std::is_nothrow_move_constructible_v<E>, "E must be nothrow move constructible");

  union {
    E m_error;
  };
  bool m_has_value;

public:
  using value_type = void;
  using error_type = E;

  constexpr expected() noexcept : m_has_value(true) {}
  constexpr expected(unexpected<E> &&err) noexcept : m_error(std::move(err.value())), m_has_value(false) {}

  ~expected() noexcept {
    if (!m_has_value)
      m_error.~E();
  }

  constexpr bool has_value() const noexcept { return m_has_value; }
  constexpr explicit operator bool() const noexcept { return m_has_value; }

  void value() const noexcept { RELOCO_ASSERT(m_has_value, "Result contains an error"); }

  constexpr E &error() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!m_has_value, "Result does not contain an error");
    return m_error;
  }

  constexpr const E &error() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!m_has_value, "Result does not contain an error");
    return m_error;
  }

  constexpr E &&error() && noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!m_has_value, "Result does not contain an error");
    return std::move(m_error);
  }

  constexpr const E &&error() const && noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!m_has_value, "Result does not contain an error");
    return std::move(m_error);
  }

  /**
   * @brief Rust `Result::and_then` equivalent: if this holds a value,
   * invokes @p f (which takes no arguments) and returns its `expected`
   * result; otherwise propagates the current error unchanged.
   */
  template <typename F> auto and_then(F &&f) const noexcept {
    if (m_has_value)
      return f();
    return decltype(f())(unexpected(m_error));
  }

  /**
   * @brief Rust `Result::map_err` equivalent: applies @p f to the
   * contained error, leaving success untouched.
   */
  template <typename F> auto map_err(F &&f) const noexcept {
    using NewError = decltype(f(error()));
    if (m_has_value)
      return expected<void, NewError>();
    return expected<void, NewError>(unexpected(f(error())));
  }

  /**
   * @brief Rust `Result::or_else` equivalent: if this holds a value,
   * returns success (wrapped in @p f's `expected` return type); otherwise
   * invokes @p f with the error and returns its result.
   */
  template <typename F> auto or_else(F &&f) const noexcept {
    using Ret = decltype(f(error()));
    if (m_has_value)
      return Ret();
    return f(error());
  }

  constexpr bool operator==(const expected &other) const noexcept {
    if (m_has_value != other.m_has_value)
      return false;
    if (m_has_value)
      return true;
    return m_error == other.m_error;
  }

  constexpr bool operator!=(const expected &other) const noexcept { return !(*this == other); }
};

template <typename E> expected(unexpected<E>) -> expected<void, E>;

} // namespace reloco
