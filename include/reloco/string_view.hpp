// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "optional.hpp"
#include "type_id.hpp"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>

namespace reloco {

// All of the below classes contain checked pointer arithmetic
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

/**
 * @brief Hardened, self-contained equivalent of `std::basic_string_view`.
 *
 * Unlike an earlier revision, this view does not store a
 * `std::basic_string_view` internally: it holds its own `data_`/`size_`
 * pair and implements element access, comparison, and prefix/suffix checks
 * directly against `TraitsT`. It only calls into the standard library for
 * the handful of algorithms that are not worth re-deriving by hand
 * (`std::search` for substring search, `std::find_first_of` for
 * character-class search), and for interop conversions to/from
 * `std::basic_string_view` (`to_std()`, the implicit conversion operator,
 * and construction from `std::basic_string_view`/`std::basic_string`).
 */
template <typename CharT, typename TraitsT = std::char_traits<CharT>> class RELOCO_POINTER basic_string_view {
public:
  using base = std::basic_string_view<CharT, TraitsT>;
  using traits_type = TraitsT;
  using value_type = CharT;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using const_reference = const CharT &;
  using const_pointer = const CharT *;
  using const_iterator = const CharT *;
  using iterator = const_iterator;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reverse_iterator = const_reverse_iterator;

  static constexpr size_type npos = static_cast<size_type>(-1);

  constexpr basic_string_view() noexcept = default;
  constexpr basic_string_view(const basic_string_view &) noexcept = default;
  constexpr basic_string_view &operator=(const basic_string_view &) noexcept = default;

  constexpr basic_string_view(base rhs RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : data_(rhs.data()), size_(rhs.size()) {}

  template <typename Allocator>
  constexpr basic_string_view(const std::basic_string<CharT, TraitsT, Allocator> &rhs RELOCO_LIFETIMEBOUND
                                  RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : data_(rhs.data()), size_(rhs.size()) {}

  template <typename Allocator> basic_string_view(std::basic_string<CharT, TraitsT, Allocator> &&) = delete;

  constexpr basic_string_view(std::nullptr_t) noexcept {}

  constexpr basic_string_view(const CharT *str RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS,
                              size_type len) noexcept
      : data_(str), size_(str == nullptr ? 0 : len) {
    RELOCO_ASSERT(str != nullptr || len == 0, "string_view data is null with non-zero length");
  }

  RELOCO_ALWAYS_INLINE
  constexpr basic_string_view(const CharT *str RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : data_(str), size_(str == nullptr ? 0 : TraitsT::length(str)) {}

  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr size_type length() const noexcept { return size_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

  [[nodiscard]] constexpr const_reference operator[](size_type pos) const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos < size(), "string_view index out of bounds");
    return data_[pos];
  }

  [[nodiscard]] result<std::reference_wrapper<const CharT>> try_front() const noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::cref(data_[0]);
  }

  [[nodiscard]] result<std::reference_wrapper<const CharT>> try_back() const noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::cref(data_[size_ - 1]);
  }

  [[nodiscard]] constexpr const_reference front() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "front() called on empty string_view");
    return data_[0];
  }

  [[nodiscard]] constexpr const_reference back() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "back() called on empty string_view");
    return data_[size_ - 1];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const_reference
  unsafe_front() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "front() called on empty string_view");
    return data_[0];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const_reference unsafe_back() const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "back() called on empty string_view");
    return data_[size_ - 1];
  }

  [[nodiscard]] constexpr basic_string_view substr(size_type pos = 0,
                                                   size_type count = npos) const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos <= size(), "substr position out of bounds");
    return basic_string_view(data_ + pos, std::min(count, size_ - pos));
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr basic_string_view
  unsafe_substr(size_type pos = 0, size_type count = npos) const noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(pos <= size(), "substr position out of bounds");
    return basic_string_view(data_ + pos, std::min(count, size_ - pos));
  }

  [[nodiscard]] result<std::reference_wrapper<const CharT>> try_at(size_type pos) const noexcept RELOCO_LIFETIMEBOUND {
    if (pos >= size())
      return unexpected(error::out_of_bounds);
    return std::cref(data_[pos]);
  }

  [[nodiscard]] result<basic_string_view> try_substr(size_type pos,
                                                     size_type count = npos) const noexcept RELOCO_LIFETIMEBOUND {
    if (pos > size())
      return unexpected(error::out_of_bounds);
    return basic_string_view(data_ + pos, std::min(count, size_ - pos));
  }

  [[nodiscard]] constexpr const_pointer data() const noexcept RELOCO_LIFETIMEBOUND { return data_; }

  [[nodiscard]] result<const_pointer> try_data() const noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return data_;
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr const_pointer unsafe_data() const noexcept RELOCO_LIFETIMEBOUND {
    return data_;
  }

  [[nodiscard]] constexpr base to_std() const noexcept RELOCO_LIFETIMEBOUND { return base(data_, size_); }
  [[nodiscard]] constexpr operator base() const noexcept RELOCO_LIFETIMEBOUND { return base(data_, size_); }

  constexpr void remove_prefix(size_type n) & noexcept {
    RELOCO_ASSERT(n <= size(), "remove_prefix exceeds view size");
    data_ += n;
    size_ -= n;
  }

  RELOCO_UNSAFE_BUFFER_USAGE constexpr void unsafe_remove_prefix(size_type n) & noexcept {
    RELOCO_DEBUG_ASSERT(n <= size(), "remove_prefix exceeds view size");
    data_ += n;
    size_ -= n;
  }

  constexpr void remove_suffix(size_type n) & noexcept {
    RELOCO_ASSERT(n <= size(), "remove_suffix exceeds view size");
    size_ -= n;
  }

  RELOCO_UNSAFE_BUFFER_USAGE constexpr void unsafe_remove_suffix(size_type n) & noexcept {
    RELOCO_DEBUG_ASSERT(n <= size(), "remove_suffix exceeds view size");
    size_ -= n;
  }

  [[nodiscard]] result<void> try_remove_prefix(size_type n) & noexcept {
    if (n > size())
      return unexpected(error::out_of_bounds);
    data_ += n;
    size_ -= n;
    return {};
  }

  [[nodiscard]] result<void> try_remove_suffix(size_type n) & noexcept {
    if (n > size())
      return unexpected(error::out_of_bounds);
    size_ -= n;
    return {};
  }

  // A single-character search is a direct TraitsT primitive, so it is
  // implemented locally rather than delegating to a standard algorithm.
  [[nodiscard]] constexpr size_type find(CharT ch, size_type pos = 0) const noexcept {
    if (pos >= size_)
      return npos;
    const CharT *found = TraitsT::find(data_ + pos, size_ - pos, ch);
    return found == nullptr ? npos : static_cast<size_type>(found - data_);
  }

  // Substring search is not worth re-deriving by hand: delegate to
  // `std::search` over the raw buffers instead of wrapping the whole view
  // in `std::basic_string_view`.
  [[nodiscard]] size_type find(basic_string_view needle, size_type pos = 0) const noexcept {
    if (pos > size_)
      return npos;
    if (needle.empty())
      return pos;
    if (needle.size_ > size_ - pos)
      return npos;
    const CharT *first = std::search(data_ + pos, data_ + size_, needle.data_, needle.data_ + needle.size_,
                                     [](CharT a, CharT b) { return TraitsT::eq(a, b); });
    return first == data_ + size_ ? npos : static_cast<size_type>(first - data_);
  }

  // Character-class search likewise delegates to `std::find_first_of`,
  // applied to a reverse range to search backwards from `pos`.
  [[nodiscard]] size_type find_last_of(basic_string_view chars, size_type pos = npos) const noexcept {
    if (empty() || chars.empty())
      return npos;
    const size_type last = pos >= size_ - 1 ? size_ - 1 : pos;
    auto rbegin_ = std::make_reverse_iterator(data_ + last + 1);
    auto rend_ = std::make_reverse_iterator(data_);
    auto it = std::find_first_of(rbegin_, rend_, chars.data_, chars.data_ + chars.size_,
                                 [](CharT a, CharT b) { return TraitsT::eq(a, b); });
    return it == rend_ ? npos : static_cast<size_type>(std::addressof(*it) - data_);
  }

  [[nodiscard]] constexpr int compare(size_type pos, size_type count, basic_string_view rhs) const noexcept {
    return substr(pos, count).compare_impl(rhs);
  }

  [[nodiscard]] constexpr bool starts_with(basic_string_view prefix) const noexcept {
    return size_ >= prefix.size_ && (prefix.size_ == 0 || TraitsT::compare(data_, prefix.data_, prefix.size_) == 0);
  }

  [[nodiscard]] constexpr bool starts_with(CharT ch) const noexcept { return !empty() && TraitsT::eq(front(), ch); }

  [[nodiscard]] constexpr bool ends_with(basic_string_view suffix) const noexcept {
    return size_ >= suffix.size_ &&
           (suffix.size_ == 0 || TraitsT::compare(data_ + (size_ - suffix.size_), suffix.data_, suffix.size_) == 0);
  }

  [[nodiscard]] constexpr bool ends_with(CharT ch) const noexcept { return !empty() && TraitsT::eq(back(), ch); }

  /**
   * @brief Rust `str::strip_prefix` equivalent: returns the view with
   * @p prefix removed from the front, or an empty `optional` if this view
   * does not start with @p prefix.
   */
  [[nodiscard]] constexpr optional<basic_string_view>
  strip_prefix(basic_string_view prefix) const noexcept RELOCO_LIFETIMEBOUND {
    if (!starts_with(prefix))
      return nullopt;
    return substr(prefix.size_);
  }

  /**
   * @brief Rust `str::strip_suffix` equivalent: returns the view with
   * @p suffix removed from the back, or an empty `optional` if this view
   * does not end with @p suffix.
   */
  [[nodiscard]] constexpr optional<basic_string_view>
  strip_suffix(basic_string_view suffix) const noexcept RELOCO_LIFETIMEBOUND {
    if (!ends_with(suffix))
      return nullopt;
    return substr(0, size_ - suffix.size_);
  }

  /**
   * @brief Returns `true` if @p ch is one of the default ASCII whitespace
   * characters trimmed by `trim`/`trim_start`/`trim_end` (space, tab,
   * newline, carriage return, form feed, vertical tab).
   */
  [[nodiscard]] static constexpr bool is_ascii_space(CharT ch) noexcept {
    return TraitsT::eq(ch, CharT(' ')) || TraitsT::eq(ch, CharT('\t')) || TraitsT::eq(ch, CharT('\n')) ||
           TraitsT::eq(ch, CharT('\r')) || TraitsT::eq(ch, CharT('\f')) || TraitsT::eq(ch, CharT('\v'));
  }

  /**
   * @brief Rust `str::trim_start` equivalent: returns the view with every
   * leading character satisfying @p pred (default: ASCII whitespace)
   * removed.
   */
  template <typename Pred = decltype(&basic_string_view::is_ascii_space)>
  [[nodiscard]] constexpr basic_string_view
  trim_start(Pred pred = &basic_string_view::is_ascii_space) const noexcept RELOCO_LIFETIMEBOUND {
    size_type i = 0;
    while (i < size_ && pred(data_[i]))
      ++i;
    return substr(i);
  }

  /**
   * @brief Rust `str::trim_end` equivalent: returns the view with every
   * trailing character satisfying @p pred (default: ASCII whitespace)
   * removed.
   */
  template <typename Pred = decltype(&basic_string_view::is_ascii_space)>
  [[nodiscard]] constexpr basic_string_view
  trim_end(Pred pred = &basic_string_view::is_ascii_space) const noexcept RELOCO_LIFETIMEBOUND {
    size_type i = size_;
    while (i > 0 && pred(data_[i - 1]))
      --i;
    return substr(0, i);
  }

  /**
   * @brief Rust `str::trim` equivalent: removes both leading and trailing
   * characters satisfying @p pred (default: ASCII whitespace).
   */
  template <typename Pred = decltype(&basic_string_view::is_ascii_space)>
  [[nodiscard]] constexpr basic_string_view
  trim(Pred pred = &basic_string_view::is_ascii_space) const noexcept RELOCO_LIFETIMEBOUND {
    return trim_start(pred).trim_end(pred);
  }

  [[nodiscard]] constexpr const_iterator begin() const noexcept RELOCO_LIFETIMEBOUND { return data_; }
  [[nodiscard]] constexpr const_iterator end() const noexcept RELOCO_LIFETIMEBOUND { return data_ + size_; }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept RELOCO_LIFETIMEBOUND { return data_; }
  [[nodiscard]] constexpr const_iterator cend() const noexcept RELOCO_LIFETIMEBOUND { return data_ + size_; }
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(end());
  }
  [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(begin());
  }
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept RELOCO_LIFETIMEBOUND { return rbegin(); }
  [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept RELOCO_LIFETIMEBOUND { return rend(); }

  friend constexpr bool operator==(basic_string_view lhs, basic_string_view rhs) noexcept {
    return lhs.size_ == rhs.size_ && (lhs.size_ == 0 || TraitsT::compare(lhs.data_, rhs.data_, lhs.size_) == 0);
  }

  friend constexpr bool operator!=(basic_string_view lhs, basic_string_view rhs) noexcept { return !(lhs == rhs); }

  friend constexpr bool operator==(basic_string_view lhs, base rhs) noexcept { return lhs == basic_string_view(rhs); }

  friend constexpr bool operator==(base lhs, basic_string_view rhs) noexcept { return basic_string_view(lhs) == rhs; }

  friend constexpr bool operator!=(basic_string_view lhs, base rhs) noexcept { return !(lhs == rhs); }

  friend constexpr bool operator!=(base lhs, basic_string_view rhs) noexcept { return !(lhs == rhs); }

  friend constexpr bool operator==(basic_string_view lhs, const CharT *rhs) noexcept {
    return lhs == basic_string_view(rhs);
  }

  friend constexpr bool operator==(const CharT *lhs, basic_string_view rhs) noexcept {
    return basic_string_view(lhs) == rhs;
  }

  friend constexpr bool operator!=(basic_string_view lhs, const CharT *rhs) noexcept { return !(lhs == rhs); }

  friend constexpr bool operator!=(const CharT *lhs, basic_string_view rhs) noexcept { return !(lhs == rhs); }

  friend constexpr bool operator<(basic_string_view lhs, basic_string_view rhs) noexcept {
    return lhs.compare_impl(rhs) < 0;
  }

  friend constexpr bool operator<(basic_string_view lhs, base rhs) noexcept { return lhs < basic_string_view(rhs); }

  friend constexpr bool operator<(base lhs, basic_string_view rhs) noexcept { return basic_string_view(lhs) < rhs; }

  friend constexpr bool operator<(basic_string_view lhs, const CharT *rhs) noexcept {
    return lhs < basic_string_view(rhs);
  }

  friend constexpr bool operator<(const CharT *lhs, basic_string_view rhs) noexcept {
    return basic_string_view(lhs) < rhs;
  }

  [[nodiscard]] static constexpr basic_string_view from_range(const CharT *first, const CharT *last) noexcept {
    RELOCO_ASSERT(first <= last, "invalid pointer range for string_view");
    return basic_string_view(first, static_cast<size_type>(last - first));
  }

private:
  // Lexicographic three-way compare against `rhs`, matching
  // `std::basic_string_view::compare`'s semantics: trivial enough (a
  // `TraitsT::compare` on the common prefix plus a length tie-break) that
  // it is implemented locally instead of delegating to `std::`.
  [[nodiscard]] constexpr int compare_impl(basic_string_view rhs) const noexcept {
    const size_type common = std::min(size_, rhs.size_);
    const int r = common == 0 ? 0 : TraitsT::compare(data_, rhs.data_, common);
    if (r != 0)
      return r;
    if (size_ < rhs.size_)
      return -1;
    if (size_ > rhs.size_)
      return 1;
    return 0;
  }

  const_pointer data_ = nullptr;
  size_type size_ = 0;
};

using string_view = basic_string_view<char>;
using wstring_view = basic_string_view<wchar_t>;

RELOCO_END_UNSAFE_BUFFER_USAGE

} // namespace reloco

// See `type_id.hpp` for the full `RELOCO_TYPE_ID_NAME` rationale; only the
// two common instantiations are named here, not the generic
// `basic_string_view<CharT, TraitsT>` template itself.
RELOCO_TYPE_ID_NAME(reloco::string_view, "reloco::string_view");
RELOCO_TYPE_ID_NAME(reloco::wstring_view, "reloco::wstring_view");

namespace std {

template <typename CharT, typename TraitsT> struct hash<reloco::basic_string_view<CharT, TraitsT>> {
  [[nodiscard]] size_t operator()(reloco::basic_string_view<CharT, TraitsT> value) const noexcept {
    return hash<std::basic_string_view<CharT, TraitsT>>{}(value.to_std());
  }
};

} // namespace std
