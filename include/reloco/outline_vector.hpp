// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file outline_vector.hpp
 * @brief Non-owning, growable-up-to-capacity dynamic array over a
 * caller-supplied byte buffer, with fallible mutation.
 *
 * `outline_vector<T>` is `vector<T>`'s (see `vector.hpp`) non-owning
 * counterpart: instead of an `allocator_ref`-backed heap allocation or an
 * embedded `inline_vector<T, Capacity>`-style buffer, its elements live in
 * a `span<std::byte>` the caller passes in at construction and continues
 * to own -- `outline_vector<T>` never allocates, never deallocates, and
 * never frees that memory itself. This is the shape needed wherever the
 * backing bytes come from somewhere `reloco` doesn't control (a memory-mapped
 * region, a hardware DMA buffer, a slab handed out by an arena the caller
 * manages directly, ...), but the caller still wants `vector<T>`'s fallible
 * `try_*` mutation/access surface over it instead of hand-rolled placement-new
 * bookkeeping.
 *
 * The bound span is captured exactly once, at construction, and is
 * afterwards immutable: there is no default constructor, no rebind/reset
 * method, and -- unlike every other reloco vector flavor -- no move
 * constructor or move assignment operator either. There is no well-defined
 * way to "steal" a borrowed span out from under whoever actually owns it,
 * the way `vector<T>`'s move constructor steals an owned heap allocation or
 * `inline_vector<T, Capacity>`'s relocates an embedded buffer's contents
 * into another object's own embedded buffer; `outline_vector<T>` is
 * therefore permanently pinned to the object it was constructed on, exactly
 * like `mutable_sequence_container_ref<T>`/`function_ref<Signature>` are
 * permanently bound to whatever they were constructed to reference (see
 * `container_ref.hpp`/`function_ref.hpp`). `reloco::is_trivially_relocatable`
 * is deliberately *not* specialized for `outline_vector<T>`: the primary
 * template's `std::is_trivially_copyable<T>` fallback already evaluates to
 * `false` (copy is deleted), correctly reporting that `outline_vector<T>`
 * may neither be moved nor relocated by any means.
 *
 * `try_reserve`/`try_emplace_back`/`try_push_back`/`try_insert_at`/
 * `try_resize` grow only up to the bound span's capacity
 * (`storage.size() / sizeof(T)`, computed once at construction and fixed
 * for the object's entire lifetime) and fail with `error::capacity_exceeded`
 * beyond that, exactly like `inline_vector<T, Capacity>` -- there is no
 * allocator to spill onto. Element construction/cloning go through
 * `construction_helpers` (see `construction_helpers.hpp`) exactly like
 * `vector<T>`/`inline_vector<T, Capacity>`, passing `default_allocator()`
 * as the allocator argument those helpers require -- `outline_vector<T>`
 * itself never allocates, but a nested `T` that implements its own fallible
 * construction protocol still needs an `allocator_ref` to hand to its own
 * allocation.
 *
 * The bound span must already be sized and aligned for `T`:
 * `storage.data()` is asserted (`RELOCO_ASSERT`, active even when `NDEBUG`
 * is defined, see `docs/hardened-containers.md`) to satisfy
 * `effective_alignment_v<T>` (see `alignment.hpp`) at construction time --
 * unlike `allocator_ref::allocate`, there is no allocator here to request a
 * specific alignment from, so the caller is responsible for over-aligning
 * the buffer up front (e.g. via `alignas(reloco::effective_alignment_v<T>)`
 * on a local/static/heap buffer, or an alignment-aware arena allocation).
 *
 * Read-only/mutating access to already-owned elements follows the same
 * checked/`try_*`/`unsafe_*` tri-tier convention as `vector`/`inline_vector`/
 * `span`/`array` (see `docs/hardened-containers.md`). There is no
 * `try_create`/`try_allocate`/`try_clone`/`try_clone_at` fallible
 * construction protocol: binding a caller-owned span can never itself fail
 * (there is nothing to allocate), so the plain constructor is sufficient,
 * and a fallible deep copy would need a *second* caller-owned destination
 * span the type has no way to ask for on its own.
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
#include <iterator>
#include <type_traits>
#include <utility>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename T>
class RELOCO_POINTER outline_vector : public detail::typed_vector_base<T, detail::outline_vector_base> {
  using base_t = detail::typed_vector_base<T, detail::outline_vector_base>;

public:
  using typename base_t::allocator_type;
  using typename base_t::const_iterator;
  using typename base_t::const_pointer;
  using typename base_t::const_reference;
  using typename base_t::difference_type;
  using typename base_t::iterator;
  using typename base_t::pointer;
  using typename base_t::reference;
  using typename base_t::size_type;
  using typename base_t::value_type;

  // -------------------------------------------------------------------------
  // Constructor & Destructor
  // -------------------------------------------------------------------------

  /**
   * @brief Binds this outline_vector to a caller-owned byte span, sized and
   * aligned for `T`. The span is captured exactly once and is never
   * rebound, reallocated, or resized past `storage.size() / sizeof(T)`
   * elements afterwards; `storage` itself must remain valid, untouched by
   * the caller, and stable in address for the entire lifetime of `*this`.
   *
   * @param storage Byte-addressed backing storage. Must satisfy
   * `effective_alignment_v<T>` (checked via `RELOCO_ASSERT`) and outlive
   * `*this`.
   */
  constexpr explicit outline_vector(
      span<std::byte> storage RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : base_t(storage.data(), storage.size() / sizeof(T)) {
    RELOCO_ASSERT(reinterpret_cast<std::uintptr_t>(storage.data()) % effective_alignment_v<T> == 0,
                  "outline_vector: storage span is not aligned for T");
  }

  /**
   * @brief Destructor. Destroys any live elements; never touches the
   * caller-owned backing storage itself (it is not this object's to free).
   */
  ~outline_vector() noexcept { this->destroy_elements(detail::get_operations_for<T>(), detail::metadata_for<T>); }

  /**
   * @brief `true` once `size() == capacity()` -- the bound span has no
   * room left, matching `inline_vector<T, Capacity>::full()`.
   */
  [[nodiscard]] constexpr bool full() const noexcept { return this->size() == this->capacity(); }

  // -------------------------------------------------------------------------
  // No copy, no move: permanently bound to the span it was constructed with.
  // -------------------------------------------------------------------------

  outline_vector(const outline_vector &) = delete;
  outline_vector &operator=(const outline_vector &) = delete;
  outline_vector(outline_vector &&) = delete;
  outline_vector &operator=(outline_vector &&) = delete;
};

/**
 * @brief Adapts `outline_vector<T>` for the collection views.
 */
template <typename T> struct collection_view_traits<reloco::outline_vector<T>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = true;
  static constexpr bool is_mutable = true;

  static std::size_t size(const reloco::outline_vector<T> &c) noexcept { return c.size(); }
  static bool empty(const reloco::outline_vector<T> &c) noexcept { return c.empty(); }
  static T &at(reloco::outline_vector<T> &c, std::size_t index) noexcept { return c[index]; }
  static const T &at(const reloco::outline_vector<T> &c, std::size_t index) noexcept { return c[index]; }
  static T *data(reloco::outline_vector<T> &c) noexcept { return c.data(); }
  static const T *data(const reloco::outline_vector<T> &c) noexcept { return c.data(); }
};

template <typename T> struct container_ref_traits<reloco::outline_vector<T>> {
  using element_type = T;
  static constexpr bool is_associative = false;

  static std::size_t size(const reloco::outline_vector<T> &c) noexcept { return c.size(); }
  static bool empty(const reloco::outline_vector<T> &c) noexcept { return c.empty(); }
  static void clear(reloco::outline_vector<T> &c) noexcept { c.clear(); }
  static T &at(reloco::outline_vector<T> &c, std::size_t index) noexcept { return c[index]; }

  static result<void> try_push_back(reloco::outline_vector<T> &c, T value) noexcept {
    auto res = c.try_push_back(std::move(value));
    if (!res)
      return unexpected(res.error());
    return {};
  }

  static result<void> try_push_front(reloco::outline_vector<T> &c, T value) noexcept {
    auto res = c.try_insert_at(0, std::move(value));
    if (!res)
      return unexpected(res.error());
    return {};
  }

  static result<void> try_insert_at(reloco::outline_vector<T> &c, std::size_t index, T value) noexcept {
    auto res = c.try_insert_at(index, std::move(value));
    if (!res)
      return unexpected(res.error());
    return {};
  }

  static result<void> try_erase_at(reloco::outline_vector<T> &c, std::size_t index) noexcept {
    return c.try_erase_at(index);
  }
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
