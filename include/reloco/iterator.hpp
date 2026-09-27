// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file iterator.hpp
 * @brief Lazy, zero-allocation iterator adaptors mirroring Rust's
 * `std::iter::Iterator` adapter chain (`Fuse`, `Zip`, `Map`, `Filter`,
 * `Enumerate`, `Take`, `Skip`, `Chain`), plus `std::iter` *source*
 * (generating) iterators (`from_fn`, `once`, `repeat`, `successors`,
 * `empty`), layered on top of ordinary C++
 * `begin()`/`end()` range-for support.
 *
 * Every adaptor in this file is, at the same time:
 *
 * - **A Rust-style pull iterator**: `next()` returns `optional<Item>`
 *   (empty once exhausted), matching Rust's `Iterator::next(&mut self)
 *   -> Option<Self::Item>` exactly -- repeatedly calling `next()` after
 *   exhaustion is well-defined (keeps returning an empty `optional`), the
 *   same guarantee Rust's `FusedIterator` documents (every adaptor here
 *   already behaves as if fused; `.fuse()` exists mainly so a *foreign*
 *   iterator that does not already guarantee this can be wrapped safely).
 * - **A C++ range**: `begin()`/`end()` return a single-pass
 *   `std::input_iterator_tag` cursor plus a distinct sentinel type (this
 *   has been legal for a C++ range-for loop since C++11 -- the loop only
 *   ever compares `__begin != __end`, it never requires the two to share
 *   a type), so every adaptor chain is directly usable in a range-for
 *   loop: `for (auto &x : reloco::iter(v).filter(pred)) ...`.
 *
 * **Item shape.** An adaptor that only *borrows* from an underlying range
 * (the root `reloco::iter(range)`, `filter`, `take`, `skip`, `fuse`,
 * `chain`) has `item_type = std::reference_wrapper<T>` (or `const T`) --
 * reloco's established "nullable reference" idiom for anything that must
 * fit inside an `optional<...>` (see `variant.hpp`'s `as<T>()`, which uses
 * the exact same shape for the same reason: `optional<T&>` is not a thing
 * reloco's `optional<T>` supports, since it manages storage by value).
 * The range-for cursor transparently unwraps this back to `T &` before
 * handing it to the loop body, so callers only ever see `reference_wrapper`
 * if they call `next()` directly. An adaptor that *produces new values*
 * (`map`, `enumerate`, `zip`) has a plain, owned `item_type` instead (the
 * callable's return type, or a `std::pair` of the upstream item types).
 *
 * **Consuming, single-pass, lvalue-only.** Every adaptor factory method
 * (`map`/`filter`/`fuse`/`zip`/`enumerate`/`take`/`skip`/`chain`) moves
 * `*this` into the returned adaptor, matching Rust's `self`-by-value
 * adapter methods -- the original binding is left moved-from and should
 * not be reused, exactly like a consumed Rust iterator. `next()` and
 * `begin()`/`end()` are all lvalue-`&`-qualified only, with an explicit
 * `= delete`d rvalue overload: an adaptor's C++ cursor holds a pointer
 * back into the adaptor object itself (to call `next()` through it on
 * every `operator++`), so handing back a live cursor/iterator from a
 * temporary adaptor (`reloco::iter(v).map(f).begin()`) would immediately
 * dangle once that temporary is destroyed at the end of the full
 * expression -- the same hazard, and the same fix (delete the rvalue
 * overload), `rvalue_safety.hpp`'s `RELOCO_BLOCK_RVALUE_ACCESS` applies to
 * every reloco container's own `begin()`/`end()`. This does *not* get in
 * the way of the common case, `for (auto &x : reloco::iter(v).map(f))`:
 * the range-for loop binds the range-expression to a hidden `auto &&`
 * local first (lifetime-extending the temporary adaptor chain for the
 * loop's duration), so `begin()`/`end()` are always called through that
 * lvalue, never directly on the rvalue expression.
 *
 * **No allocation, no exceptions, no RTTI**: every adaptor stores its
 * upstream adaptor(s)/iterators and callable(s) inline by value; nothing
 * here allocates, so this header is safe to use in kernel/bare-metal code
 * exactly like the rest of reloco.
 *
 * **Writing your own generating (source) iterator.** `iterator_adaptor`
 * is a CRTP base, not a closed set of adaptors: any class publicly
 * derived from `iterator_adaptor<Derived, Item>` that implements
 * `optional<Item> next_impl() noexcept` -- called once per `next()`/
 * cursor-increment, never again once it has returned empty -- is a full
 * citizen of the adaptor chain (`.map()`, `.take()`, range-for, etc. all
 * work on it for free), infinite generators included (see `repeat()`
 * below for why an infinite one must always be paired with `.take(n)` or
 * another early-stopping adaptor). `from_fn()`/`once()`/`repeat()`/
 * `successors()`/`empty()` are exactly such classes, provided so most
 * generating iterators never need a hand-written one at all.
 */

#include "error.hpp"
#include "lifetime.hpp"
#include "optional.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

namespace detail {

template <typename T> struct is_reference_wrapper : std::false_type {};
template <typename T> struct is_reference_wrapper<std::reference_wrapper<T>> : std::true_type {};
template <typename T> inline constexpr bool is_reference_wrapper_v = is_reference_wrapper<T>::value;

template <typename T, bool IsWrapper = is_reference_wrapper_v<T>> struct peek_underlying {
  using type = T;
};

template <typename T> struct peek_underlying<T, true> {
  using type = typename T::type;
};

template <typename Item>
using unwrapped_raw_t = std::remove_cv_t<
    std::remove_reference_t<typename detail::peek_underlying<std::remove_cv_t<std::remove_reference_t<Item>>>::type>>;

template <typename T, typename = void> struct has_as_known_impl : std::false_type {};

// std::declval<T&>() ensures we test the exact reference type (const or mutable)
template <typename T>
struct has_as_known_impl<T, std::void_t<decltype(std::declval<T &>().as_known())>> : std::true_type {};

template <typename T> inline constexpr bool has_as_known_v = detail::has_as_known_impl<T>::value;

template <typename T, bool IsWrapper = is_reference_wrapper_v<std::remove_reference_t<T>>> struct unwrap_return {
  using type = T &;
};

// If it's a wrapper, it always returns an lvalue reference to the inner type
template <typename T> struct unwrap_return<T, true> {
  using type = typename std::remove_reference_t<T>::type &;
};

template <typename Item> using unwrap_return_t = typename unwrap_return<Item>::type;

template <typename Item>
[[nodiscard]] constexpr std::enable_if_t<!has_as_known_v<unwrapped_raw_t<Item>>,
                                         unwrap_return_t<Item> // <--- Replaces decltype(auto)
                                         >
unwrap_item(Item &item) noexcept {
  if constexpr (is_reference_wrapper_v<std::remove_reference_t<Item>>) {
    return item.get();
  } else {
    return (item);
  }
}

template <typename Item>
[[nodiscard]] constexpr std::enable_if_t<has_as_known_v<unwrapped_raw_t<Item>>,
                                         unwrap_return_t<Item> // <--- Replaces decltype(auto)
                                         >
unwrap_item(Item &item) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
  if constexpr (is_reference_wrapper_v<std::remove_reference_t<Item>>) {
    return item.get().as_known();
  } else {
    return item.as_known();
  }
}

} // namespace detail

template <typename Upstream> class fuse_iterator;
template <typename A, typename B> class zip_iterator;
template <typename Upstream, typename F> class map_iterator;
template <typename Upstream, typename F> class filter_iterator;
template <typename Upstream> class enumerate_iterator;
template <typename Upstream> class take_iterator;
template <typename Upstream> class skip_iterator;
template <typename A, typename B> class chain_iterator;
template <typename It, typename Sentinel> class range_iterator;
template <typename Upstream> class peekable_iterator;
template <typename Upstream> class flatten_iterator;
template <typename Upstream> class step_by_iterator;
template <typename Upstream> class dedup_iterator;
template <typename Upstream> class intersperse_iterator;
template <typename Upstream, std::size_t N> class windows_iterator;
template <typename A, typename B> class merge_iterator;

/**
 * @brief CRTP base providing Rust's `Iterator` adapter/terminal methods on
 * top of a single `Derived::next_impl()` primitive.
 *
 * `Derived` must be publicly derived from `iterator_adaptor<Derived, Item>`
 * and implement `optional<Item> next_impl() noexcept` (called exactly once
 * per `next()`/cursor-increment; never called again once it has returned
 * an empty `optional`, so it is free to assume it will not be polled past
 * exhaustion). See the file-level docs for the `Item` shape convention.
 */
template <typename Derived, typename Item> class iterator_adaptor {
public:
  using item_type = Item;

  /** @brief The unwrapped element type (`T` whether `item_type` is `T`
   * itself or `std::reference_wrapper<T>`); the type callables/predicates
   * passed to `.map()`/`.filter()`/etc. actually see. */
  using value_type = std::remove_reference_t<decltype(detail::unwrap_item(std::declval<item_type &>()))>;

  /** @brief Rust `Iterator::next()` equivalent. Lvalue-only: see the
   * file-level docs for why an rvalue overload is deleted. */
  [[nodiscard]] optional<item_type> next() & noexcept { return derived().next_impl(); }
  optional<item_type> next() && noexcept = delete;

  /** @brief Sentinel type `end()` returns; compares equal to a `cursor`
   * that has been exhausted. Carries no state. */
  class sentinel {};

  /** @brief Single-pass `std::input_iterator_tag` cursor driving `Derived`
   * through repeated `next()` calls; caches the current item so
   * `operator*` can be called more than once between increments. */
  class cursor {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = std::remove_reference_t<decltype(detail::unwrap_item(std::declval<item_type &>()))>;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = decltype(detail::unwrap_item(std::declval<item_type &>()));

    constexpr cursor() noexcept = default;

    [[nodiscard]] reference operator*() noexcept RELOCO_LIFETIMEBOUND { return detail::unwrap_item(*current_); }

    cursor &operator++() noexcept {
      current_ = owner_->next();
      return *this;
    }

    void operator++(int) noexcept { ++(*this); }

    [[nodiscard]] friend bool operator==(const cursor &c, sentinel) noexcept { return !c.current_.has_value(); }
    [[nodiscard]] friend bool operator!=(const cursor &c, sentinel s) noexcept { return !(c == s); }
    [[nodiscard]] friend bool operator==(sentinel s, const cursor &c) noexcept { return c == s; }
    [[nodiscard]] friend bool operator!=(sentinel s, const cursor &c) noexcept { return c != s; }

  private:
    friend class iterator_adaptor;

    explicit cursor(Derived &owner) noexcept : owner_(&owner), current_(owner.next()) {}

    Derived *owner_{nullptr};
    optional<item_type> current_{};
  };

  /** @brief Range-for entry point. Lvalue-only: see the file-level docs. */
  [[nodiscard]] cursor begin() & noexcept RELOCO_LIFETIMEBOUND { return cursor(derived()); }
  [[nodiscard]] sentinel end() & noexcept { return sentinel{}; }
  cursor begin() && noexcept = delete;
  sentinel end() && noexcept = delete;

  /** @brief Rust `Iterator::fuse()`: wraps `*this` so that, once it has
   * returned one empty `next()`, every subsequent call also returns empty
   * without polling `*this` again. Every adaptor in this file already
   * guarantees this on its own; `.fuse()` matters only when wrapping a
   * foreign, hand-written `next_impl()` that might not. */
  [[nodiscard]] fuse_iterator<Derived> fuse() noexcept;

  /** @brief Rust `Iterator::zip()`: pairs up items from `*this` and
   * @p other, stopping as soon as either side is exhausted. */
  template <typename Other> [[nodiscard]] zip_iterator<Derived, Other> zip(Other other) noexcept;

  /** @brief Rust `Iterator::map()`: applies @p f to every (unwrapped)
   * item, producing an adaptor whose `item_type` is `f`'s return type. */
  template <typename F> [[nodiscard]] map_iterator<Derived, F> map(F f) noexcept;

  /** @brief Rust `Iterator::filter()`: yields only the (unwrapped) items
   * for which @p pred returns `true`. */
  template <typename F> [[nodiscard]] filter_iterator<Derived, F> filter(F pred) noexcept;

  /** @brief Rust `Iterator::enumerate()`: pairs every item with its
   * zero-based position, `std::pair<std::size_t, item_type>`. */
  [[nodiscard]] enumerate_iterator<Derived> enumerate() noexcept;

  /** @brief Rust `Iterator::take(n)`: yields at most @p n items, then
   * stops without polling `*this` any further. */
  [[nodiscard]] take_iterator<Derived> take(std::size_t n) noexcept;

  /** @brief Rust `Iterator::skip(n)`: discards the first @p n items (on
   * the first `next()` call, matching Rust's own laziness), then yields
   * the rest. */
  [[nodiscard]] skip_iterator<Derived> skip(std::size_t n) noexcept;

  /** @brief Rust `Iterator::chain()`: yields every item of `*this`, then
   * every item of @p other. Both sides must share the same `item_type`. */
  template <typename Other> [[nodiscard]] chain_iterator<Derived, Other> chain(Other other) noexcept;

  /** @brief Rust `itertools::Itertools::peekable()`/std `Iterator::peekable()`:
   * wraps `*this` so the next item can be inspected via `.peek()` without
   * consuming it. */
  [[nodiscard]] peekable_iterator<Derived> peekable() noexcept;

  /** @brief Rust `Iterator::flatten()`: flattens an iterator whose items
   * are themselves iterable (anything with `begin()`/`end()`, including a
   * plain container or another adaptor chain) into one flat stream.
   * `value_type` must have `begin()`/`end()`. */
  [[nodiscard]] flatten_iterator<Derived> flatten() noexcept;

  /** @brief Rust `Iterator::flat_map()`: `.map(f).flatten()` fused into
   * one call -- @p f must return something iterable (see @ref flatten). */
  template <typename F> [[nodiscard]] auto flat_map(F f) noexcept;

  /** @brief Rust `Iterator::step_by(n)`: yields every `n`-th item,
   * starting with the first (`n` must be `> 0`). */
  [[nodiscard]] step_by_iterator<Derived> step_by(std::size_t n) noexcept;

  /** @brief itertools `Itertools::dedup()`: drops consecutive items that
   * compare equal to the one before them (via `operator==` on the
   * unwrapped item), keeping the first of each run. Requires `item_type`
   * to be copy-constructible (to remember the previous item). */
  [[nodiscard]] dedup_iterator<Derived> dedup() noexcept;

  /** @brief itertools `Itertools::intersperse()`: inserts a copy of
   * @p separator between every pair of consecutive items (never before
   * the first or after the last). Requires `value_type` to be
   * copy-constructible. */
  [[nodiscard]] intersperse_iterator<Derived> intersperse(value_type separator) noexcept;

  /** @brief Rust slice `[T]::windows(N)` equivalent: yields every
   * contiguous, overlapping run of `N` consecutive items as an owned
   * `std::array<value_type, N>` (a copy of each window, so this works
   * regardless of whether `*this` borrows or owns its items). Requires
   * `value_type` to be default-constructible and copyable. */
  template <std::size_t N> [[nodiscard]] windows_iterator<Derived, N> windows() noexcept;

  /** @brief itertools `Itertools::merge()`: merges `*this` and @p other --
   * both assumed already sorted (ascending, via `operator<` on the
   * unwrapped item) -- into one sorted stream. Both sides must share the
   * same `item_type`. */
  template <typename Other> [[nodiscard]] merge_iterator<Derived, Other> merge(Other other) noexcept;

  /** @brief Rust `Iterator::for_each()`: calls @p f with every (unwrapped)
   * item, in order, draining `*this`. */
  template <typename F> void for_each(F f) noexcept {
    while (auto item = next())
      f(detail::unwrap_item(*item));
  }

  /** @brief Rust `Iterator::fold()`: left-fold, draining `*this`. */
  template <typename Acc, typename F> [[nodiscard]] Acc fold(Acc acc, F f) noexcept {
    while (auto item = next())
      acc = f(std::move(acc), detail::unwrap_item(*item));
    return acc;
  }

  /** @brief Rust `Iterator::count()`: number of remaining items, draining
   * `*this`. */
  [[nodiscard]] std::size_t count() noexcept {
    std::size_t n = 0;
    while (next())
      ++n;
    return n;
  }

  /** @brief Rust `Iterator::nth(n)`: the (0-based) `n`-th remaining item,
   * discarding everything before it; empty if `*this` is exhausted first
   * (matching Rust's `None`). */
  [[nodiscard]] optional<item_type> nth(std::size_t n) noexcept {
    for (;;) {
      auto item = next();
      if (!item)
        return nullopt;
      if (n == 0)
        return item;
      --n;
    }
  }

  /** @brief Rust `Iterator::all()`: `true` if @p pred holds for every
   * remaining item (vacuously `true` if already exhausted); stops at the
   * first `false`, draining only up to that point. */
  template <typename F> [[nodiscard]] bool all(F pred) noexcept {
    while (auto item = next())
      if (!pred(detail::unwrap_item(*item)))
        return false;
    return true;
  }

  /** @brief Rust `Iterator::any()`: `true` as soon as @p pred holds for
   * some remaining item; `false` if exhausted without one. */
  template <typename F> [[nodiscard]] bool any(F pred) noexcept {
    while (auto item = next())
      if (pred(detail::unwrap_item(*item)))
        return true;
    return false;
  }

  /** @brief Rust `Iterator::find()`: the first remaining item for which
   * @p pred returns `true`, or empty if none does -- draining every item
   * up to and including the match (or the whole adaptor, on no match). */
  template <typename F> [[nodiscard]] optional<item_type> find(F pred) noexcept {
    while (auto item = next())
      if (pred(detail::unwrap_item(*item)))
        return item;
    return nullopt;
  }

  /** @brief Rust `Iterator::last()`: the final remaining item, or empty
   * if already exhausted; drains `*this`. */
  [[nodiscard]] optional<item_type> last() noexcept {
    optional<item_type> result = nullopt;
    while (auto item = next())
      result = std::move(item);
    return result;
  }

  /** @brief Rust `Iterator::min()`: the smallest remaining item (via
   * `operator<` on the unwrapped item), or empty if already exhausted.
   * If several items are equally smallest, the *first* is returned
   * (matching Rust). */
  [[nodiscard]] optional<item_type> min() noexcept {
    optional<item_type> best = next();
    if (!best)
      return nullopt;
    while (auto item = next())
      if (detail::unwrap_item(*item) < detail::unwrap_item(*best))
        best = std::move(item);
    return best;
  }

  /** @brief Rust `Iterator::max()`: the largest remaining item, or empty
   * if already exhausted. If several items are equally largest, the
   * *last* is returned (matching Rust). */
  [[nodiscard]] optional<item_type> max() noexcept {
    optional<item_type> best = next();
    if (!best)
      return nullopt;
    while (auto item = next())
      if (!(detail::unwrap_item(*item) < detail::unwrap_item(*best)))
        best = std::move(item);
    return best;
  }

  /** @brief Rust `Iterator::min_by_key()`: the item for which @p key_fn
   * returns the smallest key, or empty if already exhausted. Ties keep
   * the *first* match (matching Rust). */
  template <typename F> [[nodiscard]] optional<item_type> min_by_key(F key_fn) noexcept {
    optional<item_type> best = next();
    if (!best)
      return nullopt;
    auto best_key = key_fn(detail::unwrap_item(*best));
    while (auto item = next()) {
      auto key = key_fn(detail::unwrap_item(*item));
      if (key < best_key) {
        best_key = std::move(key);
        best = std::move(item);
      }
    }
    return best;
  }

  /** @brief Rust `Iterator::max_by_key()`: the item for which @p key_fn
   * returns the largest key, or empty if already exhausted. Ties keep the
   * *last* match (matching Rust). */
  template <typename F> [[nodiscard]] optional<item_type> max_by_key(F key_fn) noexcept {
    optional<item_type> best = next();
    if (!best)
      return nullopt;
    auto best_key = key_fn(detail::unwrap_item(*best));
    while (auto item = next()) {
      auto key = key_fn(detail::unwrap_item(*item));
      if (!(key < best_key)) {
        best_key = std::move(key);
        best = std::move(item);
      }
    }
    return best;
  }

  /** @brief Rust `Iterator::sum()`: left-fold over `operator+`, starting
   * from `Acc{}` (defaults to `value_type`). */
  template <typename Acc = value_type> [[nodiscard]] Acc sum() noexcept {
    Acc total{};
    while (auto item = next())
      total = std::move(total) + detail::unwrap_item(*item);
    return total;
  }

  /** @brief Rust `Iterator::product()`: left-fold over `operator*`,
   * starting from `Acc{1}` (defaults to `value_type`). */
  template <typename Acc = value_type> [[nodiscard]] Acc product() noexcept {
    Acc total{1};
    while (auto item = next())
      total = std::move(total) * detail::unwrap_item(*item);
    return total;
  }

private:
  [[nodiscard]] Derived &derived() noexcept { return static_cast<Derived &>(*this); }
};

/**
 * @brief Wraps any `[first, last)` iterator pair (typically a container's
 * own `begin()`/`end()`) as a Rust-style pull iterator borrowing from it:
 * `item_type` is `std::reference_wrapper<T>` (or `<const T>`), never a
 * copy. Constructed via `reloco::iter()`, never named directly.
 */
template <typename It, typename Sentinel = It>
class range_iterator
    : public iterator_adaptor<range_iterator<It, Sentinel>,
                              std::reference_wrapper<std::remove_reference_t<decltype(*std::declval<It &>())>>> {
public:
  range_iterator(It first, Sentinel last) noexcept : current_(std::move(first)), last_(std::move(last)) {}

  using item_type = std::reference_wrapper<std::remove_reference_t<decltype(*std::declval<It &>())>>;

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (current_ == last_)
      return optional<item_type>();
    item_type ref(*current_);
    ++current_;
    return ref;
  }

private:
  It current_;
  Sentinel last_;
};

/**
 * @brief Wraps `range` (any type with `begin()`/`end()`) as a Rust-style
 * pull/range iterator borrowing from it (see `range_iterator`). Only
 * lvalue ranges are accepted -- the returned adaptor stores plain C++
 * iterators into `range`'s own storage, which would dangle immediately if
 * `range` were a temporary (see the file-level docs for the analogous
 * `begin()`/`end()` rvalue-blocking rationale).
 */
template <typename Range> [[nodiscard]] auto iter(Range &range) noexcept {
  return range_iterator<decltype(range.begin())>(range.begin(), range.end());
}

/** @copydoc iter(Range&) */
template <typename Range> [[nodiscard]] auto iter(const Range &range) noexcept {
  return range_iterator<decltype(range.begin())>(range.begin(), range.end());
}

// Blocks `reloco::iter(temporary())`: for a would-be rvalue argument, this
// forwarding-reference overload is the only viable candidate (a `Range &`/
// `const Range &` parameter cannot bind an rvalue... except `const Range &`
// *can*, via lifetime extension -- but partial ordering between function
// templates still prefers this exact-match forwarding reference over the
// const-lvalue-reference overload for an rvalue argument), so the call
// resolves here and fails to compile with a clear "use of deleted
// function" diagnostic instead of silently dangling.
template <typename Range> void iter(Range &&range) noexcept = delete;

/** @brief Rust `Iterator::fuse()`. See `iterator_adaptor::fuse()`. */
template <typename Upstream>
class fuse_iterator : public iterator_adaptor<fuse_iterator<Upstream>, typename Upstream::item_type> {
public:
  using item_type = typename Upstream::item_type;

  explicit fuse_iterator(Upstream upstream) noexcept : upstream_(std::move(upstream)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (done_)
      return nullopt;
    auto item = upstream_.next();
    if (!item)
      done_ = true;
    return item;
  }

private:
  Upstream upstream_;
  bool done_{false};
};

/** @brief Rust `Iterator::zip()`. See `iterator_adaptor::zip()`. */
template <typename A, typename B>
class zip_iterator
    : public iterator_adaptor<zip_iterator<A, B>, std::pair<typename A::item_type, typename B::item_type>> {
public:
  using item_type = std::pair<typename A::item_type, typename B::item_type>;

  zip_iterator(A a, B b) noexcept : a_(std::move(a)), b_(std::move(b)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (done_)
      return nullopt;
    auto a_item = a_.next();
    if (!a_item) {
      done_ = true;
      return nullopt;
    }
    auto b_item = b_.next();
    if (!b_item) {
      done_ = true;
      return nullopt;
    }
    return item_type(std::move(*a_item), std::move(*b_item));
  }

private:
  A a_;
  B b_;
  bool done_{false};
};

/** @brief Rust `Iterator::map()`. See `iterator_adaptor::map()`. */
template <typename Upstream, typename F>
class map_iterator
    : public iterator_adaptor<map_iterator<Upstream, F>, std::decay_t<decltype(std::declval<F &>()(detail::unwrap_item(
                                                             std::declval<typename Upstream::item_type &>())))>> {
public:
  using item_type =
      std::decay_t<decltype(std::declval<F &>()(detail::unwrap_item(std::declval<typename Upstream::item_type &>())))>;

  map_iterator(Upstream upstream, F f) noexcept : upstream_(std::move(upstream)), f_(std::move(f)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    auto item = upstream_.next();
    if (!item)
      return nullopt;
    return item_type(f_(detail::unwrap_item(*item)));
  }

private:
  Upstream upstream_;
  F f_;
};

/** @brief Rust `Iterator::filter()`. See `iterator_adaptor::filter()`. */
template <typename Upstream, typename F>
class filter_iterator : public iterator_adaptor<filter_iterator<Upstream, F>, typename Upstream::item_type> {
public:
  using item_type = typename Upstream::item_type;

  filter_iterator(Upstream upstream, F pred) noexcept : upstream_(std::move(upstream)), pred_(std::move(pred)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    for (;;) {
      auto item = upstream_.next();
      if (!item)
        return nullopt;
      if (pred_(detail::unwrap_item(*item)))
        return item;
    }
  }

private:
  Upstream upstream_;
  F pred_;
};

/** @brief Rust `Iterator::enumerate()`. See `iterator_adaptor::enumerate()`. */
template <typename Upstream>
class enumerate_iterator
    : public iterator_adaptor<enumerate_iterator<Upstream>, std::pair<std::size_t, typename Upstream::item_type>> {
public:
  using item_type = std::pair<std::size_t, typename Upstream::item_type>;

  explicit enumerate_iterator(Upstream upstream) noexcept : upstream_(std::move(upstream)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    auto item = upstream_.next();
    if (!item)
      return nullopt;
    item_type result(index_, std::move(*item));
    ++index_;
    return result;
  }

private:
  Upstream upstream_;
  std::size_t index_{0};
};

/** @brief Rust `Iterator::take(n)`. See `iterator_adaptor::take()`. */
template <typename Upstream>
class take_iterator : public iterator_adaptor<take_iterator<Upstream>, typename Upstream::item_type> {
public:
  using item_type = typename Upstream::item_type;

  take_iterator(Upstream upstream, std::size_t n) noexcept : upstream_(std::move(upstream)), remaining_(n) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (remaining_ == 0)
      return nullopt;
    auto item = upstream_.next();
    if (!item) {
      remaining_ = 0;
      return nullopt;
    }
    --remaining_;
    return item;
  }

private:
  Upstream upstream_;
  std::size_t remaining_;
};

/** @brief Rust `Iterator::skip(n)`. See `iterator_adaptor::skip()`. */
template <typename Upstream>
class skip_iterator : public iterator_adaptor<skip_iterator<Upstream>, typename Upstream::item_type> {
public:
  using item_type = typename Upstream::item_type;

  skip_iterator(Upstream upstream, std::size_t n) noexcept : upstream_(std::move(upstream)), remaining_(n) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (!skipped_) {
      skipped_ = true;
      while (remaining_ > 0) {
        --remaining_;
        if (!upstream_.next())
          return nullopt;
      }
    }
    return upstream_.next();
  }

private:
  Upstream upstream_;
  std::size_t remaining_;
  bool skipped_{false};
};

/** @brief Rust `Iterator::chain()`. See `iterator_adaptor::chain()`. */
template <typename A, typename B>
class chain_iterator : public iterator_adaptor<chain_iterator<A, B>, typename A::item_type> {
public:
  static_assert(std::is_same_v<typename A::item_type, typename B::item_type>,
                "chain: both sides must share the same item_type");

  using item_type = typename A::item_type;

  chain_iterator(A a, B b) noexcept : a_(std::move(a)), b_(std::move(b)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (!a_done_) {
      auto item = a_.next();
      if (item)
        return item;
      a_done_ = true;
    }
    return b_.next();
  }

private:
  A a_;
  B b_;
  bool a_done_{false};
};

/** @brief Rust `Iterator::peekable()`. See `iterator_adaptor::peekable()`. */
template <typename Upstream>
class peekable_iterator : public iterator_adaptor<peekable_iterator<Upstream>, typename Upstream::item_type> {
public:
  using item_type = typename Upstream::item_type;

  explicit peekable_iterator(Upstream upstream) noexcept : upstream_(std::move(upstream)) {}

  /** @brief Rust `Peekable::peek()`: the next item, without consuming it
   * -- calling `.peek()` again (or `.next()`) before any intervening
   * mutation returns the exact same item. Empty once exhausted. */
  [[nodiscard]] const optional<item_type> &peek() noexcept RELOCO_LIFETIMEBOUND {
    if (!cache_.has_value())
      cache_ = upstream_.next();
    return cache_;
  }

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (cache_.has_value())
      return cache_.take();
    return upstream_.next();
  }

private:
  Upstream upstream_;
  optional<item_type> cache_{};
};

namespace detail {

// The type `flatten_iterator<Upstream>` iterates *into*: `Upstream`'s own
// (unwrapped) item, stripped of any reference so it can be stored inline
// (see `flatten_iterator::current_` -- the inner container/adaptor must
// stay alive for as long as we're iterating its elements).
template <typename Upstream>
using flatten_inner_t = std::remove_reference_t<decltype(unwrap_item(std::declval<typename Upstream::item_type &>()))>;

} // namespace detail

/** @brief Rust `Iterator::flatten()`. See `iterator_adaptor::flatten()`. */
template <typename Upstream>
class flatten_iterator
    : public iterator_adaptor<flatten_iterator<Upstream>,
                              std::reference_wrapper<std::remove_reference_t<
                                  decltype(*std::declval<detail::flatten_inner_t<Upstream> &>().begin())>>> {
public:
  using inner_type = detail::flatten_inner_t<Upstream>;
  using item_type = std::reference_wrapper<std::remove_reference_t<decltype(*std::declval<inner_type &>().begin())>>;

  explicit flatten_iterator(Upstream upstream) noexcept : upstream_(std::move(upstream)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    for (;;) {
      if (current_.has_value()) {
        if (inner_it_ != inner_end_) {
          item_type ref(*inner_it_);
          ++inner_it_;
          return ref;
        }
        current_.reset();
      }
      auto outer_item = upstream_.next();
      if (!outer_item)
        return nullopt;
      current_ = std::move(outer_item);
      inner_it_ = detail::unwrap_item(*current_).begin();
      inner_end_ = detail::unwrap_item(*current_).end();
    }
  }

private:
  Upstream upstream_;
  optional<typename Upstream::item_type> current_{};
  decltype(std::declval<inner_type &>().begin()) inner_it_{};
  decltype(std::declval<inner_type &>().end()) inner_end_{};
};

/** @brief Rust `Iterator::step_by()`. See `iterator_adaptor::step_by()`. */
template <typename Upstream>
class step_by_iterator : public iterator_adaptor<step_by_iterator<Upstream>, typename Upstream::item_type> {
public:
  using item_type = typename Upstream::item_type;

  step_by_iterator(Upstream upstream, std::size_t step) noexcept : upstream_(std::move(upstream)), step_(step) {
    RELOCO_ASSERT(step > 0, "step_by: step must be > 0");
  }

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (first_) {
      first_ = false;
      return upstream_.next();
    }
    for (std::size_t i = 1; i < step_; ++i)
      if (!upstream_.next())
        return nullopt;
    return upstream_.next();
  }

private:
  Upstream upstream_;
  std::size_t step_;
  bool first_{true};
};

/** @brief itertools `Itertools::dedup()`. See `iterator_adaptor::dedup()`. */
template <typename Upstream>
class dedup_iterator : public iterator_adaptor<dedup_iterator<Upstream>, typename Upstream::item_type> {
public:
  using item_type = typename Upstream::item_type;

  explicit dedup_iterator(Upstream upstream) noexcept : upstream_(std::move(upstream)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    for (;;) {
      auto item = upstream_.next();
      if (!item)
        return nullopt;
      const bool is_dup = previous_.has_value() && detail::unwrap_item(*previous_) == detail::unwrap_item(*item);
      previous_ = item;
      if (!is_dup)
        return item;
    }
  }

private:
  Upstream upstream_;
  optional<item_type> previous_{};
};

/** @brief itertools `Itertools::intersperse()`. See
 * `iterator_adaptor::intersperse()`. */
template <typename Upstream>
class intersperse_iterator : public iterator_adaptor<intersperse_iterator<Upstream>, typename Upstream::item_type> {
public:
  using item_type = typename Upstream::item_type;
  using value_type = std::remove_reference_t<decltype(detail::unwrap_item(std::declval<item_type &>()))>;

  intersperse_iterator(Upstream upstream, value_type separator) noexcept
      : upstream_(std::move(upstream)), separator_(std::move(separator)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (!started_) {
      started_ = true;
      pending_ = upstream_.next();
      if (!pending_)
        return nullopt;
    }
    if (emit_separator_next_) {
      emit_separator_next_ = false;
      return item_type(separator_);
    }
    if (!pending_)
      return nullopt;
    optional<item_type> result = pending_.take();
    pending_ = upstream_.next();
    if (pending_)
      emit_separator_next_ = true;
    return result;
  }

private:
  Upstream upstream_;
  value_type separator_;
  optional<item_type> pending_{};
  bool started_{false};
  bool emit_separator_next_{false};
};

/** @brief Rust slice `windows(N)` equivalent. See
 * `iterator_adaptor::windows()`. */
template <typename Upstream, std::size_t N>
class windows_iterator : public iterator_adaptor<windows_iterator<Upstream, N>,
                                                 std::array<std::remove_reference_t<decltype(detail::unwrap_item(
                                                                std::declval<typename Upstream::item_type &>()))>,
                                                            N>> {
public:
  static_assert(N > 0, "windows<N>: N must be > 0");

  using value_type =
      std::remove_reference_t<decltype(detail::unwrap_item(std::declval<typename Upstream::item_type &>()))>;
  using item_type = std::array<value_type, N>;

  explicit windows_iterator(Upstream upstream) noexcept : upstream_(std::move(upstream)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (!filled_) {
      for (std::size_t i = 0; i < N; ++i) {
        auto item = upstream_.next();
        if (!item)
          return nullopt;
        buffer_[i] = detail::unwrap_item(*item);
      }
      filled_ = true;
      return item_type(buffer_);
    }
    auto item = upstream_.next();
    if (!item)
      return nullopt;
    for (std::size_t i = 0; i + 1 < N; ++i)
      buffer_[i] = std::move(buffer_[i + 1]);
    buffer_[N - 1] = detail::unwrap_item(*item);
    return item_type(buffer_);
  }

private:
  Upstream upstream_;
  std::array<value_type, N> buffer_{};
  bool filled_{false};
};

/** @brief itertools `Itertools::merge()`. See `iterator_adaptor::merge()`. */
template <typename A, typename B>
class merge_iterator : public iterator_adaptor<merge_iterator<A, B>, typename A::item_type> {
public:
  static_assert(std::is_same_v<typename A::item_type, typename B::item_type>,
                "merge: both sides must share the same item_type");

  using item_type = typename A::item_type;

  merge_iterator(A a, B b) noexcept : a_(std::move(a)), b_(std::move(b)), a_item_(a_.next()), b_item_(b_.next()) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (!a_item_ && !b_item_)
      return nullopt;
    if (!b_item_ || (a_item_ && !(detail::unwrap_item(*b_item_) < detail::unwrap_item(*a_item_)))) {
      optional<item_type> result = a_item_.take();
      a_item_ = a_.next();
      return result;
    }
    optional<item_type> result = b_item_.take();
    b_item_ = b_.next();
    return result;
  }

private:
  A a_;
  B b_;
  optional<item_type> a_item_;
  optional<item_type> b_item_;
};

/**
 * @brief Rust `std::iter::from_fn()` equivalent: a source iterator that
 * calls @p f (a `FnMut() -> optional<Item>` in Rust terms) on every
 * `next()`, forwarding its result directly -- for a "generating" iterator
 * that needs no more state than a capturing lambda already gives it, this
 * is the escape hatch that avoids hand-writing a whole `next_impl()`
 * class. See `from_fn()` below for the usual entry point.
 */
template <typename F>
class from_fn_iterator
    : public iterator_adaptor<from_fn_iterator<F>, typename decltype(std::declval<F &>()())::value_type> {
public:
  using item_type = typename decltype(std::declval<F &>()())::value_type;

  explicit from_fn_iterator(F f) noexcept : f_(std::move(f)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept { return f_(); }

private:
  F f_;
};

/**
 * @brief Rust `std::iter::from_fn()`: builds a source iterator directly
 * out of a callable `optional<Item> f()` -- @p f is called once per
 * `next()`, and its result is forwarded as-is (so @p f itself decides
 * when the sequence ends, by returning an empty `optional`).
 *
 * ```cpp
 * int n = 0;
 * auto counter = reloco::from_fn([n]() mutable -> reloco::optional<int> {
 *   if (n >= 5)
 *     return reloco::nullopt;
 *   return n++;
 * });
 * // counter.count() == 5, yielding 0, 1, 2, 3, 4.
 * ```
 */
template <typename F> [[nodiscard]] auto from_fn(F f) noexcept { return from_fn_iterator<F>(std::move(f)); }

/**
 * @brief Rust `std::iter::once()`: a source iterator yielding exactly one
 * item (a move of @p value), then stopping.
 */
template <typename T> class once_iterator : public iterator_adaptor<once_iterator<T>, T> {
public:
  using item_type = T;

  explicit once_iterator(T value) noexcept : value_(std::move(value)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (!has_value_)
      return nullopt;
    has_value_ = false;
    return optional<item_type>(std::move(value_));
  }

private:
  T value_;
  bool has_value_{true};
};

/** @copydoc once_iterator */
template <typename T> [[nodiscard]] once_iterator<std::decay_t<T>> once(T value) noexcept {
  return once_iterator<std::decay_t<T>>(std::move(value));
}

/**
 * @brief Rust `std::iter::repeat()`: an infinite source iterator that
 * yields an endless stream of copies of @p value. `T` must be copyable.
 * Never exhausts on its own -- always combine with `.take(n)` (or another
 * adaptor that stops early, like `.zip()` against a finite iterator), or
 * a terminal operation such as `.count()`/`.for_each()` will loop forever.
 */
template <typename T> class repeat_iterator : public iterator_adaptor<repeat_iterator<T>, T> {
public:
  using item_type = T;

  explicit repeat_iterator(T value) noexcept : value_(std::move(value)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept { return optional<item_type>(value_); }

private:
  T value_;
};

/** @copydoc repeat_iterator */
template <typename T> [[nodiscard]] repeat_iterator<std::decay_t<T>> repeat(T value) noexcept {
  return repeat_iterator<std::decay_t<T>>(std::move(value));
}

/**
 * @brief Rust `std::iter::successors()`: a source iterator seeded with
 * @p first; each subsequent item is computed by calling @p f with a
 * reference to the previous one (a `FnMut(&Item) -> optional<Item>` in
 * Rust terms). Stops as soon as @p first is empty, or @p f returns empty.
 *
 * ```cpp
 * // Powers of two while doubling stays <= 64: 1, 2, 4, 8, 16, 32, 64.
 * auto powers = reloco::successors(reloco::optional<int>(1), [](int &prev) {
 *   return prev <= 32 ? reloco::optional<int>(prev * 2) : reloco::nullopt;
 * });
 * ```
 */
template <typename T, typename F> class successors_iterator : public iterator_adaptor<successors_iterator<T, F>, T> {
public:
  using item_type = T;

  successors_iterator(optional<T> first, F f) noexcept : current_(std::move(first)), f_(std::move(f)) {}

  [[nodiscard]] optional<item_type> next_impl() noexcept {
    if (!current_)
      return nullopt;
    optional<item_type> result = std::move(current_);
    current_ = f_(*result.as_known());
    return result.as_known();
  }

private:
  optional<T> current_;
  F f_;
};

/** @copydoc successors_iterator */
template <typename T, typename F> [[nodiscard]] successors_iterator<T, F> successors(optional<T> first, F f) noexcept {
  return successors_iterator<T, F>(std::move(first), std::move(f));
}

/**
 * @brief Rust `std::iter::empty()`: a source iterator that is always
 * immediately exhausted. Useful as a neutral placeholder wherever a
 * concrete iterator type is required (`.chain(empty<T>())`, generic code,
 * etc.).
 */
template <typename T> class empty_iterator : public iterator_adaptor<empty_iterator<T>, T> {
public:
  using item_type = T;

  [[nodiscard]] optional<item_type> next_impl() noexcept { return nullopt; }
};

/** @copydoc empty_iterator */
template <typename T> [[nodiscard]] empty_iterator<T> empty() noexcept { return empty_iterator<T>(); }

template <typename Derived, typename Item> fuse_iterator<Derived> iterator_adaptor<Derived, Item>::fuse() noexcept {
  return fuse_iterator<Derived>(std::move(derived()));
}

template <typename Derived, typename Item>
template <typename Other>
zip_iterator<Derived, Other> iterator_adaptor<Derived, Item>::zip(Other other) noexcept {
  return zip_iterator<Derived, Other>(std::move(derived()), std::move(other));
}

template <typename Derived, typename Item>
template <typename F>
map_iterator<Derived, F> iterator_adaptor<Derived, Item>::map(F f) noexcept {
  return map_iterator<Derived, F>(std::move(derived()), std::move(f));
}

template <typename Derived, typename Item>
template <typename F>
filter_iterator<Derived, F> iterator_adaptor<Derived, Item>::filter(F pred) noexcept {
  return filter_iterator<Derived, F>(std::move(derived()), std::move(pred));
}

template <typename Derived, typename Item>
enumerate_iterator<Derived> iterator_adaptor<Derived, Item>::enumerate() noexcept {
  return enumerate_iterator<Derived>(std::move(derived()));
}

template <typename Derived, typename Item>
take_iterator<Derived> iterator_adaptor<Derived, Item>::take(std::size_t n) noexcept {
  return take_iterator<Derived>(std::move(derived()), n);
}

template <typename Derived, typename Item>
skip_iterator<Derived> iterator_adaptor<Derived, Item>::skip(std::size_t n) noexcept {
  return skip_iterator<Derived>(std::move(derived()), n);
}

template <typename Derived, typename Item>
template <typename Other>
chain_iterator<Derived, Other> iterator_adaptor<Derived, Item>::chain(Other other) noexcept {
  return chain_iterator<Derived, Other>(std::move(derived()), std::move(other));
}

template <typename Derived, typename Item>
peekable_iterator<Derived> iterator_adaptor<Derived, Item>::peekable() noexcept {
  return peekable_iterator<Derived>(std::move(derived()));
}

template <typename Derived, typename Item>
flatten_iterator<Derived> iterator_adaptor<Derived, Item>::flatten() noexcept {
  return flatten_iterator<Derived>(std::move(derived()));
}

template <typename Derived, typename Item>
template <typename F>
auto iterator_adaptor<Derived, Item>::flat_map(F f) noexcept {
  return map(std::move(f)).flatten();
}

template <typename Derived, typename Item>
step_by_iterator<Derived> iterator_adaptor<Derived, Item>::step_by(std::size_t n) noexcept {
  return step_by_iterator<Derived>(std::move(derived()), n);
}

template <typename Derived, typename Item> dedup_iterator<Derived> iterator_adaptor<Derived, Item>::dedup() noexcept {
  return dedup_iterator<Derived>(std::move(derived()));
}

template <typename Derived, typename Item>
intersperse_iterator<Derived> iterator_adaptor<Derived, Item>::intersperse(value_type separator) noexcept {
  return intersperse_iterator<Derived>(std::move(derived()), std::move(separator));
}

template <typename Derived, typename Item>
template <std::size_t N>
windows_iterator<Derived, N> iterator_adaptor<Derived, Item>::windows() noexcept {
  return windows_iterator<Derived, N>(std::move(derived()));
}

template <typename Derived, typename Item>
template <typename Other>
merge_iterator<Derived, Other> iterator_adaptor<Derived, Item>::merge(Other other) noexcept {
  return merge_iterator<Derived, Other>(std::move(derived()), std::move(other));
}

template <typename T> class iota_iterator : public iterator_adaptor<iota_iterator<T>, T> {
public:
  using item_type = T;

  /**
   * @brief Constructs a bounded iota iterator [start, end)
   */
  constexpr iota_iterator(T start, T end) noexcept : m_current(start), m_end(end) {}

  /**
   * @brief Generates the next value in the sequence.
   */
  [[nodiscard]] constexpr optional<T> next_impl() noexcept RELOCO_RETURN_TYPESTATE(unknown) {
    if (m_current < m_end) {
      return optional<T>(m_current++); // Return the current value, then increment
    }
    return optional<T>(); // Sequence exhausted
  }

private:
  T m_current;
  T m_end;
};

/**
 * @brief Generates a stream of values from `start` up to (but excluding) `end`.
 *
 * @param start The first value in the sequence.
 * @param end The exclusive upper bound.
 * @return A zero-allocation CRTP pipeline generator.
 */
template <typename T> [[nodiscard]] constexpr iota_iterator<T> iota(T start, T end) noexcept {
  static_assert(std::is_integral_v<T>, "reloco::iota requires an integral type");
  return iota_iterator<T>{start, end};
}

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
