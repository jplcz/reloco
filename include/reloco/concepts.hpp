#pragma once
#include <reloco/core.hpp>
#include <type_traits>

namespace reloco {

// Detect if T has: static result<T> try_create(Args...)
template <typename T, typename... Args>
concept has_try_create = requires(Args &&...args) {
  { T::try_create(std::forward<Args>(args)...) } -> std::same_as<result<T>>;
};

template <typename T, typename... Args>
concept has_try_allocate = requires(fallible_allocator &alloc, Args &&...args) {
  {
    T::try_allocate(alloc, std::forward<Args>(args)...)
  } -> std::same_as<result<T>>;
};

// Detect if T has: result<T> try_clone() const
template <typename T>
concept has_try_clone = requires(const T &obj) {
  { obj.try_clone() } -> std::same_as<result<T>>;
};

// General trait for "Fallible Types"
template <typename T>
struct is_fallible_type : std::bool_constant<has_try_clone<T>> {};

// Concept for types that support the reloco fallible pattern
template <typename T>
concept is_fallible_initializable = requires {
  typename T::reloco_fallible_t; // Concept only checks for this name
};

} // namespace reloco
