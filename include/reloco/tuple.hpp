// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file tuple.hpp
 * @brief `tuple<Ts...>`: a `std::tuple` with Rust-like combinators (`map`, `zip`, `append`, `concat`, ...) and
 * reloco-style hardened access. See `docs/tuple.md`.
 *
 * `reloco::tuple` *derives* from `std::tuple`, so it converts to it implicitly, inherits every constructor,
 * comparison, `swap` and `std::get`, and works with structured bindings and `std::apply`.
 */

#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace reloco {

template <typename... Ts> class tuple;

namespace detail {

template <typename T> struct is_reloco_tuple : std::false_type {};
template <typename... Ts> struct is_reloco_tuple<tuple<Ts...>> : std::true_type {};

// Result type of `visit_at`: every element must yield the same type, otherwise a runtime index could not name it.
template <typename F, typename... Es> struct visit_result {
  static_assert(sizeof...(Es) > 0, "visit_at needs a non-empty tuple");
  using type = std::invoke_result_t<F &, std::tuple_element_t<0, std::tuple<Es...>>>;
  static_assert((std::is_same_v<type, std::invoke_result_t<F &, Es>> && ...),
                "visit_at: the callable must return the same type for every element");
};

} // namespace detail

/**
 * @brief `std::tuple` plus combinators.
 *
 * Every combinator is available on lvalues (elements are passed as `T &`), const lvalues (`const T &`) and rvalues
 * (elements are moved out). Accessors that return references are deleted on `const &&`, and `get` on an rvalue returns
 * the element by value, so a reference into a temporary tuple can never dangle.
 */
template <typename... Ts> class RELOCO_OWNER tuple : public std::tuple<Ts...> {
  using base = std::tuple<Ts...>;

  template <std::size_t I> using element_t = std::tuple_element_t<I, base>;

public:
  using base::base;
  using base::operator=;

  constexpr tuple() = default;
  // Inherited constructors never include copy/move from the base itself, so adopt a plain `std::tuple` explicitly.
  constexpr explicit tuple(const base &other) noexcept(std::is_nothrow_copy_constructible_v<base>) : base(other) {}
  constexpr explicit tuple(base &&other) noexcept(std::is_nothrow_move_constructible_v<base>)
      : base(std::move(other)) {}

  [[nodiscard]] static constexpr std::size_t size() noexcept { return sizeof...(Ts); }
  [[nodiscard]] static constexpr bool empty() noexcept { return sizeof...(Ts) == 0; }

  // ---- element access ----

  template <std::size_t I> [[nodiscard]] constexpr element_t<I> &get() & noexcept RELOCO_LIFETIMEBOUND {
    return std::get<I>(static_cast<base &>(*this));
  }
  template <std::size_t I> [[nodiscard]] constexpr const element_t<I> &get() const & noexcept RELOCO_LIFETIMEBOUND {
    return std::get<I>(static_cast<const base &>(*this));
  }
  /** @brief Moves the element out; by value (not `T &&`) so a chained call never keeps a reference into a temporary. */
  template <std::size_t I>
  [[nodiscard]] constexpr element_t<I> get() && noexcept(std::is_nothrow_move_constructible_v<element_t<I>> ||
                                                         std::is_reference_v<element_t<I>>) {
    return static_cast<element_t<I> &&>(std::get<I>(static_cast<base &&>(*this)));
  }
  template <std::size_t I> auto get() const && = delete;

  /** @brief First element (Rust `.0`); the tuple must not be empty. */
  [[nodiscard]] constexpr decltype(auto) first() & noexcept RELOCO_LIFETIMEBOUND {
    static_assert(sizeof...(Ts) > 0, "first() needs a non-empty tuple");
    return get<0>();
  }
  [[nodiscard]] constexpr decltype(auto) first() const & noexcept RELOCO_LIFETIMEBOUND {
    static_assert(sizeof...(Ts) > 0, "first() needs a non-empty tuple");
    return get<0>();
  }
  [[nodiscard]] constexpr decltype(auto) first() && noexcept(noexcept(std::declval<tuple &&>().template get<0>())) {
    static_assert(sizeof...(Ts) > 0, "first() needs a non-empty tuple");
    return std::move(*this).template get<0>();
  }
  auto first() const && = delete;

  /** @brief Last element; the tuple must not be empty. */
  [[nodiscard]] constexpr decltype(auto) last() & noexcept RELOCO_LIFETIMEBOUND {
    static_assert(sizeof...(Ts) > 0, "last() needs a non-empty tuple");
    return get<sizeof...(Ts) - 1>();
  }
  [[nodiscard]] constexpr decltype(auto) last() const & noexcept RELOCO_LIFETIMEBOUND {
    static_assert(sizeof...(Ts) > 0, "last() needs a non-empty tuple");
    return get<sizeof...(Ts) - 1>();
  }
  [[nodiscard]] constexpr decltype(auto)
  last() && noexcept(noexcept(std::declval<tuple &&>().template get<(sizeof...(Ts) > 0 ? sizeof...(Ts) - 1 : 0)>())) {
    static_assert(sizeof...(Ts) > 0, "last() needs a non-empty tuple");
    return std::move(*this).template get<sizeof...(Ts) - 1>();
  }
  auto last() const && = delete;

  // ---- std interop ----

  [[nodiscard]] constexpr base &as_std() & noexcept RELOCO_LIFETIMEBOUND { return *this; }
  [[nodiscard]] constexpr const base &as_std() const & noexcept RELOCO_LIFETIMEBOUND { return *this; }
  auto as_std() const && = delete;
  /** @brief Moves the elements into a plain `std::tuple`. */
  [[nodiscard]] constexpr base into_std() && noexcept(std::is_nothrow_move_constructible_v<base>) {
    return static_cast<base &&>(*this);
  }

  // ---- calling ----

  /** @brief Calls `fn(elements...)` (like `std::apply`); elements are forwarded according to the tuple's category. */
  template <typename F> constexpr decltype(auto) apply(F &&fn) & noexcept {
    return std::apply(std::forward<F>(fn), static_cast<base &>(*this));
  }
  template <typename F> constexpr decltype(auto) apply(F &&fn) const & noexcept {
    return std::apply(std::forward<F>(fn), static_cast<const base &>(*this));
  }
  template <typename F> constexpr decltype(auto) apply(F &&fn) && noexcept {
    return std::apply(std::forward<F>(fn), static_cast<base &&>(*this));
  }
  template <typename F> auto apply(F &&) const && = delete;

  /** @brief Calls `fn(element)` for every element, in order. */
  template <typename F> constexpr void for_each(F &&fn) & noexcept {
    for_each_impl(*this, fn, std::index_sequence_for<Ts...>{});
  }
  template <typename F> constexpr void for_each(F &&fn) const & noexcept {
    for_each_impl(*this, fn, std::index_sequence_for<Ts...>{});
  }
  template <typename F> constexpr void for_each(F &&fn) && noexcept {
    for_each_impl(std::move(*this), fn, std::index_sequence_for<Ts...>{});
  }
  template <typename F> void for_each(F &&) const && = delete;

  /**
   * @brief Calls `fn` on the element at runtime index @p index and returns its result, or `out_of_bounds` when
   * @p index >= `size()`. `fn` must return the same type for every element. Elements are passed as `T &`, `const T &`
   * or moved, according to the tuple's category.
   */
  template <typename F>
  [[nodiscard]] constexpr auto visit_at(std::size_t index, F &&fn) & noexcept
      -> result<typename detail::visit_result<F, Ts &...>::type> {
    return visit_impl<typename detail::visit_result<F, Ts &...>::type, 0>(*this, index, fn);
  }
  template <typename F>
  [[nodiscard]] constexpr auto visit_at(std::size_t index, F &&fn) const & noexcept
      -> result<typename detail::visit_result<F, const Ts &...>::type> {
    return visit_impl<typename detail::visit_result<F, const Ts &...>::type, 0>(*this, index, fn);
  }
  template <typename F>
  [[nodiscard]] constexpr auto visit_at(std::size_t index, F &&fn) && noexcept
      -> result<typename detail::visit_result<F, Ts &&...>::type> {
    return visit_impl<typename detail::visit_result<F, Ts &&...>::type, 0>(std::move(*this), index, fn);
  }
  template <typename F> auto visit_at(std::size_t, F &&) const && = delete;

  // ---- transforming ----

  /**
   * @brief Applies `fn` to every element and collects the results in a new tuple (`fn` must not return `void`).
   * Elements are passed as `T &`, `const T &` or moved, according to the tuple's category.
   */
  template <typename F> [[nodiscard]] constexpr auto map(F &&fn) & noexcept {
    return map_impl(*this, fn, std::index_sequence_for<Ts...>{});
  }
  template <typename F> [[nodiscard]] constexpr auto map(F &&fn) const & noexcept {
    return map_impl(*this, fn, std::index_sequence_for<Ts...>{});
  }
  template <typename F> [[nodiscard]] constexpr auto map(F &&fn) && noexcept {
    return map_impl(std::move(*this), fn, std::index_sequence_for<Ts...>{});
  }
  template <typename F> auto map(F &&) const && = delete;

  /** @brief Pairs elements by position: `tuple<A, B>.zip(tuple<X, Y>)` is `tuple<tuple<A, X>, tuple<B, Y>>`. */
  template <typename... Us> [[nodiscard]] constexpr auto zip(const tuple<Us...> &other) const & noexcept {
    static_assert(sizeof...(Us) == sizeof...(Ts), "zip needs tuples of the same size");
    return zip_impl(*this, other, std::index_sequence_for<Ts...>{});
  }
  template <typename... Us> [[nodiscard]] constexpr auto zip(tuple<Us...> &&other) const & noexcept {
    static_assert(sizeof...(Us) == sizeof...(Ts), "zip needs tuples of the same size");
    return zip_impl(*this, std::move(other), std::index_sequence_for<Ts...>{});
  }
  template <typename... Us> [[nodiscard]] constexpr auto zip(const tuple<Us...> &other) && noexcept {
    static_assert(sizeof...(Us) == sizeof...(Ts), "zip needs tuples of the same size");
    return zip_impl(std::move(*this), other, std::index_sequence_for<Ts...>{});
  }
  template <typename... Us> [[nodiscard]] constexpr auto zip(tuple<Us...> &&other) && noexcept {
    static_assert(sizeof...(Us) == sizeof...(Ts), "zip needs tuples of the same size");
    return zip_impl(std::move(*this), std::move(other), std::index_sequence_for<Ts...>{});
  }
  template <typename... Us> auto zip(const tuple<Us...> &) const && = delete;
  template <typename... Us> auto zip(tuple<Us...> &&) const && = delete;

  /** @brief Returns a copy (or, on an rvalue, the moved elements) followed by @p value. */
  template <typename U> [[nodiscard]] constexpr auto append(U &&value) const & noexcept {
    return concat(tuple<std::decay_t<U>>(std::forward<U>(value)));
  }
  template <typename U> [[nodiscard]] constexpr auto append(U &&value) && noexcept {
    return std::move(*this).concat(tuple<std::decay_t<U>>(std::forward<U>(value)));
  }
  template <typename U> auto append(U &&) const && = delete;

  /** @brief Returns @p value followed by the elements. */
  template <typename U> [[nodiscard]] constexpr auto prepend(U &&value) const & noexcept {
    return tuple<std::decay_t<U>>(std::forward<U>(value)).concat(*this);
  }
  template <typename U> [[nodiscard]] constexpr auto prepend(U &&value) && noexcept {
    return tuple<std::decay_t<U>>(std::forward<U>(value)).concat(std::move(*this));
  }
  template <typename U> auto prepend(U &&) const && = delete;

  /** @brief Returns the elements of this tuple followed by those of @p other (Rust-style chaining). */
  template <typename... Us> [[nodiscard]] constexpr auto concat(const tuple<Us...> &other) const & noexcept {
    return concat_impl(*this, other, std::index_sequence_for<Ts...>{}, std::index_sequence_for<Us...>{});
  }
  template <typename... Us> [[nodiscard]] constexpr auto concat(tuple<Us...> &&other) const & noexcept {
    return concat_impl(*this, std::move(other), std::index_sequence_for<Ts...>{}, std::index_sequence_for<Us...>{});
  }
  template <typename... Us> [[nodiscard]] constexpr auto concat(const tuple<Us...> &other) && noexcept {
    return concat_impl(std::move(*this), other, std::index_sequence_for<Ts...>{}, std::index_sequence_for<Us...>{});
  }
  template <typename... Us> [[nodiscard]] constexpr auto concat(tuple<Us...> &&other) && noexcept {
    return concat_impl(std::move(*this), std::move(other), std::index_sequence_for<Ts...>{},
                       std::index_sequence_for<Us...>{});
  }
  template <typename... Us> auto concat(const tuple<Us...> &) const && = delete;
  template <typename... Us> auto concat(tuple<Us...> &&) const && = delete;

private:
  // `Self` is `tuple &`, `const tuple &` or `tuple` (rvalue); `std::get` on the forwarded tuple keeps the category.
  template <typename Self, typename F, std::size_t... I>
  static constexpr void for_each_impl(Self &&self, F &fn, std::index_sequence<I...>) noexcept {
    (static_cast<void>(fn(std::get<I>(std::forward<Self>(self)))), ...);
  }

  template <typename Self, typename F, std::size_t... I>
  static constexpr auto map_impl(Self &&self, F &fn, std::index_sequence<I...>) noexcept {
    static_assert((!std::is_void_v<decltype(fn(std::get<I>(std::forward<Self>(self))))> && ...),
                  "map: the callable must not return void (use for_each)");
    return tuple<decltype(fn(std::get<I>(std::forward<Self>(self))))...>(fn(std::get<I>(std::forward<Self>(self)))...);
  }

  template <typename A, typename B, std::size_t... I>
  static constexpr auto zip_impl(A &&a, B &&b, std::index_sequence<I...>) noexcept {
    return tuple<tuple<std::tuple_element_t<I, std::remove_cv_t<std::remove_reference_t<A>>>,
                       std::tuple_element_t<I, std::remove_cv_t<std::remove_reference_t<B>>>>...>(
        tuple<std::tuple_element_t<I, std::remove_cv_t<std::remove_reference_t<A>>>,
              std::tuple_element_t<I, std::remove_cv_t<std::remove_reference_t<B>>>>(
            std::get<I>(std::forward<A>(a)), std::get<I>(std::forward<B>(b)))...);
  }

  template <typename A, typename B, std::size_t... I, std::size_t... J>
  static constexpr auto concat_impl(A &&a, B &&b, std::index_sequence<I...>, std::index_sequence<J...>) noexcept {
    using ta = std::remove_cv_t<std::remove_reference_t<A>>;
    using tb = std::remove_cv_t<std::remove_reference_t<B>>;
    return tuple<std::tuple_element_t<I, ta>..., std::tuple_element_t<J, tb>...>(std::get<I>(std::forward<A>(a))...,
                                                                                 std::get<J>(std::forward<B>(b))...);
  }

  template <typename R, std::size_t I, typename Self, typename F>
  static constexpr result<R> visit_impl(Self &&self, std::size_t index, F &fn) noexcept {
    if constexpr (I == sizeof...(Ts)) {
      static_cast<void>(self);
      static_cast<void>(index);
      static_cast<void>(fn);
      return unexpected(error::out_of_bounds);
    } else {
      if (index == I) {
        return result<R>(fn(std::get<I>(std::forward<Self>(self))));
      }
      return visit_impl<R, I + 1>(std::forward<Self>(self), index, fn);
    }
  }
};

// CTAD: inherited constructors do not take part in deduction.
template <typename... Ts> tuple(Ts...) -> tuple<Ts...>;
template <typename... Ts> tuple(std::tuple<Ts...>) -> tuple<Ts...>;
template <typename T1, typename T2> tuple(std::pair<T1, T2>) -> tuple<T1, T2>;

/** @brief Builds a `tuple` from values (decayed, like `std::make_tuple`). */
template <typename... Ts> [[nodiscard]] constexpr tuple<std::decay_t<Ts>...> make_tuple(Ts &&...values) noexcept {
  return tuple<std::decay_t<Ts>...>(std::forward<Ts>(values)...);
}

/** @brief Builds a `tuple` of lvalue references to the arguments. */
template <typename... Ts> [[nodiscard]] constexpr tuple<Ts &...> tie(Ts &...values) noexcept {
  return tuple<Ts &...>(values...);
}

/** @brief Builds a `tuple` of forwarding references to the arguments. */
template <typename... Ts> [[nodiscard]] constexpr tuple<Ts &&...> forward_as_tuple(Ts &&...values) noexcept {
  return tuple<Ts &&...>(std::forward<Ts>(values)...);
}

/** @brief A tuple is trivially relocatable iff every element is. */
template <typename... Ts>
struct is_trivially_relocatable<tuple<Ts...>> : std::bool_constant<(is_trivially_relocatable_v<Ts> && ...)> {};

} // namespace reloco

// Structured bindings (`auto [a, b] = t;`) look these up, then call the member `get<I>()`.
template <typename... Ts>
struct std::tuple_size<reloco::tuple<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)> {};
template <std::size_t I, typename... Ts>
struct std::tuple_element<I, reloco::tuple<Ts...>> : std::tuple_element<I, std::tuple<Ts...>> {};
