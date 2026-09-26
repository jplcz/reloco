// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file outline_vec_deque.hpp
 * @brief Non-owning, growable-up-to-capacity double-ended queue over a
 * caller-supplied byte buffer, with fallible mutation.
 *
 * `outline_vec_deque<T>` is `vec_deque<T>`'s (see `vec_deque.hpp`)
 * non-owning counterpart, exactly mirroring how `outline_vector<T>` (see
 * `outline_vector.hpp`) relates to `vector<T>`: its elements live in a
 * `span<std::byte>` the caller passes in at construction and continues to
 * own -- `outline_vec_deque<T>` never allocates, never deallocates, and
 * never frees that memory itself.
 *
 * The bound span is captured exactly once, at construction, and is
 * afterwards immutable: there is no default constructor, no rebind/reset
 * method, and -- like `outline_vector<T>` -- no move constructor or move
 * assignment operator either, for the same reason: there is no
 * well-defined way to "steal" a borrowed span out from under whoever
 * actually owns it. `reloco::is_trivially_relocatable` is deliberately
 * *not* specialized for `outline_vec_deque<T>`, for the same reason as
 * `outline_vector<T>`.
 *
 * `try_reserve`/`try_emplace_back`/`try_push_back`/`try_emplace_front`/
 * `try_push_front`/`try_insert_at` grow only up to the bound span's
 * capacity (`storage.size() / sizeof(T)`, computed once at construction
 * and fixed for the object's entire lifetime) and fail with
 * `error::capacity_exceeded` beyond that, exactly like `inline_vec_deque<T,
 * Capacity>` -- there is no allocator to spill onto.
 *
 * The bound span must already be sized and aligned for `T`, checked the
 * same way `outline_vector<T>` checks it (`RELOCO_ASSERT`, active even
 * when `NDEBUG` is defined, see `docs/hardened-containers.md`).
 *
 * There is no `try_create`/`try_allocate`/`try_clone`/`try_clone_at`:
 * binding a caller-owned span can never itself fail, so the plain
 * constructor is sufficient, exactly like `outline_vector<T>`.
 */

#include "alignment.hpp"
#include "allocator.hpp"
#include "collection_view.hpp"
#include "construction_helpers.hpp"
#include "container_ref.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "detail/vector_base.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "rvalue_safety.hpp"
#include "span.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T>
class RELOCO_POINTER outline_vec_deque : public detail::typed_deque_base<T, detail::outline_deque_base> {
  using base_t = detail::typed_deque_base<T, detail::outline_deque_base>;

public:
  using typename base_t::allocator_type;
  using typename base_t::const_pointer;
  using typename base_t::const_reference;
  using typename base_t::difference_type;
  using typename base_t::pointer;
  using typename base_t::reference;
  using typename base_t::size_type;
  using typename base_t::value_type;

  // -------------------------------------------------------------------------
  // Constructor & Destructor
  // -------------------------------------------------------------------------

  /**
   * @brief Binds this outline_vec_deque to a caller-owned byte span, sized
   * and aligned for `T`. The span is captured exactly once and is never
   * rebound, reallocated, or resized past `storage.size() / sizeof(T)`
   * elements afterwards; `storage` itself must remain valid, untouched by
   * the caller, and stable in address for the entire lifetime of `*this`.
   *
   * @param storage Byte-addressed backing storage. Must satisfy
   * `effective_alignment_v<T>` (checked via `RELOCO_ASSERT`) and outlive
   * `*this`.
   */
  constexpr explicit outline_vec_deque(
      span<std::byte> storage RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : base_t(storage.data(), storage.size() / sizeof(T)) {
    RELOCO_ASSERT(reinterpret_cast<std::uintptr_t>(storage.data()) % effective_alignment_v<T> == 0,
                  "outline_vec_deque: storage span is not aligned for T");
  }

  /**
   * @brief Destructor. Destroys any live elements; never touches the
   * caller-owned backing storage itself (it is not this object's to free).
   */
  ~outline_vec_deque() noexcept { this->destroy_elements(detail::get_operations_for<T>(), detail::metadata_for<T>); }

  /**
   * @brief `true` once `size() == capacity()` -- the bound span has no
   * room left, matching `inline_vec_deque<T, Capacity>::full()`.
   */
  [[nodiscard]] constexpr bool full() const noexcept { return this->size() == this->capacity(); }

  // -------------------------------------------------------------------------
  // No copy, no move: permanently bound to the span it was constructed with.
  // -------------------------------------------------------------------------

  outline_vec_deque(const outline_vec_deque &) = delete;
  outline_vec_deque &operator=(const outline_vec_deque &) = delete;
  outline_vec_deque(outline_vec_deque &&) = delete;
  outline_vec_deque &operator=(outline_vec_deque &&) = delete;
};

/**
 * @brief Adapts `outline_vec_deque<T>` for collection views. `has_data` is
 * `false` because the queue might wrap around physically.
 */
template <typename T> struct collection_view_traits<reloco::outline_vec_deque<T>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = false;
  static constexpr bool is_mutable = true;

  static std::size_t size(const reloco::outline_vec_deque<T> &c) noexcept { return c.size(); }
  static bool empty(const reloco::outline_vec_deque<T> &c) noexcept { return c.empty(); }
  static T &at(reloco::outline_vec_deque<T> &c, std::size_t index) noexcept { return c[index]; }
  static const T &at(const reloco::outline_vec_deque<T> &c, std::size_t index) noexcept { return c[index]; }
};

/**
 * @brief Adapts `outline_vec_deque<T>` for `mutable_container_ref`.
 */
template <typename T> struct container_ref_traits<reloco::outline_vec_deque<T>> {
  using element_type = std::decay_t<T>;
  static constexpr bool is_associative = false;

  static std::size_t size(const reloco::outline_vec_deque<T> &c) noexcept { return c.size(); }
  static bool empty(const reloco::outline_vec_deque<T> &c) noexcept { return c.empty(); }
  static void clear(reloco::outline_vec_deque<T> &c) noexcept { c.clear(); }
  static T &at(reloco::outline_vec_deque<T> &c, std::size_t index) noexcept { return c[index]; }

  static result<void> try_push_back(reloco::outline_vec_deque<T> &c, T value) noexcept {
    return c.try_push_back(std::move(value));
  }

  static result<void> try_push_front(reloco::outline_vec_deque<T> &c, T value) noexcept {
    return c.try_push_front(std::move(value));
  }

  static result<void> try_insert_at(reloco::outline_vec_deque<T> &c, std::size_t index, T value) noexcept {
    return c.try_insert_at(index, std::move(value));
  }

  static result<void> try_erase_at(reloco::outline_vec_deque<T> &c, std::size_t index) noexcept {
    return c.try_erase_at(index);
  }
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
