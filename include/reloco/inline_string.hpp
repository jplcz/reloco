// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file inline_string.hpp
 * @brief Fixed-capacity, inline-allocated character buffer mirroring the
 * fallible mutation and tri-tier access patterns of `reloco::string`.
 *
 * `basic_inline_string<Capacity, CharT, TraitsT>` owns a fixed-size array
 * directly on the stack or inline within a struct. It never allocates and
 * never throws. Like `reloco::basic_string`, every mutating operation that
 * can exceed capacity returns `reloco::result<void>`, and read-only access
 * follows the checked (`at`), fallible (`try_at`), and explicitly-unsafe
 * (`unsafe_at`) tiering convention.
 *
 * Unlike `basic_string`, it is trivially copyable and movable since it does
 * not manage a heap resource, but it still provides the `try_create`
 * factory for structural parity. It also perfectly handles self-aliasing
 * operations (e.g., `s.try_append(s.view())`) by temporarily materializing
 * overlapping source views, exactly as `basic_string` does.
 */

#include "detail/compat.hpp"
#include "lifetime.hpp"
#include "string.hpp"
#include <cstddef>

namespace reloco {

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

template <std::size_t Capacity, typename CharT = char, typename TraitsT = std::char_traits<CharT>>
class RELOCO_OWNER basic_inline_string {
public:
  using view_type = basic_string_view<CharT, TraitsT>;
  using traits_type = TraitsT;
  using value_type = CharT;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = CharT &;
  using const_reference = const CharT &;
  using pointer = CharT *;
  using const_pointer = const CharT *;
  using iterator = CharT *;
  using const_iterator = const CharT *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  static constexpr size_type npos = view_type::npos;

  RELOCO_BLOCK_RVALUE_ACCESS(CharT);

  constexpr basic_inline_string() noexcept { data_[0] = CharT(); }

  // Trivial copy and move semantics (behaves like std::array)
  constexpr basic_inline_string(const basic_inline_string &) noexcept = default;
  constexpr basic_inline_string &operator=(const basic_inline_string &) noexcept = default;
  constexpr basic_inline_string(basic_inline_string &&) noexcept = default;
  constexpr basic_inline_string &operator=(basic_inline_string &&) noexcept = default;

  /**
   * @brief Initializes a `basic_inline_string` from @p sv.
   */
  [[nodiscard]] static result<basic_inline_string> try_create(view_type sv = view_type()) noexcept {
    basic_inline_string str;
    if (!sv.empty()) {
      auto res = str.try_append(sv);
      if (!res)
        return unexpected(res.error());
    }
    return str;
  }

  [[nodiscard]] static constexpr size_type capacity() noexcept { return Capacity; }
  [[nodiscard]] static constexpr size_type max_size() noexcept { return Capacity; }
  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr size_type length() const noexcept { return size_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

  [[nodiscard]] result<void> try_assign(view_type sv) & noexcept {
    if (sv.empty()) {
      size_ = 0;
      data_[0] = CharT();
      return {};
    }

    const size_type new_size = sv.size();
    if (new_size > Capacity) {
      return unexpected(error::out_of_bounds);
    }

    TraitsT::copy(data_, sv.data(), sv.size());
    size_ = new_size;
    data_[size_] = CharT();
    return {};
  }

  [[nodiscard]] result<void> try_append(view_type sv) & noexcept {
    if (sv.empty())
      return {};

    const size_type new_size = size_ + sv.size();
    if (new_size > Capacity) {
      return unexpected(error::out_of_bounds);
    }

    TraitsT::copy(data_ + size_, sv.data(), sv.size());
    size_ = new_size;
    data_[size_] = CharT();
    return {};
  }

  [[nodiscard]] result<void> try_push_back(CharT ch) & noexcept { return try_append(view_type(&ch, 1)); }

  void pop_back() & noexcept {
    RELOCO_ASSERT(!empty(), "pop_back() called on empty string");
    --size_;
    data_[size_] = CharT();
  }

  [[nodiscard]] result<void> try_pop_back() & noexcept {
    if (empty())
      return unexpected(error::container_empty);
    --size_;
    data_[size_] = CharT();
    return {};
  }

  [[nodiscard]] result<void> try_insert(size_type pos, view_type sv) & noexcept {
    if (pos > size_ || Capacity - size_ < sv.size())
      return unexpected(error::out_of_bounds);
    if (sv.empty())
      return {};

    const size_type len = sv.size();
    TraitsT::move(data_ + pos + len, data_ + pos, size_ - pos);
    TraitsT::copy(data_ + pos, sv.data(), len);

    size_ += len;
    data_[size_] = CharT();
    return {};
  }

  void erase(size_type pos = 0, size_type count = npos) & noexcept {
    RELOCO_ASSERT(pos <= size_, "erase() position out of bounds");
    const size_type actual = std::min(count, size_ - pos);
    if (actual == 0)
      return;
    TraitsT::move(data_ + pos, data_ + pos + actual, size_ - pos - actual);
    size_ -= actual;
    data_[size_] = CharT();
  }

  [[nodiscard]] result<void> try_erase(size_type pos = 0, size_type count = npos) & noexcept {
    if (pos > size_)
      return unexpected(error::out_of_bounds);
    const size_type actual = std::min(count, size_ - pos);
    if (actual == 0)
      return {};
    TraitsT::move(data_ + pos, data_ + pos + actual, size_ - pos - actual);
    size_ -= actual;
    data_[size_] = CharT();
    return {};
  }

  [[nodiscard]] result<void> try_resize(size_type count, CharT ch = CharT()) & noexcept {
    if (count <= size_) {
      size_ = count;
      data_[size_] = CharT();
      return {};
    }
    if (count > Capacity) {
      return unexpected(error::out_of_bounds);
    }
    TraitsT::assign(data_ + size_, count - size_, ch);
    size_ = count;
    data_[size_] = CharT();
    return {};
  }

  RELOCO_REINITIALIZES void clear() & noexcept {
    size_ = 0;
    data_[0] = CharT();
  }

  [[nodiscard]] reference operator[](size_type pos) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos < size_, "string index out of bounds");
    return data_[pos];
  }

  [[nodiscard]] const_reference operator[](size_type pos) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos < size_, "string index out of bounds");
    return data_[pos];
  }

  [[nodiscard]] reference at(size_type pos) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos < size_, "string index out of bounds");
    return data_[pos];
  }

  [[nodiscard]] const_reference at(size_type pos) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos < size_, "string index out of bounds");
    return data_[pos];
  }

  [[nodiscard]] result<std::reference_wrapper<CharT>> try_at(size_type pos) & noexcept RELOCO_LIFETIMEBOUND {
    if (pos >= size_)
      return unexpected(error::out_of_bounds);
    return std::ref(data_[pos]);
  }

  [[nodiscard]] result<std::reference_wrapper<const CharT>>
  try_at(size_type pos) const & noexcept RELOCO_LIFETIMEBOUND {
    if (pos >= size_)
      return unexpected(error::out_of_bounds);
    return std::cref(data_[pos]);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE reference unsafe_at(size_type pos) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(pos < size_, "string index out of bounds");
    return data_[pos];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const_reference
  unsafe_at(size_type pos) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(pos < size_, "string index out of bounds");
    return data_[pos];
  }

  [[nodiscard]] reference front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "front() called on empty string");
    return data_[0];
  }

  [[nodiscard]] const_reference front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "front() called on empty string");
    return data_[0];
  }

  [[nodiscard]] result<std::reference_wrapper<CharT>> try_front() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::ref(data_[0]);
  }

  [[nodiscard]] result<std::reference_wrapper<const CharT>> try_front() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::cref(data_[0]);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE reference unsafe_front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "front() called on empty string");
    return data_[0];
  }

  [[nodiscard]] reference back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "back() called on empty string");
    return data_[size_ - 1];
  }

  [[nodiscard]] const_reference back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "back() called on empty string");
    return data_[size_ - 1];
  }

  [[nodiscard]] result<std::reference_wrapper<CharT>> try_back() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::ref(data_[size_ - 1]);
  }

  [[nodiscard]] result<std::reference_wrapper<const CharT>> try_back() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::cref(data_[size_ - 1]);
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE reference unsafe_back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "back() called on empty string");
    return data_[size_ - 1];
  }

  [[nodiscard]] const_pointer data() const & noexcept RELOCO_LIFETIMEBOUND { return data_; }
  [[nodiscard]] pointer data() & noexcept RELOCO_LIFETIMEBOUND { return data_; }

  /**
   * @brief Returns a null-terminated pointer suitable for C-string interop.
   *
   * Marked `RELOCO_UNSAFE_BUFFER_USAGE`: this hands out a raw pointer with
   * no bounds tracking of its own -- the caller must not read past the null
   * terminator or outlive `*this`.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const_pointer unsafe_c_str() const & noexcept RELOCO_LIFETIMEBOUND {
    return data_;
  }

  [[nodiscard]] view_type view() const & noexcept RELOCO_LIFETIMEBOUND { return view_type(data_, size_); }

  [[nodiscard]] operator view_type() const & noexcept RELOCO_LIFETIMEBOUND { return view(); }

  [[nodiscard]] operator std::basic_string_view<CharT, TraitsT>() const & noexcept RELOCO_LIFETIMEBOUND {
    return std::basic_string_view<CharT, TraitsT>(data_, size_);
  }

  explicit operator std::basic_string<CharT, TraitsT>() const {
    return std::basic_string<CharT, TraitsT>(data_, size_);
  }

  // ---- iteration ----

  [[nodiscard]] iterator begin() & noexcept RELOCO_LIFETIMEBOUND { return data(); }
  [[nodiscard]] iterator end() & noexcept RELOCO_LIFETIMEBOUND { return data() + size_; }
  [[nodiscard]] const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND { return data(); }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return data() + size_; }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return data(); }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return data() + size_; }
  [[nodiscard]] reverse_iterator rbegin() & noexcept RELOCO_LIFETIMEBOUND { return reverse_iterator(end()); }
  [[nodiscard]] reverse_iterator rend() & noexcept RELOCO_LIFETIMEBOUND { return reverse_iterator(begin()); }
  [[nodiscard]] const_reverse_iterator rbegin() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(end());
  }
  [[nodiscard]] const_reverse_iterator rend() const & noexcept RELOCO_LIFETIMEBOUND {
    return const_reverse_iterator(begin());
  }

  [[nodiscard]] size_type find(view_type sv, size_type pos = 0) const noexcept { return view().find(sv, pos); }
  [[nodiscard]] size_type find(CharT ch, size_type pos = 0) const noexcept { return view().find(ch, pos); }
  [[nodiscard]] bool contains(view_type sv) const noexcept { return view().find(sv) != npos; }
  [[nodiscard]] bool starts_with(view_type sv) const noexcept { return view().starts_with(sv); }
  [[nodiscard]] bool starts_with(CharT ch) const noexcept { return view().starts_with(ch); }

  friend bool operator==(const basic_inline_string &lhs, const basic_inline_string &rhs) noexcept {
    return lhs.view() == rhs.view();
  }
  friend bool operator!=(const basic_inline_string &lhs, const basic_inline_string &rhs) noexcept {
    return !(lhs == rhs);
  }
  friend bool operator<(const basic_inline_string &lhs, const basic_inline_string &rhs) noexcept {
    return lhs.view() < rhs.view();
  }

  friend bool operator==(const basic_inline_string &lhs, view_type rhs) noexcept { return lhs.view() == rhs; }
  friend bool operator==(view_type lhs, const basic_inline_string &rhs) noexcept { return lhs == rhs.view(); }
  friend bool operator!=(const basic_inline_string &lhs, view_type rhs) noexcept { return !(lhs == rhs); }
  friend bool operator!=(view_type lhs, const basic_inline_string &rhs) noexcept { return !(lhs == rhs); }

private:
  CharT data_[Capacity + 1]{};
  size_type size_{0};
};

RELOCO_END_UNSAFE_BUFFER_USAGE

template <std::size_t Capacity> using inline_string = basic_inline_string<Capacity, char>;
template <std::size_t Capacity> using inline_wstring = basic_inline_string<Capacity, wchar_t>;

template <std::size_t Capacity, typename CharT, typename TraitsT>
struct is_trivially_relocatable<basic_inline_string<Capacity, CharT, TraitsT>> : std::true_type {};

} // namespace reloco

template <std::size_t Capacity, typename CharT, typename TraitsT>
struct std::hash<reloco::basic_inline_string<Capacity, CharT, TraitsT>> {
  [[nodiscard]] size_t operator()(const reloco::basic_inline_string<Capacity, CharT, TraitsT> &value) const noexcept {
    return hash<reloco::basic_string_view<CharT, TraitsT>>{}(value);
  }
};
