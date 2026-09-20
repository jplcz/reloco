// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file collection_view.hpp
 * @brief Type-erased, non-owning views over an adapted sequence container.
 *
 * `collection_view<T>` erases the concrete container type (`reloco::span<T>`,
 * `reloco::array<T, N>`, `std::array<T, N>`, `std::span<T>`, ...) behind a
 * small, fixed vtable, so a function can accept "any adapted container of
 * `T`" without becoming a template itself -- the same customization-point
 * shape `allocator.hpp` uses for `allocator_ref`: a two-word handle (an
 * untyped context pointer plus a `const vtable *`), no virtual base class,
 * no RTTI, and no allocation of its own.
 *
 * There is deliberately no structural ("any type that happens to have
 * `size()`/`begin()`/`end()`") detection: a container type is only usable
 * through `collection_view` once someone specializes
 * @ref collection_view_traits for it, exactly like `allocator_ref` requires
 * an explicit `allocator_traits<Tag>` specialization rather than accepting
 * anything that happens to expose `allocate`/`deallocate` members. Every
 * accessor the protocol requires must be supplied explicitly by the adapter
 * author; every *optional* capability (contiguous `data()` access, mutable
 * access) is switched on by a required `static constexpr bool` flag on the
 * traits specialization, the same way
 * `allocator_ref::can_expand_in_place()`/`can_reallocate()`/`can_advise()`
 * report whether a backend opted into an optional operation.
 *
 * `collection_view<T>` is *always* read-only: every accessor returns
 * `const T &`/`const T *`, regardless of whether the bound container or `T`
 * itself is `const`-qualified. @ref mutable_collection_view<T> is a
 * distinct, derived type (mirroring `reloco_legacy`'s `collection_view` /
 * `mutable_collection_view` split) that is the only way to obtain mutable
 * element access: it requires the adapted container to have opted into
 * mutation (`collection_view_traits::is_mutable == true`) and to have been
 * bound as a non-`const` lvalue, and adds `T &`/`T *`-returning overloads of
 * `at`/`data`/`for_each` alongside the read-only ones it inherits.
 *
 * Built-in adapters (`collection_view_traits` specializations) are provided
 * at the bottom of this file for `reloco::span<T>`, `reloco::array<T, N>`,
 * `std::array<T, N>`, and `std::span<T>` (the last one gated behind
 * `RELOCO_HAS_STD_SPAN`). No adapters for owning/node-based containers
 * (`std::vector`, `std::list`, `std::map`, ...) are provided yet.
 *
 * Both converting constructors are `explicit`: binding a container to a
 * view is always a deliberate, visible step at the call site, never an
 * implicit conversion.
 *
 * Neither view ever allocates, copies, or owns the underlying container:
 * like `span`/`allocator_ref`, the referenced container must outlive every
 * view built from it.
 */

#include "array.hpp"
#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "span.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief Opt-in customization point describing how to view `Container` as a
 * collection of `element_type`.
 *
 * Intentionally left undefined for any `Container` that hasn't been
 * adapted, mirroring `allocator_traits<Tag>`. A specialization must supply,
 * for its (unqualified) `Container`:
 *
 * - `using element_type = T;` the element type exposed to the views (may
 *   itself be `const`-qualified for a container that is inherently
 *   read-only, e.g. `span<const T>`; then `is_mutable` below must be
 *   `false`).
 * - `static constexpr bool is_random_access;` whether indexed access is
 *   O(1) -- read by `collection_view::is_random_access()`.
 * - `static constexpr bool has_data;` whether the `data()` overload(s)
 *   below are provided; if `false`, do not define them.
 * - `static constexpr bool is_mutable;` whether a non-`const` `Container`
 *   instance can ever yield mutable element access; if `false`, only the
 *   `const Container &`-taking overloads below need to exist.
 * - `static std::size_t size(const Container &) noexcept;`
 * - `static bool empty(const Container &) noexcept;`
 * - `static const element_type &at(const Container &, std::size_t) noexcept;`
 *   required for read-only access via `collection_view`.
 * - `static element_type &at(Container &, std::size_t) noexcept;` required
 *   only if `is_mutable` is `true`, for @ref mutable_collection_view. A
 *   single `const Container &`-taking overload returning `element_type &`
 *   suffices for both when the container's own constness does not restrict
 *   element access (e.g. `span`).
 * - When `has_data` is `true`: `static const element_type *data(const Container &) noexcept;`
 *   and, if `is_mutable` is also `true`,
 *   `static element_type *data(Container &) noexcept;` (again, a single
 *   overload suffices where applicable).
 *
 * @tparam Container Concrete container type to adapt.
 */
template <typename Container> struct collection_view_traits;

namespace detail {

template <typename Container, typename = void> struct has_collection_view_traits : std::false_type {};

template <typename Container>
struct has_collection_view_traits<Container, std::void_t<typename collection_view_traits<Container>::element_type>>
    : std::true_type {};

/**
 * @brief Checks whether `Container` (possibly `const`-qualified) may back a
 * read-only `collection_view<T>`.
 *
 * The adapted, unqualified `Container` must have a `collection_view_traits`
 * specialization whose `element_type` matches `T` up to `const`-qualifiers.
 * No other implicit conversion between element types is permitted.
 */
template <typename Container, typename T, typename = void>
struct is_collection_view_source : std::false_type {};

template <typename Container, typename T>
struct is_collection_view_source<Container, T,
                                 std::enable_if_t<has_collection_view_traits<std::remove_const_t<Container>>::value>>
    : std::bool_constant<std::is_same_v<
          std::remove_cv_t<T>,
          std::remove_cv_t<typename collection_view_traits<std::remove_const_t<Container>>::element_type>>> {};

/**
 * @brief Checks whether `Container` may back a @ref mutable_collection_view<T>.
 *
 * In addition to @ref is_collection_view_source, requires a non-`const`
 * `Container` and `collection_view_traits<Container>::is_mutable == true`.
 */
template <typename Container, typename T, typename = void>
struct is_mutable_collection_view_source : std::false_type {};

template <typename Container, typename T>
struct is_mutable_collection_view_source<
    Container, T, std::enable_if_t<has_collection_view_traits<std::remove_const_t<Container>>::value>>
    : std::bool_constant<is_collection_view_source<Container, T>::value && !std::is_const_v<Container> &&
                          collection_view_traits<std::remove_const_t<Container>>::is_mutable> {};

} // namespace detail

/**
 * @brief Non-owning, type-erased, read-only view over an adapted container
 * of `T`.
 *
 * Every accessor returns `const T &`/`const T *`, regardless of whether the
 * bound container or `T` itself is `const`-qualified: mutation is never
 * possible through this type. A container is only usable here once it has
 * an @ref collection_view_traits specialization; see that template's
 * documentation and this file's built-in adapters for
 * `span`/`array`/`std::array`/`std::span`. See @ref mutable_collection_view
 * for the only way to obtain write access.
 *
 * @tparam T Element type.
 */
template <typename T> class RELOCO_POINTER collection_view {
public:
  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using size_type = std::size_t;
  using visit_fn = void (*)(void *, const T &) noexcept;

  /**
   * @brief Fixed, per-bound-container-type dispatch table.
   *
   * `data` is nullable: it is only ever non-null when the bound
   * container's `collection_view_traits` specialization declares
   * `has_data = true`.
   */
  struct vtable {
    size_type (*size)(const void *ctx) noexcept;
    bool (*empty)(const void *ctx) noexcept;
    void (*for_each)(const void *ctx, void *visitor_ctx, visit_fn visit) noexcept;
    const T &(*at)(const void *ctx, size_type index) noexcept;
    const T *(*data)(const void *ctx) noexcept;
    bool is_random_access;
  };

  /**
   * @brief Constructs an empty view bound to no container.
   */
  constexpr collection_view() noexcept = default;

  /**
   * @brief Binds this view to an existing, adapted container.
   * @tparam Container Concrete container type, deduced. Must have a
   * @ref collection_view_traits specialization compatible with `T` (see
   * @ref detail::is_collection_view_source).
   * @param c Container to view. Must outlive this handle and every copy of
   * it. Marked `explicit`: binding a container is always a deliberate step,
   * never an implicit conversion.
   */
  template <typename Container, std::enable_if_t<detail::is_collection_view_source<Container, T>::value, int> = 0>
  constexpr explicit collection_view(Container &c RELOCO_LIFETIMEBOUND
                                          RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(const_cast<void *>(static_cast<const void *>(std::addressof(c)))), vtbl_(&s_vtbl<Container>) {}

  /**
   * @brief Rejects rvalue/temporary container bindings.
   */
  template <typename Container, std::enable_if_t<!std::is_lvalue_reference_v<Container>, int> = 0>
  collection_view(Container &&) = delete;

  /**
   * @brief Returns the number of elements in the bound container, or `0` if
   * unbound.
   */
  [[nodiscard]] size_type size() const noexcept { return vtbl_ ? vtbl_->size(ctx_) : 0; }

  /**
   * @brief Checks whether the bound container (or the view itself) is empty.
   */
  [[nodiscard]] bool empty() const noexcept { return !vtbl_ || vtbl_->empty(ctx_); }

  /**
   * @brief Reports whether the bound container's `collection_view_traits`
   * declares indexed access as O(1).
   */
  [[nodiscard]] constexpr bool is_random_access() const noexcept { return vtbl_ && vtbl_->is_random_access; }

  /**
   * @brief Reports whether `try_data()`/`data()`/`unsafe_data()` are usable,
   * i.e. whether the bound container's adapter declared `has_data = true`.
   */
  [[nodiscard]] constexpr bool supports_direct_access() const noexcept { return vtbl_ && vtbl_->data != nullptr; }

  /**
   * @brief Visits every element of the bound container in order.
   * @tparam Fn Callable invocable as `Fn(const T &)`.
   */
  template <typename Fn> void for_each(Fn &&fn) const {
    if (!vtbl_)
      return;
    using decayed_fn = std::remove_reference_t<Fn>;
    vtbl_->for_each(ctx_, std::addressof(fn), [](void *visitor_ctx, const T &elem) noexcept {
      (*static_cast<decayed_fn *>(visitor_ctx))(elem);
    });
  }

  /**
   * @brief Returns the contiguous data pointer, if the bound container's
   * adapter supports it.
   */
  [[nodiscard]] result<const T *> try_data() const noexcept {
    if (!supports_direct_access())
      return unexpected(error::unsupported_operation);
    return vtbl_->data(ctx_);
  }

  /**
   * @brief Returns the contiguous data pointer with an always-on precondition
   * check.
   */
  [[nodiscard]] const T *data() const noexcept {
    RELOCO_ASSERT(supports_direct_access(), "collection_view does not support contiguous data access");
    return vtbl_->data(ctx_);
  }

  /**
   * @brief Returns the contiguous data pointer with a debug-only
   * precondition check.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T *unsafe_data() const noexcept {
    RELOCO_DEBUG_ASSERT(supports_direct_access(), "collection_view does not support contiguous data access");
    return vtbl_->data(ctx_);
  }

  /**
   * @brief Attempts to access an element without trapping.
   * @param index Zero-based index of the element to access.
   */
  [[nodiscard]] result<std::reference_wrapper<const T>> try_at(size_type index) const noexcept {
    if (!vtbl_ || index >= vtbl_->size(ctx_))
      return unexpected(error::out_of_bounds);
    return std::cref(vtbl_->at(ctx_, index));
  }

  /**
   * @brief Accesses an element with an always-on bounds check.
   * @param index Zero-based index of the element to access.
   */
  [[nodiscard]] const T &at(size_type index) const noexcept {
    RELOCO_ASSERT(vtbl_ && index < vtbl_->size(ctx_), "collection_view index out of bounds");
    return vtbl_->at(ctx_, index);
  }

  /**
   * @brief Accesses an element with a debug-only bounds check.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_at(size_type index) const noexcept {
    RELOCO_DEBUG_ASSERT(vtbl_ && index < vtbl_->size(ctx_), "collection_view index out of bounds");
    return vtbl_->at(ctx_, index);
  }

protected:
  /**
   * @brief Returns the type-erased context pointer, for use by
   * @ref mutable_collection_view's own dispatch table.
   */
  [[nodiscard]] void *context() const noexcept { return ctx_; }

private:
  template <typename Container> static size_type size_entry(const void *ctx) noexcept {
    using traits = collection_view_traits<std::remove_const_t<Container>>;
    return traits::size(*static_cast<const Container *>(ctx));
  }

  template <typename Container> static bool empty_entry(const void *ctx) noexcept {
    using traits = collection_view_traits<std::remove_const_t<Container>>;
    return traits::empty(*static_cast<const Container *>(ctx));
  }

  template <typename Container>
  static void for_each_entry(const void *ctx, void *visitor_ctx, visit_fn visit) noexcept {
    using traits = collection_view_traits<std::remove_const_t<Container>>;
    const auto &container = *static_cast<const Container *>(ctx);
    const size_type n = traits::size(container);
    for (size_type i = 0; i < n; ++i)
      visit(visitor_ctx, traits::at(container, i));
  }

  template <typename Container> static const T &at_entry(const void *ctx, size_type index) noexcept {
    using traits = collection_view_traits<std::remove_const_t<Container>>;
    return traits::at(*static_cast<const Container *>(ctx), index);
  }

  template <typename Container> static constexpr auto data_entry() noexcept {
    using traits = collection_view_traits<std::remove_const_t<Container>>;
    if constexpr (traits::has_data) {
      return +[](const void *ctx) noexcept -> const T * {
        return traits::data(*static_cast<const Container *>(ctx));
      };
    } else {
      return static_cast<const T *(*)(const void *) noexcept>(nullptr);
    }
  }

  template <typename Container>
  static constexpr vtable s_vtbl{&size_entry<Container>, &empty_entry<Container>, &for_each_entry<Container>,
                                 &at_entry<Container>, data_entry<Container>(),
                                 collection_view_traits<std::remove_const_t<Container>>::is_random_access};

  void *ctx_ = nullptr;
  const vtable *vtbl_ = nullptr;
};

/**
 * @brief Non-owning, type-erased, mutable view over an adapted container of
 * `T`.
 *
 * The only one of the two views that allows mutating elements. Adds
 * `T &`/`T *`-returning `at`/`data`/`for_each` overloads to the read-only
 * ones inherited from @ref collection_view<T>. Requires the adapted
 * container's `collection_view_traits::is_mutable` to be `true` and to be
 * bound as a non-`const` lvalue.
 *
 * @tparam T Element type.
 */
template <typename T> class RELOCO_POINTER mutable_collection_view : public collection_view<T> {
  using base = collection_view<T>;

public:
  using element_type = T;
  using value_type = typename base::value_type;
  using size_type = typename base::size_type;
  using mutable_visit_fn = void (*)(void *, T &) noexcept;

  /**
   * @brief Fixed, per-bound-container-type dispatch table for the mutable
   * operations.
   */
  struct mutable_vtable {
    void (*for_each)(const void *ctx, void *visitor_ctx, mutable_visit_fn visit) noexcept;
    T &(*at)(const void *ctx, size_type index) noexcept;
    T *(*data)(const void *ctx) noexcept;
  };

  /**
   * @brief Constructs an empty view bound to no container.
   */
  constexpr mutable_collection_view() noexcept = default;

  /**
   * @brief Binds this view to an existing, adapted, mutable container.
   * @tparam Container Concrete container type, deduced. Must have a
   * @ref collection_view_traits specialization compatible with `T` and
   * `is_mutable == true` (see @ref detail::is_mutable_collection_view_source).
   * @param c Container to view. Must outlive this handle and every copy of
   * it. Marked `explicit`: binding a container is always a deliberate step,
   * never an implicit conversion.
   */
  template <typename Container,
            std::enable_if_t<detail::is_mutable_collection_view_source<Container, T>::value, int> = 0>
  constexpr explicit mutable_collection_view(Container &c RELOCO_LIFETIMEBOUND
                                                  RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : base(c), mvtbl_(&s_mvtbl<Container>) {}

  /**
   * @brief Rejects rvalue/temporary container bindings.
   */
  template <typename Container, std::enable_if_t<!std::is_lvalue_reference_v<Container>, int> = 0>
  mutable_collection_view(Container &&) = delete;

  using base::for_each;

  /**
   * @brief Visits every element of the bound container in order, allowing
   * mutation.
   * @tparam Fn Callable invocable as `Fn(T &)`.
   */
  template <typename Fn> void for_each(Fn &&fn) {
    if (!mvtbl_)
      return;
    using decayed_fn = std::remove_reference_t<Fn>;
    mvtbl_->for_each(this->context(), std::addressof(fn), [](void *visitor_ctx, T &elem) noexcept {
      (*static_cast<decayed_fn *>(visitor_ctx))(elem);
    });
  }

  using base::data;
  using base::try_data;
  using base::unsafe_data;

  /**
   * @brief Returns the contiguous data pointer, if the bound container's
   * adapter supports it.
   */
  [[nodiscard]] result<T *> try_data() noexcept {
    if (!mvtbl_ || !mvtbl_->data)
      return unexpected(error::unsupported_operation);
    return mvtbl_->data(this->context());
  }

  /**
   * @brief Returns the contiguous data pointer with an always-on
   * precondition check.
   */
  [[nodiscard]] T *data() noexcept {
    RELOCO_ASSERT(mvtbl_ && mvtbl_->data, "mutable_collection_view does not support contiguous data access");
    return mvtbl_->data(this->context());
  }

  /**
   * @brief Returns the contiguous data pointer with a debug-only
   * precondition check.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T *unsafe_data() noexcept {
    RELOCO_DEBUG_ASSERT(mvtbl_ && mvtbl_->data, "mutable_collection_view does not support contiguous data access");
    return mvtbl_->data(this->context());
  }

  using base::at;
  using base::try_at;
  using base::unsafe_at;

  /**
   * @brief Attempts to access an element without trapping.
   * @param index Zero-based index of the element to access.
   */
  [[nodiscard]] result<std::reference_wrapper<T>> try_at(size_type index) noexcept {
    if (!mvtbl_ || index >= this->size())
      return unexpected(error::out_of_bounds);
    return std::ref(mvtbl_->at(this->context(), index));
  }

  /**
   * @brief Accesses an element with an always-on bounds check.
   * @param index Zero-based index of the element to access.
   */
  [[nodiscard]] T &at(size_type index) noexcept {
    RELOCO_ASSERT(mvtbl_ && index < this->size(), "mutable_collection_view index out of bounds");
    return mvtbl_->at(this->context(), index);
  }

  /**
   * @brief Accesses an element with a debug-only bounds check.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_at(size_type index) noexcept {
    RELOCO_DEBUG_ASSERT(mvtbl_ && index < this->size(), "mutable_collection_view index out of bounds");
    return mvtbl_->at(this->context(), index);
  }

private:
  template <typename Container>
  static void mutable_for_each_entry(const void *ctx, void *visitor_ctx, mutable_visit_fn visit) noexcept {
    using traits = collection_view_traits<std::remove_const_t<Container>>;
    auto &container = *static_cast<Container *>(const_cast<void *>(ctx));
    const size_type n = traits::size(container);
    for (size_type i = 0; i < n; ++i)
      visit(visitor_ctx, traits::at(container, i));
  }

  template <typename Container> static T &mutable_at_entry(const void *ctx, size_type index) noexcept {
    using traits = collection_view_traits<std::remove_const_t<Container>>;
    auto &container = *static_cast<Container *>(const_cast<void *>(ctx));
    return traits::at(container, index);
  }

  template <typename Container> static constexpr auto mutable_data_entry() noexcept {
    using traits = collection_view_traits<std::remove_const_t<Container>>;
    if constexpr (traits::has_data) {
      return +[](const void *ctx) noexcept -> T * {
        auto &container = *static_cast<Container *>(const_cast<void *>(ctx));
        return traits::data(container);
      };
    } else {
      return static_cast<T *(*)(const void *) noexcept>(nullptr);
    }
  }

  template <typename Container>
  static constexpr mutable_vtable s_mvtbl{&mutable_for_each_entry<Container>, &mutable_at_entry<Container>,
                                          mutable_data_entry<Container>()};

  const mutable_vtable *mvtbl_ = nullptr;
};

// ---------------------------------------------------------------------------
// Built-in adapters.
// ---------------------------------------------------------------------------

/**
 * @brief Adapts `reloco::array<T, N>` for the collection views.
 *
 * `array`'s own constness restricts element access (like a raw C array), so
 * `at`/`data` each need a mutable and a `const` overload.
 */
template <typename T, std::size_t N> struct collection_view_traits<array<T, N>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = true;

  static std::size_t size(const array<T, N> &) noexcept { return N; }
  static bool empty(const array<T, N> &c) noexcept { return c.empty(); }
  static T &at(array<T, N> &c, std::size_t index) noexcept { return c[index]; }
  static const T &at(const array<T, N> &c, std::size_t index) noexcept { return c[index]; }
  static T *data(array<T, N> &c) noexcept { return c.data(); }
  static const T *data(const array<T, N> &c) noexcept { return c.data(); }
};

/**
 * @brief Adapts `reloco::span<T>` for the collection views.
 *
 * `span<T>`'s own constness never restricts element access -- `operator[]`
 * and `data()` are `const`-qualified members returning `T &`/`T *`
 * regardless -- so a single overload of each suffices. Mutability instead
 * depends entirely on whether `T` itself is `const`: `span<const U>` is a
 * distinct specialization with `element_type = const U` and
 * `is_mutable = false`.
 */
template <typename T> struct collection_view_traits<span<T>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = !std::is_const_v<T>;

  static std::size_t size(const span<T> &c) noexcept { return c.size(); }
  static bool empty(const span<T> &c) noexcept { return c.empty(); }
  static T &at(const span<T> &c, std::size_t index) noexcept { return c[index]; }
  static T *data(const span<T> &c) noexcept { return c.data(); }
};

/**
 * @brief Adapts `std::array<T, N>` for the collection views.
 */
template <typename T, std::size_t N> struct collection_view_traits<std::array<T, N>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = true;

  static std::size_t size(const std::array<T, N> &) noexcept { return N; }
  static bool empty(const std::array<T, N> &c) noexcept { return c.empty(); }
  static T &at(std::array<T, N> &c, std::size_t index) noexcept { return c[index]; }
  static const T &at(const std::array<T, N> &c, std::size_t index) noexcept { return c[index]; }
  static T *data(std::array<T, N> &c) noexcept { return c.data(); }
  static const T *data(const std::array<T, N> &c) noexcept { return c.data(); }
};

#if RELOCO_HAS_STD_SPAN

/**
 * @brief Adapts `std::span<T>` for the collection views.
 *
 * Like `reloco::span<T>`, `std::span<T>`'s own constness never restricts
 * element access, so a single overload of each accessor suffices, and
 * mutability depends entirely on `T`'s own constness.
 */
template <typename T> struct collection_view_traits<std::span<T>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = !std::is_const_v<T>;

  static std::size_t size(const std::span<T> &c) noexcept { return c.size(); }
  static bool empty(const std::span<T> &c) noexcept { return c.empty(); }
  static T &at(const std::span<T> &c, std::size_t index) noexcept { return c[index]; }
  static T *data(const std::span<T> &c) noexcept { return c.data(); }
};

#endif // RELOCO_HAS_STD_SPAN

} // namespace reloco
