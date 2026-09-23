// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file sso_string.hpp
 * @brief Move-only, allocator-backed, growable character buffer with an
 * inline small-string optimization (SSO) buffer, mirroring the fallible
 * construction/mutation API of `reloco::basic_string`.
 *
 * `basic_sso_string<CharT, TraitsT>` behaves exactly like
 * `reloco::basic_string`: same `try_create`/`try_allocate`/`try_clone`/
 * `try_clone_at` protocol, same fallible mutators returning
 * `reloco::result<...>`, same checked/`try_*`/`unsafe_*` read-access tiers.
 * The one difference is storage: strings whose size never exceeds
 * `RELOCO_SSO_STRING_CAPACITY` characters (15 by default; see below) live
 * entirely inside the object itself, in a fixed-size buffer, and never
 * touch the allocator at all. Growing past that inline capacity
 * transparently falls back to a heap allocation obtained through the
 * bound `allocator_ref`, exactly like `basic_string` does from the start.
 * `shrink_to_fit()` can move a string that has shrunk back down to
 * `RELOCO_SSO_STRING_CAPACITY` or fewer characters back into the inline
 * buffer, releasing the heap allocation.
 *
 * This inline buffer is *self-referencing*: while a `basic_sso_string` is
 * in the small/inline state, its internal `data()` pointer points inside
 * the object itself. Unlike `basic_string` (whose heap pointer is never
 * self-referential and is therefore unconditionally trivially
 * relocatable), `basic_sso_string` can never be safely relocated with a
 * raw `memcpy` -- doing so while an instance is small would leave the
 * relocated copy's `data()` pointer dangling into the old object's
 * memory. Accordingly, `is_trivially_relocatable<basic_sso_string<...>>`
 * is unconditionally `false`; containers built on `is_trivially_relocatable`
 * (`vector`, `inline_vector`, `flat_set`, `flat_map`, ...) always use their
 * slower, safe, per-element move path for `basic_sso_string` elements.
 *
 * The inline capacity is a single, process-wide, compile-time constant:
 * define `RELOCO_SSO_STRING_CAPACITY` (via a compiler `-D` flag or
 * `reloco_user_config.hpp`, see `reloco_config.hpp`) to override the
 * default of 15 characters. It is not a template parameter, so every
 * `basic_sso_string<CharT, TraitsT>` instantiation in a program shares the
 * same inline capacity.
 *
 * Use `basic_sso_string` instead of `basic_string` when most expected
 * string values are short (fitting within the inline capacity) and
 * avoiding an allocation for them matters more than keeping the type
 * trivially relocatable. Use `basic_string` when relocatability matters
 * (e.g. as the element type of a `vector`/`flat_set` that is grown/erased
 * from frequently) or when strings are typically long enough that SSO
 * would rarely apply.
 */

#include "allocator.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "relocatable.hpp"
#include "rvalue_safety.hpp"
#include "string_view.hpp"
#include "type_id.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#ifndef RELOCO_SSO_STRING_CAPACITY
#define RELOCO_SSO_STRING_CAPACITY 15
#endif

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

template <typename CharT, typename TraitsT = std::char_traits<CharT>> class RELOCO_OWNER basic_sso_string {
public:
  using view_type = basic_string_view<CharT, TraitsT>;
  using traits_type = TraitsT;
  using value_type = CharT;
  using allocator_type = allocator_ref;
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
  static constexpr size_type sso_capacity = RELOCO_SSO_STRING_CAPACITY;

  RELOCO_BLOCK_RVALUE_ACCESS(CharT);

  constexpr basic_sso_string() noexcept : basic_sso_string(default_allocator()) {}

  constexpr explicit basic_sso_string(allocator_ref alloc) noexcept : alloc_(alloc) {}

  basic_sso_string(const basic_sso_string &) = delete;
  basic_sso_string &operator=(const basic_sso_string &) = delete;

  basic_sso_string(basic_sso_string &&other) noexcept : alloc_(other.alloc_) {
    if (other.is_inline()) {
      TraitsT::copy(sso_buf_, other.sso_buf_, other.size_ + 1);
      data_ = sso_buf_;
      size_ = other.size_;
      cap_ = sso_capacity;
    } else {
      data_ = other.data_;
      size_ = other.size_;
      cap_ = other.cap_;
    }
    other.reset_to_empty_inline();
  }

  basic_sso_string &operator=(basic_sso_string &&other) noexcept {
    if (this != &other) {
      release();
      alloc_ = other.alloc_;
      if (other.is_inline()) {
        TraitsT::copy(sso_buf_, other.sso_buf_, other.size_ + 1);
        data_ = sso_buf_;
        size_ = other.size_;
        cap_ = sso_capacity;
      } else {
        data_ = other.data_;
        size_ = other.size_;
        cap_ = other.cap_;
      }
      other.reset_to_empty_inline();
    }
    return *this;
  }

  ~basic_sso_string() noexcept { release(); }

  // ---- fallible construction / cloning (see concepts.hpp) ----

  /**
   * @brief Allocates and initializes a `basic_sso_string` from @p sv, using
   * the given allocator.
   */
  [[nodiscard]] static result<basic_sso_string> try_allocate(allocator_ref alloc, view_type sv = view_type()) noexcept {
    basic_sso_string str(alloc);
    if (!sv.empty()) {
      auto res = str.try_append(sv);
      if (!res)
        return unexpected(res.error());
    }
    return str;
  }

  /**
   * @brief Allocates and initializes a `basic_sso_string` from @p sv, using
   * the process-wide default allocator (see `default_allocator()`).
   */
  [[nodiscard]] static result<basic_sso_string> try_create(view_type sv = view_type()) noexcept {
    return try_allocate(default_allocator(), sv);
  }

  /**
   * @brief Fallible deep copy using a caller-chosen allocator.
   */
  [[nodiscard]] result<basic_sso_string> try_clone(allocator_ref alloc) const noexcept {
    return try_allocate(alloc, view());
  }

  /**
   * @brief Fallible deep copy reusing this string's own allocator.
   */
  [[nodiscard]] result<basic_sso_string> try_clone() const noexcept { return try_clone(alloc_); }

  /**
   * @brief Fallible deep copy directly into uninitialized storage.
   */
  [[nodiscard]] static result<void> try_clone_at(allocator_ref alloc, basic_sso_string *storage,
                                                 const basic_sso_string &source) noexcept {
    auto res = try_allocate(alloc, source.view());
    if (!res)
      return unexpected(res.error());
    new (storage) basic_sso_string(std::move(*res));
    return {};
  }

  // ---- capacity ----

  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr size_type length() const noexcept { return size_; }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return cap_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr allocator_ref get_allocator() const noexcept { return alloc_; }
  [[nodiscard]] constexpr bool is_inline() const noexcept { return data_ == sso_buf_; }

  /**
   * @brief Ensures capacity for at least @p new_cap characters (excluding
   * the trailing null terminator), growing the backing allocation (or
   * promoting out of the inline buffer) if needed.
   */
  [[nodiscard]] result<void> try_reserve(size_type new_cap) & noexcept {
    if (new_cap <= cap_)
      return {};

    const size_type required_bytes = (new_cap + 1) * sizeof(CharT);

    if (is_inline()) {
      auto res = alloc_.allocate(required_bytes, alignof(CharT));
      if (!res)
        return unexpected(res.error());
      auto *new_data = static_cast<CharT *>(res->ptr);
      TraitsT::copy(new_data, sso_buf_, size_ + 1);
      data_ = new_data;
      cap_ = new_cap;
      return {};
    }

    if (auto res = alloc_.expand_in_place(data_, (cap_ + 1) * sizeof(CharT), required_bytes); res) {
      cap_ = new_cap;
      return {};
    }

    auto res = alloc_.reallocate(data_, (cap_ + 1) * sizeof(CharT), required_bytes, alignof(CharT));
    if (!res)
      return unexpected(res.error());

    data_ = static_cast<CharT *>(res->ptr);
    cap_ = new_cap;
    data_[size_] = CharT();
    return {};
  }

  /**
   * @brief Releases unused capacity back to the allocator, if supported,
   * moving back into the inline buffer when the current content once
   * again fits within `sso_capacity`.
   */
  [[nodiscard]] result<void> shrink_to_fit() & noexcept {
    if (is_inline())
      return {};

    if (size_ <= sso_capacity) {
      TraitsT::copy(sso_buf_, data_, size_);
      sso_buf_[size_] = CharT();
      alloc_.deallocate(data_, (cap_ + 1) * sizeof(CharT));
      data_ = sso_buf_;
      cap_ = sso_capacity;
      return {};
    }

    if (cap_ <= size_)
      return {};

    auto res = alloc_.reallocate(data_, (cap_ + 1) * sizeof(CharT), (size_ + 1) * sizeof(CharT), alignof(CharT));
    if (!res)
      return unexpected(res.error());

    data_ = static_cast<CharT *>(res->ptr);
    cap_ = size_;
    return {};
  }

  // ---- mutation ----

  [[nodiscard]] result<void> try_assign(view_type sv) & noexcept {
    if (sv.empty()) {
      size_ = 0;
      data_[0] = CharT();
      return {};
    }

    if (auto materialized = materialize_if_aliasing(sv); materialized) {
      if (!*materialized)
        return unexpected(materialized->error());
      return try_assign((*materialized)->view());
    }

    const size_type new_size = sv.size();
    if (new_size > cap_) {
      auto res = try_reserve(std::max(cap_ * 2, new_size));
      if (!res)
        return res;
    }

    TraitsT::copy(data_, sv.data(), sv.size());
    size_ = new_size;
    data_[size_] = CharT();
    return {};
  }

  [[nodiscard]] result<void> try_append(view_type sv) & noexcept {
    if (sv.empty())
      return {};

    if (auto materialized = materialize_if_aliasing(sv); materialized) {
      if (!*materialized)
        return unexpected(materialized->error());
      return try_append((*materialized)->view());
    }

    const size_type new_size = size_ + sv.size();
    if (new_size > cap_) {
      auto res = try_reserve(std::max(cap_ * 2, new_size));
      if (!res)
        return res;
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
    if (pos > size_)
      return unexpected(error::out_of_bounds);
    if (sv.empty())
      return {};

    if (auto materialized = materialize_if_aliasing(sv); materialized) {
      if (!*materialized)
        return unexpected(materialized->error());
      return try_insert(pos, (*materialized)->view());
    }

    const size_type len = sv.size();
    auto res = try_reserve(std::max(cap_ * 2, size_ + len));
    if (!res)
      return res;

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

    auto res = try_reserve(count);
    if (!res)
      return res;

    TraitsT::assign(data_ + size_, count - size_, ch);
    size_ = count;
    data_[size_] = CharT();
    return {};
  }

  void clear() & noexcept {
    size_ = 0;
    data_[0] = CharT();
  }

  // ---- element access (checked / fallible / explicitly-unsafe tiers) ----

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
   * Marked `RELOCO_UNSAFE_BUFFER_USAGE`: like `unique_ptr::unsafe_get`,
   * this hands out a raw pointer with no bounds tracking of its own -- the
   * caller must not read past the null terminator or outlive `*this`.
   */
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const_pointer unsafe_c_str() const & noexcept RELOCO_LIFETIMEBOUND {
    return data_;
  }

  [[nodiscard]] view_type view() const & noexcept RELOCO_LIFETIMEBOUND { return view_type(data(), size_); }

  [[nodiscard]] operator view_type() const & noexcept RELOCO_LIFETIMEBOUND { return view(); }

  [[nodiscard]] operator std::basic_string_view<CharT, TraitsT>() const & noexcept RELOCO_LIFETIMEBOUND {
    return std::basic_string_view<CharT, TraitsT>(data(), size_);
  }

  explicit operator std::basic_string<CharT, TraitsT>() const {
    return std::basic_string<CharT, TraitsT>(data(), size_);
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

  // ---- searching ----

  [[nodiscard]] size_type find(view_type sv, size_type pos = 0) const noexcept { return view().find(sv, pos); }
  [[nodiscard]] size_type find(CharT ch, size_type pos = 0) const noexcept { return view().find(ch, pos); }
  [[nodiscard]] bool contains(view_type sv) const noexcept { return view().find(sv) != npos; }
  [[nodiscard]] bool starts_with(view_type sv) const noexcept { return view().starts_with(sv); }
  [[nodiscard]] bool starts_with(CharT ch) const noexcept { return view().starts_with(ch); }

  // ---- comparison ----

  friend bool operator==(const basic_sso_string &lhs, const basic_sso_string &rhs) noexcept {
    return lhs.view() == rhs.view();
  }
  friend bool operator!=(const basic_sso_string &lhs, const basic_sso_string &rhs) noexcept { return !(lhs == rhs); }
  friend bool operator<(const basic_sso_string &lhs, const basic_sso_string &rhs) noexcept {
    return lhs.view() < rhs.view();
  }

  friend bool operator==(const basic_sso_string &lhs, view_type rhs) noexcept { return lhs.view() == rhs; }
  friend bool operator==(view_type lhs, const basic_sso_string &rhs) noexcept { return lhs == rhs.view(); }
  friend bool operator!=(const basic_sso_string &lhs, view_type rhs) noexcept { return !(lhs == rhs); }
  friend bool operator!=(view_type lhs, const basic_sso_string &rhs) noexcept { return !(lhs == rhs); }

private:
  /**
   * @brief Reports whether @p sv references bytes inside this string's own
   * live content -- i.e. whether it is (or was derived from) `this->view()`
   * or a substring of it. See `basic_string::aliases` for the full
   * rationale; identical here.
   */
  [[nodiscard]] bool aliases(view_type sv) const noexcept {
    return !sv.empty() && sv.data() >= data_ && sv.data() < data_ + size_;
  }

  /**
   * @brief If @p sv aliases this string's own buffer (see @ref aliases),
   * returns an independent, freshly allocated copy of its contents;
   * otherwise returns `std::nullopt` so the caller can use @p sv directly.
   */
  [[nodiscard]] std::optional<result<basic_sso_string>> materialize_if_aliasing(view_type sv) const noexcept {
    if (!aliases(sv))
      return std::nullopt;
    basic_sso_string copy(alloc_);
    auto res = copy.try_append(sv);
    if (!res)
      return result<basic_sso_string>(unexpected(res.error()));
    return result<basic_sso_string>(std::move(copy));
  }

  /**
   * @brief Resets `*this` to the empty, inline-buffer state, used to leave
   * a well-defined moved-from object regardless of which state (small or
   * heap-backed) it held its content in beforehand. Does not free any heap
   * allocation -- callers that steal a heap pointer out of `*this` must
   * not call this until after doing so.
   */
  void reset_to_empty_inline() noexcept {
    data_ = sso_buf_;
    size_ = 0;
    cap_ = sso_capacity;
    sso_buf_[0] = CharT();
  }

  void release() noexcept {
    if (!is_inline())
      alloc_.deallocate(data_, (cap_ + 1) * sizeof(CharT));
  }

  allocator_ref alloc_{};
  CharT sso_buf_[sso_capacity + 1]{};
  CharT *data_{sso_buf_};
  size_type size_{0};
  size_type cap_{sso_capacity};
};

using sso_string = basic_sso_string<char>;
using wsso_string = basic_sso_string<wchar_t>;

/**
 * @brief `basic_sso_string` self-references its own inline buffer while
 * small: `data()` can point inside the object itself. Relocating such an
 * instance with a raw `memcpy` (as `is_trivially_relocatable` licenses)
 * would leave the relocated copy's `data()` dangling into the old
 * object's memory, so this is unconditionally `false` regardless of
 * `CharT`/`TraitsT`, unlike `basic_string`.
 */
template <typename CharT, typename TraitsT>
struct is_trivially_relocatable<basic_sso_string<CharT, TraitsT>> : std::false_type {};

} // namespace reloco

// See `type_id.hpp` for the full `RELOCO_TYPE_ID_NAME` rationale; only the
// two common instantiations are named here, not the generic
// `basic_sso_string<CharT, TraitsT>` template itself.
RELOCO_TYPE_ID_NAME(reloco::sso_string, "reloco::sso_string");
RELOCO_TYPE_ID_NAME(reloco::wsso_string, "reloco::wsso_string");

RELOCO_END_UNSAFE_BUFFER_USAGE

namespace std {

template <typename CharT, typename TraitsT> struct hash<reloco::basic_sso_string<CharT, TraitsT>> {
  [[nodiscard]] size_t operator()(const reloco::basic_sso_string<CharT, TraitsT> &value) const noexcept {
    return hash<basic_string_view<CharT, TraitsT>>{}(value);
  }
};

} // namespace std
