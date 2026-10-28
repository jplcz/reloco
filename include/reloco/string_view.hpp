// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "byte_like.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "optional.hpp"
#include "type_id.hpp"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// Unsafe by design: string_view is the bounds-carrying view over raw character
// storage, and its C-string interop takes null-terminated `const CharT *`.
RELOCO_BEGIN_UNSAFE_CODE

namespace reloco {

template <typename T> class span; // complete type needed only where the span interop is used (include span.hpp)
template <typename CharT, typename TraitsT>
class basic_string; // complete type needed only where try_to_string() is called

// All of the below classes contain checked pointer arithmetic
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace detail {

// A pattern is a single character, a predicate over one character, or a string (anything convertible to the view).
template <typename SV, typename Pat> struct string_pattern {
  using decayed = std::decay_t<Pat>;
  static constexpr bool is_char = std::is_same_v<decayed, typename SV::value_type>;
  static constexpr bool is_pred = !is_char && std::is_invocable_r_v<bool, const decayed &, typename SV::value_type>;
  using type = std::conditional_t<is_char || is_pred, decayed, SV>;
};

template <typename SV, typename Pat> using string_pattern_t = typename string_pattern<SV, Pat>::type;

template <typename SV, typename Pat>
[[nodiscard]] constexpr string_pattern_t<SV, Pat> make_pattern(Pat &&pat) noexcept {
  return string_pattern_t<SV, Pat>(std::forward<Pat>(pat));
}

struct string_match {
  std::size_t pos; // SV::npos when nothing matched
  std::size_t len;
};

template <typename SV, typename P>
[[nodiscard]] constexpr string_match find_pattern(const SV &hay, std::size_t from, const P &pat) noexcept {
  constexpr std::size_t none = SV::npos;
  if constexpr (std::is_same_v<P, typename SV::value_type>) {
    const std::size_t p = hay.find(pat, from);
    return {p, p == none ? std::size_t{0} : std::size_t{1}};
  } else if constexpr (std::is_invocable_r_v<bool, const P &, typename SV::value_type>) {
    for (std::size_t i = from; i < hay.size(); ++i)
      if (pat(hay.data()[i]))
        return {i, 1};
    return {none, 0};
  } else {
    if (pat.empty())
      return {none, 0};
    const std::size_t p = hay.find(pat, from);
    return {p, p == none ? std::size_t{0} : pat.size()};
  }
}

template <typename SV, typename P>
[[nodiscard]] constexpr string_match rfind_pattern(const SV &hay, const P &pat) noexcept {
  constexpr std::size_t none = SV::npos;
  if constexpr (std::is_same_v<P, typename SV::value_type>) {
    const std::size_t p = hay.rfind(pat);
    return {p, p == none ? std::size_t{0} : std::size_t{1}};
  } else if constexpr (std::is_invocable_r_v<bool, const P &, typename SV::value_type>) {
    for (std::size_t i = hay.size(); i > 0; --i)
      if (pat(hay.data()[i - 1]))
        return {i - 1, 1};
    return {none, 0};
  } else {
    if (pat.empty())
      return {none, 0};
    const std::size_t p = hay.rfind(pat);
    return {p, p == none ? std::size_t{0} : pat.size()};
  }
}

// Length of the match anchored at the start (or end) of `hay`, or npos.
template <typename SV, typename P>
[[nodiscard]] constexpr std::size_t match_start(const SV &hay, const P &pat) noexcept {
  if constexpr (std::is_same_v<P, typename SV::value_type>) {
    return hay.starts_with(pat) ? 1 : SV::npos;
  } else if constexpr (std::is_invocable_r_v<bool, const P &, typename SV::value_type>) {
    return !hay.empty() && pat(hay.data()[0]) ? 1 : SV::npos;
  } else {
    return !pat.empty() && hay.starts_with(pat) ? pat.size() : SV::npos;
  }
}

template <typename SV, typename P> [[nodiscard]] constexpr std::size_t match_end(const SV &hay, const P &pat) noexcept {
  if constexpr (std::is_same_v<P, typename SV::value_type>) {
    return hay.ends_with(pat) ? 1 : SV::npos;
  } else if constexpr (std::is_invocable_r_v<bool, const P &, typename SV::value_type>) {
    return !hay.empty() && pat(hay.data()[hay.size() - 1]) ? 1 : SV::npos;
  } else {
    return !pat.empty() && hay.ends_with(pat) ? pat.size() : SV::npos;
  }
}

struct string_split_options {
  std::size_t limit = static_cast<std::size_t>(-1); // maximum number of pieces
  bool reverse = false;                             // scan from the end (rsplit)
  bool skip_trailing_empty = false;                 // split_terminator / split_inclusive / lines
  bool inclusive = false;                           // keep the separator at the end of each piece
  bool skip_empty = false;                          // split_whitespace
  bool strip_cr = false;                            // lines: drop a '\r' before the '\n'
};

struct string_range_end {};

/// Two adjacent pieces of a text (`split_at`, `split_once`); usable with structured bindings.
template <typename SV> struct string_pair {
  SV first;
  SV second;
};

/**
 * @brief Lazy range of pieces produced by `split`, `rsplit`, `splitn`, `rsplitn`, `split_terminator`,
 * `split_inclusive`, `split_whitespace` and `lines`. Pieces are views into the original text; nothing is allocated.
 */
template <typename SV, typename Pat> class string_split {
public:
  using value_type = SV;

  class iterator {
  public:
    using value_type = SV;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    iterator(SV text, Pat pat, string_split_options opt) noexcept
        : m_rest(text), m_pat(std::move(pat)), m_opt(opt), m_left(opt.limit),
          m_done(opt.skip_trailing_empty && text.empty()) {
      advance();
    }

    [[nodiscard]] SV operator*() const noexcept { return m_piece; }

    iterator &operator++() noexcept {
      advance();
      return *this;
    }

    [[nodiscard]] friend bool operator==(const iterator &it, string_range_end) noexcept { return !it.m_valid; }
    [[nodiscard]] friend bool operator!=(const iterator &it, string_range_end) noexcept { return it.m_valid; }

  private:
    void advance() noexcept {
      m_valid = false;
      while (!m_done) {
        if (m_left == 0) {
          m_done = true;
          return;
        }
        SV piece;
        if (m_left == 1) {
          piece = m_rest;
          m_done = true;
        } else if (!m_opt.reverse) {
          const string_match m = find_pattern(m_rest, 0, m_pat);
          if (m.pos == SV::npos) {
            piece = m_rest;
            m_done = true;
          } else {
            piece = m_rest.substr(0, m.pos + (m_opt.inclusive ? m.len : 0));
            m_rest = m_rest.substr(m.pos + m.len);
            if (m_left != SV::npos)
              --m_left;
            if (m_opt.skip_trailing_empty && m_rest.empty())
              m_done = true;
          }
        } else {
          const string_match m = rfind_pattern(m_rest, m_pat);
          if (m.pos == SV::npos) {
            piece = m_rest;
            m_done = true;
          } else {
            piece = m_rest.substr(m.pos + m.len);
            m_rest = m_rest.substr(0, m.pos);
            if (m_left != SV::npos)
              --m_left;
          }
        }
        if (m_opt.skip_empty && piece.empty())
          continue;
        if (m_opt.strip_cr && piece.ends_with(typename SV::value_type('\r')))
          piece.remove_suffix(1);
        m_piece = piece;
        m_valid = true;
        return;
      }
    }

    SV m_piece{};
    SV m_rest;
    Pat m_pat;
    string_split_options m_opt;
    std::size_t m_left;
    bool m_done;
    bool m_valid = false;
  };

  string_split(SV text, Pat pat, string_split_options opt) noexcept : m_text(text), m_pat(std::move(pat)), m_opt(opt) {}

  [[nodiscard]] iterator begin() const noexcept { return iterator(m_text, m_pat, m_opt); }
  [[nodiscard]] string_range_end end() const noexcept { return {}; }

private:
  SV m_text;
  Pat m_pat;
  string_split_options m_opt;
};

/// Lazy range of the non-overlapping matches of a pattern (`matches`), or of `(index, match)` pairs (`match_indices`).
template <typename SV, typename Pat, bool Indices> class string_matches {
public:
  using value_type = std::conditional_t<Indices, std::pair<std::size_t, SV>, SV>;

  class iterator {
  public:
    using value_type = string_matches::value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    iterator(SV text, Pat pat) noexcept : m_text(text), m_pat(std::move(pat)) { advance(); }

    [[nodiscard]] value_type operator*() const noexcept {
      const SV found = m_text.substr(m_match.pos, m_match.len);
      if constexpr (Indices)
        return value_type(m_match.pos, found);
      else
        return found;
    }

    iterator &operator++() noexcept {
      advance();
      return *this;
    }

    [[nodiscard]] friend bool operator==(const iterator &it, string_range_end) noexcept { return !it.m_valid; }
    [[nodiscard]] friend bool operator!=(const iterator &it, string_range_end) noexcept { return it.m_valid; }

  private:
    void advance() noexcept {
      m_match = find_pattern(m_text, m_next, m_pat);
      m_valid = m_match.pos != SV::npos;
      if (m_valid)
        m_next = m_match.pos + m_match.len;
    }

    SV m_text;
    Pat m_pat;
    string_match m_match{SV::npos, 0};
    std::size_t m_next = 0;
    bool m_valid = false;
  };

  string_matches(SV text, Pat pat) noexcept : m_text(text), m_pat(std::move(pat)) {}

  [[nodiscard]] iterator begin() const noexcept { return iterator(m_text, m_pat); }
  [[nodiscard]] string_range_end end() const noexcept { return {}; }

private:
  SV m_text;
  Pat m_pat;
};

struct utf8_decoded {
  char32_t code_point;
  std::size_t length;
  bool valid;
};

// Decodes the UTF-8 sequence starting at `pos`; malformed input yields U+FFFD with length 1.
template <typename SV> [[nodiscard]] constexpr utf8_decoded decode_utf8(const SV &text, std::size_t pos) noexcept {
  constexpr utf8_decoded bad{0xFFFD, 1, false};
  const auto byte_at = [&text](std::size_t i) {
    return static_cast<unsigned>(static_cast<unsigned char>(text.data()[i]));
  };
  const unsigned b0 = byte_at(pos);
  if (b0 < 0x80)
    return {static_cast<char32_t>(b0), 1, true};
  std::size_t len = 0;
  unsigned cp = 0;
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    len = 2;
    cp = b0 & 0x1Fu;
  } else if (b0 >= 0xE0 && b0 <= 0xEF) {
    len = 3;
    cp = b0 & 0x0Fu;
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    len = 4;
    cp = b0 & 0x07u;
  } else {
    return bad;
  }
  if (text.size() - pos < len)
    return bad;
  for (std::size_t i = 1; i < len; ++i) {
    const unsigned b = byte_at(pos + i);
    if ((b & 0xC0u) != 0x80u)
      return bad;
    cp = (cp << 6) | (b & 0x3Fu);
  }
  const bool overlong = (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000);
  const bool surrogate = cp >= 0xD800 && cp <= 0xDFFF;
  if (overlong || surrogate || cp > 0x10FFFF)
    return bad;
  return {static_cast<char32_t>(cp), len, true};
}

/// Lazy range over the code points of UTF-8 text (`utf8_chars`); malformed bytes yield U+FFFD.
template <typename SV> class utf8_range {
public:
  using value_type = char32_t;

  class iterator {
  public:
    using value_type = char32_t;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    iterator(SV text, std::size_t pos) noexcept : m_text(text), m_pos(pos) {}

    [[nodiscard]] char32_t operator*() const noexcept { return decode_utf8(m_text, m_pos).code_point; }

    iterator &operator++() noexcept {
      m_pos += decode_utf8(m_text, m_pos).length;
      return *this;
    }

    [[nodiscard]] friend bool operator==(const iterator &it, string_range_end) noexcept {
      return it.m_pos >= it.m_text.size();
    }
    [[nodiscard]] friend bool operator!=(const iterator &it, string_range_end) noexcept {
      return it.m_pos < it.m_text.size();
    }

  private:
    SV m_text;
    std::size_t m_pos;
  };

  explicit utf8_range(SV text) noexcept : m_text(text) {}

  [[nodiscard]] iterator begin() const noexcept { return iterator(m_text, 0); }
  [[nodiscard]] string_range_end end() const noexcept { return {}; }

private:
  SV m_text;
};

} // namespace detail

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

  // std-interop-begin: opt-in implicit conversion from std::basic_string
  // for interop with APIs that already speak std::string.
  template <typename Allocator>
  constexpr basic_string_view(const std::basic_string<CharT, TraitsT, Allocator> &rhs RELOCO_LIFETIMEBOUND
                                  RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : data_(rhs.data()), size_(rhs.size()) {}

  template <typename Allocator> basic_string_view(std::basic_string<CharT, TraitsT, Allocator> &&) = delete;
  // std-interop-end

  constexpr basic_string_view(std::nullptr_t) noexcept {}

  /**
   * @brief Views the characters of a `span<CharT>`/`span<const CharT>` (implicit, mirroring the implicit
   * `string_view` -> `span<const CharT>` conversion below). Not lifetimebound: the view borrows what the span
   * views, not the span object.
   */
  template <typename U, std::enable_if_t<std::is_same_v<std::remove_cv_t<U>, CharT>, int> = 0>
  // NOLINTNEXTLINE(reloco-explicit-conversion)
  constexpr basic_string_view(const span<U> &chars) noexcept : data_(chars.data()), size_(chars.size()) {}

  // Dependent on CharT so `span` only has to be complete where the interop members are used.
  template <typename E> using dependent_span_t = span<std::conditional_t<sizeof(CharT) != 0, E, CharT>>;

  /**
   * @brief Views a byte span (`span<const char>`, `span<const uint8_t>`, `span<const std::byte>` ...) as text.
   * Only for one-byte `CharT`; the bytes are taken as-is (no validation).
   */
  template <typename U, std::enable_if_t<is_byte_like_v<U> && sizeof(CharT) == 1, int> = 0>
  [[nodiscard]] static basic_string_view from_bytes(const span<U> &bytes) noexcept {
    return basic_string_view(bytes.template cast_to<const CharT>());
  }

  /// The characters as a `span<const CharT>` (include `span.hpp` where used).
  [[nodiscard]] constexpr auto as_span() const noexcept { return span<const CharT>(data_, size_); }

  // string_view -> span<const CharT> is implicit through span's own container constructor.

  /// The characters as raw bytes (`std::byte`).
  [[nodiscard]] dependent_span_t<const std::byte> as_bytes() const noexcept {
    // NOLINTNEXTLINE(reloco-banned-construct): byte view of the characters, within the view bounds.
    return dependent_span_t<const std::byte>(reinterpret_cast<const std::byte *>(data_), size_ * sizeof(CharT));
  }

  /// The characters as `uint8_t` (one-byte `CharT` only; `cast_to` rejects other sizes at compile time).
  [[nodiscard]] dependent_span_t<const unsigned char> as_uint8() const noexcept {
    static_assert(sizeof(CharT) == 1, "as_uint8 requires a one-byte character type");
    // NOLINTNEXTLINE(reloco-banned-construct): byte view of the characters, within the view bounds.
    return dependent_span_t<const unsigned char>(reinterpret_cast<const unsigned char *>(data_), size_);
  }

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

  RELOCO_CHECKED [[nodiscard]] constexpr const_reference operator[](size_type pos) const noexcept RELOCO_LIFETIMEBOUND {
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

  [[nodiscard]] constexpr basic_string_view substr(size_type pos = 0, size_type count = npos) const noexcept {
    RELOCO_ASSERT(pos <= size(), "substr position out of bounds");
    return basic_string_view(data_ + pos, std::min(count, size_ - pos));
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE constexpr basic_string_view
  unsafe_substr(size_type pos = 0, size_type count = npos) const noexcept {
    RELOCO_DEBUG_ASSERT(pos <= size(), "substr position out of bounds");
    return basic_string_view(data_ + pos, std::min(count, size_ - pos));
  }

  [[nodiscard]] result<std::reference_wrapper<const CharT>> try_at(size_type pos) const noexcept RELOCO_LIFETIMEBOUND {
    if (pos >= size())
      return unexpected(error::out_of_bounds);
    return std::cref(data_[pos]);
  }

  [[nodiscard]] result<basic_string_view> try_substr(size_type pos, size_type count = npos) const noexcept {
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

  /**
   * @brief Copies the characters into a new owning `basic_string` (default allocator). Include `string.hpp` where
   * this is called.
   */
  template <typename Str = basic_string<CharT, TraitsT>> [[nodiscard]] result<Str> try_to_string() const noexcept {
    return Str::try_create(*this);
  }

  /// `try_to_string()` with an explicit `allocator_ref`.
  template <typename Alloc, typename Str = basic_string<CharT, TraitsT>>
  [[nodiscard]] result<Str> try_to_string(Alloc alloc) const noexcept {
    return Str::try_allocate(alloc, *this);
  }
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
  [[nodiscard]] constexpr optional<basic_string_view> strip_prefix(basic_string_view prefix) const noexcept {
    if (!starts_with(prefix))
      return nullopt;
    return substr(prefix.size_);
  }

  /**
   * @brief Rust `str::strip_suffix` equivalent: returns the view with
   * @p suffix removed from the back, or an empty `optional` if this view
   * does not end with @p suffix.
   */
  [[nodiscard]] constexpr optional<basic_string_view> strip_suffix(basic_string_view suffix) const noexcept {
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
  [[nodiscard]] constexpr basic_string_view trim_start(Pred pred = &basic_string_view::is_ascii_space) const noexcept {
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
  [[nodiscard]] constexpr basic_string_view trim_end(Pred pred = &basic_string_view::is_ascii_space) const noexcept {
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
  [[nodiscard]] constexpr basic_string_view trim(Pred pred = &basic_string_view::is_ascii_space) const noexcept {
    return trim_start(pred).trim_end(pred);
  }

  // ---------------------------------------------------------------------------------------------------------------
  // Rust `str`-style operations. A "pattern" is a character, a predicate `bool(CharT)` or a string; results are
  // views into this view's characters (nothing is copied or allocated).
  // ---------------------------------------------------------------------------------------------------------------

  template <typename Pat>
  using split_t = detail::string_split<basic_string_view, detail::string_pattern_t<basic_string_view, Pat>>;
  template <typename Pat>
  using matches_t = detail::string_matches<basic_string_view, detail::string_pattern_t<basic_string_view, Pat>, false>;
  template <typename Pat>
  using match_indices_t =
      detail::string_matches<basic_string_view, detail::string_pattern_t<basic_string_view, Pat>, true>;

  [[nodiscard]] constexpr size_type rfind(CharT ch, size_type pos = npos) const noexcept {
    if (size_ == 0)
      return npos;
    size_type i = pos >= size_ ? size_ : pos + 1;
    while (i > 0) {
      --i;
      if (TraitsT::eq(data_[i], ch))
        return i;
    }
    return npos;
  }

  [[nodiscard]] constexpr size_type rfind(basic_string_view needle, size_type pos = npos) const noexcept {
    if (needle.size_ > size_)
      return npos;
    size_type i = std::min(pos, size_ - needle.size_) + 1;
    while (i > 0) {
      --i;
      if (needle.size_ == 0 || TraitsT::compare(data_ + i, needle.data_, needle.size_) == 0)
        return i;
    }
    return npos;
  }

  [[nodiscard]] constexpr size_type find_first_of(basic_string_view chars, size_type pos = 0) const noexcept {
    for (size_type i = pos; i < size_; ++i)
      if (chars.find(data_[i]) != npos)
        return i;
    return npos;
  }

  [[nodiscard]] constexpr size_type find_first_not_of(basic_string_view chars, size_type pos = 0) const noexcept {
    for (size_type i = pos; i < size_; ++i)
      if (chars.find(data_[i]) == npos)
        return i;
    return npos;
  }

  [[nodiscard]] constexpr size_type find_last_not_of(basic_string_view chars, size_type pos = npos) const noexcept {
    size_type i = pos >= size_ ? size_ : pos + 1;
    while (i > 0) {
      --i;
      if (chars.find(data_[i]) == npos)
        return i;
    }
    return npos;
  }

  /// Index of the first character at or after @p pos satisfying @p pred, or `npos`.
  template <typename Pred> [[nodiscard]] constexpr size_type find_if(Pred &&pred, size_type pos = 0) const noexcept {
    for (size_type i = pos; i < size_; ++i)
      if (pred(data_[i]))
        return i;
    return npos;
  }

  /// Index of the last character satisfying @p pred, or `npos`.
  template <typename Pred> [[nodiscard]] constexpr size_type rfind_if(Pred &&pred) const noexcept {
    for (size_type i = size_; i > 0; --i)
      if (pred(data_[i - 1]))
        return i - 1;
    return npos;
  }

  /// True when @p pat (character, predicate or string) occurs in the view. An empty string always occurs.
  template <typename Pat> [[nodiscard]] constexpr bool contains(Pat &&pat) const noexcept {
    const auto p = detail::make_pattern<basic_string_view>(std::forward<Pat>(pat));
    if constexpr (std::is_same_v<std::remove_const_t<decltype(p)>, basic_string_view>)
      return find(p) != npos;
    else
      return detail::find_pattern(*this, 0, p).pos != npos;
  }

  /// Number of non-overlapping matches of @p pat.
  template <typename Pat> [[nodiscard]] constexpr size_type count(Pat &&pat) const noexcept {
    const auto p = detail::make_pattern<basic_string_view>(std::forward<Pat>(pat));
    size_type n = 0;
    size_type from = 0;
    for (;;) {
      const detail::string_match m = detail::find_pattern(*this, from, p);
      if (m.pos == npos)
        return n;
      ++n;
      from = m.pos + m.len;
    }
  }

  /// Lazy range of the non-overlapping matches of @p pat.
  template <typename Pat> [[nodiscard]] matches_t<Pat> matches(Pat &&pat) const noexcept {
    return matches_t<Pat>(*this, detail::make_pattern<basic_string_view>(std::forward<Pat>(pat)));
  }

  /// Like `matches`, yielding `std::pair<index, match>`.
  template <typename Pat> [[nodiscard]] match_indices_t<Pat> match_indices(Pat &&pat) const noexcept {
    return match_indices_t<Pat>(*this, detail::make_pattern<basic_string_view>(std::forward<Pat>(pat)));
  }

  /// Pieces between the matches of @p pat (an empty string pattern never matches, giving the whole view).
  template <typename Pat> [[nodiscard]] split_t<Pat> split(Pat &&pat) const noexcept {
    return make_split(std::forward<Pat>(pat), detail::string_split_options{});
  }

  /// `split` from the end: pieces are produced last to first.
  template <typename Pat> [[nodiscard]] split_t<Pat> rsplit(Pat &&pat) const noexcept {
    detail::string_split_options opt;
    opt.reverse = true;
    return make_split(std::forward<Pat>(pat), opt);
  }

  /// At most @p max_pieces pieces; the last one holds the unsplit remainder.
  template <typename Pat> [[nodiscard]] split_t<Pat> splitn(size_type max_pieces, Pat &&pat) const noexcept {
    detail::string_split_options opt;
    opt.limit = max_pieces;
    return make_split(std::forward<Pat>(pat), opt);
  }

  /// `splitn` from the end; the last piece produced holds the unsplit beginning.
  template <typename Pat> [[nodiscard]] split_t<Pat> rsplitn(size_type max_pieces, Pat &&pat) const noexcept {
    detail::string_split_options opt;
    opt.limit = max_pieces;
    opt.reverse = true;
    return make_split(std::forward<Pat>(pat), opt);
  }

  /// `split` that treats @p pat as a terminator: a trailing empty piece is not produced.
  template <typename Pat> [[nodiscard]] split_t<Pat> split_terminator(Pat &&pat) const noexcept {
    detail::string_split_options opt;
    opt.skip_trailing_empty = true;
    return make_split(std::forward<Pat>(pat), opt);
  }

  /// `split_terminator` that keeps the matched separator at the end of each piece.
  template <typename Pat> [[nodiscard]] split_t<Pat> split_inclusive(Pat &&pat) const noexcept {
    detail::string_split_options opt;
    opt.skip_trailing_empty = true;
    opt.inclusive = true;
    return make_split(std::forward<Pat>(pat), opt);
  }

  /// Splits at ASCII whitespace, skipping empty pieces.
  [[nodiscard]] split_t<bool (*)(CharT) noexcept> split_whitespace() const noexcept {
    detail::string_split_options opt;
    opt.skip_empty = true;
    return make_split(&basic_string_view::is_ascii_space, opt);
  }

  /// Lines separated by `\n` or `\r\n` (terminators removed, no trailing empty line).
  [[nodiscard]] split_t<CharT> lines() const noexcept {
    detail::string_split_options opt;
    opt.skip_trailing_empty = true;
    opt.strip_cr = true;
    return make_split(CharT('\n'), opt);
  }

  /// Splits at the first match: `{before, after}` or `error::not_found`.
  template <typename Pat>
  [[nodiscard]] result<detail::string_pair<basic_string_view>> split_once(Pat &&pat) const noexcept {
    const auto p = detail::make_pattern<basic_string_view>(std::forward<Pat>(pat));
    const detail::string_match m = detail::find_pattern(*this, 0, p);
    if (m.pos == npos)
      return unexpected(error::not_found);
    return detail::string_pair<basic_string_view>{substr(0, m.pos), substr(m.pos + m.len)};
  }

  /// Splits at the last match: `{before, after}` or `error::not_found`.
  template <typename Pat>
  [[nodiscard]] result<detail::string_pair<basic_string_view>> rsplit_once(Pat &&pat) const noexcept {
    const auto p = detail::make_pattern<basic_string_view>(std::forward<Pat>(pat));
    const detail::string_match m = detail::rfind_pattern(*this, p);
    if (m.pos == npos)
      return unexpected(error::not_found);
    return detail::string_pair<basic_string_view>{substr(0, m.pos), substr(m.pos + m.len)};
  }

  [[nodiscard]] constexpr detail::string_pair<basic_string_view> split_at(size_type pos) const noexcept {
    RELOCO_ASSERT(pos <= size_, "split_at position exceeds view size");
    return {substr(0, pos), substr(pos)};
  }

  [[nodiscard]] result<detail::string_pair<basic_string_view>> try_split_at(size_type pos) const noexcept {
    if (pos > size_)
      return unexpected(error::out_of_bounds);
    return split_at(pos);
  }

  [[nodiscard]] constexpr optional<basic_string_view> strip_prefix(CharT ch) const noexcept {
    if (!starts_with(ch))
      return nullopt;
    return substr(1);
  }

  [[nodiscard]] constexpr optional<basic_string_view> strip_suffix(CharT ch) const noexcept {
    if (!ends_with(ch))
      return nullopt;
    return substr(0, size_ - 1);
  }

  /// Removes every leading repetition of @p pat (character, predicate or string).
  template <typename Pat> [[nodiscard]] constexpr basic_string_view trim_start_matches(Pat &&pat) const noexcept {
    const auto p = detail::make_pattern<basic_string_view>(std::forward<Pat>(pat));
    basic_string_view rest = *this;
    for (size_type n = detail::match_start(rest, p); n != npos; n = detail::match_start(rest, p))
      rest.remove_prefix(n);
    return rest;
  }

  /// Removes every trailing repetition of @p pat.
  template <typename Pat> [[nodiscard]] constexpr basic_string_view trim_end_matches(Pat &&pat) const noexcept {
    const auto p = detail::make_pattern<basic_string_view>(std::forward<Pat>(pat));
    basic_string_view rest = *this;
    for (size_type n = detail::match_end(rest, p); n != npos; n = detail::match_end(rest, p))
      rest.remove_suffix(n);
    return rest;
  }

  template <typename Pat> [[nodiscard]] constexpr basic_string_view trim_matches(Pat &&pat) const noexcept {
    const auto p = detail::make_pattern<basic_string_view>(std::forward<Pat>(pat));
    return trim_start_matches(p).trim_end_matches(p);
  }

  // ---- ASCII (locale independent) ------------------------------------------------------------------------------

  [[nodiscard]] constexpr bool is_ascii() const noexcept {
    for (size_type i = 0; i < size_; ++i)
      if (static_cast<std::make_unsigned_t<CharT>>(data_[i]) >= 0x80u)
        return false;
    return true;
  }

  [[nodiscard]] constexpr bool eq_ignore_ascii_case(basic_string_view rhs) const noexcept {
    return size_ == rhs.size_ && compare_ignore_ascii_case(rhs) == 0;
  }

  /// Three-way comparison with ASCII letters folded to lower case.
  [[nodiscard]] constexpr int compare_ignore_ascii_case(basic_string_view rhs) const noexcept {
    const size_type common = std::min(size_, rhs.size_);
    for (size_type i = 0; i < common; ++i) {
      const CharT a = ascii_lower(data_[i]);
      const CharT b = ascii_lower(rhs.data_[i]);
      if (!TraitsT::eq(a, b))
        return TraitsT::lt(a, b) ? -1 : 1;
    }
    return size_ < rhs.size_ ? -1 : (size_ > rhs.size_ ? 1 : 0);
  }

  [[nodiscard]] constexpr bool starts_with_ignore_ascii_case(basic_string_view prefix) const noexcept {
    return size_ >= prefix.size_ && substr(0, prefix.size_).eq_ignore_ascii_case(prefix);
  }

  [[nodiscard]] constexpr bool ends_with_ignore_ascii_case(basic_string_view suffix) const noexcept {
    return size_ >= suffix.size_ && substr(size_ - suffix.size_).eq_ignore_ascii_case(suffix);
  }

  /// True when @p pos is the start of a UTF-8 code point (or the end). Wide character types: any `pos <= size()`.
  [[nodiscard]] constexpr bool is_char_boundary(size_type pos) const noexcept {
    if (pos > size_)
      return false;
    if constexpr (sizeof(CharT) == 1)
      return pos == size_ || (static_cast<unsigned char>(data_[pos]) & 0xC0u) != 0x80u;
    else
      return true;
  }

  // ---- Parsing (no allocation, no locale, no floating point) ---------------------------------------------------

  /**
   * @brief Parses the whole view as an integer in @p base (2..36), with an optional leading `+` (or `-` for signed
   * types). Fails with `invalid_argument` (empty, stray characters) or `out_of_range` (does not fit `Int`).
   */
  template <typename Int> [[nodiscard]] result<Int> try_parse(unsigned base = 10) const noexcept {
    size_type used = 0;
    auto value = parse_prefix<Int>(base, used);
    if (value && used != size_)
      return unexpected(error::invalid_argument);
    return value;
  }

  /// Parses an integer at the start of the view and removes the consumed characters (for tokenizers).
  template <typename Int> [[nodiscard]] result<Int> try_consume_int(unsigned base = 10) & noexcept {
    size_type used = 0;
    auto value = parse_prefix<Int>(base, used);
    if (value)
      remove_prefix(used);
    return value;
  }

  /// Parses exactly `true` or `false`.
  [[nodiscard]] result<bool> try_parse_bool() const noexcept {
    if (*this == basic_string_view("true"))
      return true;
    if (*this == basic_string_view("false"))
      return false;
    return unexpected(error::invalid_argument);
  }

  // ---- UTF-8 (one-byte CharT only) -----------------------------------------------------------------------------

  /// True when the whole view is well-formed UTF-8 (no overlong forms, surrogates or values above U+10FFFF).
  [[nodiscard]] constexpr bool is_valid_utf8() const noexcept {
    static_assert(sizeof(CharT) == 1, "UTF-8 operations require a one-byte character type");
    for (size_type i = 0; i < size_;) {
      const detail::utf8_decoded d = detail::decode_utf8(*this, i);
      if (!d.valid)
        return false;
      i += d.length;
    }
    return true;
  }

  /// Number of code points, or `error::invalid_argument` when the text is not valid UTF-8.
  [[nodiscard]] result<size_type> try_char_count() const noexcept {
    static_assert(sizeof(CharT) == 1, "UTF-8 operations require a one-byte character type");
    size_type n = 0;
    for (size_type i = 0; i < size_; ++n) {
      const detail::utf8_decoded d = detail::decode_utf8(*this, i);
      if (!d.valid)
        return unexpected(error::invalid_argument);
      i += d.length;
    }
    return n;
  }

  /// Lazy range of code points (`char32_t`); each malformed byte yields U+FFFD.
  [[nodiscard]] detail::utf8_range<basic_string_view> utf8_chars() const noexcept {
    static_assert(sizeof(CharT) == 1, "UTF-8 operations require a one-byte character type");
    return detail::utf8_range<basic_string_view>(*this);
  }

  // ---- Allocating operations (default allocator, or an explicit `allocator_ref` as the first argument) --------------

  /// Copy with every occurrence of @p from replaced by @p to (an empty @p from leaves the text unchanged).
  template <typename Str = basic_string<CharT, TraitsT>>
  [[nodiscard]] result<Str> try_replace(basic_string_view from, basic_string_view to) const noexcept {
    return build<Str>([] { return Str::try_create(basic_string_view()); },
                      [this, from, to](Str &out) { return replace_into(out, from, to); });
  }

  template <typename Alloc, typename Str = basic_string<CharT, TraitsT>>
  [[nodiscard]] result<Str> try_replace(Alloc alloc, basic_string_view from, basic_string_view to) const noexcept {
    return build<Str>([alloc] { return Str::try_allocate(alloc, basic_string_view()); },
                      [this, from, to](Str &out) { return replace_into(out, from, to); });
  }

  template <typename Str = basic_string<CharT, TraitsT>>
  [[nodiscard]] result<Str> try_to_ascii_uppercase() const noexcept {
    return build<Str>([] { return Str::try_create(basic_string_view()); },
                      [this](Str &out) { return case_into(out, true); });
  }

  template <typename Alloc, typename Str = basic_string<CharT, TraitsT>>
  [[nodiscard]] result<Str> try_to_ascii_uppercase(Alloc alloc) const noexcept {
    return build<Str>([alloc] { return Str::try_allocate(alloc, basic_string_view()); },
                      [this](Str &out) { return case_into(out, true); });
  }

  template <typename Str = basic_string<CharT, TraitsT>>
  [[nodiscard]] result<Str> try_to_ascii_lowercase() const noexcept {
    return build<Str>([] { return Str::try_create(basic_string_view()); },
                      [this](Str &out) { return case_into(out, false); });
  }

  template <typename Alloc, typename Str = basic_string<CharT, TraitsT>>
  [[nodiscard]] result<Str> try_to_ascii_lowercase(Alloc alloc) const noexcept {
    return build<Str>([alloc] { return Str::try_allocate(alloc, basic_string_view()); },
                      [this](Str &out) { return case_into(out, false); });
  }

  /// The text repeated @p times times; `error::integer_overflow` when the size does not fit.
  template <typename Str = basic_string<CharT, TraitsT>>
  [[nodiscard]] result<Str> try_repeat(size_type times) const noexcept {
    return build<Str>([] { return Str::try_create(basic_string_view()); },
                      [this, times](Str &out) { return repeat_into(out, times); });
  }

  template <typename Alloc, typename Str = basic_string<CharT, TraitsT>>
  [[nodiscard]] result<Str> try_repeat(Alloc alloc, size_type times) const noexcept {
    return build<Str>([alloc] { return Str::try_allocate(alloc, basic_string_view()); },
                      [this, times](Str &out) { return repeat_into(out, times); });
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
  [[nodiscard]] static constexpr CharT ascii_lower(CharT c) noexcept {
    return (c >= CharT('A') && c <= CharT('Z')) ? static_cast<CharT>(c + (CharT('a') - CharT('A'))) : c;
  }

  [[nodiscard]] static constexpr CharT ascii_upper(CharT c) noexcept {
    return (c >= CharT('a') && c <= CharT('z')) ? static_cast<CharT>(c - (CharT('a') - CharT('A'))) : c;
  }

  template <typename Pat>
  [[nodiscard]] split_t<Pat> make_split(Pat &&pat, detail::string_split_options opt) const noexcept {
    return split_t<Pat>(*this, detail::make_pattern<basic_string_view>(std::forward<Pat>(pat)), opt);
  }

  template <typename Str, typename Init, typename Fill>
  [[nodiscard]] static result<Str> build(Init &&init, Fill &&fill) noexcept {
    auto out = init();
    if (!out)
      return out;
    if (auto r = fill(*out); !r)
      return unexpected(r.error());
    return out;
  }

  template <typename Str>
  [[nodiscard]] result<void> replace_into(Str &out, basic_string_view from, basic_string_view to) const noexcept {
    size_type from_pos = 0;
    while (from.size_ != 0) {
      const size_type hit = find(from, from_pos);
      if (hit == npos)
        break;
      if (auto r = out.try_append(substr(from_pos, hit - from_pos)); !r)
        return r;
      if (auto r = out.try_append(to); !r)
        return r;
      from_pos = hit + from.size_;
    }
    return out.try_append(substr(from_pos));
  }

  template <typename Str> [[nodiscard]] result<void> case_into(Str &out, bool upper) const noexcept {
    if (auto r = out.try_reserve(size_); !r)
      return r;
    for (size_type i = 0; i < size_; ++i)
      if (auto r = out.try_push_back(upper ? ascii_upper(data_[i]) : ascii_lower(data_[i])); !r)
        return r;
    return {};
  }

  template <typename Str> [[nodiscard]] result<void> repeat_into(Str &out, size_type times) const noexcept {
    if (size_ != 0 && times > std::numeric_limits<size_type>::max() / size_)
      return unexpected(error::integer_overflow);
    if (auto r = out.try_reserve(size_ * times); !r)
      return r;
    for (size_type i = 0; i < times; ++i)
      if (auto r = out.try_append(*this); !r)
        return r;
    return {};
  }

  // Parses `[+-]digits` at the start; `used` receives the number of characters consumed on success.
  template <typename Int> [[nodiscard]] result<Int> parse_prefix(unsigned base, size_type &used) const noexcept {
    static_assert(std::is_integral_v<Int> && !std::is_same_v<Int, bool>, "try_parse requires an integer type");
    RELOCO_ASSERT(base >= 2 && base <= 36, "integer base must be in [2, 36]");
    using U = std::make_unsigned_t<Int>;
    size_type i = 0;
    bool negative = false;
    if (i < size_ && TraitsT::eq(data_[i], CharT('+'))) {
      ++i;
    } else if (i < size_ && TraitsT::eq(data_[i], CharT('-'))) {
      if constexpr (std::is_signed_v<Int>) {
        negative = true;
        ++i;
      } else {
        return unexpected(error::invalid_argument);
      }
    }
    U limit = static_cast<U>(std::numeric_limits<Int>::max());
    if (negative)
      limit = static_cast<U>(limit + 1u);
    const size_type digits_begin = i;
    U acc = 0;
    bool overflow = false;
    for (; i < size_; ++i) {
      const CharT c = data_[i];
      unsigned digit = 0;
      if (c >= CharT('0') && c <= CharT('9'))
        digit = static_cast<unsigned>(c - CharT('0'));
      else if (c >= CharT('a') && c <= CharT('z'))
        digit = static_cast<unsigned>(c - CharT('a')) + 10u;
      else if (c >= CharT('A') && c <= CharT('Z'))
        digit = static_cast<unsigned>(c - CharT('A')) + 10u;
      else
        break;
      if (digit >= base)
        break;
      if (!overflow) {
        const U b = static_cast<U>(base);
        const U d = static_cast<U>(digit);
        if (acc > static_cast<U>(limit / b) || (acc == static_cast<U>(limit / b) && d > static_cast<U>(limit % b)))
          overflow = true;
        else
          acc = static_cast<U>(acc * b + d);
      }
    }
    if (i == digits_begin)
      return unexpected(error::invalid_argument);
    if (overflow)
      return unexpected(error::out_of_range);
    used = i;
    return negative ? static_cast<Int>(static_cast<U>(U{0} - acc)) : static_cast<Int>(acc);
  }

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

RELOCO_END_UNSAFE_CODE

namespace std {

template <typename CharT, typename TraitsT> struct hash<reloco::basic_string_view<CharT, TraitsT>> {
  [[nodiscard]] size_t operator()(reloco::basic_string_view<CharT, TraitsT> value) const noexcept {
    // std-interop-begin: std::hash is delegated to std::string_view
    return hash<std::basic_string_view<CharT, TraitsT>>{}(value.to_std());
    // std-interop-end
  }
};

} // namespace std
