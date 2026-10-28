# string_view: Rust-style text operations

`include/reloco/string_view.hpp` — tests: `tests/test_string_view_ops.cpp`

## Why

Parsing text (config lines, protocol headers, CSV, command lines) is mostly "find a separator, take what is
before it, continue with what is after it". With `std::string_view` that means `find` + `substr` + index
arithmetic, and every off-by-one is a bug. `reloco::string_view` adds the vocabulary Rust's `str` has for this:
splitting, trimming, searching, parsing integers, case-insensitive comparison and UTF-8 walking.

All of it is **zero-allocation**: results are views into the original text, and the split/match functions return
**lazy ranges** you iterate with a range-for. The text must outlive the views, exactly as with any view.
There is no floating point and no locale dependence anywhere.

## Patterns

Most functions take a *pattern*, which is any of:

| Pattern | Matches |
|---|---|
| a character (`','`) | that character |
| a predicate (`[](char c) { return c == ' '; }`) | every character for which it returns `true` |
| a string (`"--"`, `string_view`, `std::string`) | that exact substring (an empty string never matches) |

## Conventions

- **Checked** members assert on misuse (`split_at`); **`try_*`** members return `result<T>` with
  `error::out_of_bounds`, `not_found`, `invalid_argument`, `out_of_range` or `integer_overflow`.
- Rust's `find`-style "maybe" answers are `result` (`split_once`) or `npos` (`find*`, as in `std`).
- Hardened `result` temporaries cannot be dereferenced in the same expression; bind them first
  (`auto kv = line.split_once('='); if (kv) ...`).

## Example 1: parsing `key = value` configuration lines

```cpp
#include <reloco/string_view.hpp>

// Reads a config text such as:
//   # comment
//   name = demo
//   port = 8080
reloco::result<void> load(reloco::string_view text) noexcept {
  // lines() yields each line without its "\n" or "\r\n"; no trailing empty line, nothing is copied.
  for (reloco::string_view line : text.lines()) {
    line = line.trim();                       // trim() drops ASCII whitespace at both ends
    if (line.empty() || line.starts_with('#')) // skip blanks and comments
      continue;

    // split_once() splits at the FIRST '='. It returns result<{first, second}> so a line without '='
    // is reported as error::not_found instead of being silently misparsed.
    auto kv = line.split_once('=');
    if (!kv)
      return reloco::unexpected(kv.error());
    reloco::string_view key = kv->first.trim();
    reloco::string_view value = kv->second.trim();

    if (key == "port") {
      // try_parse<Int>(base) parses the WHOLE view: "8080" -> 8080, "80x" -> error::invalid_argument,
      // "70000" for uint16_t -> error::out_of_range (does not fit).
      auto port = value.try_parse<std::uint16_t>();
      if (!port)
        return reloco::unexpected(port.error());
      // use *port ...
    }
  }
  return {};
}
```

## Example 2: splitting (CSV-ish rows, paths, command lines)

```cpp
reloco::string_view row = "alice,30,,Berlin";

// split(',') is lazy: each piece is a view into `row`. Empty fields are kept: "alice","30","","Berlin".
for (reloco::string_view field : row.split(',')) { /* ... */ }

// splitn(2, ' ') stops after 2 pieces; the last one is the untouched remainder.
// "GET /index.html HTTP/1.1" -> "GET", "/index.html HTTP/1.1"
for (auto part : reloco::string_view("GET /index.html HTTP/1.1").splitn(2, ' ')) {}

// rsplit_once() splits at the LAST match: handy for file extensions and "host:port".
auto ext = reloco::string_view("archive.tar.gz").rsplit_once('.'); // {"archive.tar", "gz"}

// A predicate pattern splits at any character class, here every run of digits.
for (auto word : reloco::string_view("a1b22c").split([](char c) { return c >= '0' && c <= '9'; })) {}

// split_whitespace() skips empty pieces, so repeated spaces/tabs/newlines are fine.
for (auto token : reloco::string_view("  ls   -la\t/tmp ").split_whitespace()) {} // "ls" "-la" "/tmp"

// split_inclusive('\n') keeps the separator, useful when you must preserve the exact text.
for (auto line : reloco::string_view("a\nb\n").split_inclusive('\n')) {} // "a\n" "b\n"
```

Variants: `rsplit` (last to first), `rsplitn`, `split_terminator` (no trailing empty piece), `split_at(pos)` /
`try_split_at(pos)` (two halves at an index; usable with structured bindings: `auto [head, tail] = s.split_at(3);`).

## Example 3: searching, counting, trimming

```cpp
reloco::string_view s = "xx--hello--world--xx";

s.contains("hello");               // true; also contains('x') and contains(predicate)
s.count("--");                     // 4: non-overlapping matches
s.rfind('-');                      // index of the last '-', or string_view::npos
s.find_first_not_of("x-");         // index of the first character NOT in the set

// trim_matches() removes repeated copies of a pattern from both ends: "hello--world"
auto core = s.trim_matches('x').trim_matches("--");

// matches()/match_indices() iterate every match; indices let you point at the exact place.
for (auto [index, hit] : s.match_indices("--")) { /* index = offset of this "--" */ }

// strip_prefix/strip_suffix return optional<view>: "does it start with it, and what is left?"
if (auto rest = reloco::string_view("--verbose").strip_prefix("--"))
  { /* *rest == "verbose" */ }
```

## Example 4: tokenizing with `try_consume_int`

```cpp
// try_consume_int() parses an integer at the START of the view and removes it, so a loop can walk a
// "10,20,-5" style list without any index bookkeeping.
reloco::result<int> sum(reloco::string_view list) noexcept {
  int total = 0;
  while (!list.empty()) {
    auto n = list.try_consume_int<int>();   // consumes "10", leaves ",20,-5"
    if (!n)
      return reloco::unexpected(n.error()); // not a number / does not fit in int
    total += *n;
    list.try_remove_prefix(list.starts_with(',') ? 1 : 0).value(); // skip the separator
  }
  return total;
}
```

`try_parse<Int>(base)` accepts bases 2..36, an optional `+` (and `-` for signed types) and never skips spaces:
trim first if the input may contain them. `try_parse_bool()` accepts exactly `true` / `false`.

## Example 5: case-insensitive comparison (HTTP headers, keywords)

```cpp
reloco::string_view header = "Content-Type: text/html";
auto [name, value] = header.split_once(':').value(); // see note on temporaries above

// ASCII-only folding: locale independent and allocation free.
if (name.eq_ignore_ascii_case("content-type")) { /* matches "CONTENT-TYPE" too */ }
if (header.starts_with_ignore_ascii_case("content-")) {}
int order = name.compare_ignore_ascii_case("Accept"); // <0, 0 or >0, usable as a sort comparator
```

## Example 6: UTF-8 text (one-byte `char` only)

```cpp
reloco::string_view text = "caf\xC3\xA9"; // "café": 5 bytes, 4 code points

text.is_valid_utf8();                 // true; false for overlong forms, surrogates, truncated sequences
auto n = text.try_char_count();       // result 4, or error::invalid_argument when malformed
text.is_char_boundary(4);             // false: byte 4 is the middle of 'é', do NOT cut the view here

// utf8_chars() yields char32_t code points; malformed bytes yield U+FFFD so the loop never gets stuck.
for (char32_t cp : text.utf8_chars()) { /* 'c' 'a' 'f' U+00E9 */ }
```

## Example 7: operations that allocate

These are the only members that allocate; they return `result<string>` (or any `basic_string` type) and take an
optional `allocator_ref` first argument. Include `string.hpp` where they are used.

```cpp
#include <reloco/string.hpp>

reloco::string_view path = "a/b/c";

// Default allocator.
auto dashed = path.try_replace("/", "-");      // result<string>: "a-b-c"
auto shout = path.try_to_ascii_uppercase();    // "A/B/C"
auto line = reloco::string_view("=").try_repeat(20); // "===================="

// Explicit allocator: pass the allocator_ref first. Failures (out of memory) are returned, never thrown.
reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
auto copy = path.try_replace(heap, "/", "\\");
if (!copy)
  return copy.error();
```

(Joining many views is `string::try_join(range, separator)`.)

## API summary

| Group | Members |
|---|---|
| Search | `find`, `rfind`, `find_first_of`, `find_first_not_of`, `find_last_not_of`, `find_if`, `rfind_if`, `contains`, `count`, `matches`, `match_indices` |
| Split | `split`, `rsplit`, `splitn`, `rsplitn`, `split_terminator`, `split_inclusive`, `split_whitespace`, `lines`, `split_once`, `rsplit_once`, `split_at`, `try_split_at` |
| Trim/strip | `trim*`, `trim_matches`, `trim_start_matches`, `trim_end_matches`, `strip_prefix`, `strip_suffix` (string or char) |
| ASCII | `is_ascii`, `eq_ignore_ascii_case`, `compare_ignore_ascii_case`, `starts_with_ignore_ascii_case`, `ends_with_ignore_ascii_case`, `is_char_boundary` |
| Parse | `try_parse<Int>`, `try_consume_int<Int>`, `try_parse_bool` |
| UTF-8 | `is_valid_utf8`, `try_char_count`, `utf8_chars` |
| Allocating | `try_replace`, `try_to_ascii_uppercase`, `try_to_ascii_lowercase`, `try_repeat` |
