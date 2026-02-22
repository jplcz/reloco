#pragma once
#include <memory>
#include <reloco/concepts.hpp>
#include <reloco/core.hpp>
#include <utility>

namespace reloco {

template <typename T, typename Element>
concept is_fallible_collection_view = requires(const T &v, size_t i) {
  { v.size() } -> std::same_as<size_t>;
  { v.empty() } -> std::same_as<bool>;
  // Fallible
  {
    v.try_at(i)
  } -> std::same_as<result<std::reference_wrapper<const Element>>>;
  { v.try_data() } -> std::same_as<result<const Element *>>;
  // Using RELOCO_ASSERT
  { v.at(i) } -> std::same_as<const Element &>;
  { v.data() } -> std::same_as<const Element *>;
  // Unsafe
  { v.unsafe_at(i) } -> std::same_as<const Element &>;
  { v.unsafe_data() } -> std::same_as<const Element *>;
};

template <typename T, typename Element>
concept is_mutable_fallible_collection_view =
    is_fallible_collection_view<T, Element> &&
    requires(T &v, size_t i, Element &&item) {
      // Fallible
      { v.try_at(i) } -> std::same_as<result<std::reference_wrapper<Element>>>;
      { v.try_data() } -> std::same_as<result<Element *>>;
      // Using RELOCO_ASSERT
      { v.at(i) } -> std::same_as<Element &>;
      { v.data() } -> std::same_as<Element *>;
      // Unsafe
      { v.unsafe_at(i) } -> std::same_as<Element &>;
      { v.unsafe_data() } -> std::same_as<Element *>;
      // Modification
      { v.try_push_back(std::move(item)) } -> std::same_as<result<Element *>>;
      { v.try_reserve(i) } -> std::same_as<result<void>>;
      { v.try_erase(i) } -> std::same_as<result<void>>;
      { v.clear() } -> std::same_as<void>;
    };

} // namespace reloco
