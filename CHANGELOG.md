# Changelog

## Unreleased

### Added

- Rust-`str`-style `string_view` operations: lazy `split`/`rsplit`/`splitn`/`rsplitn`/`split_terminator`/`split_inclusive`/`split_whitespace`/`lines`, `split_once`/`split_at`, `contains`/`count`/`matches`/`match_indices`/`rfind*`/`find_*_of`, `trim_*_matches`, ASCII case-insensitive comparison, integer parsing (`try_parse`, `try_consume_int`, `try_parse_bool`), UTF-8 validation and iteration, and allocating `try_replace`/`try_to_ascii_*case`/`try_repeat`. Patterns are a character, a predicate or a string. See `docs/string-view-ops.md`.
- Rust-slice-style `span` operations: `split_first/last`, `split_off_*`, `chunks_exact`, `rchunks`, `split/splitn/rsplit`, `starts_with/ends_with`, `strip_prefix/suffix`, `partition_point`, binary search bounds, `try_find/try_max/try_min*`, `any/all/count`, `is_sorted*`, `select_nth_unstable`, `reverse`, `rotate_*`, `fill*`, `swap_*`, `copy_from/copy_within`, `partition`, `fold`, ASCII helpers. See `docs/span-slice-ops.md`.
- `reloco::tuple<Ts...>`: a `std::tuple` with Rust-like `map`/`zip`/`append`/`prepend`/`concat`/`for_each`/`first`/`last`, runtime `visit_at` returning `result<R>`, and hardened rvalue access. See `docs/tuple.md`.
- Owning linked containers `tailq`, `stailq`, `slist`, `list` and `circular_list` (one allocator-backed node per element,
  linking delegated to the `c_*` intrusive containers), plus `c_tailq::back()`, `c_stailq::back()`/`remove_after()`,
  `c_list_head::insert_after()`/`insert_before()` and `allocator_ref::operator==`. Includes `try_prepend`, `try_splice` and `try_splice_after` (same allocator required unless the
  destination is empty). See `docs/linked-containers.md`.
- `expected::unchecked_value()` / `unchecked_error()`: unasserted accessors for hot loops that test `has_value()` outside the loop (rvalues deleted).
- Kernel `block::for_each_sub<B2>`: split a proven block into narrower proven blocks. See `docs/kernel/index.md`.
- Kernel projections `permute`, `diagonal`, `try_reshape`, `try_broadcast` (`project/permute.hpp`, `project/reshape.hpp`). See `docs/kernel/projections.md`.
- Kernel `bound::try_blockwise<B>` and `block_span`: per-block tables (`n / B` entries) indexed by the blocks of a binding. See `docs/kernel/bind.md`.
- Kernel examples: `demos/kernel/example_kernels.hpp` (rescale, rotate_pairs, row_dots, moving_sum) with scalar twins and audit tests; guide `docs/kernel/examples.md`.
- `intrusive_ptr.hpp`: `intrusive_ptr<T>`, `intrusive_ref_counted<Derived, Counter>`, `atomic_refcount`,
  `local_refcount`, `intrusive_ptr_traits<T>`, `try_allocate_intrusive`/`try_create_intrusive`. See
  `docs/intrusive-ptr.md`.
- `byte_cursor.hpp`: `byte_reader` / `byte_writer`, bounds-checked sequential binary parser and serializer. See
  `docs/byte-cursor.md`.
- `endian.hpp`: `byte_order`, `byteswap`, `to_byte_order`/`from_byte_order`, `try_load`/`try_store`. See
  `docs/endian.md`.
- `kernel/` (C++20 and newer): bounds-proven loop domains (`proven_index`, `domain`, `blocks`/`tail`, `pairs`,
  `windows`, `strided`) and the opt-in `RELOCO_KERNEL_CHECKS` inner-loop verification levels, with per-level inline
  namespaces. See `docs/kernel/README.md`.
- `kernel/view/`: rank-N `tensor_view` with dynamic/static strides, `try_make_tensor` (overflow-checked, rejects
  aliasing mutable views), `try_make_contiguous_tensor`, `lane_traits`. See `docs/kernel/views.md`.
- `reloco::kernel` binding layer (C++20+): `try_bind`, `RELOCO_KERNEL_BIND`, `with_bound`, `bound`, `bound_span` and
  `allow_alias` prove once that several spans are long enough and do not alias, so hot loops index them without
  checks. Also `span::unchecked_subspan`. See `docs/kernel/bind.md`.
- `benchmarks/kernel/`, `scripts/check-kernel-codegen.sh` (ctest `KernelCodegen`) and the opt-in
  `JPLCZ_RELOCO_BUILD_BENCHMARKS`: assembly check and timings showing the bound loop matches the unchecked one. See
  `docs/kernel/benchmarks.md`.
- `kernel/algo/`: core algorithms with one-time length and aliasing checks: `fill`, `try_copy`, `try_transform`,
  `transform_in_place`, `try_zip_transform`, `reduce`, `try_sum`, `try_dot`, `try_min`/`try_max`,
  `try_argmin`/`try_argmax`, `try_inclusive_scan`/`try_exclusive_scan`. Each has a fully checked scalar twin in `kernel::reference` for audits. See
  `docs/kernel/algorithms.md`.
- `kernel/project/`: zero-allocation `try_slice`, `try_index`, `try_row`, `try_column`, `try_tile` and `transpose` of a
  `tensor_view`. See `docs/kernel/projections.md`.
- `span::unchecked_at` / `array::unchecked_at`: element access with no check in any build mode, for loops whose
  index range was proven once. See `docs/hardened-containers.md`.

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
