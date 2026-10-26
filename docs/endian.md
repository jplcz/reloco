<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Byte order and integer encoding: `endian.hpp`

`include/reloco/endian.hpp`

Byte order tags and portable integer <-> byte conversions. Integers are
assembled from / split into single bytes with shifts: no `memcpy`, no type
punning, no alignment requirement, and the compilers emit plain loads/stores
(and `bswap`) at `-O1`+. Only integers (not `bool`) and enums are supported;
floating point is deliberately excluded (reloco does no floating point):
convert the bit pattern yourself.

For sequential parsing/serialization use [`byte_cursor.hpp`](byte-cursor.md),
which is built on these functions.

## API

| Name | Meaning |
|---|---|
| `byte_order::little`, `byte_order::big` | Byte order tag |
| `native_byte_order` | The host's order (compile-time constant) |
| `network_byte_order` | `byte_order::big` |
| `is_wire_integer_v<T>` | `true` for integers (except `bool`) and enums |
| `byteswap(v)` | Reverses the bytes of `v`; `constexpr` |
| `to_byte_order<Order>(v)` / `from_byte_order<Order>(v)` | Host <-> `Order`; a no-op when `Order == native_byte_order` |
| `try_load<T, Order>(bytes)` | Reads a `T` from the start of a byte span; `error::out_of_bounds` if too short |
| `try_store<Order>(bytes, v)` | Writes `v` to the start of a byte span; `error::out_of_bounds` (nothing written) if too short |
| `unsafe_load<T, Order>` / `unsafe_store<Order>` | Same without the always-on size check (debug assertion only); `RELOCO_UNSAFE_BUFFER_USAGE` |

## Examples

```cpp
#include <reloco/endian.hpp>

using reloco::byte_order;

// Constant-evaluable byte swap, e.g. for a magic number.
static_assert(reloco::byteswap<std::uint32_t>(0x01020304U) == 0x04030201U);

// Host value -> big endian representation that can be sent as-is on a
// little-endian machine (and is a no-op on a big-endian one).
std::uint32_t wire = reloco::to_byte_order<byte_order::big>(host_value);

void decode(reloco::span<const std::byte> packet) {
  // Template args: result type, byte order of the data in `packet`.
  // Returns result<uint32_t>; error::out_of_bounds when fewer than 4 bytes.
  auto length = reloco::try_load<std::uint32_t, byte_order::big>(packet);
  if (!length)
    return;
  // *length is already in host order.
}

void encode(reloco::span<std::byte> out) {
  // Template arg: byte order to write in. Writes 4 bytes, little endian.
  (void)reloco::try_store<byte_order::little>(out, std::uint32_t{0xDEADBEEF});
}
```
