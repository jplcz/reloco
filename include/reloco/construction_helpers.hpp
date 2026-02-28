#pragma once
#include <reloco/concepts.hpp>
#include <reloco/core.hpp>

namespace reloco {

struct construction_helpers {
  /**
   * @brief High-level in-place constructor dispatcher for Reloco-managed
   * memory.
   *
   * This utility provides a unified interface for initializing an object at a
   * specific memory location, resolving the most efficient construction
   * strategy at compile-time.
   *
   * @section Hierarchy Construction Hierarchy:
   * 1. **In-place Fallible**: If T satisfies 'has_try_construct', it
   * placement-news a shell and calls storage->try_construct(). This is the most
   * efficient (Zero-Copy) path.
   * 2. **Factory Fallible (Explicit)**: If T satisfies 'has_try_allocate', it
   * uses the factory to create a temporary and move-constructs it into storage.
   * 3. **Factory Fallible (Default)**: If T satisfies 'has_try_create', it uses
   * the default factory and move-constructs it into storage.
   * 4. **Standard Nothrow**: Falls back to standard placement-new using the
   * provided arguments.
   *
   * @note If any fallible stage fails, this helper ensures the memory is left
   * in a valid (destructed) state by invoking the destructor on the partially
   * initialized shell.
   *
   * @tparam T The type of object to construct at the storage location.
   * @tparam Args Variadic arguments to forward to the construction logic.
   *
   * @param alloc The allocator context to use for Tier 2 factory creation.
   * @param storage A pointer to valid, uninitialized memory of size >=
   * sizeof(T).
   * @param args Arguments to be forwarded.
   *
   * @return result<void> indicating success or a specific error on
   * failure.
   */
  template <typename T, typename... Args>
  static result<void> try_construct(fallible_allocator &alloc, T *storage,
                                    Args &&...args) {
    if constexpr (has_try_construct<T, Args...>) {
      static_assert(std::is_nothrow_default_constructible_v<T>,
                    "Two-phase construction requires a noexcept default "
                    "constructor for the shell.");
      new (storage) T();
      auto res = storage->try_construct(std::forward<Args>(args)...);
      if (!res) {
        storage->~T();
        return res;
      }
      return {};
    } else if constexpr (has_try_allocate<T, Args...>) {
      auto res = T::try_allocate(alloc, std::forward<Args>(args)...);
      if (!res)
        return unexpected(res.error());
      static_assert(std::is_nothrow_move_constructible_v<T>,
                    "Reloco requires noexcept move-construction.");
      new (storage) T(std::move(*res));
      return {};
    } else if constexpr (has_try_create<T, Args...>) {
      auto res = T::try_create(std::forward<Args>(args)...);
      if (!res)
        return unexpected(res.error());
      static_assert(std::is_nothrow_move_constructible_v<T>,
                    "Reloco requires noexcept move-construction.");
      new (storage) T(std::move(*res));
      return {};
    } else {
      static_assert(std::is_nothrow_constructible_v<T, Args...>,
                    "Type must be either fallible via try_construct, "
                    "try_allocate, try_create or nothrow "
                    "constructible");
      new (storage) T(std::forward<Args>(args)...);
      return {};
    }
  }

  /**
   * @brief High-level factory that produces a value of type T fallibly.
   * This helper resolves the best creation strategy to return a result<T>.
   * It prioritizes explicit allocation, then default creation, then two-phase
   * stack construction, and finally standard nothrow construction.
   * @tparam T The type to instantiate.
   * @param alloc The allocator context to provide if T supports
   * has_try_allocate.
   * @return A result<T> containing the initialized object or an error_code.
   */
  template <typename T, typename... Args>
  static result<T> try_allocate(fallible_allocator &alloc, Args &&...args) {
    static_assert(
        std::is_nothrow_move_constructible_v<T>,
        "Reloco requires noexcept move-construction to return values safely.");
    if constexpr (has_try_allocate<T, Args...>) {
      return T::try_allocate(alloc, std::forward<Args>(args)...);
    } else if constexpr (has_try_create<T, Args...>) {
      return T::try_create(std::forward<Args>(args)...);
    } else if constexpr (has_try_construct<T, Args...>) {
      static_assert(std::is_nothrow_default_constructible_v<T>,
                    "Two-phase construction requires a noexcept default "
                    "constructor for the shell.");
      T result;
      auto res = result.try_construct(std::forward<Args>(args)...);
      if (!res)
        return unexpected(res.error());
      return result;
    } else {
      static_assert(
          std::is_nothrow_constructible_v<T, Args...>,
          "Type must be either fallible (try_construct/allocate/create) "
          "or standard nothrow constructible.");
      return T(std::forward<Args>(args)...);
    }
  }

  /**
   * @brief Performs a fallible deep-copy, resolving the best strategy.
   * @section Hierarchy:
   * 1. **try_clone(alloc)**: Custom deep-copy with explicit memory context.
   * 2. **try_clone()**: Custom deep-copy (Self-contained/SOO).
   * 3. **try_allocate(alloc, source)**: Treat cloning as a new allocation from
   * source.
   * 4. **try_create(source)**: Treat cloning as a new creation from source
   * (Default pool).
   * 5. **Nothrow Copy**: Standard bitwise or member-wise copy.
   */
  template <typename T>
  static result<T> try_clone(fallible_allocator &alloc, const T &source) {
    static_assert(
        std::is_nothrow_move_constructible_v<T>,
        "Reloco requires noexcept move-construction to return values safely.");
    if constexpr (requires { source.try_clone(alloc); }) {
      return source.try_clone(alloc);
    } else if constexpr (requires { source.try_clone(); }) {
      return source.try_clone();
    } else if constexpr (has_try_allocate<T, const T &>) {
      return T::try_allocate(alloc, source);
    } else if constexpr (has_try_create<T, const T &>) {
      return T::try_create(source);
    } else {
      static_assert(
          std::is_nothrow_copy_constructible_v<T>,
          "Type must implement try_clone or be nothrow copy constructible.");
      return T(source);
    }
  }

  /**
   * @brief Performs a fallible deep-copy directly into uninitialized storage.
   * Priority:
   * 1. Specialized try_clone_at: Direct construction from source to storage.
   * 2. try_clone fallback: Create a value-result temporary and move-construct
   * it.
   */
  template <typename T>
  static result<void> try_clone_at(fallible_allocator &alloc, T *storage,
                                   const T &source) {
    static_assert(
        std::is_nothrow_move_constructible_v<T>,
        "Reloco requires noexcept move-construction for clone fallbacks.");

    if constexpr (has_try_clone_at<T>) {
      return T::try_clone_at(alloc, storage, source);
    } else {
      auto res = try_clone<T>(alloc, source);
      if (!res) {
        return unexpected(res.error());
      }

      new (storage) T(std::move(*res));
      return {};
    }
  }
};

} // namespace reloco
