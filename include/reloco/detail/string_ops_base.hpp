// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file
 * @brief `detail::string_ops_base`: the shared CRTP layer behind `reloco::string`, `sso_string` and `inline_string`.
 *
 * Why it exists: the three mutable string classes differ only in where their bytes live. Everything above the
 * handful of primitives (`view()`, `data()`, `size()`, `try_insert`, `try_erase`, `try_resize`, `try_append`,
 * `try_push_back`, `try_reserve`, `try_create_like`) is identical, so it lives here once. See
 * `docs/string-ops.md` for the user-facing guide and examples.
 *
 * Design rules:
 *  - Read-only operations are forwarded to `basic_string_view` (`view().xxx(...)`), so the semantics are defined in
 *    exactly one place. Their `&&` overloads are deleted: results (views, ranges) would dangle.
 *  - In-place edits compute their result with the view API (`view().trim()`, `view().strip_prefix(...)`) and then
 *    erase/shift, instead of re-implementing the algorithm.
 *  - Every mutator is `&`-qualified, `noexcept`, and either succeeds or leaves the string unchanged.
 */

#include "../array.hpp"
#include "../byte_like.hpp"
#include "../error.hpp"
#include "../expected.hpp"
#include "../fmt.hpp"
#include "../lifetime.hpp"
#include "../optional.hpp"
#include "../span.hpp"
#include "../string_view.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

namespace reloco {
namespace detail {

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

// Clang 18 crashes (CheckParameterPacksForExpansion) when a variadic member template carries the implicit-object
// lifetimebound attribute together with a trailing return type; the annotation is only dropped there.
#if defined(__clang__) && __clang_major__ < 19
#define RELOCO_STRING_FORWARD_LIFETIMEBOUND_
#else
#define RELOCO_STRING_FORWARD_LIFETIMEBOUND_ RELOCO_LIFETIMEBOUND
#endif

#define RELOCO_STRING_FORWARD_(name)                                                                                   \
  template <typename... Args>                                                                                          \
  [[nodiscard]] auto name(Args &&...args) const & noexcept RELOCO_STRING_FORWARD_LIFETIMEBOUND_                        \
      -> decltype(std::declval<view_type>().name(std::forward<Args>(args)...)) {                                       \
    return view_of().name(std::forward<Args>(args)...);                                                                \
  }                                                                                                                    \
  template <typename... Args> void name(Args &&...) const && = delete

/**
 * @brief CRTP base shared by every mutable reloco string.
 * @tparam Derived the concrete string (must provide the primitives listed in the file comment).
 */
template <typename Derived, typename CharT, typename TraitsT> class string_ops_base {
public:
  using view_type = basic_string_view<CharT, TraitsT>;
  using size_type = std::size_t;
  static constexpr size_type npos = view_type::npos;

  // ---- read-only operations, forwarded to basic_string_view -------------------------------------------------

  RELOCO_STRING_FORWARD_(find);
  RELOCO_STRING_FORWARD_(rfind);
  RELOCO_STRING_FORWARD_(find_first_of);
  RELOCO_STRING_FORWARD_(find_first_not_of);
  RELOCO_STRING_FORWARD_(find_last_not_of);
  RELOCO_STRING_FORWARD_(find_if);
  RELOCO_STRING_FORWARD_(rfind_if);
  RELOCO_STRING_FORWARD_(contains);
  RELOCO_STRING_FORWARD_(count);
  RELOCO_STRING_FORWARD_(matches);
  RELOCO_STRING_FORWARD_(match_indices);
  RELOCO_STRING_FORWARD_(starts_with);
  RELOCO_STRING_FORWARD_(ends_with);
  RELOCO_STRING_FORWARD_(strip_prefix);
  RELOCO_STRING_FORWARD_(strip_suffix);
  RELOCO_STRING_FORWARD_(trim);
  RELOCO_STRING_FORWARD_(trim_start);
  RELOCO_STRING_FORWARD_(trim_end);
  RELOCO_STRING_FORWARD_(trim_matches);
  RELOCO_STRING_FORWARD_(trim_start_matches);
  RELOCO_STRING_FORWARD_(trim_end_matches);
  RELOCO_STRING_FORWARD_(split);
  RELOCO_STRING_FORWARD_(rsplit);
  RELOCO_STRING_FORWARD_(splitn);
  RELOCO_STRING_FORWARD_(rsplitn);
  RELOCO_STRING_FORWARD_(split_terminator);
  RELOCO_STRING_FORWARD_(split_inclusive);
  RELOCO_STRING_FORWARD_(split_whitespace);
  RELOCO_STRING_FORWARD_(lines);
  RELOCO_STRING_FORWARD_(split_once);
  RELOCO_STRING_FORWARD_(rsplit_once);
  RELOCO_STRING_FORWARD_(split_at);
  RELOCO_STRING_FORWARD_(try_split_at);
  RELOCO_STRING_FORWARD_(substr);
  RELOCO_STRING_FORWARD_(try_substr);
  RELOCO_STRING_FORWARD_(is_ascii);
  RELOCO_STRING_FORWARD_(eq_ignore_ascii_case);
  RELOCO_STRING_FORWARD_(compare_ignore_ascii_case);
  RELOCO_STRING_FORWARD_(starts_with_ignore_ascii_case);
  RELOCO_STRING_FORWARD_(ends_with_ignore_ascii_case);
  RELOCO_STRING_FORWARD_(is_char_boundary);
  RELOCO_STRING_FORWARD_(try_parse_bool);
  RELOCO_STRING_FORWARD_(is_valid_utf8);
  RELOCO_STRING_FORWARD_(try_char_count);
  RELOCO_STRING_FORWARD_(utf8_chars);

  /// Parses the whole string as an integer (see `basic_string_view::try_parse`).
  template <typename Int> [[nodiscard]] auto try_parse(unsigned base = 10) const & noexcept {
    return view_of().template try_parse<Int>(base);
  }
  template <typename Int> void try_parse(unsigned = 10) const && = delete;

  // ---- in-place editing ---------------------------------------------------------------------------------------

  /// Shortens the string to @p len characters (no-op when already shorter). Never allocates.
  void truncate(size_type len) & noexcept {
    if (len < self().size())
      (void)self().try_erase(len, npos);
  }

  /// Inserts @p sv at the front.
  [[nodiscard]] result<void> try_prepend(view_type sv) & noexcept { return self().try_insert(0, sv); }

  /// Replaces `[pos, pos + count)` (clamped to the end) with @p sv. Safe when @p sv aliases this string.
  [[nodiscard]] result<void> try_replace_range(size_type pos, size_type count, view_type sv) & noexcept {
    if (pos > self().size())
      return unexpected(error::out_of_bounds);
    count = count < self().size() - pos ? count : self().size() - pos;
    if (sv.size() <= count) {
      if (!sv.empty())
        TraitsT::move(self().data() + pos, sv.data(), sv.size());
      return count == sv.size() ? result<void>{} : self().try_erase(pos + sv.size(), count - sv.size());
    }
    if (auto r = self().try_insert(pos, sv); !r)
      return r;
    return self().try_erase(pos + sv.size(), count);
  }

  /// Replaces every non-overlapping occurrence of @p from with @p to. @p from / @p to must not alias this string.
  [[nodiscard]] result<void> try_replace_all(view_type from, view_type to) & noexcept {
    return try_replacen(from, to, npos);
  }

  /// Like `try_replace_all` but replaces at most the first @p limit occurrences.
  [[nodiscard]] result<void> try_replacen(view_type from, view_type to, size_type limit) & noexcept {
    if (from.empty() || limit == 0)
      return {};
    if (aliases_self(from) || aliases_self(to))
      return unexpected(error::invalid_argument);
    size_type n = 0;
    {
      view_type rest = view_of();
      for (size_type p = rest.find(from); p != npos && n < limit; p = rest.find(from)) {
        ++n;
        rest.remove_prefix(p + from.size());
      }
    }
    if (n == 0)
      return {};
    const size_type old_size = self().size();
    size_type shift = 0; // distance the original text is moved right before scanning
    size_type new_size = 0;
    if (to.size() > from.size()) {
      const size_type grow = to.size() - from.size();
      if (n > (std::numeric_limits<size_type>::max() - old_size) / grow)
        return unexpected(error::integer_overflow);
      shift = n * grow;
      new_size = old_size + shift;
      if (auto r = self().try_resize(new_size); !r)
        return r;
      TraitsT::move(self().data() + shift, self().data(), old_size);
    } else {
      new_size = old_size - n * (from.size() - to.size());
    }
    size_type read = shift;
    const size_type end = shift + old_size;
    size_type write = 0;
    size_type left = n;
    while (read < end) {
      CharT *buf = self().data();
      const view_type rest(buf + read, end - read);
      const size_type p = left == 0 ? npos : rest.find(from);
      const size_type gap = p == npos ? rest.size() : p;
      if (gap != 0 && write != read)
        TraitsT::move(buf + write, buf + read, gap);
      write += gap;
      read += gap;
      if (p == npos)
        break;
      if (!to.empty())
        TraitsT::copy(buf + write, to.data(), to.size());
      write += to.size();
      read += from.size();
      --left;
    }
    if (write != self().size())
      return self().try_resize(write);
    return {};
  }

  /// Splits off `[pos, size())` into a new string of the same type; this string is truncated to @p pos.
  [[nodiscard]] result<Derived> try_split_off(size_type pos) & noexcept { return try_drain(pos, npos); }

  /// Removes `[pos, pos + count)` (clamped) and returns it as a new string of the same type.
  [[nodiscard]] result<Derived> try_drain(size_type pos, size_type count) & noexcept {
    if (pos > self().size())
      return unexpected(error::out_of_bounds);
    count = count < self().size() - pos ? count : self().size() - pos;
    auto out = self().try_create_like(view_of().substr(pos, count));
    if (!out)
      return unexpected(out.error());
    if (auto r = self().try_erase(pos, count); !r)
      return unexpected(r.error());
    return out;
  }

  /// Keeps only the characters for which @p pred returns true.
  template <typename Pred> void retain(Pred pred) & noexcept {
    CharT *buf = self().data();
    const size_type n = self().size();
    size_type w = 0;
    for (size_type r = 0; r < n; ++r)
      if (pred(buf[r]))
        buf[w++] = buf[r];
    truncate(w);
  }

  /// Collapses runs of identical characters into one.
  void dedup_chars() & noexcept {
    CharT *buf = self().data();
    const size_type n = self().size();
    size_type w = 0;
    for (size_type r = 0; r < n; ++r)
      if (w == 0 || !TraitsT::eq(buf[w - 1], buf[r]))
        buf[w++] = buf[r];
    truncate(w);
  }

  /// Removes leading characters matching @p pred (default: ASCII whitespace).
  template <typename... P> [[nodiscard]] result<void> trim_start_in_place(P &&...pred) & noexcept {
    const size_type keep = view_of().trim_start(std::forward<P>(pred)...).size();
    return keep == self().size() ? result<void>{} : self().try_erase(0, self().size() - keep);
  }

  /// Removes trailing characters matching @p pred (default: ASCII whitespace).
  template <typename... P> void trim_end_in_place(P &&...pred) & noexcept {
    truncate(view_of().trim_end(std::forward<P>(pred)...).size());
  }

  /// Removes both ends (default: ASCII whitespace).
  template <typename... P> [[nodiscard]] result<void> trim_in_place(P &&...pred) & noexcept {
    trim_end_in_place(pred...);
    return trim_start_in_place(pred...);
  }

  /// Removes every leading repetition of @p pat (char, predicate or string).
  template <typename Pat> [[nodiscard]] result<void> trim_start_matches_in_place(Pat &&pat) & noexcept {
    const size_type keep = view_of().trim_start_matches(std::forward<Pat>(pat)).size();
    return keep == self().size() ? result<void>{} : self().try_erase(0, self().size() - keep);
  }

  template <typename Pat> void trim_end_matches_in_place(Pat &&pat) & noexcept {
    truncate(view_of().trim_end_matches(std::forward<Pat>(pat)).size());
  }

  template <typename Pat> [[nodiscard]] result<void> trim_matches_in_place(Pat &&pat) & noexcept {
    trim_end_matches_in_place(pat);
    return trim_start_matches_in_place(pat);
  }

  /// Removes @p prefix when present; returns whether it was removed.
  template <typename Pat> [[nodiscard]] bool strip_prefix_in_place(Pat &&prefix) & noexcept {
    const auto rest = view_of().strip_prefix(std::forward<Pat>(prefix));
    if (!rest)
      return false;
    const size_type removed = self().size() - rest->size();
    (void)self().try_erase(0, removed);
    return true;
  }

  /// Removes @p suffix when present; returns whether it was removed.
  template <typename Pat> [[nodiscard]] bool strip_suffix_in_place(Pat &&suffix) & noexcept {
    const auto rest = view_of().strip_suffix(std::forward<Pat>(suffix));
    if (!rest)
      return false;
    truncate(rest->size());
    return true;
  }

  // ---- ASCII case ---------------------------------------------------------------------------------------------

  void make_ascii_uppercase() & noexcept {
    CharT *buf = self().data();
    for (size_type i = 0; i < self().size(); ++i)
      buf[i] = ascii_upper(buf[i]);
  }

  void make_ascii_lowercase() & noexcept {
    CharT *buf = self().data();
    for (size_type i = 0; i < self().size(); ++i)
      buf[i] = ascii_lower(buf[i]);
  }

  /// Upper-cases the first letter of every whitespace-separated word and lower-cases the rest.
  void make_ascii_titlecase() & noexcept {
    CharT *buf = self().data();
    bool start = true;
    for (size_type i = 0; i < self().size(); ++i) {
      const bool space = view_type::is_ascii_space(buf[i]);
      buf[i] = start ? ascii_upper(buf[i]) : ascii_lower(buf[i]);
      start = space;
    }
  }

  // ---- building -----------------------------------------------------------------------------------------------

  /// Replaces the content with @p count copies of @p ch.
  [[nodiscard]] result<void> try_assign_repeat(CharT ch, size_type count) & noexcept {
    const size_type old = self().size();
    if (auto r = self().try_resize(count, ch); !r)
      return r;
    CharT *buf = self().data();
    for (size_type i = 0; i < old && i < count; ++i)
      buf[i] = ch;
    return {};
  }

  /// Appends @p sv @p times times; on failure the string is restored.
  [[nodiscard]] result<void> try_append_repeat(view_type sv, size_type times) & noexcept {
    const size_type old = self().size();
    if (!sv.empty() && times > (std::numeric_limits<size_type>::max() - old) / sv.size())
      return unexpected(error::integer_overflow);
    if (auto r = self().try_reserve(old + sv.size() * times); !r)
      return r;
    for (size_type i = 0; i < times; ++i)
      if (auto r = self().try_append(sv); !r)
        return rollback(old, r);
    return {};
  }

  /// Appends every character produced by @p range.
  template <typename Range> [[nodiscard]] result<void> try_extend_from(const Range &range) & noexcept {
    const size_type old = self().size();
    for (const auto &c : range)
      if (auto r = self().try_push_back(static_cast<CharT>(c)); !r)
        return rollback(old, r);
    return {};
  }

  /// Pads on the left with @p ch until the string is @p width long.
  [[nodiscard]] result<void> try_pad_left(size_type width, CharT ch = CharT(' ')) & noexcept {
    const size_type old = self().size();
    if (old >= width)
      return {};
    if (auto r = self().try_resize(width, ch); !r)
      return r;
    CharT *buf = self().data();
    std::rotate(buf, buf + old, buf + width);
    return {};
  }

  /// Pads on the right with @p ch until the string is @p width long.
  [[nodiscard]] result<void> try_pad_right(size_type width, CharT ch = CharT(' ')) & noexcept {
    return self().size() >= width ? result<void>{} : self().try_resize(width, ch);
  }

  /// Centers the content; the extra pad character (odd padding) goes on the right.
  [[nodiscard]] result<void> try_center(size_type width, CharT ch = CharT(' ')) & noexcept {
    const size_type old = self().size();
    if (old >= width)
      return {};
    if (auto r = try_pad_left(old + (width - old) / 2, ch); !r)
      return r;
    return self().try_resize(width, ch);
  }

  // ---- number / byte / text encoders (all append; on failure the string is unchanged) --------------------------

  /// Appends @p value in @p base (2..36). Negative values get a leading '-'. Letters are lower case unless @p upper.
  template <typename Int>
  [[nodiscard]] result<void> try_append_int(Int value, unsigned base = 10, bool upper = false) & noexcept {
    static_assert(std::is_integral_v<Int> && !std::is_same_v<Int, bool>, "try_append_int requires an integer type");
    if (base < 2 || base > 36)
      return unexpected(error::invalid_argument);
    using U = std::make_unsigned_t<Int>;
    const bool negative = value < Int(0);
    U mag = negative ? static_cast<U>(U(0) - static_cast<U>(value)) : static_cast<U>(value);
    reloco::array<CharT, 72> tmp{};
    size_type pos = tmp.size();
    do {
      const auto digit = static_cast<unsigned>(mag % base);
      const char c = digit < 10 ? static_cast<char>(static_cast<unsigned>('0') + digit)
                                : static_cast<char>(static_cast<unsigned>(upper ? 'A' : 'a') + digit - 10);
      tmp[--pos] = static_cast<CharT>(c);
      mag = static_cast<U>(mag / base);
    } while (mag != 0);
    if (negative)
      tmp[--pos] = static_cast<CharT>('-');
    return self().try_append(view_type(tmp.data() + pos, tmp.size() - pos));
  }

  /// Appends the bytes as hexadecimal text (two digits per byte).
  [[nodiscard]] result<void> try_append_hex(span<const std::uint8_t> bytes, bool upper = false) & noexcept {
    const size_type old = self().size();
    if (bytes.size() > (std::numeric_limits<size_type>::max() - old) / 2)
      return unexpected(error::integer_overflow);
    if (auto r = self().try_reserve(old + bytes.size() * 2); !r)
      return r;
    const char *RELOCO_STATIC_STR digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    for (size_type i = 0; i < bytes.size(); ++i) {
      const reloco::array<CharT, 2> pair{static_cast<CharT>(digits[bytes[i] >> 4]),
                                         static_cast<CharT>(digits[bytes[i] & 0xF])};
      if (auto r = self().try_append(view_type(pair.data(), 2)); !r)
        return rollback(old, r);
    }
    return {};
  }

  /// Decodes hexadecimal text and appends the bytes (as characters) to this string.
  [[nodiscard]] result<void> try_append_from_hex(view_type hex) & noexcept {
    if (hex.size() % 2 != 0)
      return unexpected(error::invalid_argument);
    const size_type old = self().size();
    if (auto r = self().try_reserve(old + hex.size() / 2); !r)
      return r;
    for (size_type i = 0; i < hex.size(); i += 2) {
      const int hi = hex_value(hex[i]);
      const int lo = hex_value(hex[i + 1]);
      if (hi < 0 || lo < 0)
        return rollback(old, result<void>(unexpected(error::invalid_argument)));
      if (auto r = self().try_push_back(static_cast<CharT>(hi * 16 + lo)); !r)
        return rollback(old, r);
    }
    return {};
  }

  /// Appends RFC 4648 base64 text (`url_safe` selects the `-_` alphabet; @p padding appends '=').
  [[nodiscard]] result<void> try_append_base64(span<const std::uint8_t> bytes, bool url_safe = false,
                                               bool padding = true) & noexcept {
    const char *RELOCO_STATIC_STR alphabet = url_safe
                                                 ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
                                                 : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_type old = self().size();
    if (bytes.size() > (std::numeric_limits<size_type>::max() - old) / 4 * 3)
      return unexpected(error::integer_overflow);
    if (auto r = self().try_reserve(old + (bytes.size() + 2) / 3 * 4); !r)
      return r;
    size_type i = 0;
    while (i < bytes.size()) {
      const size_type left = bytes.size() - i;
      const std::uint32_t b0 = bytes[i];
      const std::uint32_t b1 = left > 1 ? bytes[i + 1] : 0u;
      const std::uint32_t b2 = left > 2 ? bytes[i + 2] : 0u;
      const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
      reloco::array<CharT, 4> quad{
          static_cast<CharT>(alphabet[(triple >> 18) & 63u]), static_cast<CharT>(alphabet[(triple >> 12) & 63u]),
          static_cast<CharT>(alphabet[(triple >> 6) & 63u]), static_cast<CharT>(alphabet[triple & 63u])};
      size_type emit = 4;
      if (left < 3) {
        emit = left + 1;
        if (padding)
          for (size_type k = emit; k < 4; ++k)
            quad[k] = static_cast<CharT>('=');
      }
      const size_type len = padding ? 4 : emit;
      if (auto r = self().try_append(view_type(quad.data(), len)); !r)
        return rollback(old, r);
      i += 3;
    }
    return {};
  }

  /// Decodes base64 text (either alphabet, padding optional) and appends the bytes (as characters).
  [[nodiscard]] result<void> try_append_from_base64(view_type text) & noexcept {
    while (!text.empty() && TraitsT::eq(text.back(), static_cast<CharT>('=')))
      text.remove_suffix(1);
    if (text.size() % 4 == 1)
      return unexpected(error::invalid_argument);
    const size_type old = self().size();
    if (auto r = self().try_reserve(old + text.size() / 4 * 3 + 2); !r)
      return r;
    std::uint32_t acc = 0;
    unsigned bits = 0;
    for (size_type i = 0; i < text.size(); ++i) {
      const int v = base64_value(text[i]);
      if (v < 0)
        return rollback(old, result<void>(unexpected(error::invalid_argument)));
      acc = (acc << 6) | static_cast<std::uint32_t>(v);
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        if (auto r = self().try_push_back(static_cast<CharT>((acc >> bits) & 0xFFu)); !r)
          return rollback(old, r);
      }
    }
    return {};
  }

  /// Appends @p sv percent-encoded: unreserved characters (`A-Za-z0-9-._~`) pass through, the rest become `%XX`.
  [[nodiscard]] result<void> try_append_url_encoded(view_type sv) & noexcept {
    const size_type old = self().size();
    for (size_type i = 0; i < sv.size(); ++i) {
      const auto c = static_cast<unsigned>(sv[i]) & 0xFFu;
      const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                         c == '.' || c == '_' || c == '~';
      result<void> r = plain ? self().try_push_back(sv[i]) : append_percent(c);
      if (!r)
        return rollback(old, r);
    }
    return {};
  }

  /// Appends @p sv with `%XX` sequences decoded (and '+' left as is). Malformed escapes are `invalid_argument`.
  [[nodiscard]] result<void> try_append_url_decoded(view_type sv) & noexcept {
    const size_type old = self().size();
    for (size_type i = 0; i < sv.size(); ++i) {
      CharT out = sv[i];
      if (TraitsT::eq(out, static_cast<CharT>('%'))) {
        if (i + 2 >= sv.size())
          return rollback(old, result<void>(unexpected(error::invalid_argument)));
        const int hi = hex_value(sv[i + 1]);
        const int lo = hex_value(sv[i + 2]);
        if (hi < 0 || lo < 0)
          return rollback(old, result<void>(unexpected(error::invalid_argument)));
        out = static_cast<CharT>(hi * 16 + lo);
        i += 2;
      }
      if (auto r = self().try_push_back(out); !r)
        return rollback(old, r);
    }
    return {};
  }

  // ---- escaping and Display bridge ----------------------------------------------------------------------------

  /// Appends @p sv with C-style escapes: `\n \r \t \0 \\ \" \'`, other control characters (and DEL) as `\xNN`.
  /// Bytes >= 0x80 pass through unchanged, so UTF-8 text stays readable.
  [[nodiscard]] result<void> try_append_escaped(view_type sv) & noexcept {
    const size_type old = self().size();
    for (size_type i = 0; i < sv.size(); ++i) {
      const auto c = static_cast<unsigned>(sv[i]) & 0xFFu;
      char letter = 0;
      switch (c) {
      case '\n':
        letter = 'n';
        break;
      case '\r':
        letter = 'r';
        break;
      case '\t':
        letter = 't';
        break;
      case 0:
        letter = '0';
        break;
      case '\\':
        letter = '\\';
        break;
      case '"':
        letter = '"';
        break;
      case '\'':
        letter = '\'';
        break;
      default:
        break;
      }
      result<void> r{};
      if (letter != 0) {
        const reloco::array<CharT, 2> esc{static_cast<CharT>('\\'), static_cast<CharT>(letter)};
        r = self().try_append(view_type(esc.data(), 2));
      } else if (c < 0x20u || c == 0x7Fu) {
        const char *RELOCO_STATIC_STR digits = "0123456789abcdef";
        const reloco::array<CharT, 4> esc{static_cast<CharT>('\\'), static_cast<CharT>('x'),
                                          static_cast<CharT>(digits[c >> 4]), static_cast<CharT>(digits[c & 0xF])};
        r = self().try_append(view_type(esc.data(), 4));
      } else {
        r = self().try_push_back(sv[i]);
      }
      if (!r)
        return rollback(old, r);
    }
    return {};
  }

  /// Appends @p sv with the escapes of `try_append_escaped` decoded. Unknown or truncated escapes are
  /// `invalid_argument`.
  [[nodiscard]] result<void> try_append_unescaped(view_type sv) & noexcept {
    const size_type old = self().size();
    for (size_type i = 0; i < sv.size(); ++i) {
      CharT out = sv[i];
      if (TraitsT::eq(out, static_cast<CharT>('\\'))) {
        if (++i >= sv.size())
          return rollback(old, result<void>(unexpected(error::invalid_argument)));
        switch (static_cast<unsigned>(sv[i])) {
        case 'n':
          out = static_cast<CharT>('\n');
          break;
        case 'r':
          out = static_cast<CharT>('\r');
          break;
        case 't':
          out = static_cast<CharT>('\t');
          break;
        case '0':
          out = static_cast<CharT>('\0');
          break;
        case '\\':
        case '"':
        case '\'':
          out = sv[i];
          break;
        case 'x': {
          if (i + 2 >= sv.size())
            return rollback(old, result<void>(unexpected(error::invalid_argument)));
          const int hi = hex_value(sv[i + 1]);
          const int lo = hex_value(sv[i + 2]);
          if (hi < 0 || lo < 0)
            return rollback(old, result<void>(unexpected(error::invalid_argument)));
          out = static_cast<CharT>(hi * 16 + lo);
          i += 2;
          break;
        }
        default:
          return rollback(old, result<void>(unexpected(error::invalid_argument)));
        }
      }
      if (auto r = self().try_push_back(out); !r)
        return rollback(old, r);
    }
    return {};
  }

  /// Appends `Display<T>` output (see fmt.hpp). Allocation failure during formatting is detected and the string is
  /// restored; the first error is returned.
  template <typename T> [[nodiscard]] result<void> try_append_display(const T &value) & noexcept {
    static_assert(has_display_v<T>, "T needs a reloco::Display<T> specialization");
    return append_via_sink([&value](const sink &out) noexcept { Display<T>::format(value, out); });
  }

  /// Appends `Debug<T>` output (see fmt.hpp); same failure behavior as `try_append_display`.
  template <typename T> [[nodiscard]] result<void> try_append_debug(const T &value) & noexcept {
    static_assert(has_debug_v<T>, "T needs a reloco::Debug<T> specialization");
    return append_via_sink([&value](const sink &out) noexcept { Debug<T>::format(value, out); });
  }

  // ---- Unicode ------------------------------------------------------------------------------------------------

  /// Appends one Unicode scalar value, encoded as UTF-8 / UTF-16 / UTF-32 depending on `sizeof(CharT)`.
  [[nodiscard]] result<void> try_append_char32(char32_t cp) & noexcept {
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
      return unexpected(error::invalid_argument);
    reloco::array<CharT, 4> buf{};
    size_type len = 0;
    const auto u = static_cast<std::uint32_t>(cp);
    if constexpr (sizeof(CharT) == 1) {
      if (u < 0x80u) {
        buf[len++] = static_cast<CharT>(u);
      } else if (u < 0x800u) {
        buf[len++] = static_cast<CharT>(0xC0u | (u >> 6));
        buf[len++] = static_cast<CharT>(0x80u | (u & 0x3Fu));
      } else if (u < 0x10000u) {
        buf[len++] = static_cast<CharT>(0xE0u | (u >> 12));
        buf[len++] = static_cast<CharT>(0x80u | ((u >> 6) & 0x3Fu));
        buf[len++] = static_cast<CharT>(0x80u | (u & 0x3Fu));
      } else {
        buf[len++] = static_cast<CharT>(0xF0u | (u >> 18));
        buf[len++] = static_cast<CharT>(0x80u | ((u >> 12) & 0x3Fu));
        buf[len++] = static_cast<CharT>(0x80u | ((u >> 6) & 0x3Fu));
        buf[len++] = static_cast<CharT>(0x80u | (u & 0x3Fu));
      }
    } else if constexpr (sizeof(CharT) == 2) {
      if (u < 0x10000u) {
        buf[len++] = static_cast<CharT>(u);
      } else {
        const std::uint32_t v = u - 0x10000u;
        buf[len++] = static_cast<CharT>(0xD800u + (v >> 10));
        buf[len++] = static_cast<CharT>(0xDC00u + (v & 0x3FFu));
      }
    } else {
      buf[len++] = static_cast<CharT>(u);
    }
    return self().try_append(view_type(buf.data(), len));
  }

  /// Appends @p sv after verifying it is valid UTF-8 (one-byte character types only).
  [[nodiscard]] result<void> try_append_utf8(view_type sv) & noexcept {
    static_assert(sizeof(CharT) == 1, "try_append_utf8 requires a one-byte character type");
    if (!sv.is_valid_utf8())
      return unexpected(error::invalid_argument);
    return self().try_append(sv);
  }

  /// Appends @p sv, replacing each invalid UTF-8 sequence with U+FFFD (one-byte character types only).
  [[nodiscard]] result<void> try_append_utf8_lossy(view_type sv) & noexcept {
    static_assert(sizeof(CharT) == 1, "try_append_utf8_lossy requires a one-byte character type");
    const size_type old = self().size();
    for (size_type i = 0; i < sv.size();) {
      const auto step = detail::decode_utf8(sv, i);
      const result<void> r =
          step.valid ? self().try_append(sv.substr(i, step.length)) : try_append_char32(char32_t{0xFFFD});
      if (!r)
        return rollback(old, r);
      i += step.length;
    }
    return {};
  }

  /// Appends UTF-16 text transcoded to this string's encoding. Lone surrogates are `invalid_argument`.
  [[nodiscard]] result<void> try_append_utf16(span<const char16_t> text) & noexcept {
    const size_type old = self().size();
    for (size_type i = 0; i < text.size(); ++i) {
      const auto u = static_cast<std::uint32_t>(text[i]);
      char32_t cp = static_cast<char32_t>(u);
      if (u >= 0xD800u && u <= 0xDBFFu) {
        const auto lo = i + 1 < text.size() ? static_cast<std::uint32_t>(text[i + 1]) : 0u;
        if (lo < 0xDC00u || lo > 0xDFFFu)
          return rollback(old, result<void>(unexpected(error::invalid_argument)));
        cp = static_cast<char32_t>(0x10000u + ((u - 0xD800u) << 10) + (lo - 0xDC00u));
        ++i;
      }
      if (auto r = try_append_char32(cp); !r)
        return rollback(old, r);
    }
    return {};
  }

  /// Transcodes this UTF-8 string into @p out; returns the number of `char16_t` written (`out_of_bounds` if too small).
  [[nodiscard]] result<size_type> try_to_utf16_into(span<char16_t> out) const & noexcept {
    static_assert(sizeof(CharT) == 1, "try_to_utf16_into requires a one-byte character type");
    const view_type sv = view_of();
    size_type w = 0;
    for (size_type i = 0; i < sv.size();) {
      const auto step = detail::decode_utf8(sv, i);
      if (!step.valid)
        return unexpected(error::invalid_argument);
      const auto cp = static_cast<std::uint32_t>(step.code_point);
      const size_type need = cp >= 0x10000u ? 2 : 1;
      if (w + need > out.size())
        return unexpected(error::out_of_bounds);
      if (need == 1) {
        out[w++] = static_cast<char16_t>(cp);
      } else {
        out[w++] = static_cast<char16_t>(0xD800u + ((cp - 0x10000u) >> 10));
        out[w++] = static_cast<char16_t>(0xDC00u + ((cp - 0x10000u) & 0x3FFu));
      }
      i += step.length;
    }
    return w;
  }

  /// Number of `char16_t` needed to hold this UTF-8 string (`invalid_argument` when it is not valid UTF-8).
  [[nodiscard]] result<size_type> try_utf16_length() const & noexcept {
    static_assert(sizeof(CharT) == 1, "try_utf16_length requires a one-byte character type");
    const view_type sv = view_of();
    size_type n = 0;
    for (size_type i = 0; i < sv.size();) {
      const auto step = detail::decode_utf8(sv, i);
      if (!step.valid)
        return unexpected(error::invalid_argument);
      n += step.code_point >= char32_t{0x10000} ? 2 : 1;
      i += step.length;
    }
    return n;
  }

protected:
  string_ops_base() = default;
  string_ops_base(const string_ops_base &) = default;
  string_ops_base(string_ops_base &&) = default;
  string_ops_base &operator=(const string_ops_base &) = default;
  string_ops_base &operator=(string_ops_base &&) = default;
  ~string_ops_base() = default;

private:
  [[nodiscard]] Derived &self() & noexcept RELOCO_LIFETIMEBOUND { return static_cast<Derived &>(*this); }
  [[nodiscard]] const Derived &self() const & noexcept RELOCO_LIFETIMEBOUND {
    return static_cast<const Derived &>(*this);
  }
  void self() const && = delete;
  [[nodiscard]] view_type view_of() const noexcept { return self().view(); }

  [[nodiscard]] bool aliases_self(view_type sv) const noexcept {
    const auto self_view = view_of();
    return !sv.empty() && !self_view.empty() && sv.data() >= self_view.data() &&
           sv.data() < self_view.data() + self_view.size();
  }

  template <typename F> result<void> append_via_sink(F &&emit) & noexcept {
    static_assert(std::is_same_v<CharT, char>, "Display/Debug sinks write `char` text");
    struct state {
      Derived *str;
      error err;
      bool failed;
    };
    state st{&self(), error::invalid_state, false};
    const sink out{&st, [](void *ctx, reloco::string_view piece) noexcept {
                     auto *stp = static_cast<state *>(ctx); // NOLINT(reloco-no-raw-pointer): sink ctx is type-erased
                     if (stp->failed)
                       return;
                     if (auto r = stp->str->try_append(typename Derived::view_type(piece.data(), piece.size())); !r) {
                       stp->failed = true;
                       stp->err = r.error();
                     }
                   }};
    const size_type old = self().size();
    emit(out);
    if (st.failed)
      return rollback(old, result<void>(unexpected(st.err)));
    return {};
  }

  result<void> rollback(size_type old_size, const result<void> &failure) noexcept {
    (void)self().try_erase(old_size, npos);
    return unexpected(failure.error());
  }

  result<void> append_percent(unsigned byte) noexcept {
    const char *RELOCO_STATIC_STR digits = "0123456789ABCDEF";
    const reloco::array<CharT, 3> esc{static_cast<CharT>('%'), static_cast<CharT>(digits[byte >> 4]),
                                      static_cast<CharT>(digits[byte & 0xF])};
    return self().try_append(view_type(esc.data(), 3));
  }

  [[nodiscard]] static int hex_value(CharT c) noexcept {
    const auto v = static_cast<unsigned>(c);
    if (v >= '0' && v <= '9')
      return static_cast<int>(v - '0');
    if (v >= 'a' && v <= 'f')
      return static_cast<int>(v - 'a') + 10;
    if (v >= 'A' && v <= 'F')
      return static_cast<int>(v - 'A') + 10;
    return -1;
  }

  [[nodiscard]] static int base64_value(CharT c) noexcept {
    const auto v = static_cast<unsigned>(c);
    if (v >= 'A' && v <= 'Z')
      return static_cast<int>(v - 'A');
    if (v >= 'a' && v <= 'z')
      return static_cast<int>(v - 'a') + 26;
    if (v >= '0' && v <= '9')
      return static_cast<int>(v - '0') + 52;
    if (v == '+' || v == '-')
      return 62;
    if (v == '/' || v == '_')
      return 63;
    return -1;
  }

  [[nodiscard]] static CharT ascii_upper(CharT c) noexcept {
    return (c >= CharT('a') && c <= CharT('z')) ? static_cast<CharT>(c - (CharT('a') - CharT('A'))) : c;
  }
  [[nodiscard]] static CharT ascii_lower(CharT c) noexcept {
    return (c >= CharT('A') && c <= CharT('Z')) ? static_cast<CharT>(c + (CharT('a') - CharT('A'))) : c;
  }
};

#undef RELOCO_STRING_FORWARD_
#undef RELOCO_STRING_FORWARD_LIFETIMEBOUND_

RELOCO_END_UNSAFE_BUFFER_USAGE

} // namespace detail
} // namespace reloco
