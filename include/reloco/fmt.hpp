// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file fmt.hpp @brief Type-erased output sink and opt-in `Display`/`Debug`
 * customization points, matching Rust's `std::fmt::Display`/`std::fmt::Debug`.
 *
 * reloco itself never specializes `Display`/`Debug` for any of its own types,
 * and this header contains no formatting code of any kind: it only declares
 * the customization points and the SFINAE-detectable `sink` shape a
 * higher-level formatting library (e.g. microfmt) can bridge to, letting
 * reloco types opt into a textual representation without reloco depending on
 * (or knowing anything about) that library.
 */

#include "lifetime.hpp"
#include "string_view.hpp"

#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief Type-erased, zero-allocation output sink.
 *
 * Encapsulates a user-provided context pointer and a stateless write
 * callback function to stream characters without dynamic allocations,
 * virtual dispatch, or RTTI overhead. Deliberately mirrors microfmt's own
 * `sink` layout (same `ctx`/`write_fn` shape, over the same
 * `reloco::string_view`) so a `Display<T>`/`Debug<T>` specialization written
 * against this header can be driven directly from a caller-owned
 * `microfmt::sink` with no adapting/copying required.
 */
struct RELOCO_EXPORT RELOCO_POINTER sink {
  // --- Container / Back-Inserter Compatibility ---
  using value_type = char;

  // --- Iterator Traits (for direct usage) ---
  using iterator_category = std::output_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using pointer = void;
  using reference = void;

  /**
   * @brief Function pointer signature for the write callback.
   *
   * @param ctx Opaque user context pointer passed through from the sink.
   * @param sv  Non-owning view of the character slice to write.
   */
  using write_fn_t = void (*)(void *ctx, reloco::string_view sv) noexcept;

  /**
   * @brief Opaque pointer to caller-defined state/context.
   */
  void *ctx{nullptr};

  /**
   * @brief Callback function invoked when output is written.
   */
  write_fn_t write_fn{nullptr};

  /**
   * @brief Emits a sequence of characters to the underlying output sink.
   *
   * If @p sv is empty or @ref write_fn is @c nullptr, this operation is a
   * no-op.
   *
   * @param sv String view containing characters to write.
   */
  void write(reloco::string_view sv) const noexcept {
    if (write_fn && !sv.empty()) {
      write_fn(ctx, sv);
    }
  }

  /**
   * @brief Emits a single character to the underlying output sink.
   *
   * @param c Character to write.
   */
  void put(char c) const noexcept { write(reloco::string_view(&c, 1)); }

  /**
   * @brief Pushes a single character, enabling standard `std::back_inserter` compatibility.
   *
   * @param c Character to write.
   */
  void push_back(char c) const noexcept { put(c); }

  [[nodiscard]] constexpr sink &operator*() noexcept { return *this; }

  template <typename T> constexpr sink &operator=(T value) noexcept {
    put(static_cast<char>(value));
    return *this;
  }

  constexpr sink &operator++() noexcept { return *this; }
  constexpr sink operator++(int) noexcept { return *this; }
};

/**
 * @brief Opt-in customization point: "how does `T` render itself for
 * user-facing display", matching Rust's `std::fmt::Display`.
 *
 * Intentionally left undefined for any `T` that hasn't opted in, mirroring
 * `collection_view_traits<Container>` (see collection_view.hpp). A
 * specialization must supply:
 *
 * - `static void format(const T &value, const reloco::sink &out) noexcept;`
 *
 * reloco never specializes this for its own types; adapting a type -- and
 * writing the formatting code itself -- is left entirely to the consumer.
 *
 * @tparam T Concrete type to adapt.
 * @tparam Enable SFINAE hook for conditional specializations (e.g. via
 * `std::enable_if_t`); unused by `Display` itself.
 */
template <typename T, typename Enable = void> struct Display;

/**
 * @brief Opt-in customization point: "how does `T` render itself for
 * debugging/diagnostics", matching Rust's `std::fmt::Debug`.
 *
 * Same shape and rules as @ref Display: intentionally left undefined, never
 * specialized by reloco itself, and specializations must supply
 * `static void format(const T &value, const reloco::sink &out) noexcept;`.
 *
 * @tparam T Concrete type to adapt.
 * @tparam Enable SFINAE hook for conditional specializations; unused by
 * `Debug` itself.
 */
template <typename T, typename Enable = void> struct Debug;

namespace detail {

template <typename T, typename = void> struct has_display : std::false_type {};

template <typename T>
struct has_display<
    T, std::void_t<decltype(Display<T>::format(std::declval<const T &>(), std::declval<const sink &>()))>>
    : std::true_type {};

template <typename T, typename = void> struct has_debug : std::false_type {};

template <typename T>
struct has_debug<T,
                std::void_t<decltype(Debug<T>::format(std::declval<const T &>(), std::declval<const sink &>()))>>
    : std::true_type {};

} // namespace detail

/**
 * @brief `true` iff `Display<T>` has been specialized with a valid
 * `static void format(const T &, const sink &) noexcept`.
 */
template <typename T> inline constexpr bool has_display_v = detail::has_display<T>::value;

/**
 * @brief `true` iff `Debug<T>` has been specialized with a valid
 * `static void format(const T &, const sink &) noexcept`.
 */
template <typename T> inline constexpr bool has_debug_v = detail::has_debug<T>::value;

} // namespace reloco
