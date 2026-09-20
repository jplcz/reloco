// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file container_ref.hpp
 * @brief Type-erased, non-owning handle over an adapted container that
 * allows *structural* mutation (growing, inserting, erasing, clearing), as
 * well as indexed/keyed access to existing elements, as opposed to
 * @ref collection_view.hpp's `mutable_collection_view`, which only iterates
 * and mutates elements already present, never resizing the container.
 *
 * `mutable_container_ref<T>` (sequence containers) and
 * `mutable_container_ref<T, Key>` (associative containers) erase the
 * concrete container type behind a small, fixed vtable, the same
 * customization-point shape `allocator.hpp`/`collection_view.hpp` use: a
 * two-word handle (an untyped context pointer plus a `const vtable *`), no
 * virtual base class, no RTTI, and no allocation of its own.
 *
 * There is deliberately no structural detection: a container type is only
 * usable through `mutable_container_ref` once someone specializes
 * @ref container_ref_traits for it, exactly like `collection_view_traits`
 * and `allocator_traits<Tag>`.
 *
 * A container adapted here is either a *sequence* container
 * (`is_associative = false`), reachable through `mutable_container_ref<T>`
 * (an alias selecting `Key = void`) and offering
 * `try_push_back`/`try_push_front`/`try_insert_at(index,
 * value)`/`try_erase_at(index)`/`clear()`/`for_each(Fn)`, plus
 * `at`/`try_at`/`unsafe_at(index)` for existing elements; or an
 * *associative* container (`is_associative = true`), reachable through
 * `mutable_container_ref<T, Key>` and offering
 * `try_insert_at(key, value)`/`try_erase(key)`/`clear()`/`for_each(Fn)`,
 * plus `at`/`try_at`/`unsafe_at(key)` for existing entries. Which of the
 * two a given `container_ref_traits<Container>` specialization describes
 * is switched by its required `is_associative` flag; the
 * `mutable_container_ref` class template alias picks between two `detail`
 * implementation classes based on whether `Key` is `void`.
 *
 * Every operation the adapted container does not support (e.g. a container
 * with no efficient front-insertion) must still be implemented by the
 * traits specialization -- it simply always returns
 * `unexpected(error::unsupported_operation)` for that operation. There is
 * no separate optional-capability flag/nullable-function-pointer scheme
 * here (unlike `collection_view_traits::has_data`): every fallible trait
 * function must exist, and reports its own support (or lack thereof)
 * through its `result<void>`.
 *
 * This header ships no built-in adapters. See `container_ref_std.hpp` for
 * `std::vector`/`std::map` adapters.
 */

#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief Opt-in customization point describing how `Container` may be
 * structurally mutated and how its existing elements may be accessed,
 * through @ref mutable_container_ref.
 *
 * Intentionally left undefined for any `Container` that hasn't been
 * adapted, mirroring `allocator_traits<Tag>`/`collection_view_traits`. A
 * specialization must supply, for its (unqualified) `Container`:
 *
 * - `static constexpr bool is_associative;` selects which of the two
 *   contracts below applies.
 * - `static std::size_t size(const Container &) noexcept;`
 * - `static bool empty(const Container &) noexcept;`
 * - `static void clear(Container &) noexcept;`
 *
 * If `is_associative` is `false` (a *sequence* container, adapted through
 * `mutable_container_ref<T>`):
 *
 * - `using element_type = T;` the element type inserted into and read from
 *   the container.
 * - `static result<void> try_push_back(Container &, element_type) noexcept;`
 * - `static result<void> try_push_front(Container &, element_type) noexcept;`
 * - `static result<void> try_insert_at(Container &, std::size_t, element_type) noexcept;`
 * - `static result<void> try_erase_at(Container &, std::size_t) noexcept;`
 * - `static element_type &at(Container &, std::size_t) noexcept;` unchecked
 *   indexed access; the caller (`mutable_container_ref`) has already
 *   verified `index < size()`.
 *
 * If `is_associative` is `true` (an *associative* container, adapted
 * through `mutable_container_ref<T, Key>`):
 *
 * - `using element_type = V;` the mapped/value type.
 * - `using key_type = K;` the key type.
 * - `static result<void> try_insert_at(Container &, key_type, element_type) noexcept;`
 * - `static result<void> try_erase(Container &, const key_type &) noexcept;`
 * - `static element_type *find(Container &, const key_type &) noexcept;`
 *   returns a pointer to the entry mapped to `key`, or `nullptr` if `key`
 *   is not present.
 * - `static void for_each(Container &, void *visitor_ctx, void
 *   (*visit)(void *, const key_type &, element_type &) noexcept) noexcept;`
 *   visits every entry in the container, invoking `visit(visitor_ctx, key,
 *   value)` for each. (The sequence contract needs no such function: its
 *   `for_each` is implemented generically on top of `at`/`size`.)
 *
 * Every fallible trait function must always be defined, even for an
 * operation the concrete container cannot support (e.g. `try_push_front`
 * for a container with no efficient front-insertion): such a function
 * should simply return `unexpected(error::unsupported_operation)` rather
 * than being omitted -- there is no separate capability flag to gate it
 * out at the vtable level.
 *
 * @tparam Container Concrete container type to adapt.
 */
template <typename Container> struct container_ref_traits;

namespace detail {

template <typename Container, typename = void> struct has_container_ref_traits : std::false_type {};

template <typename Container>
struct has_container_ref_traits<Container, std::void_t<decltype(container_ref_traits<Container>::is_associative)>>
    : std::true_type {};

/**
 * @brief Checks whether `Container` may back a sequence
 * `mutable_container_ref<T>` (i.e. `Key == void`).
 */
template <typename Container, typename T, typename = void>
struct is_container_ref_source : std::false_type {};

template <typename Container, typename T>
struct is_container_ref_source<Container, T, std::enable_if_t<has_container_ref_traits<Container>::value>>
    : std::bool_constant<!container_ref_traits<Container>::is_associative &&
                          std::is_same_v<T, typename container_ref_traits<Container>::element_type>> {};

/**
 * @brief Detects whether `container_ref_traits<Container>` defines
 * `key_type`, i.e. describes an associative container.
 *
 * Kept separate from @ref has_container_ref_traits so that referencing
 * `::key_type` below is never attempted for a sequence-only traits
 * specialization (which need not define it).
 */
template <typename Container, typename = void>
struct has_associative_container_ref_traits : std::false_type {};

template <typename Container>
struct has_associative_container_ref_traits<Container,
                                             std::void_t<typename container_ref_traits<Container>::key_type>>
    : std::true_type {};

/**
 * @brief Checks whether `Container` may back an associative
 * `mutable_container_ref<T, Key>`.
 */
template <typename Container, typename T, typename Key, typename = void>
struct is_associative_container_ref_source : std::false_type {};

template <typename Container, typename T, typename Key>
struct is_associative_container_ref_source<
    Container, T, Key,
    std::enable_if_t<has_container_ref_traits<Container>::value &&
                      has_associative_container_ref_traits<Container>::value>>
    : std::bool_constant<container_ref_traits<Container>::is_associative &&
                          std::is_same_v<T, typename container_ref_traits<Container>::element_type> &&
                          std::is_same_v<Key, typename container_ref_traits<Container>::key_type>> {};

/**
 * @brief Sequence-container implementation of `mutable_container_ref<T>`.
 *
 * Mutates via `try_push_back`/`try_push_front`/`try_insert_at(index,
 * value)`/`try_erase_at(index)`/`clear()`; reads/mutates existing elements
 * via `at`/`try_at`/`unsafe_at(index)`/`for_each(Fn)`.
 *
 * @tparam T Element type.
 */
template <typename T> class RELOCO_POINTER mutable_sequence_container_ref {
public:
  using element_type = T;
  using size_type = std::size_t;

  /**
   * @brief Fixed, per-bound-container-type dispatch table.
   */
  struct vtable {
    size_type (*size)(const void *ctx) noexcept;
    bool (*empty)(const void *ctx) noexcept;
    void (*clear)(void *ctx) noexcept;
    result<void> (*push_back)(void *ctx, T &&value) noexcept;
    result<void> (*push_front)(void *ctx, T &&value) noexcept;
    result<void> (*insert_at)(void *ctx, size_type index, T &&value) noexcept;
    result<void> (*erase_at)(void *ctx, size_type index) noexcept;
    T &(*at)(void *ctx, size_type index) noexcept;
  };

  /**
   * @brief Constructs an empty ref bound to no container.
   */
  constexpr mutable_sequence_container_ref() noexcept = default;

  /**
   * @brief Binds this ref to an existing, adapted, non-associative
   * container.
   * @tparam Container Concrete container type, deduced. Must have a
   * @ref container_ref_traits specialization compatible with `T` (see
   * @ref is_container_ref_source).
   * @param c Container to bind. Must outlive this handle and every copy of
   * it. Marked `explicit`: binding a container is always a deliberate step,
   * never an implicit conversion.
   */
  template <typename Container, std::enable_if_t<is_container_ref_source<Container, T>::value, int> = 0>
  constexpr explicit mutable_sequence_container_ref(Container &c RELOCO_LIFETIMEBOUND
                                                         RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(std::addressof(c)), vtbl_(&s_vtbl<Container>) {}

  /**
   * @brief Rejects rvalue/temporary container bindings.
   */
  template <typename Container, std::enable_if_t<!std::is_lvalue_reference_v<Container>, int> = 0>
  mutable_sequence_container_ref(Container &&) = delete;

  /**
   * @brief Returns the number of elements in the bound container, or `0` if
   * unbound.
   */
  [[nodiscard]] size_type size() const noexcept { return vtbl_ ? vtbl_->size(ctx_) : 0; }

  /**
   * @brief Checks whether the bound container (or the ref itself) is empty.
   */
  [[nodiscard]] bool empty() const noexcept { return !vtbl_ || vtbl_->empty(ctx_); }

  /**
   * @brief Always `false` for this specialization.
   */
  [[nodiscard]] static constexpr bool is_associative() noexcept { return false; }

  /**
   * @brief Removes every element from the bound container. A no-op if this
   * ref is unbound.
   */
  void clear() noexcept {
    if (vtbl_)
      vtbl_->clear(ctx_);
  }

  /**
   * @brief Appends `value` to the end of the bound container.
   *
   * Fails with `error::unsupported_operation` if this ref is unbound, or
   * whatever the adapter itself reports (e.g. `error::allocation_failed` if
   * the container failed to grow).
   */
  [[nodiscard]] result<void> try_push_back(element_type value) noexcept {
    if (!vtbl_)
      return unexpected(error::unsupported_operation);
    return vtbl_->push_back(ctx_, std::move(value));
  }

  /**
   * @brief Prepends `value` to the start of the bound container.
   *
   * Fails with `error::unsupported_operation` if this ref is unbound or the
   * adapter does not support front-insertion, or whatever else the adapter
   * itself reports.
   */
  [[nodiscard]] result<void> try_push_front(element_type value) noexcept {
    if (!vtbl_)
      return unexpected(error::unsupported_operation);
    return vtbl_->push_front(ctx_, std::move(value));
  }

  /**
   * @brief Inserts `value` at `index`, shifting existing elements at or
   * past `index` back by one.
   *
   * `index == size()` is equivalent to `try_push_back`. Fails with
   * `error::unsupported_operation` if this ref is unbound, or whatever the
   * adapter itself reports (e.g. `error::out_of_bounds` if `index` is
   * beyond `size()`).
   */
  [[nodiscard]] result<void> try_insert_at(size_type index, element_type value) noexcept {
    if (!vtbl_)
      return unexpected(error::unsupported_operation);
    return vtbl_->insert_at(ctx_, index, std::move(value));
  }

  /**
   * @brief Removes the element at `index`, shifting later elements forward
   * by one.
   *
   * Fails with `error::unsupported_operation` if this ref is unbound, or
   * whatever the adapter itself reports (e.g. `error::out_of_bounds` if
   * `index >= size()`).
   */
  [[nodiscard]] result<void> try_erase_at(size_type index) noexcept {
    if (!vtbl_)
      return unexpected(error::unsupported_operation);
    return vtbl_->erase_at(ctx_, index);
  }

  /**
   * @brief Attempts to access an existing element without trapping.
   * @param index Zero-based index of the element to access.
   */
  [[nodiscard]] result<std::reference_wrapper<T>> try_at(size_type index) noexcept {
    if (!vtbl_ || index >= vtbl_->size(ctx_))
      return unexpected(error::out_of_bounds);
    return std::ref(vtbl_->at(ctx_, index));
  }

  /**
   * @brief Accesses an existing element with an always-on bounds check.
   * @param index Zero-based index of the element to access.
   */
  [[nodiscard]] T &at(size_type index) noexcept {
    RELOCO_ASSERT(vtbl_ && index < vtbl_->size(ctx_), "mutable_container_ref index out of bounds");
    return vtbl_->at(ctx_, index);
  }

  /**
   * @brief Accesses an existing element with a debug-only bounds check.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_at(size_type index) noexcept {
    RELOCO_DEBUG_ASSERT(vtbl_ && index < vtbl_->size(ctx_), "mutable_container_ref index out of bounds");
    return vtbl_->at(ctx_, index);
  }

  /**
   * @brief Visits every element of the bound container in order, allowing
   * mutation.
   *
   * Implemented generically on top of `at`/`size` (no separate trait
   * function needed): this contract targets index-addressable "vector-
   * like" containers, so an index-based traversal is always meaningful.
   * @tparam Fn Callable invocable as `Fn(T &)`.
   */
  template <typename Fn> void for_each(Fn &&fn) {
    if (!vtbl_)
      return;
    const size_type n = vtbl_->size(ctx_);
    for (size_type i = 0; i < n; ++i)
      fn(vtbl_->at(ctx_, i));
  }

private:
  template <typename Container> static size_type size_entry(const void *ctx) noexcept {
    using traits = container_ref_traits<Container>;
    return traits::size(*static_cast<const Container *>(ctx));
  }

  template <typename Container> static bool empty_entry(const void *ctx) noexcept {
    using traits = container_ref_traits<Container>;
    return traits::empty(*static_cast<const Container *>(ctx));
  }

  template <typename Container> static void clear_entry(void *ctx) noexcept {
    using traits = container_ref_traits<Container>;
    traits::clear(*static_cast<Container *>(ctx));
  }

  template <typename Container> static result<void> push_back_entry(void *ctx, T &&value) noexcept {
    using traits = container_ref_traits<Container>;
    return traits::try_push_back(*static_cast<Container *>(ctx), std::move(value));
  }

  template <typename Container> static result<void> push_front_entry(void *ctx, T &&value) noexcept {
    using traits = container_ref_traits<Container>;
    return traits::try_push_front(*static_cast<Container *>(ctx), std::move(value));
  }

  template <typename Container> static result<void> insert_at_entry(void *ctx, size_type index, T &&value) noexcept {
    using traits = container_ref_traits<Container>;
    return traits::try_insert_at(*static_cast<Container *>(ctx), index, std::move(value));
  }

  template <typename Container> static result<void> erase_at_entry(void *ctx, size_type index) noexcept {
    using traits = container_ref_traits<Container>;
    return traits::try_erase_at(*static_cast<Container *>(ctx), index);
  }

  template <typename Container> static T &at_entry(void *ctx, size_type index) noexcept {
    using traits = container_ref_traits<Container>;
    return traits::at(*static_cast<Container *>(ctx), index);
  }

  template <typename Container>
  static constexpr vtable s_vtbl{&size_entry<Container>,  &empty_entry<Container>,     &clear_entry<Container>,
                                 &push_back_entry<Container>, &push_front_entry<Container>,
                                 &insert_at_entry<Container>, &erase_at_entry<Container>, &at_entry<Container>};

  void *ctx_ = nullptr;
  const vtable *vtbl_ = nullptr;
};

/**
 * @brief Associative-container implementation of
 * `mutable_container_ref<T, Key>`.
 *
 * Mutates via `try_insert_at(key, value)`/`try_erase(key)`/`clear()`;
 * reads/mutates existing entries via `at`/`try_at`/`unsafe_at(key)`/
 * `for_each(Fn)`.
 *
 * @tparam T Mapped/value type.
 * @tparam Key Key type.
 */
template <typename T, typename Key> class RELOCO_POINTER mutable_associative_container_ref {
public:
  using element_type = T;
  using key_type = Key;
  using size_type = std::size_t;
  using visit_fn = void (*)(void *, const key_type &, T &) noexcept;

  /**
   * @brief Fixed, per-bound-container-type dispatch table.
   */
  struct vtable {
    size_type (*size)(const void *ctx) noexcept;
    bool (*empty)(const void *ctx) noexcept;
    void (*clear)(void *ctx) noexcept;
    result<void> (*insert_at)(void *ctx, key_type &&key, T &&value) noexcept;
    result<void> (*erase)(void *ctx, const key_type &key) noexcept;
    T *(*find)(void *ctx, const key_type &key) noexcept;
    void (*for_each)(void *ctx, void *visitor_ctx, visit_fn visit) noexcept;
  };

  /**
   * @brief Constructs an empty ref bound to no container.
   */
  constexpr mutable_associative_container_ref() noexcept = default;

  /**
   * @brief Binds this ref to an existing, adapted, associative container.
   * @tparam Container Concrete container type, deduced. Must have a
   * @ref container_ref_traits specialization compatible with `T`/`Key`
   * (see @ref is_associative_container_ref_source).
   * @param c Container to bind. Must outlive this handle and every copy of
   * it. Marked `explicit`: binding a container is always a deliberate step,
   * never an implicit conversion.
   */
  template <typename Container,
            std::enable_if_t<is_associative_container_ref_source<Container, T, Key>::value, int> = 0>
  constexpr explicit mutable_associative_container_ref(Container &c RELOCO_LIFETIMEBOUND
                                                             RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(std::addressof(c)), vtbl_(&s_vtbl<Container>) {}

  /**
   * @brief Rejects rvalue/temporary container bindings.
   */
  template <typename Container, std::enable_if_t<!std::is_lvalue_reference_v<Container>, int> = 0>
  mutable_associative_container_ref(Container &&) = delete;

  /**
   * @brief Returns the number of entries in the bound container, or `0` if
   * unbound.
   */
  [[nodiscard]] size_type size() const noexcept { return vtbl_ ? vtbl_->size(ctx_) : 0; }

  /**
   * @brief Checks whether the bound container (or the ref itself) is empty.
   */
  [[nodiscard]] bool empty() const noexcept { return !vtbl_ || vtbl_->empty(ctx_); }

  /**
   * @brief Always `true` for this specialization.
   */
  [[nodiscard]] static constexpr bool is_associative() noexcept { return true; }

  /**
   * @brief Removes every entry from the bound container. A no-op if this
   * ref is unbound.
   */
  void clear() noexcept {
    if (vtbl_)
      vtbl_->clear(ctx_);
  }

  /**
   * @brief Inserts `value` under `key`.
   *
   * Fails with `error::unsupported_operation` if this ref is unbound, or
   * whatever the adapter itself reports (e.g. `error::already_exists` if
   * `key` is already present).
   */
  [[nodiscard]] result<void> try_insert_at(key_type key, element_type value) noexcept {
    if (!vtbl_)
      return unexpected(error::unsupported_operation);
    return vtbl_->insert_at(ctx_, std::move(key), std::move(value));
  }

  /**
   * @brief Removes the entry mapped to `key`, if any.
   *
   * Fails with `error::unsupported_operation` if this ref is unbound, or
   * whatever the adapter itself reports (e.g. `error::not_found` if `key`
   * is absent).
   */
  [[nodiscard]] result<void> try_erase(const key_type &key) noexcept {
    if (!vtbl_)
      return unexpected(error::unsupported_operation);
    return vtbl_->erase(ctx_, key);
  }

  /**
   * @brief Attempts to access the entry mapped to `key` without trapping.
   */
  [[nodiscard]] result<std::reference_wrapper<T>> try_at(const key_type &key) noexcept {
    if (!vtbl_)
      return unexpected(error::unsupported_operation);
    T *found = vtbl_->find(ctx_, key);
    if (!found)
      return unexpected(error::not_found);
    return std::ref(*found);
  }

  /**
   * @brief Accesses the entry mapped to `key`, trapping if `key` is absent.
   */
  [[nodiscard]] T &at(const key_type &key) noexcept {
    RELOCO_ASSERT(vtbl_, "mutable_container_ref is unbound");
    T *found = vtbl_->find(ctx_, key);
    RELOCO_ASSERT(found, "mutable_container_ref key not found");
    return *found;
  }

  /**
   * @brief Accesses the entry mapped to `key` with a debug-only presence
   * check.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_at(const key_type &key) noexcept {
    RELOCO_DEBUG_ASSERT(vtbl_, "mutable_container_ref is unbound");
    T *found = vtbl_->find(ctx_, key);
    RELOCO_DEBUG_ASSERT(found, "mutable_container_ref key not found");
    return *found;
  }

  /**
   * @brief Visits every entry of the bound container, allowing mutation of
   * each mapped value.
   * @tparam Fn Callable invocable as `Fn(const key_type &, T &)`.
   */
  template <typename Fn> void for_each(Fn &&fn) {
    if (!vtbl_)
      return;
    using decayed_fn = std::remove_reference_t<Fn>;
    vtbl_->for_each(ctx_, std::addressof(fn), [](void *visitor_ctx, const key_type &key, T &value) noexcept {
      (*static_cast<decayed_fn *>(visitor_ctx))(key, value);
    });
  }

private:
  template <typename Container> static size_type size_entry(const void *ctx) noexcept {
    using traits = container_ref_traits<Container>;
    return traits::size(*static_cast<const Container *>(ctx));
  }

  template <typename Container> static bool empty_entry(const void *ctx) noexcept {
    using traits = container_ref_traits<Container>;
    return traits::empty(*static_cast<const Container *>(ctx));
  }

  template <typename Container> static void clear_entry(void *ctx) noexcept {
    using traits = container_ref_traits<Container>;
    traits::clear(*static_cast<Container *>(ctx));
  }

  template <typename Container> static result<void> insert_at_entry(void *ctx, key_type &&key, T &&value) noexcept {
    using traits = container_ref_traits<Container>;
    return traits::try_insert_at(*static_cast<Container *>(ctx), std::move(key), std::move(value));
  }

  template <typename Container> static result<void> erase_entry(void *ctx, const key_type &key) noexcept {
    using traits = container_ref_traits<Container>;
    return traits::try_erase(*static_cast<Container *>(ctx), key);
  }

  template <typename Container> static T *find_entry(void *ctx, const key_type &key) noexcept {
    using traits = container_ref_traits<Container>;
    return traits::find(*static_cast<Container *>(ctx), key);
  }

  template <typename Container>
  static void for_each_entry(void *ctx, void *visitor_ctx, visit_fn visit) noexcept {
    using traits = container_ref_traits<Container>;
    traits::for_each(*static_cast<Container *>(ctx), visitor_ctx, visit);
  }

  template <typename Container>
  static constexpr vtable s_vtbl{&size_entry<Container>,  &empty_entry<Container>, &clear_entry<Container>,
                                 &insert_at_entry<Container>, &erase_entry<Container>,
                                 &find_entry<Container>,      &for_each_entry<Container>};

  void *ctx_ = nullptr;
  const vtable *vtbl_ = nullptr;
};

} // namespace detail

/**
 * @brief Non-owning, type-erased handle allowing structural mutation of an
 * adapted container, as well as indexed/keyed access to its existing
 * elements.
 *
 * Alias selecting between the two `detail` implementation classes: when
 * `Key` is `void` (the default), `mutable_container_ref<T>` is a sequence
 * container handle (`try_push_back`/`try_push_front`/`try_insert_at(index,
 * value)`/`try_erase_at(index)`/`clear()`/`for_each(Fn)`, plus
 * `at`/`try_at`/`unsafe_at(index)`); otherwise `mutable_container_ref<T,
 * Key>` is an associative container handle
 * (`try_insert_at(key, value)`/`try_erase(key)`/`clear()`/`for_each(Fn)`,
 * plus `at`/`try_at`/`unsafe_at(key)`).
 *
 * @tparam T Element (sequence) or mapped/value (associative) type.
 * @tparam Key Key type for an associative container, or `void` (the
 * default) for a sequence container.
 */
template <typename T, typename Key = void>
using mutable_container_ref =
    std::conditional_t<std::is_void_v<Key>, detail::mutable_sequence_container_ref<T>,
                       detail::mutable_associative_container_ref<T, Key>>;

} // namespace reloco
