#pragma once
#include <cassert>
#include <compare>
#include <cstring>
#include <iostream>
#include <reloco/allocator.hpp>
#include <reloco/concepts.hpp>
#include <string_view>

namespace reloco {

template <typename Alloc = core_allocator> class basic_string {
  Alloc *alloc_;
  char *data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t cap_ = 0;

  // Use a null character for empty strings to avoid nullptr checks in accessors
  static inline char empty_char = '\0';

public:
  using iterator = char *;
  using const_iterator = const char *;
  using size_type = std::size_t;

  basic_string() noexcept
      : alloc_(&get_default_allocator()), data_(&empty_char) {}

  explicit basic_string(Alloc &a) noexcept : alloc_(&a), data_(&empty_char) {}

  // Move-only
  basic_string(const basic_string &) = delete;
  basic_string &operator=(const basic_string &) = delete;

  basic_string(basic_string &&other) noexcept
      : alloc_(other.alloc_), data_(other.data_), size_(other.size_),
        cap_(other.cap_) {
    other.data_ = &empty_char;
    other.size_ = 0;
    other.cap_ = 0;
  }

  static result<basic_string> create(const char *s) noexcept {
    basic_string str;
    if (s) {
      auto res = str.try_append(s);
      if (!res)
        return std::unexpected(res.error());
    }
    return str;
  }

  [[nodiscard]] result<void> try_reserve(std::size_t new_cap) noexcept {
    if (new_cap <= cap_)
      return {};

    std::size_t required_bytes = new_cap + 1;

    // Try to grow in-place
    if (cap_ > 0) {
      if (auto res = alloc_->expand_in_place(data_, cap_ + 1, required_bytes);
          res) {
        cap_ = new_cap;
        return {};
      }
    }

    // Bitwise Reallocate (Strings are always relocatable)
    auto old_ptr = (cap_ == 0) ? nullptr : data_;
    auto res =
        alloc_->reallocate(old_ptr, cap_ + 1, required_bytes, alignof(char));
    if (!res)
      return std::unexpected(error::allocation_failed);

    data_ = static_cast<char *>(res->ptr);
    cap_ = new_cap;
    return {};
  }

  [[nodiscard]] result<void> try_append(const char *s) noexcept {
    if (!s)
      return {};
    std::size_t len = std::strlen(s);
    std::size_t new_size = size_ + len;

    if (new_size > cap_) {
      auto res = try_reserve(std::max(cap_ * 2, new_size));
      if (!res)
        return res;
    }

    std::memcpy(data_ + size_, s, len);
    size_ = new_size;
    data_[size_] = '\0';
    return {};
  }

  const char *c_str() const noexcept { return data_; }
  std::size_t length() const noexcept { return size_; }
  std::size_t size() const noexcept { return size_; }
  std::size_t capacity() const noexcept { return cap_; }

  ~basic_string() noexcept {
    if (cap_ > 0 && data_ != &empty_char) {
      alloc_->deallocate(data_, cap_ + 1);
    }
  }

  iterator begin() noexcept { return data_; }
  iterator end() noexcept { return data_ + size_; }
  const_iterator begin() const noexcept { return data_; }
  const_iterator end() const noexcept { return data_ + size_; }

  char &operator[](size_type pos) noexcept { return data_[pos]; }
  const char &operator[](size_type pos) const noexcept { return data_[pos]; }

  char &at(size_type pos) noexcept {
    // In reloco, we prefer assertions over throwing exceptions for
    // out-of-bounds
    assert(pos < size_);
    return data_[pos];
  }

  char &back() noexcept { return data_[size_ - 1]; }
  char &front() noexcept { return data_[0]; }

  bool empty() const noexcept { return size_ == 0; }
  void clear() noexcept {
    size_ = 0;
    if (cap_ > 0)
      data_[0] = '\0';
  }

  [[nodiscard]] result<void> shrink_to_fit() noexcept {
    if (cap_ <= size_)
      return {};

    // Use reallocate to potentially release memory back to OS
    auto res = alloc_->reallocate(data_, cap_ + 1, size_ + 1, alignof(char));
    if (!res)
      return std::unexpected(error::allocation_failed);

    data_ = static_cast<char *>(res->ptr);
    cap_ = size_;
    return {};
  }

  auto operator<=>(const basic_string &other) const noexcept {
    return std::lexicographical_compare_three_way(begin(), end(), other.begin(),
                                                  other.end());
  }

  auto operator<=>(const char *s) const noexcept {
    if (!s)
      return std::strong_ordering::greater;
    std::string_view other_view(s);
    return std::lexicographical_compare_three_way(
        begin(), end(), other_view.begin(), other_view.end());
  }

  bool operator==(const basic_string &other) const noexcept {
    if (size_ != other.size_)
      return false;
    return std::memcmp(data_, other.data_, size_) == 0;
  }

  bool operator==(const char *s) const noexcept {
    if (!s)
      return false;
    return std::strcmp(data_, s) == 0;
  }

  std::string_view view() const noexcept {
    return std::string_view(data_, size_);
  }
};

using string = basic_string<>;

template <typename Alloc>
struct is_relocatable<basic_string<Alloc>> : std::true_type {};

template <typename Alloc>
std::ostream &operator<<(std::ostream &os, const basic_string<Alloc> &str) {
  return os << str.view();
}

} // namespace reloco
