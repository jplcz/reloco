# Changelog

## Unreleased

### Added

- `intrusive_ptr.hpp`: `intrusive_ptr<T>`, `intrusive_ref_counted<Derived, Counter>`, `atomic_refcount`,
  `local_refcount`, `intrusive_ptr_traits<T>`, `try_allocate_intrusive`/`try_create_intrusive`. See
  `docs/intrusive-ptr.md`.
- `byte_cursor.hpp`: `byte_reader` / `byte_writer`, bounds-checked sequential binary parser and serializer. See
  `docs/byte-cursor.md`.
- `endian.hpp`: `byte_order`, `byteswap`, `to_byte_order`/`from_byte_order`, `try_load`/`try_store`. See
  `docs/endian.md`.

## v0.4.5

### Breaking

- The iterator source function `reloco::once(value)` is now `reloco::once_item(value)`. It clashed with the
  `reloco::once` class (`once.hpp`) whenever both headers were included in one translation unit.

### Added

- `byte_like.hpp`: `is_byte_like_v<T>`.
- `span`: `cast_to<U>()`, `unsafe_cast_to<U>()`, `try_cast_to<U>()`, `as_chars()`, `as_uint8()`,
  `as_writable_bytes()`, `position()`, `position_if()`, `try_map_to()`.
- `string_view`: implicit conversion from `span<CharT>`, `from_bytes()`, `as_span()`, `as_bytes()`, `as_uint8()`.
- `string` family: `try_append_all`, `try_concat`, `try_allocate_concat`, `try_join`, `try_convert_to`.
- Vectors: `try_insert_span`, `try_erase_range`, `try_swap_remove`, `truncate`, `try_convert_to`, `try_split_off`.
- Deques: `try_append(span)` / `try_assign(span)` (reject spans aliasing the ring storage), `try_create_from` /
  `try_allocate_from`.
- `boxed_slice::try_from_vector` accepts any vector flavour; `boxed_slice::try_convert_to`.
- Sized and span-based factories: `try_create_sized`, `try_create_from`, `try_to_vector`, `try_to_string`.

### Fixed

- `vector::retain` / `dedup_by` did not compile when instantiated.

See `docs/fallible-construction.md` for the new construction and conversion helpers.
