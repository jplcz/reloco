#pragma once
#include <reloco/core.hpp>
#include <type_traits>

namespace reloco {

// Detect if T has: static result<T> try_create(Args...)
template <typename T, typename... Args>
concept has_try_create = requires(Args &&...args) {
  { T::try_create(std::forward<Args>(args)...) } -> std::same_as<result<T>>;
};

// Detect if T has: result<T> try_clone() const
template <typename T>
concept has_try_clone = requires(const T &obj) {
  { obj.try_clone() } -> std::same_as<result<T>>;
};

// General trait for "Fallible Types"
template <typename T>
struct is_fallible_type : std::bool_constant<has_try_clone<T>> {};

} // namespace reloco
