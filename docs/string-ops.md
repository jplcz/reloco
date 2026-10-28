# Mutable string operations (`string`, `sso_string`, `inline_string`)

`include/reloco/detail/string_ops_base.hpp` — tests: `tests/test_string_ops.cpp`

## Why

The three owning strings differ only in *where the characters live* (heap, small-buffer or fixed array). Everything
you do with text afterwards — trim it, replace parts of it, pad it, append a number, hex or base64 — is the same.
Instead of three copies, the operations live once in a CRTP base class that every mutable string inherits, so
**every API below works identically on `reloco::string`, `reloco::sso_string` and `reloco::inline_string<N>`** (and
on the wide variants, where it makes sense).

Three rules keep the code predictable:

1. **Read-only operations are the `string_view` ones.** `s.split(',')`, `s.trim()`, `s.rfind(...)` forward to
   `s.view()`. They are documented in [string-view-ops.md](string-view-ops.md). Calling them on a temporary string is
   a compile error (the result would dangle).
2. **In-place edits reuse the view algorithms.** `trim_in_place()` is "ask `view().trim()` how much to keep, then
   erase the rest" — there is no second trim implementation to keep in sync.
3. **Mutators are all-or-nothing.** Every `try_*` mutator returns `result<void>` (or `result<T>`); when it fails
   (out of memory, `inline_string` capacity, bad input) the string is exactly as it was before.

## Reference

| Group | Members |
|---|---|
| View forwarding (read-only) | `find`, `rfind`, `find_first_of`, `find_first_not_of`, `find_last_not_of`, `find_if`, `rfind_if`, `contains`, `count`, `matches`, `match_indices`, `starts_with`, `ends_with`, `strip_prefix`, `strip_suffix`, `trim*`, `split*`, `rsplit*`, `lines`, `split_once`, `rsplit_once`, `split_at`, `try_split_at`, `substr`, `try_substr`, `try_parse<Int>`, `try_parse_bool`, ASCII comparisons, `is_valid_utf8`, `try_char_count`, `utf8_chars` |
| Cutting | `truncate(len)`, `trim_in_place`, `trim_start_in_place`, `trim_end_in_place`, `trim_matches_in_place`, `trim_start_matches_in_place`, `trim_end_matches_in_place`, `strip_prefix_in_place`, `strip_suffix_in_place`, `retain(pred)`, `dedup_chars()` |
| Replacing / splitting off | `try_replace_range(pos, count, sv)`, `try_replace_all(from, to)`, `try_replacen(from, to, n)`, `try_prepend(sv)`, `try_split_off(pos)`, `try_drain(pos, count)` |
| Case | `make_ascii_uppercase`, `make_ascii_lowercase`, `make_ascii_titlecase` |
| Building | `try_assign_repeat(ch, n)`, `try_append_repeat(sv, n)`, `try_extend_from(range)`, `try_pad_left/right(width, ch)`, `try_center(width, ch)` |
| Encoders / decoders | `try_append_int<Int>(v, base, upper)`, `try_append_hex`, `try_append_from_hex`, `try_append_base64`, `try_append_from_base64`, `try_append_url_encoded`, `try_append_url_decoded` |
| Escaping / Display | `try_append_escaped`, `try_append_unescaped`, `try_append_display<T>`, `try_append_debug<T>` |
| Unicode | `try_append_char32`, `try_append_utf8`, `try_append_utf8_lossy`, `try_append_utf16`, `try_to_utf16_into`, `try_utf16_length` |

`inline_string` additionally gained `try_reserve(n)`, which succeeds when `n <= Capacity`.

Notes worth knowing:

- Functions that must not alias their own text (`try_replace_all` / `try_replacen` with a `from`/`to` pointing into the
  string) return `error::invalid_argument`. `try_replace_range` and `try_prepend` *are* alias-safe.
- Byte decoders (`try_append_from_hex`, `try_append_from_base64`, `try_append_url_decoded`) append the decoded bytes
  *as characters*, so the string doubles as a byte buffer.
- Base64 and hex are encodings, not cryptography; use a crypto library for hashes, signatures and ciphers.
- No floating point and no locale dependence anywhere.

## Example 1 — cleaning user input

Config values arrive with stray whitespace and quotes. Trim in place instead of allocating a second string.

```cpp
#include <reloco/string.hpp>

reloco::result<void> normalize(reloco::string &value) noexcept {
  // Removes leading/trailing ASCII whitespace; computed by view().trim(), so identical to the view behavior.
  if (auto r = value.trim_in_place(); !r)
    return r;
  // The argument is a pattern: a char, a predicate or a string. Here: strip any number of surrounding quotes.
  if (auto r = value.trim_matches_in_place('"'); !r)
    return r;
  // `true` when the prefix was present (and has been removed); no error possible, erasing never allocates.
  (void)value.strip_prefix_in_place(reloco::string_view("v="));
  // Lower-case ASCII letters in place; never allocates.
  value.make_ascii_lowercase();
  return {};
}
```

## Example 2 — templating with `try_replace_all`

```cpp
#include <reloco/sso_string.hpp>

reloco::result<reloco::sso_string> greet(reloco::string_view name) noexcept {
  auto text = reloco::sso_string::try_create("Hello, {name}! Bye, {name}.");
  if (!text)
    return text;
  // from = "{name}", to = the replacement. Every occurrence is replaced in a single pass, growing the buffer once.
  // On failure (allocation) the string keeps its original text. from/to must not point into `text` itself.
  if (auto r = text->try_replace_all("{name}", name); !r)
    return reloco::unexpected(r.error());
  return text;
}
```

## Example 3 — fixed-size formatted line without allocation

`inline_string` never allocates, so it suits logging in kernel or interrupt-like code. Every append reports
`out_of_bounds` instead of overflowing.

```cpp
#include <reloco/inline_string.hpp>

reloco::result<reloco::inline_string<48>> status_line(int code, unsigned bytes) noexcept {
  auto line = reloco::inline_string<48>::try_create("status=");
  if (!line)
    return line;
  // try_append_int(value, base = 10, upper = false). Negative numbers get a '-'.
  if (auto r = line->try_append_int(code); !r)
    return reloco::unexpected(r.error());
  if (auto r = line->try_append(" bytes=0x"); !r)
    return reloco::unexpected(r.error());
  // base 16 with upper-case digits.
  if (auto r = line->try_append_int(bytes, 16, true); !r)
    return reloco::unexpected(r.error());
  // Right-pad with '.' to a fixed 40-column field so lines align.
  if (auto r = line->try_pad_right(40, '.'); !r)
    return reloco::unexpected(r.error());
  return line;
}
```

## Example 4 — hex / base64 / URL encoding

```cpp
#include <reloco/string.hpp>
#include <reloco/span.hpp>

reloco::result<void> encode(reloco::string &out, reloco::span<const std::uint8_t> payload) noexcept {
  // Lower-case hex, two digits per byte: {0xDE,0xAD} -> "dead".
  if (auto r = out.try_append_hex(payload); !r)
    return r;
  if (auto r = out.try_append(" "); !r)
    return r;
  // base64(bytes, url_safe = false, padding = true). url_safe=true swaps '+/' for '-_'.
  if (auto r = out.try_append_base64(payload, true, false); !r)
    return r;
  // Percent-encodes everything except A-Z a-z 0-9 - . _ ~ ; useful for query parameters.
  return out.try_append_url_encoded("a b&c");
}
```

## Example 5 — splitting a string in two

```cpp
#include <reloco/string.hpp>

// Turns "key=value" into key (left in `line`) and value (returned), without searching twice.
reloco::result<reloco::string> take_value(reloco::string &line) noexcept {
  const auto eq = line.find('=');
  if (eq == reloco::string::npos)
    return reloco::unexpected(reloco::error::not_found);
  // Moves everything from `eq` on into a new string and truncates `line` to `eq`; same allocator as `line`.
  auto tail = line.try_split_off(eq);
  if (!tail)
    return tail;
  // Drop the "=" the tail still starts with.
  (void)tail->strip_prefix_in_place('=');
  return tail;
}
```

## Example 6 — Unicode

```cpp
#include <reloco/string.hpp>
#include <reloco/span.hpp>

reloco::result<void> unicode(reloco::string &s, reloco::span<const char16_t> wide) noexcept {
  // A Unicode scalar value is encoded according to the string's character type: UTF-8 for `char`,
  // UTF-16 for `char16_t`, UTF-32 for `char32_t`. Surrogates and values > U+10FFFF are `invalid_argument`.
  if (auto r = s.try_append_char32(0x1F600); !r)
    return r;
  // Transcodes UTF-16 -> UTF-8 (surrogate pairs are combined; a lone surrogate is `invalid_argument`).
  if (auto r = s.try_append_utf16(wide); !r)
    return r;
  // Untrusted bytes: keep what is valid and put U+FFFD in place of every broken sequence.
  return s.try_append_utf8_lossy(reloco::string_view("ok\xFF"));
}
```

## Example 7 — escaping and Display

```cpp
#include <reloco/string.hpp>

// Builds a log line: the raw text is escaped so control characters can't break the line.
reloco::result<void> log_line(reloco::string &out, reloco::string_view raw, const MyType &value) noexcept {
  // Newline -> \n, tab -> \t, quotes/backslash escaped, other control bytes -> \xNN. UTF-8 passes through.
  if (auto r = out.try_append_escaped(raw); !r)
    return r;
  // Runs reloco::Display<MyType>::format into this string (needs a Display specialization; char strings only).
  // If an append fails mid-way the string is restored and the first error returned.
  return out.try_append_display(value);
}
// try_append_unescaped(text) is the inverse; unknown escapes give error::invalid_argument.
```

## Integration notes

- **Adding a new string type:** inherit `detail::string_ops_base<Derived, CharT, Traits>` and provide `view()`,
  `data()` (`&`), `size()`, `try_insert`, `try_erase`, `try_resize`, `try_append`, `try_push_back`, `try_reserve` and
  `try_create_like(view)` (a new string of the same kind holding a copy, used by `try_split_off`/`try_drain`).
- **Display / formatting:** `try_append_display`/`try_append_debug` bridge `fmt.hpp`'s `Display<T>`/`Debug<T>` to any of
  these strings through a `sink`; sink writes cannot report errors, so failures are recorded and surfaced afterwards.
