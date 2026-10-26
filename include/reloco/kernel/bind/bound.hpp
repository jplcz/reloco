// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/bind/bound.hpp
 * @brief `try_bind` / `with_bound`: prove once that several spans are long enough for a loop, and hand out
 * domains and views that share that proof.
 *
 * A binding is created by one fallible call (`try_bind` or `with_bound`) that checks, outside the hot loop, that
 *  - every span holds at least `n` elements, and
 *  - no mutable span overlaps another span (unless you opt in with `allow_alias`).
 * It returns a `bound` token. The token's domains yield `proven_index<Brand>` values `< n`, and its views accept only
 * those indexes, so the loop body needs no check.
 *
 * The `Brand` is a type unique to the binding site. `RELOCO_KERNEL_BIND` makes one per call site; `with_bound` uses the
 * type of the lambda you pass.
 *
 * See `docs/kernel/bind.md`.
 */

#include "../../array.hpp"
#include "../../error.hpp"
#include "../../fault_injection.hpp"
#include "../../lifetime.hpp"
#include "../../span.hpp"
#include "../config.hpp"
#include "../index/blocks.hpp"
#include "../index/range.hpp"
#include "../index/strided.hpp"
#include "../index/windows.hpp"
#include "../view/stride.hpp"
#include "bound_span.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace reloco::kernel {

/** @brief Fault points of the binding (see `docs/fault-injection.md`); each hook receives the verdict by reference. */
namespace fault {
/** @brief Hook receives "every span is long enough" (`bool &`); clear it to force `out_of_bounds`. */
RELOCO_FAULT_TAG(bind_extent);
/** @brief Hook receives "no forbidden overlap" (`bool &`); clear it to force `invalid_argument`. */
RELOCO_FAULT_TAG(bind_disjoint);
} // namespace fault

/** @brief Tag type of `allow_alias`. */
struct allow_alias_t {
  explicit constexpr allow_alias_t() noexcept = default;
};

/**
 * @brief Opt-in for in-place kernels (`out` is the same buffer as `in`).
 * @details Pass it as the first argument of `try_bind`. Two spans may then overlap only if they are *exactly* the same
 * range of the same element type, so element `i` of one is element `i` of the other. Partial overlap is still rejected.
 */
inline constexpr allow_alias_t allow_alias{};

namespace detail {

/** @brief One span reduced to integers, so the overlap test needs no pointer arithmetic. */
struct range_info {
  std::uintptr_t begin = 0;
  std::uintptr_t end = 0;
  std::uintptr_t type_key = 0;
  bool writable = false;
};

template <class T> inline constexpr char type_key_object = 0;

template <class T> [[nodiscard]] range_info unsafe_describe(span<T> s) noexcept {
  range_info info;
  info.begin = reinterpret_cast<std::uintptr_t>(s.data());
  info.end = info.begin + s.size_bytes();
  info.type_key = reinterpret_cast<std::uintptr_t>(&type_key_object<std::remove_const_t<T>>);
  info.writable = !std::is_const_v<T>;
  return info;
}

/** @brief `true` if the ranges respect the rules: only read-read overlap, or exact aliasing when `AllowAlias`. */
template <bool AllowAlias, std::size_t N>
[[nodiscard]] bool ranges_are_compatible(const reloco::array<range_info, N> &ranges) noexcept {
  for (std::size_t i = 0; i < N; ++i) {
    for (std::size_t j = i + 1; j < N; ++j) {
      const range_info &a = ranges[i];
      const range_info &b = ranges[j];
      if (a.begin >= a.end || b.begin >= b.end)
        continue; // an empty span overlaps nothing
      const bool overlap = a.begin < b.end && b.begin < a.end;
      if (!overlap || (!a.writable && !b.writable))
        continue;
      const bool identical = a.begin == b.begin && a.end == b.end && a.type_key == b.type_key;
      if (!(AllowAlias && identical))
        return false;
    }
  }
  return true;
}

/** @brief Storage of one bound span. */
template <std::size_t I, class T> class span_leaf {
public:
  constexpr explicit span_leaf(span<T> s) noexcept : storage_(s) {}
  [[nodiscard]] constexpr span<T> get() const noexcept { return storage_; }

private:
  span<T> storage_;
};

template <class Seq, class... Ts> class span_pack;

template <std::size_t... Is, class... Ts>
class span_pack<std::index_sequence<Is...>, Ts...> : public span_leaf<Is, Ts>... {
public:
  constexpr explicit span_pack(span<Ts>... spans) noexcept : span_leaf<Is, Ts>(spans)... {}
};

} // namespace detail

} // namespace reloco::kernel

RELOCO_KERNEL_BEGIN
template <class Brand, class... Ts> class bound;
RELOCO_KERNEL_END

namespace reloco::kernel::detail {

/** @brief Internal: the only place that creates tokens and branded views. Not public API. */
struct bind_access {
  template <class Brand, class T> [[nodiscard]] static constexpr bound_span<T, Brand> make_view(span<T> s) noexcept {
    return bound_span<T, Brand>(s);
  }

  template <class Brand, std::size_t B, class T>
  [[nodiscard]] static constexpr block_span<T, B, Brand> make_block_span(span<T> s) noexcept {
    return block_span<T, B, Brand>(s);
  }

  template <class Brand, class... Ts>
  [[nodiscard]] static constexpr bound<Brand, Ts...> make_bound(std::size_t n, span<Ts>... spans) noexcept {
    return bound<Brand, Ts...>(n, spans...);
  }
};

} // namespace reloco::kernel::detail

RELOCO_KERNEL_BEGIN

/**
 * @brief Proof that `Ts...` spans each hold at least `size()` elements and do not alias illegally.
 * @tparam Brand type unique to this binding; domains and views of the token share it.
 * @tparam Ts element types of the bound spans, in the order they were passed.
 * @details A pointer-like handle: it copies the spans, not the data, and is valid as long as the underlying storage.
 */
template <class Brand, class... Ts>
class RELOCO_POINTER bound : private detail::span_pack<std::index_sequence_for<Ts...>, Ts...> {
  static_assert(sizeof...(Ts) >= 1, "bind at least one span");
  using pack = detail::span_pack<std::index_sequence_for<Ts...>, Ts...>;

public:
  using brand = Brand;
  static constexpr std::size_t view_count = sizeof...(Ts);

  /** @brief The extent every domain of this binding ranges over. */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return extent_; }

  /** @brief The view of the I-th bound span (in the order passed to `try_bind`). */
  template <std::size_t I> [[nodiscard]] constexpr auto view() const noexcept {
    static_assert(I < view_count, "bound::view: no such span");
    using element = typename detail::type_at<I, Ts...>::type;
    return detail::bind_access::make_view<Brand>(static_cast<const detail::span_leaf<I, element> &>(*this).get());
  }

  /** @brief The proven indexes `0 .. size()-1`. */
  [[nodiscard]] constexpr index_range<Brand> domain() const noexcept {
    return index_range<Brand>(detail::index_access::key(), 0, extent_, extent_);
  }

  /** @brief Full blocks of `B` proven indexes; call `.tail()` on the result for the leftovers. */
  template <std::size_t B> [[nodiscard]] constexpr block_domain<B, Brand> blocks() const noexcept {
    return block_domain<B, Brand>(detail::index_access::key(), extent_);
  }

  /** @brief The leftover indexes of `blocks<B>()`. */
  template <std::size_t B> [[nodiscard]] constexpr index_range<Brand> tail() const noexcept {
    static_assert(B > 0, "tail: block width must be positive");
    return index_range<Brand>(detail::index_access::key(), extent_ - extent_ % B, extent_, extent_);
  }

  /**
   * @brief Binds a table with one entry per full block, indexed by the blocks of `blocks<B>()`.
   * @details Fails with `out_of_bounds` if the table has fewer than `size() / B` entries, and with `invalid_argument`
   * if it overlaps a bound span and either is writable (exact aliasing is not allowed here). Checked once.
   */
  template <std::size_t B, class U>
  [[nodiscard]] result<block_span<U, B, Brand>> try_blockwise(span<U> table) const noexcept {
    static_assert(B > 0, "try_blockwise: block width must be positive");
    bool long_enough = table.size() >= extent_ / B;
    RELOCO_FAULT_POINT_ARGS(fault::bind_extent, long_enough);
    if (!long_enough)
      return unexpected(error::out_of_bounds);
    bool compatible = describes_compatible(table, std::index_sequence_for<Ts...>{});
    RELOCO_FAULT_POINT_ARGS(fault::bind_disjoint, compatible);
    if (!compatible)
      return unexpected(error::invalid_argument);
    return detail::bind_access::make_block_span<Brand, B>(table);
  }

  /** @brief Adjacent pairs `(2i, 2i + 1)`. */
  [[nodiscard]] constexpr block_domain<2, Brand> pairs() const noexcept { return blocks<2>(); }

  /** @brief All windows of width `w` (none if `w` is 0 or larger than `size()`). */
  [[nodiscard]] constexpr window_domain<Brand> windows(std::size_t w) const noexcept {
    return window_domain<Brand>(detail::index_access::key(), extent_, w);
  }

  /** @brief Indexes `0, step, 2*step, ...` below `size()` (none if `step` is 0). */
  [[nodiscard]] constexpr strided_domain<Brand> strided(std::size_t step) const noexcept {
    return strided_domain<Brand>(detail::index_access::key(), extent_, step);
  }

private:
  friend struct detail::bind_access;

  template <class U, std::size_t... Is>
  [[nodiscard]] bool describes_compatible(span<U> table, std::index_sequence<Is...>) const noexcept {
    RELOCO_BEGIN_UNSAFE_CODE
    const reloco::array<detail::range_info, sizeof...(Ts) + 1> ranges{
        detail::unsafe_describe(this->template view<Is>().storage())..., detail::unsafe_describe(table)};
    RELOCO_END_UNSAFE_CODE
    return detail::ranges_are_compatible<false>(ranges);
  }

  // NOLINTNEXTLINE(reloco-explicit-conversion): private, always has at least two arguments (extent and one span)
  constexpr bound(std::size_t extent, span<Ts>... spans) noexcept : pack(spans...), extent_(extent) {}

  std::size_t extent_;
};

RELOCO_KERNEL_END

namespace reloco::kernel::detail {

template <class Brand, bool AllowAlias, class... Ts>
[[nodiscard]] result<bound<Brand, Ts...>> bind_impl(std::size_t n, span<Ts>... views) noexcept {
  bool long_enough = ((views.size() >= n) && ...);
  RELOCO_FAULT_POINT_ARGS(fault::bind_extent, long_enough);
  if (!long_enough)
    return unexpected(error::out_of_bounds);

  RELOCO_BEGIN_UNSAFE_CODE
  const reloco::array<range_info, sizeof...(Ts)> ranges{unsafe_describe(views)...};
  RELOCO_END_UNSAFE_CODE
  bool compatible = ranges_are_compatible<AllowAlias>(ranges);
  RELOCO_FAULT_POINT_ARGS(fault::bind_disjoint, compatible);
  if (!compatible)
    return unexpected(error::invalid_argument);

  return bind_access::make_bound<Brand>(n, views...);
}

} // namespace reloco::kernel::detail

RELOCO_KERNEL_BEGIN

/**
 * @brief Binds `views` to the extent `n`.
 * @tparam Brand type unique to the binding (use `RELOCO_KERNEL_BIND` to get one per call site).
 * @return the token; `out_of_bounds` if some span holds fewer than `n` elements; `invalid_argument` if a mutable span
 * overlaps another span.
 */
template <class Brand, class... Ts>
[[nodiscard]] result<bound<Brand, Ts...>> try_bind(std::size_t n, span<Ts>... views) noexcept {
  return detail::bind_impl<Brand, false>(n, views...);
}

/** @brief Like `try_bind`, but spans may alias when they are exactly the same range of the same type (in-place). */
template <class Brand, class... Ts>
[[nodiscard]] result<bound<Brand, Ts...>> try_bind(allow_alias_t, std::size_t n, span<Ts>... views) noexcept {
  return detail::bind_impl<Brand, true>(n, views...);
}

/**
 * @brief Binds `views`, runs `f(token)` and returns its result, so indexes cannot escape the lambda.
 * @param f callable taking the token by reference, normally a generic lambda `[&](auto &k) { ... }`. Its closure type
 * is the brand, so the proven indexes live and die inside it.
 * @return `result<R>` where `R` is what `f` returns (`result<void>` for `void`); binding errors are returned before
 * `f` runs.
 */
template <class F, class... Ts>
[[nodiscard]] auto
with_bound(std::size_t n, F &&f,
           span<Ts>... views) noexcept(std::is_nothrow_invocable_v<F &, bound<std::remove_cvref_t<F>, Ts...> &>) {
  using brand = std::remove_cvref_t<F>;
  using token = bound<brand, Ts...>;
  using returned = std::invoke_result_t<F &, token &>;
  using out = result<std::conditional_t<std::is_void_v<returned>, void, returned>>;

  auto bound_views = try_bind<brand>(n, views...);
  if (!bound_views)
    return out(unexpected(bound_views.error()));
  if constexpr (std::is_void_v<returned>) {
    f(*bound_views);
    return out();
  } else {
    return out(f(*bound_views));
  }
}

RELOCO_KERNEL_END

/**
 * @def RELOCO_KERNEL_BIND
 * @brief `try_bind` with a brand unique to this call site: `RELOCO_KERNEL_BIND(n, span_a, span_b)`.
 * @details Use `RELOCO_KERNEL_BIND_ALIASED` for the `allow_alias` form. A brand is per call site, so if the same site
 * runs twice with different extents, run with `RELOCO_KERNEL_CHECKS >= 1` in tests or prefer `with_bound`.
 */
#define RELOCO_KERNEL_BIND(n, ...) ::reloco::kernel::try_bind<decltype([] {})>((n), __VA_ARGS__)

/** @brief `RELOCO_KERNEL_BIND` with `allow_alias`. */
#define RELOCO_KERNEL_BIND_ALIASED(n, ...)                                                                             \
  ::reloco::kernel::try_bind<decltype([] {})>(::reloco::kernel::allow_alias, (n), __VA_ARGS__)
