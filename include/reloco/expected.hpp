#pragma once
#include <memory>
#include <reloco/assert.hpp>

namespace reloco {

template <typename E> class unexpected {
  E m_error;

public:
  constexpr explicit unexpected(E e) : m_error(std::move(e)) {}
  constexpr E &value() & { return m_error; }
  constexpr const E &value() const & { return m_error; }
  constexpr E &&value() && { return std::move(m_error); }
};

template <typename E> unexpected(E) -> unexpected<E>;

template <typename T, typename E> class [[nodiscard]] expected {
  static_assert(std::is_nothrow_move_constructible_v<T>,
                "T must be nothrow move constructible");
  static_assert(std::is_nothrow_move_constructible_v<E>,
                "E must be nothrow move constructible");

  union {
    T m_value;
    E m_error;
  };
  bool m_has_value;

public:
  using value_type = T;
  using error_type = E;

  template <typename U = T>
    requires std::is_nothrow_default_constructible_v<U>
  constexpr expected() : m_has_value(true) {
    new (&m_value) T();
  }

  constexpr expected(T &&val) : m_value(std::move(val)), m_has_value(true) {}

  template <typename U = T>
    requires std::is_nothrow_copy_constructible_v<U>
  constexpr expected(const U &val) : m_value(val), m_has_value(true) {}

  template <typename U>
    requires std::is_constructible_v<T, U &&>
  constexpr expected(U &&val) : m_has_value(true) {
    new (&m_value) T(std::forward<U>(val));
  }

  template <typename U, typename G>
    requires std::is_nothrow_constructible_v<T, U &&> &&
             std::is_nothrow_constructible_v<E, G &&>
  constexpr expected(expected<U, G> &&other) : m_has_value(other.has_value()) {
    if (m_has_value) {
      new (&m_value) T(std::move(other.value()));
    } else {
      new (&m_error) E(std::move(other.error()));
    }
  }

  template <typename G>
    requires std::is_constructible_v<E, G &&>
  constexpr expected(unexpected<G> &&err) : m_has_value(false) {
    new (&m_error) E(std::move(err.value()));
  }

  constexpr expected(unexpected<E> &&err)
      : m_error(std::move(err.value())), m_has_value(false) {}
  constexpr expected(const unexpected<E> &err)
      : m_error(err.value()), m_has_value(false) {}

  ~expected() {
    if (m_has_value)
      m_value.~T();
    else
      m_error.~E();
  }

  constexpr bool has_value() const noexcept { return m_has_value; }
  constexpr explicit operator bool() const noexcept { return m_has_value; }

  constexpr T &value() & {
    RELOCO_ASSERT(m_has_value && "Result does not contain a value");
    return m_value;
  }

  constexpr T &&value() && {
    RELOCO_ASSERT(m_has_value && "Result does not contain a value");
    return std::move(m_value);
  }

  constexpr const T &value() const & {
    RELOCO_ASSERT(m_has_value && "Result does not contain a value");
    return m_value;
  }

  constexpr const T &&value() const && {
    RELOCO_ASSERT(m_has_value && "Result does not contain a value");
    return std::move(m_value);
  }

  constexpr E &error() & {
    RELOCO_ASSERT(!m_has_value && "Result does not contain an error");
    return m_error;
  }

  constexpr const E &error() const & {
    RELOCO_ASSERT(!m_has_value && "Result does not contain an error");
    return m_error;
  }

  constexpr E &&error() && {
    RELOCO_ASSERT(!m_has_value && "Result does not contain an error");
    return std::move(m_error);
  }

  constexpr const E &&error() const && {
    RELOCO_ASSERT(!m_has_value && "Result does not contain an error");
    return std::move(m_error);
  }

  template <typename F> auto transform(F &&f) const {
    using NewValue = decltype(f(value()));
    if (m_has_value)
      return expected<NewValue, E>(f(value()));
    return expected<NewValue, E>(unexpected(m_error));
  }

  template <typename F> auto and_then(F &&f) const {
    if (m_has_value)
      return f(value());
    return decltype(f(value()))(unexpected(m_error));
  }

  constexpr T value_or(T &&fallback) const {
    return m_has_value ? m_value : std::move(fallback);
  }

  constexpr T *operator->() noexcept { return &value(); }
  constexpr const T *operator->() const noexcept { return &value(); }

  constexpr T &operator*() & noexcept { return value(); }
  constexpr const T &operator*() const & noexcept { return value(); }
  constexpr T &&operator*() && noexcept { return std::move(value()); }

  constexpr bool operator==(const expected &other) const {
    if (m_has_value != other.m_has_value)
      return false;
    if (m_has_value)
      return m_value == other.m_value;
    return m_error == other.m_error;
  }

  constexpr bool operator!=(const expected &other) const {
    return !(*this == other);
  }
};

template <typename E> class [[nodiscard]] expected<void, E> {
  union {
    E m_error;
  };
  bool m_has_value;

public:
  constexpr expected() : m_has_value(true) {}
  constexpr expected(unexpected<E> &&err)
      : m_error(std::move(err.value())), m_has_value(false) {}

  constexpr bool has_value() const noexcept { return m_has_value; }
  constexpr explicit operator bool() const noexcept { return m_has_value; }

  void value() const {
    RELOCO_ASSERT(m_has_value && "Result contains an error");
  }

  constexpr E &error() & {
    RELOCO_ASSERT(!m_has_value && "Result does not contain an error");
    return m_error;
  }

  constexpr const E &error() const & {
    RELOCO_ASSERT(!m_has_value && "Result does not contain an error");
    return m_error;
  }

  constexpr E &&error() && {
    RELOCO_ASSERT(!m_has_value && "Result does not contain an error");
    return std::move(m_error);
  }

  constexpr const E &&error() const && {
    RELOCO_ASSERT(!m_has_value && "Result does not contain an error");
    return std::move(m_error);
  }

  constexpr bool operator==(const expected &other) const {
    if (m_has_value != other.m_has_value)
      return false;
    if (m_has_value)
      return true;
    return m_error == other.m_error;
  }

  constexpr bool operator!=(const expected &other) const {
    return !(*this == other);
  }
};

template <typename E> expected(unexpected<E>) -> expected<void, E>;

} // namespace reloco
