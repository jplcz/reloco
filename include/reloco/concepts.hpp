#pragma once
#include <reloco/core.hpp>
#include <type_traits>

namespace reloco {

/**
 * @brief Concept for types providing a static factory method using a default
 * allocator.
 * ``has_try_create`` marks types that can be instantiated without an explicit
 * allocator reference, typically by delegating to a global or thread-local
 * fallible_allocator.
 *
 * @tparam T The type to be created.
 * @tparam Args Constructor arguments to be forwarded to try_create.
 * @return Must return a reloco::result<T>.
 */
template <typename T, typename... Args>
concept has_try_create = requires(Args &&...args) {
  { T::try_create(std::forward<Args>(args)...) } -> std::same_as<result<T>>;
};

/**
 * @brief Concept for types providing a static factory method with explicit
 * memory context.
 * ``has_try_allocate`` is the preferred pattern for kernel subsystems where
 * memory residency (e.g., Paged vs. Non-Paged pools) must be strictly
 * controlled by the caller.
 * @tparam T The type to be created.
 * @tparam Args Constructor arguments to be forwarded to try_allocate.
 * @return Must return a reloco::result<T>.
 */
template <typename T, typename... Args>
concept has_try_allocate = requires(fallible_allocator &alloc, Args &&...args) {
  {
    T::try_allocate(alloc, std::forward<Args>(args)...)
  } -> std::same_as<result<T>>;
};

/**
 * @brief Concept for types supporting two-phase, in-place fallible
 * construction.
 * ``has_try_construct`` enables zero-copy initialization. The object is first
 * placement-newed into a "shell" state (requiring a noexcept default
 * constructor), followed by a call to this method to perform fallible logic
 * (e.g., resource acquisition).
 * @note If try_construct fails, the caller is responsible for invoking the
 * destructor on the shell object before reclaiming the raw memory.
 * @tparam T The type being initialized.
 * @tparam Args Arguments for the second phase of initialization.
 * @return Must return a reloco::result<void> to signal completion of the shell.
 */
template <typename T, typename... Args>
concept has_try_construct = requires(T *storage, Args &&...args) {
  {
    storage->try_construct(std::forward<Args>(args)...)
  } -> std::same_as<result<void>>;
};

template <typename T>
concept has_try_clone = requires(const T &source, fallible_allocator &alloc) {
  // Allocator-aware clone (e.g., containers)
  { source.try_clone(alloc) } -> std::same_as<result<T>>;
} || requires(const T &source) {
  // Self-contained clone (e.g., simple objects)
  { source.try_clone() } -> std::same_as<result<T>>;
};

// General trait for "Fallible Types"
template <typename T>
struct is_fallible_type : std::bool_constant<has_try_clone<T>> {};

// Concept for types that support the reloco fallible pattern
template <typename T>
concept is_fallible_initializable = requires {
  typename T::reloco_fallible_t; // Concept only checks for this name
};

/**
 * @brief Concept for types providing an optimized in-place clone.
 * Requirements:
 * - Takes an allocator for nested resources.
 * - Takes a raw pointer to uninitialized destination storage.
 * - Takes a reference to the source object to be cloned.
 */
template <typename T>
concept has_try_clone_at =
    requires(fallible_allocator &alloc, T *storage, const T &source) {
      { T::try_clone_at(alloc, storage, source) } -> std::same_as<result<void>>;
    };

} // namespace reloco
