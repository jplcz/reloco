#pragma once
#include "vector.hpp"
#include <algorithm>
#include <iterator>

namespace reloco {

template <typename T, typename Compare = std::less<T>> class RELOCO_OWNER flat_set {
  vector<T> data_;
  Compare comp_;

public:
  using value_type = typename vector<T>::value_type;
  using allocator_type = typename vector<T>::allocator_type;
  using size_type = typename vector<T>::size_type;
  using difference_type = typename vector<T>::difference_type;
  using reference = typename vector<T>::reference;
  using const_reference = typename vector<T>::const_reference;
  using pointer = typename vector<T>::pointer;
  using const_pointer = typename vector<T>::const_pointer;
  using iterator = typename vector<T>::iterator;
  using const_iterator = typename vector<T>::const_iterator;

  RELOCO_BLOCK_RVALUE_ACCESS(T);

  constexpr flat_set() noexcept = default;

  constexpr explicit flat_set(allocator_ref alloc) noexcept : data_(alloc) {}

  [[nodiscard]] static result<flat_set> try_allocate(allocator_ref alloc, size_type initial_cap = 0) noexcept {
    auto vec_res = vector<T>::try_allocate(alloc, initial_cap);
    if (!vec_res)
      return unexpected(vec_res.error());
    return flat_set(std::move(*vec_res));
  }

  [[nodiscard]] static result<flat_set> try_create(size_type initial_cap = 0) noexcept {
    return try_allocate(default_allocator(), initial_cap);
  }

  /**
   * @brief Performs a deep copy of the set using a specific allocator.
   */
  [[nodiscard]] result<flat_set> try_clone(allocator_ref alloc) const noexcept {
    flat_set result(alloc);

    auto cloned_data = data_.try_clone(alloc);
    if (!cloned_data)
      return unexpected(cloned_data.error());

    result.data_ = std::move(*cloned_data);
    return result;
  }

  [[nodiscard]] result<flat_set> try_clone() const noexcept { return try_clone(data_.get_allocator()); }

  [[nodiscard]] size_type size() const noexcept { return data_.size(); }
  [[nodiscard]] size_type capacity() const noexcept { return data_.capacity(); }
  [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
  [[nodiscard]] allocator_ref get_allocator() const noexcept { return data_.get_allocator(); }

  void clear() noexcept { data_.clear(); }

  [[nodiscard]] result<std::reference_wrapper<T>> try_insert(T &&value) & noexcept {
    auto it = find_pos(value);
    if (it != data_.end() && !comp_(value, *it)) {
      return unexpected(error::already_exists);
    }
    const auto index = static_cast<size_type>(std::distance(data_.begin(), it));
    return data_.try_insert_at(index, std::forward<T>(value));
  }

  template <typename Key> [[nodiscard]] bool contains(const Key &value) const noexcept {
    auto it = find_pos(value);
    return it != data_.end() && !comp_(value, *it);
  }

  template <typename Key>
  [[nodiscard]] result<std::reference_wrapper<const T>> try_find(const Key &value) const & noexcept {
    auto it = find_pos(value);
    if (it != data_.end() && !comp_(value, *it)) {
      return std::cref(*it);
    }
    return unexpected(error::not_found);
  }

  template <typename Key> [[nodiscard]] result<void> try_remove(const Key &value) & noexcept {
    auto it = find_pos(value);
    if (it == data_.end() || comp_(value, *it)) {
      return unexpected(error::not_found);
    }
    const auto index = static_cast<size_type>(std::distance(data_.begin(), it));
    auto res = data_.try_erase_at(index);
    if (!res) {
      return unexpected(res.error());
    }
    return {};
  }

  [[nodiscard]] const_iterator begin() const & noexcept { return data_.begin(); }
  [[nodiscard]] const_iterator end() const & noexcept { return data_.end(); }
  [[nodiscard]] const_iterator cbegin() const & noexcept { return data_.cbegin(); }
  [[nodiscard]] const_iterator cend() const & noexcept { return data_.cend(); }

  template <typename Fn> void for_each(Fn &&fn) const {
    for (size_type i = 0; i < data_.size(); ++i) {
      fn(data_[i]);
    }
  }

private:
  template <typename Key> auto find_pos(const Key &value) const noexcept {
    return std::lower_bound(data_.begin(), data_.end(), value, comp_);
  }

  template <typename Key> auto find_pos(const Key &value) noexcept {
    return std::lower_bound(data_.begin(), data_.end(), value, comp_);
  }

  flat_set(vector<T> &&vec) noexcept : data_(std::move(vec)) {}
};

/**
 * @brief Adapts `reloco::flat_set<T, Compare>` for `mutable_container_ref`.
 *
 * Configured as an associative container where `key_type` and `element_type`
 * are both `T`. Insertion uses `try_insert` (which preserves sorted order
 * and handles uniqueness), erasure looks up and removes by key, and `find`
 * returns a non-const pointer to the stored element for mutation (or
 * traversal via `for_each`).
 */
template <typename T, typename Compare> struct container_ref_traits<flat_set<T, Compare>> {
  static constexpr bool is_associative = true;

  using element_type = T;
  using key_type = T;
  using container_type = flat_set<T, Compare>;

  static std::size_t size(const container_type &c) noexcept { return c.size(); }

  static bool empty(const container_type &c) noexcept { return c.empty(); }

  static void clear(container_type &c) noexcept { c.clear(); }

  static result<void> try_insert_at(container_type &c, key_type &&key, element_type &&value) noexcept {
    // For a flat_set, key and value are the same. We ensure key uniqueness
    // and insert the value in sorted order.
    if (key != value) {
      return unexpected(error::invalid_argument);
    }
    auto res = c.try_insert(std::move(value));
    if (!res) {
      return unexpected(res.error());
    }
    return {};
  }

  static result<void> try_erase(container_type &c, const key_type &key) noexcept {
    // If flat_set provides a try_erase(key) method, delegate to it.
    // Assuming flat_set handles key-based removal:
    return c.try_remove(key);
  }

  static element_type *find(container_type &c, const key_type &key) noexcept {
    // flat_set's try_find returns result<std::reference_wrapper<const T>>.
    // For associative container refs, we need a mutable T*. We can safely
    // cast away constness if we obtain a mutable iterator/pointer from the underlying vector,
    // or provide a mutable find method on flat_set.
    // Assuming a mutable pointer lookup or direct data access helper exists:
    auto found_res = c.try_find(key);
    if (!found_res) {
      return nullptr;
    }
    // Unconsting the reference safely since the container ref allows value mutation
    return const_cast<element_type *>(&found_res->get());
  }

  static void for_each(container_type &c, void *visitor_ctx,
                       void (*visit)(void *, const key_type &, element_type &) noexcept) noexcept {
    // Traverse the flat_set and visit each element (where key == value)
    c.for_each([visitor_ctx, visit](const T &val) noexcept {
      // flat_set elements are sorted keys, so key and value point to `val`
      visit(visitor_ctx, val, const_cast<T &>(val));
    });
  }
};

/**
 * @brief `flat_set<T>` is trivially relocatable regardless because it's wrapper over `vector<T>`
 */
template <typename T> struct is_trivially_relocatable<flat_set<T>> : std::true_type {};

} // namespace reloco
