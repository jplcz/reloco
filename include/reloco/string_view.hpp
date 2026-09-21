// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include <cstddef>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>

namespace reloco {

// All of the below classes contain checked pointer arithmetic
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

template <typename CharT, typename TraitsT = std::char_traits<CharT>> class RELOCO_POINTER basic_string_view {
public:
  using base = std::basic_string_view<CharT, TraitsT>;
  using traits_type = TraitsT;
  using value_type = CharT;
  using size_type = typename base::size_type;
  using difference_type = typename base::difference_type;
  using const_reference = typename base::const_reference;
  using const_pointer = typename base::const_pointer;
  using const_iterator = typename base::const_iterator;
  using iterator = const_iterator;
  using const_reverse_iterator = typename base::const_reverse_iterator;
  using reverse_iterator = const_reverse_iterator;

  static constexpr size_type npos = base::npos;

  constexpr basic_string_view() noexcept = default;
  constexpr basic_string_view(const basic_string_view &) noexcept = default;
  constexpr basic_string_view &operator=(const basic_string_view &) noexcept = default;
  constexpr basic_string_view(base rhs RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept : view_(rhs) {}

  template <typename Allocator>
  constexpr basic_string_view(const std::basic_string<CharT, TraitsT, Allocator> &rhs RELOCO_LIFETIMEBOUND
                                  RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : view_(rhs.data(), rhs.size()) {}

  template <typename Allocator> basic_string_view(std::basic_string<CharT, TraitsT, Allocator> &&) = delete;

  constexpr basic_string_view(std::nullptr_t) noexcept {}

  constexpr basic_string_view(const CharT *str RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS,
                              size_type len) noexcept
      : view_(str == nullptr ? base() : base(str, len)) {
    RELOCO_ASSERT(str != nullptr || len == 0, "string_view data is null with non-zero length");
  }

  RELOCO_ALWAYS_INLINE
  constexpr basic_string_view(const CharT *str RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : view_(str == nullptr ? base() : base(str)) {}

  [[nodiscard]] constexpr size_type size() const noexcept { return view_.size(); }
  [[nodiscard]] constexpr size_type length() const noexcept { return view_.length(); }
  [[nodiscard]] constexpr bool empty() const noexcept { return view_.empty(); }

  [[nodiscard]] constexpr const_reference operator[](size_type pos) const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos < size(), "string_view index out of bounds");
    return view_[pos];
  }

  [[nodiscard]] result<std::reference_wrapper<const CharT>> try_front() const noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::cref(view_.front());
  }

  [[nodiscard]] result<std::reference_wrapper<const CharT>> try_back() const noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::cref(view_.back());
  }

  [[nodiscard]] constexpr const_reference front() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "front() called on empty string_view");
    return view_.front();
  }

  [[nodiscard]] constexpr const_reference back() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "back() called on empty string_view");
    return view_.back();
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const_reference
  unsafe_front() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "front() called on empty string_view");
    return view_.front();
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const_reference unsafe_back() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "back() called on empty string_view");
    return view_.back();
  }

  [[nodiscard]] constexpr basic_string_view substr(size_type pos = 0,
                                                   size_type count = npos) const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos <= size(), "substr position out of bounds");
    return basic_string_view(view_.substr(pos, count));
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr basic_string_view
  unsafe_substr(size_type pos = 0, size_type count = npos) const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(pos <= size(), "substr position out of bounds");
    return basic_string_view(view_.substr(pos, count));
  }

  [[nodiscard]] result<std::reference_wrapper<const CharT>> try_at(size_type pos) const noexcept RELOCO_LIFETIMEBOUND {
    if (pos >= size())
      return unexpected(error::out_of_bounds);
    return std::cref(view_[pos]);
  }

  [[nodiscard]] result<basic_string_view> try_substr(size_type pos,
                                                     size_type count = npos) const noexcept RELOCO_LIFETIMEBOUND {
    if (pos > size())
      return unexpected(error::out_of_bounds);
    return basic_string_view(view_.substr(pos, count));
  }

  [[nodiscard]] constexpr const_pointer data() const noexcept RELOCO_LIFETIMEBOUND { return view_.data(); }

  [[nodiscard]] result<const_pointer> try_data() const noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return view_.data();
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const_pointer unsafe_data() const noexcept RELOCO_LIFETIMEBOUND {
    return view_.data();
  }

  [[nodiscard]] constexpr base to_std() const noexcept RELOCO_LIFETIMEBOUND { return view_; }
  [[nodiscard]] constexpr operator base() const noexcept RELOCO_LIFETIMEBOUND { return view_; }

  constexpr void remove_prefix(size_type n) & noexcept {
    RELOCO_ASSERT(n <= size(), "remove_prefix exceeds view size");
    view_.remove_prefix(n);
  }

  RELOCO_UNSAFE_BUFFER_USAGE constexpr void unsafe_remove_prefix(size_type n) & noexcept {
    RELOCO_DEBUG_ASSERT(n <= size(), "remove_prefix exceeds view size");
    view_.remove_prefix(n);
  }

  constexpr void remove_suffix(size_type n) & noexcept {
    RELOCO_ASSERT(n <= size(), "remove_suffix exceeds view size");
    view_.remove_suffix(n);
  }

  RELOCO_UNSAFE_BUFFER_USAGE constexpr void unsafe_remove_suffix(size_type n) & noexcept {
    RELOCO_DEBUG_ASSERT(n <= size(), "remove_suffix exceeds view size");
    view_.remove_suffix(n);
  }

  [[nodiscard]] result<void> try_remove_prefix(size_type n) & noexcept {
    if (n > size())
      return unexpected(error::out_of_bounds);
    view_.remove_prefix(n);
    return {};
  }

  [[nodiscard]] result<void> try_remove_suffix(size_type n) & noexcept {
    if (n > size())
      return unexpected(error::out_of_bounds);
    view_.remove_suffix(n);
    return {};
  }

  [[nodiscard]] constexpr size_type find(CharT ch, size_type pos = 0) const noexcept { return view_.find(ch, pos); }

  [[nodiscard]] constexpr size_type find(basic_string_view needle, size_type pos = 0) const noexcept {
    return view_.find(needle.view_, pos);
  }

  [[nodiscard]] constexpr size_type find_last_of(basic_string_view chars, size_type pos = npos) const noexcept {
    return view_.find_last_of(chars.view_, pos);
  }

  [[nodiscard]] constexpr int compare(size_type pos, size_type count, basic_string_view rhs) const noexcept {
    return view_.compare(pos, count, rhs.view_);
  }

  [[nodiscard]] constexpr bool starts_with(basic_string_view prefix) const noexcept {
    return size() >= prefix.size() && view_.compare(0, prefix.size(), prefix.view_) == 0;
  }

  [[nodiscard]] constexpr bool starts_with(CharT ch) const noexcept { return !empty() && front() == ch; }

  [[nodiscard]] constexpr const_iterator begin() const noexcept RELOCO_LIFETIMEBOUND { return view_.begin(); }
  [[nodiscard]] constexpr const_iterator end() const noexcept RELOCO_LIFETIMEBOUND { return view_.end(); }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept RELOCO_LIFETIMEBOUND { return view_.cbegin(); }
  [[nodiscard]] constexpr const_iterator cend() const noexcept RELOCO_LIFETIMEBOUND { return view_.cend(); }
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept RELOCO_LIFETIMEBOUND { return view_.rbegin(); }
  [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept RELOCO_LIFETIMEBOUND { return view_.rend(); }
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept RELOCO_LIFETIMEBOUND {
    return view_.crbegin();
  }
  [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept RELOCO_LIFETIMEBOUND { return view_.crend(); }

  friend constexpr bool operator==(basic_string_view lhs, basic_string_view rhs) noexcept {
    return lhs.view_ == rhs.view_;
  }

  friend constexpr bool operator!=(basic_string_view lhs, basic_string_view rhs) noexcept { return !(lhs == rhs); }

  friend constexpr bool operator==(basic_string_view lhs, base rhs) noexcept { return lhs.view_ == rhs; }

  friend constexpr bool operator==(base lhs, basic_string_view rhs) noexcept { return lhs == rhs.view_; }

  friend constexpr bool operator!=(basic_string_view lhs, base rhs) noexcept { return !(lhs == rhs); }

  friend constexpr bool operator!=(base lhs, basic_string_view rhs) noexcept { return !(lhs == rhs); }

  friend constexpr bool operator==(basic_string_view lhs, const CharT *rhs) noexcept { return lhs.view_ == base(rhs); }

  friend constexpr bool operator==(const CharT *lhs, basic_string_view rhs) noexcept { return base(lhs) == rhs.view_; }

  friend constexpr bool operator!=(basic_string_view lhs, const CharT *rhs) noexcept { return !(lhs == rhs); }

  friend constexpr bool operator!=(const CharT *lhs, basic_string_view rhs) noexcept { return !(lhs == rhs); }

  friend constexpr bool operator<(basic_string_view lhs, basic_string_view rhs) noexcept {
    return lhs.view_ < rhs.view_;
  }

  friend constexpr bool operator<(basic_string_view lhs, base rhs) noexcept { return lhs.view_ < rhs; }

  friend constexpr bool operator<(base lhs, basic_string_view rhs) noexcept { return lhs < rhs.view_; }

  friend constexpr bool operator<(basic_string_view lhs, const CharT *rhs) noexcept { return lhs.view_ < base(rhs); }

  friend constexpr bool operator<(const CharT *lhs, basic_string_view rhs) noexcept { return base(lhs) < rhs.view_; }

  [[nodiscard]] static constexpr basic_string_view from_range(const CharT *first, const CharT *last) noexcept {
    RELOCO_ASSERT(first <= last, "invalid pointer range for string_view");
    return basic_string_view(first, static_cast<size_type>(last - first));
  }

private:
  base view_{};
};

using string_view = basic_string_view<char>;
using wstring_view = basic_string_view<wchar_t>;

RELOCO_END_UNSAFE_BUFFER_USAGE

} // namespace reloco

namespace std {

template <typename CharT, typename TraitsT> struct hash<reloco::basic_string_view<CharT, TraitsT>> {
  [[nodiscard]] size_t operator()(reloco::basic_string_view<CharT, TraitsT> value) const noexcept {
    return hash<std::basic_string_view<CharT, TraitsT>>{}(value.to_std());
  }
};

} // namespace std
