// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file allocator.hpp
 * @brief Type-erased allocator provider.
 *
 * Built on the tag + `*_traits<Tag>` + `context_type` provider pattern (see
 * docs/extending.md, "Context_type-based provider template", adapted from
 * microfmt). Unlike `reloco_legacy`'s `fallible_allocator`, which forced
 * every backend to inherit from a pure-virtual base, a concrete allocator
 * backend here is just an empty tag plus an `allocator_traits<Tag>`
 * specialization: no virtual base class, no vtable-carrying inheritance, no
 * allocation, and no runtime registration.
 *
 * Every operational method on @ref allocator_ref (`allocate`, `deallocate`,
 * `expand_in_place`, `reallocate`, `advise`) is `RELOCO_UNSAFE_BUFFER_USAGE`:
 * there is no checked/bounds-tracked alternative for raw memory management
 * the way `span`/`array` offer `at()` alongside `unsafe_at()`, so the entire
 * interface is callable only from inside a
 * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE` block.
 *
 * Every operation returns `reloco::result<T>` (`expected<T, reloco::error>`,
 * see `error.hpp`) rather than a dedicated `allocator_error` enum: its two
 * failure modes, `allocation_failed` and `unsupported_operation`, are
 * already exactly what `reloco::error` provides, so introducing a
 * single-purpose enum here would only duplicate it.
 */

#include "detail/compat.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "value_ptr.hpp"
#include "value_ref.hpp"
#include <cstddef>
#include <type_traits>
#include <utility>

// The whole allocator interface deals in raw sized/aligned pointers (mem_block,
// void* ctx recovery, raw allocate/deallocate/reallocate/advise signatures),
// so it is treated as a single checked boundary, like span.hpp/array.hpp.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief Represents a raw memory block with its pointer and actual allocated size.
 *
 * Returned by allocator backends and type-erased allocator handles. The `size`
 * field represents the **actual** available capacity of the block, which is
 * guaranteed to be greater than or equal to the originally requested size
 * (e.g., due to alignment padding, slab binning, or page-size rounding).
 */
struct [[nodiscard]] mem_block {
  /** Pointer to the start of the allocated memory block, or `nullptr` if empty. */
  void *ptr;
  /** Actual capacity of the memory block in bytes (guaranteed to be >= requested size). */
  std::size_t size;
};

enum class usage_hint {
  normal,     // Default behavior.
  sequential, // Expecting to read from start to finish.
  random,     // No predictable pattern (disables aggressive prefetching).
  will_need,  // Load these pages into RAM now (async prefetch).
  dont_need,  // Can be reclaimed by the OS if memory is tight (soft free).
  cold,       // This memory is unlikely to be touched soon (swap priority).
  huge_pages, // Attempt to back with transparent huge pages (THP).
};

/**
 * @brief Static customization point describing an allocator backend.
 *
 * Specialize for a tag type to provide `context_type`, `allocate`, and
 * `deallocate`. Add `expand_in_place`, `reallocate`, and/or `advise` to
 * expose those optional operations; omitting one makes it report as
 * unsupported through @ref allocator_ref (via `can_expand_in_place()` /
 * `can_reallocate()` / `can_advise()`) instead of failing at compile time.
 *
 * For a stateless backend (answers purely from global/static state, e.g. the
 * process heap), declare `using context_type = void;` and drop the
 * `value_ref<context_type>` parameter from every operation.
 *
 * Contract:
 * - Functions returning `result<mem_block>` (`allocate`, `reallocate`) are
 *   guaranteed to return a block whose `size` is **at least** the requested
 *   `bytes` size (e.g., alignment padding, page sizing, or SLAB binning).
 *
 * @tparam Tag Tag identifying the allocator implementation.
 *
 */
template <typename Tag> struct allocator_traits;

// --- Mandatory Operations ---

/**
 * @brief Allocates a memory block of at least `bytes` with the specified `alignment`.
 * @return result<mem_block> containing the pointer and the *actual* allocated size (>= bytes).
 */
// static constexpr result<mem_block> allocate(std::size_t bytes, std::size_t alignment) noexcept;
// Stateful overload: static constexpr result<mem_block> allocate(context_type &ctx, std::size_t bytes, std::size_t
// alignment) noexcept;

/**
 * @brief Deallocates a previously allocated block.
 */
// static constexpr void deallocate(void *ptr, std::size_t bytes) noexcept;
// Stateful overload: static constexpr void deallocate(context_type &ctx, void *ptr, std::size_t bytes) noexcept;

// --- Optional Operations (can be omitted or defaulted) ---

/**
 * @brief Attempts to expand a block in-place without moving its address.
 * @return result<std::size_t> representing the new total size, or an error if it failed.
 */
// static constexpr result<std::size_t> expand_in_place(void *ptr, std::size_t old_size, std::size_t new_size) noexcept;

/**
 * @brief Reallocates a block, potentially moving it and changing its size/alignment.
 * @return result<mem_block> with the new block (size >= new_size).
 */
// static constexpr result<mem_block> reallocate(void *ptr, std::size_t old_size, std::size_t new_size, std::size_t
// alignment) noexcept;

/**
 * @brief Provides memory usage hints to the backend/OS (e.g., madvise).
 */
// static constexpr void advise(void *ptr, std::size_t bytes, usage_hint hint) noexcept;

namespace detail {

template <typename Tag, typename = void> struct has_allocator_expand_in_place : std::false_type {};

template <typename Tag>
struct has_allocator_expand_in_place<
    Tag, std::void_t<decltype(allocator_traits<Tag>::expand_in_place(
             std::declval<value_ref<typename allocator_traits<Tag>::context_type>>(), std::declval<void *>(),
             std::declval<std::size_t>(), std::declval<std::size_t>()))>>
    : std::is_same<decltype(allocator_traits<Tag>::expand_in_place(
                       std::declval<value_ref<typename allocator_traits<Tag>::context_type>>(), std::declval<void *>(),
                       std::declval<std::size_t>(), std::declval<std::size_t>())),
                   result<std::size_t>> {};

template <typename Tag, typename = void> struct has_stateless_allocator_expand_in_place : std::false_type {};

template <typename Tag>
struct has_stateless_allocator_expand_in_place<
    Tag, std::void_t<decltype(allocator_traits<Tag>::expand_in_place(
             std::declval<void *>(), std::declval<std::size_t>(), std::declval<std::size_t>()))>>
    : std::is_same<decltype(allocator_traits<Tag>::expand_in_place(std::declval<void *>(), std::declval<std::size_t>(),
                                                                   std::declval<std::size_t>())),
                   result<std::size_t>> {};

template <typename Tag, typename = void> struct has_allocator_reallocate : std::false_type {};

template <typename Tag>
struct has_allocator_reallocate<
    Tag, std::void_t<decltype(allocator_traits<Tag>::reallocate(
             std::declval<value_ref<typename allocator_traits<Tag>::context_type>>(), std::declval<void *>(),
             std::declval<std::size_t>(), std::declval<std::size_t>(), std::declval<std::size_t>()))>>
    : std::is_same<decltype(allocator_traits<Tag>::reallocate(
                       std::declval<value_ref<typename allocator_traits<Tag>::context_type>>(), std::declval<void *>(),
                       std::declval<std::size_t>(), std::declval<std::size_t>(), std::declval<std::size_t>())),
                   result<mem_block>> {};

template <typename Tag, typename = void> struct has_stateless_allocator_reallocate : std::false_type {};

template <typename Tag>
struct has_stateless_allocator_reallocate<Tag, std::void_t<decltype(allocator_traits<Tag>::reallocate(
                                                   std::declval<void *>(), std::declval<std::size_t>(),
                                                   std::declval<std::size_t>(), std::declval<std::size_t>()))>>
    : std::is_same<decltype(allocator_traits<Tag>::reallocate(std::declval<void *>(), std::declval<std::size_t>(),
                                                              std::declval<std::size_t>(),
                                                              std::declval<std::size_t>())),
                   result<mem_block>> {};

template <typename Tag, typename = void> struct has_allocator_advise : std::false_type {};

template <typename Tag>
struct has_allocator_advise<Tag, std::void_t<decltype(allocator_traits<Tag>::advise(
                                     std::declval<value_ref<typename allocator_traits<Tag>::context_type>>(),
                                     std::declval<void *>(), std::declval<std::size_t>(), std::declval<usage_hint>()))>>
    : std::true_type {};

template <typename Tag, typename = void> struct has_stateless_allocator_advise : std::false_type {};

template <typename Tag>
struct has_stateless_allocator_advise<
    Tag, std::void_t<decltype(allocator_traits<Tag>::advise(std::declval<void *>(), std::declval<std::size_t>(),
                                                            std::declval<usage_hint>()))>> : std::true_type {};

} // namespace detail

/**
 * @brief Type-erased, two-word handle to an allocator backend.
 *
 * Packs a context pointer and a vtable pointer into two words: no virtual
 * base class, no RTTI, and no allocation of its own. The bound context (for
 * stateful tags) must outlive every `allocator_ref` built from it.
 */
class RELOCO_POINTER allocator_ref {
public:
  struct vtable {
    result<mem_block> (*allocate)(void *ctx, std::size_t bytes, std::size_t alignment) noexcept;
    result<std::size_t> (*expand_in_place)(void *ctx, void *ptr, std::size_t old_size, std::size_t new_size) noexcept;
    result<mem_block> (*reallocate)(void *ctx, void *ptr, std::size_t old_size, std::size_t new_size,
                                    std::size_t alignment) noexcept;
    void (*deallocate)(void *ctx, void *ptr, std::size_t bytes) noexcept;
    void (*advise)(void *ctx, void *ptr, std::size_t bytes, usage_hint hint) noexcept;
  };

  constexpr allocator_ref() noexcept = default;

  /**
   * @brief Constructs a handle for a stateless allocator tag.
   * @tparam Tag Allocator tag type.
   * @tparam Traits Specialized traits, enabled when `context_type` is `void`.
   */
  template <typename Tag, typename Traits = allocator_traits<Tag>,
            std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit allocator_ref(Tag) noexcept : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  /**
   * @brief Constructs a handle for a stateful allocator tag.
   * @tparam Tag Allocator tag type.
   * @tparam Context Concrete context type.
   * @tparam Traits Specialized traits, enabled when `context_type` is
   * non-void and @p Context converts to it.
   * @param ctx Context object backing the allocations.
   */
  template <typename Tag, typename Context, typename Traits = allocator_traits<Tag>,
            std::enable_if_t<!std::is_void_v<typename Traits::context_type> &&
                                 std::is_convertible_v<Context *, typename Traits::context_type *>,
                             int> = 0>
  constexpr allocator_ref(Tag, Context &ctx RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  template <typename Tag, typename Context, std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  constexpr allocator_ref(Tag, Context &&) = delete;

  /**
   * @brief Allocates a block of at least `bytes` size, aligned to
   * `alignment`.
   *
   * Marked `RELOCO_UNSAFE_BUFFER_USAGE`: the returned block is a raw,
   * unchecked `void*` + size pair with no bounds tracking of its own, so
   * every call site must be wrapped in
   * `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE`,
   * making the opt-in to raw memory management explicit and greppable.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE result<mem_block> allocate(std::size_t bytes,
                                                                      std::size_t alignment) const noexcept {
    if (!vtbl_)
      return unexpected(error::unsupported_operation);
    return vtbl_->allocate(ctx_.get(), bytes, alignment);
  }

  /**
   * @brief Releases a block previously returned by `allocate`/`reallocate`.
   * See @ref allocate for why this is `RELOCO_UNSAFE_BUFFER_USAGE`.
   */
  RELOCO_UNSAFE_BUFFER_USAGE void deallocate(void *ptr, std::size_t bytes) const noexcept {
    if (vtbl_)
      vtbl_->deallocate(ctx_.get(), ptr, bytes);
  }

  /**
   * @brief Reports whether the bound backend supports in-place growth.
   */
  [[nodiscard]] constexpr bool can_expand_in_place() const noexcept { return vtbl_ && vtbl_->expand_in_place; }

  /**
   * @brief Attempts to grow a block in place, without moving it.
   * See @ref allocate for why this is `RELOCO_UNSAFE_BUFFER_USAGE`.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE result<std::size_t> expand_in_place(void *ptr, std::size_t old_size,
                                                                               std::size_t new_size) const noexcept {
    if (!can_expand_in_place())
      return unexpected(error::unsupported_operation);
    return vtbl_->expand_in_place(ctx_.get(), ptr, old_size, new_size);
  }

  /**
   * @brief Reports whether the bound backend supports reallocation.
   */
  [[nodiscard]] constexpr bool can_reallocate() const noexcept { return vtbl_ && vtbl_->reallocate; }

  /**
   * @brief Resizes a block, possibly moving it.
   * See @ref allocate for why this is `RELOCO_UNSAFE_BUFFER_USAGE`.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE result<mem_block>
  reallocate(void *ptr, std::size_t old_size, std::size_t new_size, std::size_t alignment) const noexcept {
    if (!can_reallocate())
      return unexpected(error::unsupported_operation);
    return vtbl_->reallocate(ctx_.get(), ptr, old_size, new_size, alignment);
  }

  /**
   * @brief Reports whether the bound backend accepts usage advice.
   */
  [[nodiscard]] constexpr bool can_advise() const noexcept { return vtbl_ && vtbl_->advise; }

  /**
   * @brief Hints at the intended access pattern for a block.
   * See @ref allocate for why this is `RELOCO_UNSAFE_BUFFER_USAGE`.
   */
  RELOCO_UNSAFE_BUFFER_USAGE void advise(void *ptr, std::size_t bytes, usage_hint hint) const noexcept {
    if (can_advise())
      vtbl_->advise(ctx_.get(), ptr, bytes, hint);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

private:
  template <typename Tag>
  static result<mem_block> allocate_entry(void *ctx, std::size_t bytes, std::size_t alignment) noexcept {
    using context_type = typename allocator_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      (void)ctx;
      return allocator_traits<Tag>::allocate(bytes, alignment);
    } else {
      auto &typed = *static_cast<context_type *>(ctx);
      return allocator_traits<Tag>::allocate(value_ref<context_type>(typed), bytes, alignment);
    }
  }

  template <typename Tag> static void deallocate_entry(void *ctx, void *ptr, std::size_t bytes) noexcept {
    using context_type = typename allocator_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      (void)ctx;
      allocator_traits<Tag>::deallocate(ptr, bytes);
    } else {
      auto &typed = *static_cast<context_type *>(ctx);
      allocator_traits<Tag>::deallocate(value_ref<context_type>(typed), ptr, bytes);
    }
  }

  template <typename Tag> [[nodiscard]] static constexpr auto expand_in_place_entry() noexcept {
    using context_type = typename allocator_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      if constexpr (detail::has_stateless_allocator_expand_in_place<Tag>::value) {
        return +[](void *, void *ptr, std::size_t old_size, std::size_t new_size) noexcept {
          return allocator_traits<Tag>::expand_in_place(ptr, old_size, new_size);
        };
      } else {
        return static_cast<result<std::size_t> (*)(void *, void *, std::size_t, std::size_t) noexcept>(nullptr);
      }
    } else if constexpr (detail::has_allocator_expand_in_place<Tag>::value) {
      return +[](void *ctx, void *ptr, std::size_t old_size, std::size_t new_size) noexcept {
        auto &typed = *static_cast<context_type *>(ctx);
        return allocator_traits<Tag>::expand_in_place(value_ref<context_type>(typed), ptr, old_size, new_size);
      };
    } else {
      return static_cast<result<std::size_t> (*)(void *, void *, std::size_t, std::size_t) noexcept>(nullptr);
    }
  }

  template <typename Tag> [[nodiscard]] static constexpr auto reallocate_entry() noexcept {
    using context_type = typename allocator_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      if constexpr (detail::has_stateless_allocator_reallocate<Tag>::value) {
        return +[](void *, void *ptr, std::size_t old_size, std::size_t new_size, std::size_t alignment) noexcept {
          return allocator_traits<Tag>::reallocate(ptr, old_size, new_size, alignment);
        };
      } else {
        return static_cast<result<mem_block> (*)(void *, void *, std::size_t, std::size_t, std::size_t) noexcept>(
            nullptr);
      }
    } else if constexpr (detail::has_allocator_reallocate<Tag>::value) {
      return +[](void *ctx, void *ptr, std::size_t old_size, std::size_t new_size, std::size_t alignment) noexcept {
        auto &typed = *static_cast<context_type *>(ctx);
        return allocator_traits<Tag>::reallocate(value_ref<context_type>(typed), ptr, old_size, new_size, alignment);
      };
    } else {
      return static_cast<result<mem_block> (*)(void *, void *, std::size_t, std::size_t, std::size_t) noexcept>(
          nullptr);
    }
  }

  template <typename Tag> [[nodiscard]] static constexpr auto advise_entry() noexcept {
    using context_type = typename allocator_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      if constexpr (detail::has_stateless_allocator_advise<Tag>::value) {
        return +[](void *, void *ptr, std::size_t bytes, usage_hint hint) noexcept {
          allocator_traits<Tag>::advise(ptr, bytes, hint);
        };
      } else {
        return static_cast<void (*)(void *, void *, std::size_t, usage_hint) noexcept>(nullptr);
      }
    } else if constexpr (detail::has_allocator_advise<Tag>::value) {
      return +[](void *ctx, void *ptr, std::size_t bytes, usage_hint hint) noexcept {
        auto &typed = *static_cast<context_type *>(ctx);
        allocator_traits<Tag>::advise(value_ref<context_type>(typed), ptr, bytes, hint);
      };
    } else {
      return static_cast<void (*)(void *, void *, std::size_t, usage_hint) noexcept>(nullptr);
    }
  }

  template <typename Tag>
  static constexpr vtable s_vtbl{&allocate_entry<Tag>, expand_in_place_entry<Tag>(), reallocate_entry<Tag>(),
                                 &deallocate_entry<Tag>, advise_entry<Tag>()};

  value_ptr<void> ctx_{};
  const vtable *vtbl_{nullptr};
};

/**
 * @brief Owning wrapper that holds a stateful backend's `context_type` by
 * value, so the context and the handle derived from it share a single
 * object's lifetime.
 */
template <typename Tag, bool Stateless = std::is_void_v<typename allocator_traits<Tag>::context_type>> class allocator;

template <typename Tag> class RELOCO_OWNER allocator<Tag, false> {
public:
  using traits_type = allocator_traits<Tag>;
  using context_type = typename traits_type::context_type;

  constexpr explicit allocator(context_type context) noexcept : context_(std::move(context)) {}

  [[nodiscard]] constexpr value_ref<context_type> context() & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<context_type>(context_);
  }

  [[nodiscard]] constexpr value_ref<const context_type> context() const & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<const context_type>(context_);
  }

  [[nodiscard]] constexpr allocator_ref ref() & noexcept RELOCO_LIFETIMEBOUND { return allocator_ref(Tag{}, context_); }

  value_ref<context_type> context() && = delete;
  value_ref<const context_type> context() const && = delete;
  allocator_ref ref() && = delete;

private:
  context_type context_;
};

/**
 * @brief Owning wrapper specialization for stateless backends: no context is
 * stored since every operation answers purely from global/static state.
 */
template <typename Tag> class allocator<Tag, true> {
public:
  [[nodiscard]] static constexpr allocator_ref ref() noexcept { return allocator_ref(Tag{}); }
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
