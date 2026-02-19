#pragma once
#include "assert.hpp"

#include <cassert>
#include <compare>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
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
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  using value_type = char;
  using allocator_type = Alloc;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = char &;
  using const_reference = const char &;
  using pointer = char *;
  using const_pointer = const char *;

  static constexpr size_type npos = std::string_view::npos;

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

  static result<basic_string> try_create(const char *s) noexcept {
    basic_string str;
    if (s) {
      auto res = str.try_append(s);
      if (!res)
        return std::unexpected(res.error());
    }
    return str;
  }

  static result<basic_string> try_create(std::string_view sv) noexcept {
    basic_string str;
    if (!sv.empty()) {
      auto res = str.try_append(sv);
      if (!res)
        return std::unexpected(res.error());
    }
    return str;
  }

  [[nodiscard]] result<basic_string> try_clone() const noexcept {
    // Create an empty string using the same allocator
    basic_string clone(*alloc_);

    if (size_ > 0) {
      auto res = clone.try_assign(this->view());
      if (!res)
        return std::unexpected(res.error());
    }

    return clone;
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

  [[nodiscard]] result<void> try_append(std::string_view sv) noexcept {
    if (sv.empty())
      return {};

    const std::size_t len = sv.size();
    const std::size_t new_size = size_ + len;

    // Check if we need to grow
    if (new_size > cap_) {
      // Standard geometric growth or exact size if huge
      std::size_t growth = std::max(cap_ * 2, new_size);
      auto res = try_reserve(growth);
      if (!res)
        return res;
    }

    // Copy the data (sv.data() might not be null-terminated, which is fine)
    std::memcpy(data_ + size_, sv.data(), len);

    size_ = new_size;
    data_[size_] = '\0'; // Ensure C-string compatibility

    return {};
  }

  [[nodiscard]] result<void> try_append(const char *s) noexcept {
    if (!s)
      return {};
    return try_append(std::string_view(s));
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
  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator rbegin() const noexcept {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator rend() const noexcept {
    return const_reverse_iterator(begin());
  }

  char &operator[](size_type pos) noexcept { return data_[pos]; }
  const char &operator[](size_type pos) const noexcept { return data_[pos]; }

  char &at(size_type pos) noexcept {
    // In reloco, we prefer assertions over throwing exceptions for
    // out-of-bounds
    RELOCO_ASSERT(pos < size_);
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

  operator std::string_view() const noexcept {
    return std::string_view(data_, size_);
  }

  explicit operator std::string() const { return std::string(data_, size_); }

  [[nodiscard]] static result<basic_string>
  from_view(std::string_view sv) noexcept {
    basic_string str;
    auto res = str.try_reserve(sv.size());
    if (!res)
      return std::unexpected(res.error());

    std::memcpy(str.data_, sv.data(), sv.size());
    str.size_ = sv.size();
    str.data_[str.size_] = '\0';
    return str;
  }

  [[nodiscard]] result<void> try_append_fmt(const char *format, ...) noexcept {
    va_list args;
    va_start(args, format);

    // Determine how much space is needed (excluding null terminator)
    va_list args_copy;
    va_copy(args_copy, args);
    const auto needed = std::vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    if (needed < 0) {
      va_end(args);
      return std::unexpected(error::unsupported_operation);
    }

    std::size_t len = static_cast<std::size_t>(needed);
    std::size_t new_size = size_ + len;

    // Fallible Reservation
    if (new_size > cap_) {
      auto res = try_reserve(std::max(cap_ * 2, new_size));
      if (!res) {
        va_end(args);
        return res;
      }
    }

    // Actual formatting into our hardware-aligned buffer
    std::vsnprintf(data_ + size_, len + 1, format, args);
    va_end(args);

    size_ = new_size;
    // vsnprintf adds the null terminator for us at data_[size_]
    return {};
  }

  void pop_back() noexcept {
    RELOCO_ASSERT(size_ > 0);
    --size_;
    data_[size_] = '\0';
  }

  [[nodiscard]] result<void> try_pop_back() noexcept {
    if (size_ == 0) {
      return std::unexpected(error::out_of_range);
    }
    --size_;
    data_[size_] = '\0';
    return {};
  }

  [[nodiscard]] result<void> try_resize(size_type count,
                                        char ch = '\0') noexcept {
    if (count <= size_) {
      size_ = count;
      if (cap_ > 0)
        data_[size_] = '\0';
      return {};
    }

    auto res = try_reserve(count);
    if (!res)
      return res;

    std::memset(data_ + size_, ch, count - size_);
    size_ = count;
    data_[size_] = '\0';
    return {};
  }

  [[nodiscard]] result<void> try_insert(size_type pos,
                                        std::string_view sv) noexcept {
    if (pos > size_)
      return std::unexpected(error::out_of_range);

    if (sv.empty())
      return {};

    const size_type len = sv.size();
    auto res = try_reserve(size_ + len);
    if (!res)
      return res;

    // Move existing data to the right
    std::memmove(data_ + pos + len, data_ + pos, size_ - pos);
    // Copy new data into the gap
    std::memcpy(data_ + pos, sv.data(), len);

    size_ += len;
    data_[size_] = '\0';
    return {};
  }

  void erase(size_type pos = 0, size_type count = npos) noexcept {
    RELOCO_ASSERT(pos <= size_);

    // Adjust count if it exceeds remaining string length
    size_type actual_count = std::min(count, size_ - pos);
    if (actual_count == 0)
      return;

    // Shift remaining data to the left to fill the gap
    std::memmove(data_ + pos, data_ + pos + actual_count,
                 size_ - pos - actual_count);

    size_ -= actual_count;
    data_[size_] = '\0'; // Maintain hardware-honest null terminator
  }

  [[nodiscard]] result<void> try_erase(size_type pos = 0,
                                       size_type count = npos) noexcept {
    if (pos > size_) {
      return std::unexpected(error::out_of_range);
    }

    size_type actual_count = std::min(count, size_ - pos);
    if (actual_count == 0)
      return {};

    // Move memory
    std::memmove(data_ + pos, data_ + pos + actual_count,
                 size_ - pos - actual_count);

    size_ -= actual_count;
    data_[size_] = '\0';

    return {};
  }

  [[nodiscard]] result<void> try_assign(std::string_view sv) noexcept {
    if (sv.size() <= cap_) {
      // Fast path: reuse existing block
      std::memcpy(data_, sv.data(), sv.size());
      size_ = sv.size();
      data_[size_] = '\0';
      return {};
    }

    auto res = alloc_->allocate(sv.size() + 1, alignof(char));
    if (!res)
      return std::unexpected(error::allocation_failed);

    if (cap_ > 0 && data_ != &empty_char) {
      alloc_->deallocate(data_, cap_ + 1);
    }

    data_ = static_cast<char *>(res->ptr);
    cap_ = sv.size();
    size_ = sv.size();

    std::memcpy(data_, sv.data(), size_);
    data_[size_] = '\0';

    return {};
  }

  size_type find(std::string_view sv, size_type pos = 0) const noexcept {
    return view().find(sv, pos);
  }

  size_type find(char c, size_type pos = 0) const noexcept {
    return view().find(c, pos);
  }

  size_type rfind(std::string_view sv, size_type pos = npos) const noexcept {
    return view().rfind(sv, pos);
  }

  bool contains(std::string_view sv) const noexcept {
    return view().find(sv) != std::string_view::npos;
  }

  bool starts_with(std::string_view sv) const noexcept {
    return view().starts_with(sv);
  }

  bool ends_with(std::string_view sv) const noexcept {
    return view().ends_with(sv);
  }

  allocator_type &get_allocator() const noexcept { return *alloc_; }
};

using string = basic_string<>;

template <typename Alloc>
struct is_relocatable<basic_string<Alloc>> : std::true_type {};

template <typename Alloc>
std::ostream &operator<<(std::ostream &os, const basic_string<Alloc> &str) {
  return os << str.view();
}

} // namespace reloco
