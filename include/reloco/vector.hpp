#pragma once
#include <initializer_list>
#include <iterator>
#include <reloco/allocator.hpp>
#include <reloco/concepts.hpp>
#include <span>

namespace reloco {

template <typename T, typename Alloc = core_allocator> class vector {
  Alloc *alloc_;
  T *data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t cap_ = 0;

public:
  using value_type = T;
  using allocator_type = Alloc;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T &;
  using const_reference = const T &;
  using pointer = T *;
  using const_pointer = const T *;
  using iterator = T *;
  using const_iterator = const T *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  vector() noexcept : alloc_(&get_default_allocator()) {}
  explicit vector(Alloc &a) noexcept : alloc_(&a) {}

  // Move-only
  vector(const vector &) = delete;
  vector &operator=(const vector &) = delete;

  vector(vector &&other) noexcept
      : alloc_(other.alloc_), data_(other.data_), size_(other.size_),
        cap_(other.cap_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.cap_ = 0;
  }

  ~vector() {
    clear();
    if (data_)
      alloc_->deallocate(data_, cap_ * sizeof(T));
  }

  static result<vector> try_create(std::size_t initial_cap = 0) noexcept {
    vector v;
    if (initial_cap > 0) {
      auto res = v.try_reserve(initial_cap);
      if (!res)
        return std::unexpected(res.error());
    }
    return v;
  }

  iterator begin() noexcept { return data_; }
  iterator end() noexcept { return data_ + size_; }
  const_iterator begin() const noexcept { return data_; }
  const_iterator end() const noexcept { return data_ + size_; }
  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return cap_; }
  bool empty() const noexcept { return size_ == 0; }
  reference operator[](size_type pos) { return data_[pos]; }
  const_reference operator[](size_type pos) const { return data_[pos]; }
  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator rbegin() const noexcept {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator rend() const noexcept {
    return const_reverse_iterator(begin());
  }

  [[nodiscard]] result<void> try_reserve(size_type new_cap) noexcept {
    if (new_cap <= cap_)
      return {};

    // Try In-Place Growth
    if (data_) {
      if (auto res = alloc_->expand_in_place(data_, cap_ * sizeof(T),
                                             new_cap * sizeof(T));
          res) {
        cap_ = new_cap;
        return {};
      }
    }

    // Optimized Relocation
    if constexpr (is_relocatable<T>::value) {
      auto res = alloc_->reallocate(data_, cap_ * sizeof(T),
                                    new_cap * sizeof(T), alignof(T));
      if (!res)
        return std::unexpected(error::allocation_failed);
      data_ = static_cast<T *>(res->ptr);
      cap_ = new_cap;
    }
    // Fallback: Manual Move/Destroy
    else {
      auto res = alloc_->allocate(new_cap * sizeof(T), alignof(T));
      if (!res)
        return std::unexpected(error::allocation_failed);

      T *new_ptr = static_cast<T *>(res->ptr);
      for (size_type i = 0; i < size_; ++i) {
        new (new_ptr + i) T(std::move(data_[i]));
        data_[i].~T();
      }
      if (data_)
        alloc_->deallocate(data_, cap_ * sizeof(T));
      data_ = new_ptr;
      cap_ = new_cap;
    }
    return {};
  }

  template <typename... Args>
  [[nodiscard]] result<T *> try_emplace_back(Args &&...args) noexcept {
    if (size_ == cap_) {
      auto res = try_reserve(cap_ == 0 ? 8 : cap_ * 2);
      if (!res)
        return std::unexpected(res.error());
    }

    T *ptr = data_ + size_;

    if constexpr (has_try_create<T, Args...>) {
      auto res = T::try_create(std::forward<Args>(args)...);
      if (!res) {
        return std::unexpected(res.error());
      }

      new (ptr) T(std::move(*res));
    } else {
      new (ptr) T(std::forward<Args>(args)...);
    }

    size_++;
    return ptr;
  }

  [[nodiscard]] result<T *> try_push_back(T &&val) noexcept {
    if (size_ == cap_) {
      auto res = try_reserve(cap_ == 0 ? 8 : cap_ * 2);
      if (!res)
        return std::unexpected(res.error());
    }

    T *ptr = new (data_ + size_) T(std::move(val));
    size_++;
    return ptr;
  }

  [[nodiscard]] result<void> try_pop_back() noexcept {
    if (size_ == 0)
      return std::unexpected(error::out_of_range);
    data_[--size_].~T();
    return {};
  }

  void clear() noexcept {
    for (size_type i = 0; i < size_; ++i)
      data_[i].~T();
    size_ = 0;
  }

  [[nodiscard]] result<vector> try_clone() const noexcept {
    vector clone(*alloc_);
    auto res = clone.try_reserve(size_);
    if (!res)
      return std::unexpected(res.error());

    for (size_type i = 0; i < size_; ++i) {
      if constexpr (has_try_clone<T>) {
        auto item_res = data_[i].try_clone();
        if (!item_res)
          return std::unexpected(item_res.error());
        auto item_res_push = clone.try_push_back(std::move(*item_res));
        if (!item_res_push)
          return std::unexpected(item_res.error());
      } else {
        auto item_res = clone.try_push_back(T(data_[i]));
        if (!item_res)
          return std::unexpected(item_res.error());
      }
    }
    return clone;
  }

  [[nodiscard]] result<void> try_erase(size_type pos) noexcept {
    if (pos >= size_)
      return std::unexpected(error::out_of_range);

    data_[pos].~T();
    size_type move_count = size_ - pos - 1;

    if (move_count > 0) {
      if constexpr (is_relocatable<T>::value) {
        std::memmove(data_ + pos, data_ + pos + 1, move_count * sizeof(T));
      } else {
        // Manual move loop for non-relocatable types
        for (size_type i = 0; i < move_count; ++i) {
          new (data_ + pos + i) T(std::move(data_[pos + i + 1]));
          data_[pos + i + 1].~T();
        }
      }
    }
    size_--;
    return {};
  }

  template <typename... Args>
  [[nodiscard]] result<T *> try_insert(size_type pos, Args &&...args) noexcept {
    if (pos > size_) {
      return std::unexpected(error::out_of_range);
    }

    // Ensure capacity
    if (size_ == cap_) {
      auto res = try_reserve(cap_ == 0 ? 8 : cap_ * 2);
      if (!res) {
        return std::unexpected(res.error());
      }
    }

    size_type move_count = size_ - pos;

    if (move_count > 0) {
      if constexpr (is_relocatable<T>::value) {
        // Relocatable: Just shift the bytes to the right
        std::memmove(data_ + pos + 1, data_ + pos, move_count * sizeof(T));
      } else {
        new (data_ + size_) T(std::move(data_[size_ - 1]));

        for (size_type i = size_ - 1; i > pos; --i) {
          data_[i] = std::move(data_[i - 1]);
        }

        data_[pos].~T();
      }
    }

    // Construct the new element in the hole
    T *ptr = new (data_ + pos) T(std::forward<Args>(args)...);

    size_++;
    return ptr;
  }

  [[nodiscard]] result<T *> try_at(const size_t index) noexcept {
    if (index >= size_)
      return std::unexpected(error::out_of_range);
    return &data_[index];
  }
};

template <typename T, typename A>
struct is_relocatable<vector<T, A>> : is_relocatable<T> {};

template <typename T, typename A>
struct is_fallible_type<vector<T, A>> : std::true_type {};

} // namespace reloco
