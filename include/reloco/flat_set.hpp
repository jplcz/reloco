#pragma once
#include <algorithm>
#include <iterator>
#include <reloco/collection_view.hpp>
#include <reloco/rvalue_safety.hpp>
#include <reloco/vector.hpp>

namespace reloco {

template <typename T, typename Compare = std::less<T>> class flat_set {
  vector<T> m_data;
  Compare m_comp;

public:
  RELOCO_BLOCK_RVALUE_ACCESS(T);

  static result<flat_set> try_allocate(fallible_allocator &alloc,
                                       size_t initial_capacity = 0) {
    auto vec_res = vector<T>::try_allocate(alloc, initial_capacity);
    if (!vec_res)
      return unexpected(vec_res.error());
    return flat_set(std::move(*vec_res));
  }

  static result<flat_set> try_create(size_t initial_capacity = 0) {
    return try_allocate(get_default_allocator(), initial_capacity);
  }

  size_t size() const noexcept { return m_data.size(); }
  void clear() noexcept { m_data.clear(); }

  result<T *> try_insert(T &&value) & noexcept {
    auto it = find_pos(value);
    if (it != m_data.end() && !m_comp(value, *it)) {
      return unexpected(error::already_exists);
    }
    const auto index = std::distance(std::as_const(m_data).begin(), it);
    RELOCO_ASSERT(index >= 0);
    return m_data.try_insert((size_t)index, std::forward<T>(value));
  }

  template <typename Key> bool contains(const Key &value) const noexcept {
    auto it = find_pos(value);
    return it != m_data.end() && !m_comp(value, *it);
  }

  template <typename Key>
  result<std::reference_wrapper<const T>>
  try_find(const Key &value) const & noexcept {
    auto it = find_pos(value);
    if (it != m_data.end() && !m_comp(value, *it)) {
      return std::cref(*it);
    }
    return unexpected(error::not_found);
  }

  /**
   * Construct non-owning view of this flat map
   */
  result<any_view<T>> as_view() & noexcept { return m_data.as_view(); }

  /**
   * @brief Performs a deep copy of the set using a specific allocator.
   */
  result<flat_set> try_clone(fallible_allocator &alloc) const noexcept {
    flat_set result;

    auto cloned_data = m_data.try_clone(alloc);
    if (!cloned_data)
      return unexpected(cloned_data.error());

    result.m_data = std::move(*cloned_data);
    return result;
  }

  result<flat_set> try_clone() const noexcept {
    return try_clone(*m_data.get_allocator());
  }

private:
  template <typename Key> auto find_pos(const Key &value) const noexcept {
    return std::lower_bound(m_data.begin(), m_data.end(), value, m_comp);
  }

  flat_set(vector<T> &&vec) noexcept : m_data(std::move(vec)) {}
};

} // namespace reloco
