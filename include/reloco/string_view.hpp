#pragma once
#include <reloco/assert.hpp>
#include <reloco/core.hpp>
#include <string_view>

namespace reloco {

template <typename CharT, typename TraitsT = std::char_traits<CharT>>
struct basic_string_view : std::basic_string_view<CharT, TraitsT> {
  using base = std::basic_string_view<CharT, TraitsT>;
  using size_type = typename base::size_type;
  using const_reference = typename base::const_reference;
  using const_pointer = typename base::const_pointer;
  using base::base;
  using base::empty;
  using base::size;

  constexpr basic_string_view(const basic_string_view &rhs) noexcept
      : std::basic_string_view<CharT, TraitsT>(rhs) {}

  constexpr basic_string_view(
      const std::basic_string_view<CharT, TraitsT> &rhs) noexcept
      : std::basic_string_view<CharT, TraitsT>(rhs) {}

  constexpr basic_string_view(std::nullptr_t) noexcept : base() {
    // We allow creating an empty view from nullptr,
    // but it will be subject to our hardened data() checks.
  }

  constexpr basic_string_view(const CharT *str, size_type len) noexcept
      : base(str, len) {
    // Data must not be null if length is > 0
    RELOCO_ASSERT((str != nullptr || len == 0) &&
                  "Attempted to construct string_view from nullptr with "
                  "non-zero length");
  }

  constexpr basic_string_view(const CharT *str) noexcept : base() {
    if (str == nullptr) {
      // pointing to nullptr results in an empty view
      return;
    }
    *this = base(str);
  }

  [[nodiscard]] constexpr const_reference
  operator[](size_type pos) const noexcept {
    RELOCO_ASSERT(pos < size(), "string_view index out of bounds");
    return base::operator[](pos);
  }

  [[nodiscard]] constexpr result<std::reference_wrapper<const CharT>>
  try_front() const noexcept {
    if (empty)
      return unexpected(error::container_empty);
    return base::front();
  }

  [[nodiscard]] constexpr result<std::reference_wrapper<const CharT>>
  try_back() const noexcept {
    if (empty)
      return unexpected(error::container_empty);
    return base::back();
  }

  [[nodiscard]] constexpr const_reference front() const noexcept {
    RELOCO_ASSERT(!empty(), "front() called on empty string_view");
    return base::front();
  }

  [[nodiscard]] constexpr const_reference back() const noexcept {
    RELOCO_ASSERT(!empty(), "back() called on empty string_view");
    return base::back();
  }

  [[nodiscard]] constexpr const_reference unsafe_front() const noexcept {
    RELOCO_DEBUG_ASSERT(!empty(), "front() called on empty string_view");
    return base::front();
  }

  [[nodiscard]] constexpr const_reference unsafe_back() const noexcept {
    RELOCO_DEBUG_ASSERT(!empty(), "back() called on empty string_view");
    return base::back();
  }

  [[nodiscard]] constexpr basic_string_view
  substr(size_type pos, size_type count = base::npos) const noexcept {
    RELOCO_ASSERT(pos <= size(), "substr position out of bounds");
    return basic_string_view(base::substr(pos, count));
  }

  [[nodiscard]] constexpr basic_string_view
  unsafe_substr(size_type pos, size_type count = base::npos) const noexcept {
    RELOCO_DEBUG_ASSERT(pos <= size(), "substr position out of bounds");
    return basic_string_view(base::substr(pos, count));
  }

  [[nodiscard]] constexpr result<std::reference_wrapper<const CharT>>
  try_at(size_type pos) const noexcept {
    if (pos >= size()) {
      return unexpected(error::out_of_bounds);
    }
    return std::ref(base::operator[](pos));
  }

  [[nodiscard]] constexpr result<basic_string_view>
  try_substr(size_type pos, size_type count = base::npos) const noexcept {
    if (pos > size()) {
      return unexpected(error::out_of_bounds);
    }
    return basic_string_view(base::substr(pos, count));
  }

  /**
   * @brief Asserted access to raw data.
   * Triggers RELOCO_ASSERT if the view is empty, as dereferencing
   * the result of data() on an empty view is usually a logic error.
   */
  [[nodiscard]] constexpr const_pointer data() const noexcept {
    RELOCO_ASSERT(!empty(), "data() called on empty string_view");
    return base::data();
  }

  /**
   * @brief Safe-by-default access to the raw pointer.
   * returns a result containing the pointer only if the view is non-empty.
   */
  [[nodiscard]] constexpr result<const_pointer> try_data() const noexcept {
    if (empty()) {
      return unexpected(error::container_empty);
    }
    return base::data();
  }

  /**
   * @brief Explicitly allows null pointer access if intended.
   * Use this only when passing to external APIs that explicitly handle null.
   */
  [[nodiscard]] constexpr const_pointer unsafe_data() const noexcept {
    return base::data();
  }

  constexpr base to_std() const noexcept { return static_cast<base>(*this); }

  constexpr void remove_prefix(size_type n) noexcept {
    RELOCO_ASSERT(n <= size(), "remove_prefix exceeds view size");
    base::remove_prefix(n);
  }

  constexpr void unsafe_remove_prefix(size_type n) noexcept {
    RELOCO_DEBUG_ASSERT(n <= size(), "remove_prefix exceeds view size");
    base::remove_prefix(n);
  }

  constexpr void remove_suffix(size_type n) noexcept {
    RELOCO_ASSERT(n <= size(), "remove_suffix exceeds view size");
    base::remove_suffix(n);
  }

  constexpr void unsafe_remove_suffix(size_type n) noexcept {
    RELOCO_DEBUG_ASSERT(n <= size(), "remove_suffix exceeds view size");
    base::remove_suffix(n);
  }

  constexpr result<void> try_remove_prefix(size_type n) noexcept {
    if (n > size())
      return unexpected(error::out_of_bounds);
    base::remove_prefix(n);
    return {};
  }

  constexpr result<void> try_remove_suffix(size_type n) noexcept {
    if (n > size())
      return unexpected(error::out_of_bounds);
    base::remove_suffix(n);
    return {};
  }

  static constexpr basic_string_view from_range(const CharT *first,
                                                const CharT *last) noexcept {
    RELOCO_ASSERT(first <= last, "Invalid pointer range for string_view");
    return safe_string_view(first, static_cast<size_type>(last - first));
  }
};

using string_view = basic_string_view<char>;
using wstring_view = basic_string_view<wchar_t>;

} // namespace reloco
