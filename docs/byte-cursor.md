<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Binary parsing and serialization: `byte_reader` / `byte_writer`

`include/reloco/byte_cursor.hpp`

Bounds-checked sequential cursors over a caller-owned byte buffer. Both are
small views (a span plus a position), never allocate, and make byte order
explicit on every integer access (see [`endian.hpp`](endian.md)).

Every operation is **all-or-nothing**: on failure the position is unchanged
and, for the writer, the buffer is untouched. Errors:

| Error | When |
|---|---|
| `out_of_bounds` | Not enough bytes left / not enough room, or a seek past the end |
| `invalid_argument` | Alignment is not a power of two |
| `not_found` | `try_read_until` did not find the delimiter |
| `integer_overflow` | A LEB128 value does not fit 64 bits |

The viewed buffer must outlive the cursor and every span it returns.
Supported integer types: integers except `bool`, and enums. No floating point:
read a `uint32_t`/`uint64_t` and reinterpret the bits yourself.

## `byte_reader`

```cpp
#include <reloco/byte_cursor.hpp>

using reloco::byte_order;

struct header {
  std::uint16_t version;
  std::uint32_t length;
};

// Parses: u16 version (big endian), u32 length (big endian), then `length`
// bytes of body that are returned as a view into `packet`.
reloco::result<reloco::span<const std::byte>> parse(reloco::span<const std::byte> packet) {
  // The reader starts at offset 0 of `packet`; it does not copy anything.
  reloco::byte_reader in(packet);

  // Each read returns result<T>. RELOCO_TRY (error.hpp) unwraps it or returns
  // the error to the caller; <T> is the type, _be means big endian.
  auto version = RELOCO_TRY(in.try_read_be<std::uint16_t>());
  auto length = RELOCO_TRY(in.try_read_be<std::uint32_t>());
  (void)version;

  // Returns a view of the next `length` bytes (error if the packet is short).
  return in.try_read_bytes(length);
}
```

Reading API (all `[[nodiscard]]`, all return `result<...>`):

| Call | Effect |
|---|---|
| `try_read<T, Order>()`, `try_read_le<T>()`, `try_read_be<T>()`, `try_read_ne<T>()`, `try_read_u8()` | Read and consume an integer/enum |
| `try_peek<T, Order>()` | Same, without consuming |
| `try_read_bytes(n)` / `try_peek_bytes(n)` | View of the next `n` bytes |
| `try_read_into(span<std::byte>)` | Copy `out.size()` bytes out |
| `try_skip(n)`, `try_seek(offset)`, `try_align(a)` | Move the position; `a` must be a power of two, measured from the buffer start |
| `try_split(n)` | Consume `n` bytes and return a `byte_reader` limited to them |
| `try_read_until(delim)` | Bytes before the first `delim`; the delimiter is consumed too |
| `try_read_uleb128()` / `try_read_sleb128()` | LEB128 varints up to 64 bits |
| `size()`, `position()`, `remaining()`, `at_end()`, `remaining_bytes()`, `consumed_bytes()` | Inspection |
| `unsafe_read<T, Order>()` | No bounds check beyond a debug assertion; `RELOCO_UNSAFE_BUFFER_USAGE`, for after you verified `remaining()` |

### Length-prefixed records

`try_split` keeps a record's parser from running past the record:

```cpp
// Record layout: u16 length (little endian), then `length` bytes of payload.
reloco::result<void> parse_record(reloco::byte_reader &in) {
  auto length = RELOCO_TRY(in.try_read_le<std::uint16_t>());
  // `payload` can only read the next `length` bytes; `in` continues after them.
  reloco::byte_reader payload = RELOCO_TRY(in.try_split(length));
  auto tag = RELOCO_TRY(payload.try_read_u8());
  (void)tag;
  return {};
}
```

### Speculative parsing

A reader is cheap to copy, so "try a format, fall back to another" needs no
rewind API:

```cpp
reloco::byte_reader attempt = in; // copy: same buffer, same position
if (parse_v2(attempt))
  in = attempt;                   // commit only on success
```

### C strings

```cpp
// Bytes up to the first NUL (excluded); the NUL is consumed.
// std::byte{0} is the delimiter. error::not_found if there is none.
auto name = in.try_read_until(std::byte{0});
```

## `byte_writer`

Writes into a fixed buffer; nothing grows. A write that does not fit fails with
`error::out_of_bounds` and writes nothing.

```cpp
reloco::array<std::byte, 64> storage{};

// Wrap the buffer; the writer starts at offset 0.
reloco::byte_writer out(reloco::span<std::byte>(storage.data(), storage.size()));

(void)out.try_write_be(std::uint16_t{1});   // u16 version, big endian
// Reserve 4 zeroed bytes for a length that is only known after the body.
// Returns the offset to patch later.
auto length_at = out.try_reserve(4);
(void)out.try_write_u8(0x2A);               // body...
(void)out.try_write_uleb128(300);           // ...a varint
// Patch the reserved bytes: <byte order>(offset, value). The position does
// not move. The bytes must already have been written (reserved).
(void)out.try_patch<byte_order::big>(*length_at, std::uint32_t{4});

// The bytes written so far, ready to send.
reloco::span<const std::byte> packet = out.written();
```

| Call | Effect |
|---|---|
| `try_write<Order>(v)`, `try_write_le(v)`, `try_write_be(v)`, `try_write_ne(v)`, `try_write_u8(v)` | Append an integer/enum |
| `try_write_bytes(span)` | Append a copy of the bytes |
| `try_fill(byte, n)`, `try_write_zeros(n)` | Append `n` identical bytes |
| `try_align(a)` | Zero-pad to a multiple of `a` (power of two) |
| `try_reserve(n)` | Append `n` zero bytes, return the starting offset |
| `try_patch<Order>(offset, v)` | Overwrite already-written bytes, position unchanged |
| `try_write_uleb128(v)` / `try_write_sleb128(v)` | LEB128 varints |
| `capacity()`, `position()`, `remaining()`, `written()`, `reset()` | Inspection / rewind to 0 (buffer not cleared) |

## Integration notes

* Get a byte span from other containers with `span::as_bytes()` /
  `as_writable_bytes()` (see `span.hpp`); `vector`, `array` and `string_view`
  convert to spans.
* Fixed layouts that are plain structs of integers can use
  `packed_header.hpp`; use the cursors when fields are variable-length,
  length-prefixed or order-dependent.
* The reader/writer are views (`RELOCO_POINTER`): do not outlive the buffer.
