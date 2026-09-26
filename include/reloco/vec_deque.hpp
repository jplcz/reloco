#pragma once
#include "vector.hpp"

namespace reloco {

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

template <typename T> class RELOCO_OWNER vec_deque : public detail::typed_deque_base<T, detail::heap_deque_base> {
  using base = detail::typed_deque_base<T, detail::heap_deque_base>;

public:
  using value_type = T;
  using allocator_type = allocator_ref;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  /**
   * @brief Constructs an empty deque with no allocation.
   */
  constexpr vec_deque() noexcept = default;

  /**
   * @brief Constructs an empty deque using a specific allocator.
   */
  constexpr explicit vec_deque(allocator_ref alloc) noexcept : base(alloc) {}

  vec_deque(const vec_deque &) = delete;
  vec_deque &operator=(const vec_deque &) = delete;

  vec_deque(vec_deque &&other) noexcept : base(other.get_allocator()) {
    this->move_construct_from_base(std::move(other));
  }

  vec_deque &operator=(vec_deque &&other) noexcept {
    if (this != &other) {
      this->move_assign_from_base(detail::get_operations_for<T>(), detail::metadata_for<T>, std::move(other));
    }
    return *this;
  }

  ~vec_deque() noexcept { this->destroy_elements(detail::get_operations_for<T>(), detail::metadata_for<T>); }

  // ---- Factories ----

  [[nodiscard]] static result<vec_deque> try_allocate(allocator_ref alloc, size_type initial_cap = 0) noexcept {
    vec_deque deque(alloc);
    if (initial_cap > 0) {
      auto res = deque.try_reserve(initial_cap);
      if (!res)
        return unexpected(res.error());
    }
    return deque;
  }

  [[nodiscard]] static result<vec_deque> try_create(size_type initial_cap = 0) noexcept {
    return try_allocate(default_allocator(), initial_cap);
  }

  // ---- Fallible Cloning ----

  [[nodiscard]] result<vec_deque> try_clone(allocator_ref alloc) const noexcept {
    auto res = try_allocate(alloc, this->size());
    if (!res)
      return unexpected(res.error());
    vec_deque clone = std::move(*res);

    auto ops = detail::get_operations_for<T>();
    const auto &type = detail::metadata_for<T>;

    if (!ops->clone_range) {
      return unexpected(error::unsupported_operation);
    }

    // A vec_deque can be wrapped, so we must clone its physical slices sequentially.
    // We clone them directly into the contiguous layout of the newly allocated target.
    auto [s1, s2] = this->as_slices();

    if (!s1.empty()) {
      auto cr1 = ops->clone_range(type, s1.data(), clone.data_, s1.size(), alloc);
      if (!cr1)
        return unexpected(cr1.error());
      clone.len_ += s1.size();
    }

    if (!s2.empty()) {
      auto cr2 = ops->clone_range(type, s2.data(), static_cast<T *>(clone.data_) + s1.size(), s2.size(), alloc);
      if (!cr2)
        return unexpected(cr2.error());
      clone.len_ += s2.size();
    }

    return clone;
  }

  [[nodiscard]] result<vec_deque> try_clone() const noexcept { return try_clone(default_allocator()); }

  [[nodiscard]] static result<void> try_clone_at(const allocator_ref alloc, vec_deque *storage,
                                                 const vec_deque &source) noexcept {
    auto res = source.try_clone(alloc);
    if (!res)
      return unexpected(res.error());
    new (storage) vec_deque(std::move(*res));
    return {};
  }
};

/**
 * @brief `vec_deque<T>` is unconditionally trivially relocatable.
 * Its state consists entirely of a heap pointer, size/capacity/head indices,
 * and an allocator reference, with no self-references.
 */
template <typename T> struct is_trivially_relocatable<vec_deque<T>> : std::true_type {};

/**
 * @brief Adapts `vec_deque<T>` for collection views.
 * Notice that `has_data` is `false` because the queue might wrap around physically.
 */
template <typename T> struct collection_view_traits<reloco::vec_deque<T>> {
  using element_type = T;
  static constexpr bool is_random_access = true;
  static constexpr bool has_data = false;
  static constexpr bool is_mutable = true;

  static std::size_t size(const reloco::vec_deque<T> &c) noexcept { return c.size(); }
  static bool empty(const reloco::vec_deque<T> &c) noexcept { return c.empty(); }
  static T &at(reloco::vec_deque<T> &c, std::size_t index) noexcept { return c[index]; }
  static const T &at(const reloco::vec_deque<T> &c, std::size_t index) noexcept { return c[index]; }
};

/**
 * @brief Adapts `vec_deque<T>` for `mutable_container_ref`.
 */
template <typename T> struct container_ref_traits<reloco::vec_deque<T>> {
  using element_type = std::decay_t<T>;
  static constexpr bool is_associative = false;

  static std::size_t size(const reloco::vec_deque<T> &c) noexcept { return c.size(); }
  static bool empty(const reloco::vec_deque<T> &c) noexcept { return c.empty(); }
  static void clear(reloco::vec_deque<T> &c) noexcept { c.clear(); }
  static T &at(reloco::vec_deque<T> &c, std::size_t index) noexcept { return c[index]; }

  static result<void> try_push_back(reloco::vec_deque<T> &c, T value) noexcept {
    return c.try_push_back(std::move(value));
  }

  static result<void> try_push_front(reloco::vec_deque<T> &c, T value) noexcept {
    return c.try_push_front(std::move(value));
  }

  static result<void> try_insert_at(reloco::vec_deque<T> &c, std::size_t index, T value) noexcept {
    return c.try_insert_at(index, std::move(value));
  }

  static result<void> try_erase_at(reloco::vec_deque<T> &c, std::size_t index) noexcept {
    return c.try_erase_at(index);
  }
};

RELOCO_END_UNSAFE_BUFFER_USAGE

} // namespace reloco
