<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# API reference

Quick, per-type reference for every public reloco header. Start with a
[guide](../README.md#guides) for the concepts behind these types (fallible
construction, the checked/`try_*`/`unsafe_*` tri-tier convention, lifetime
annotations, trivial relocation); this page is a map of what exists and
where, not a tutorial.

| Header | Type(s) | One-line summary |
|---|---|---|
| `expected.hpp` | `expected<T, E>`, `unexpected<E>` | Allocation-free value-or-error result |
| `error.hpp` | `error`, `result<T>` | The one error enum every fallible reloco operation returns, and its `expected<T, error>` alias |
| `error_std.hpp` | `error_category()`, `make_error_code(error)`, `make_error_condition(error)` | Opt-in `<system_error>` binding: makes `reloco::error` convert to `std::error_code`/`std::error_condition` |
| `span.hpp` | `span<T>` | Non-owning, checked view over a contiguous range |
| `array.hpp` | `array<T, N>` | Fixed-size owning array with hardened element access |
| `string_view.hpp` | `basic_string_view<CharT, TraitsT>` (`string_view`, `wstring_view`) | Non-owning, checked view over character data |
| `string.hpp` | `basic_string<CharT, TraitsT>` (`string`, `wstring`) | Move-only, allocator-backed, growable character buffer |
| `inline_string.hpp` | `basic_inline_string<Capacity, CharT, TraitsT>` | Fixed-capacity, trivially-copyable, allocation-free character buffer |
| `vector.hpp` | `vector<T>` | Move-only, allocator-backed, growable dynamic array |
| `flat_set.hpp` | `flat_set<T, Compare>` | Sorted, unique-element set backed by `vector<T>`, with fallible insertion |
| `flat_map.hpp` | `flat_map<Key, Mapped, Compare>` | Sorted, unique-key map backed by `vector<std::pair<Key, Mapped>>`, with fallible insertion |
| `inline_flat_set.hpp` | `inline_flat_set<T, Capacity, Compare>` | Allocation-free counterpart of `flat_set<T, Compare>`, backed by `inline_vector<T, Capacity>` |
| `inline_flat_map.hpp` | `inline_flat_map<Key, Mapped, Capacity, Compare>` | Allocation-free counterpart of `flat_map<Key, Mapped, Compare>`, backed by `inline_vector<std::pair<Key, Mapped>, Capacity>` |
| `tree_set.hpp` | `tree_set<T, Compare>` | Sorted, unique-element set backed by an allocator-managed, unbalanced binary search tree, with stable references and fallible insertion |
| `tree_map.hpp` | `tree_map<Key, Mapped, Compare>` | Sorted, unique-key map backed by an allocator-managed, unbalanced binary search tree over `(Key, Mapped)` pairs, with stable references and fallible insertion |
| `flat_hash_set.hpp` | `flat_hash_set<T, Hash, KeyEqual>` | Unordered, unique-element set backed by an open-addressing `vector<optional<T>>` hash table, with fallible insertion |
| `flat_hash_map.hpp` | `flat_hash_map<Key, Mapped, Hash, KeyEqual>` | Unordered, unique-key map backed by an open-addressing `vector<optional<std::pair<Key, Mapped>>>` hash table, with fallible insertion |
| `optional.hpp` | `optional<T>`, `nullopt_t`, `nullopt` | Zero-allocation, conditionally-present value wrapper with tri-tier access |
| `function_ref.hpp` | `function_ref<R(Args...)>` | Non-owning, zero-allocation borrow of any callable |
| `inplace_function.hpp` | `inplace_function<Signature, Capacity>` | Zero-allocation, fixed-capacity callable wrapper |
| `stack_allocator.hpp` | `stack_allocator`, `stack_allocator_tag`, `stack_allocator_context` | Bump-pointer `allocator_traits` backend over a caller-owned buffer |
| `unique_ptr.hpp` | `unique_ptr<T>` | Move-only, allocator-backed smart pointer with fallible construction |
| `shared_ptr.hpp` | `shared_ptr<T>`, `weak_ptr<T>`, `enable_shared_from_this<T>` | Reference-counted, allocator-backed smart pointer with fallible construction |
| `rc.hpp` | `rc<T>`, `weak_rc<T>`, `enable_rc_from_this<T>` | Single-threaded (non-atomic) reference-counted smart pointer, matching Rust's `Rc<T>`/`Weak<T>` |
| `bytes.hpp` | `bytes`, `bytes_mut` | Immutable, reference-counted, cheaply-cloneable byte buffer and its growable, exclusively-owned mutable counterpart, matching Rust's `bytes::Bytes`/`bytes::BytesMut` |
| `binary_heap.hpp` | `binary_heap<T, Compare>` | Allocator-backed priority queue matching Rust's `BinaryHeap<T>`, built on `vector<T>` |
| `boxed_slice.hpp` | `boxed_slice<T>` | Fixed-size, allocator-backed owned array with no spare capacity, matching Rust's `Box<[T]>` |
| `cow.hpp` | `cow<T>`, `cow_traits<T>` | Clone-on-write wrapper matching Rust's `Cow<'a, T>`, with a user-specializable clone customization point |
| `function.hpp` | `function<R(Args...)>` | Type-erased, allocator-backed callable wrapper with fallible construction |
| `type_id.hpp` | `type_id`, `type_id_of<T>()`, `RELOCO_TYPE_ID_NAME` | Process-wide type identity established without RTTI, matching Rust's `std::any::TypeId`, with optional debug names |
| `any.hpp` | `any` | Type-erased, allocator-backed single-value container with fallible construction and no RTTI dependency |
| `collection_view.hpp` | `collection_view<T>`, `mutable_collection_view<T>`, `collection_view_traits<Container>` | Type-erased, non-owning views over an adapted sequence container |
| `container_ref.hpp` | `mutable_container_ref<T, Key = void>`, `container_ref_traits<Container>` | Type-erased handle for structurally mutating (growing/inserting/erasing) an adapted sequence or associative container |
| `container_ref_std.hpp` | `container_ref_traits<std::vector<T>>`, `container_ref_traits<std::map<Key, Value>>` | Opt-in `container_ref_traits` adapters for `std::vector`/`std::map` |
| `value_ptr.hpp` | `value_ptr<T>` | Nullable, non-owning pointer that rejects binding to prvalue temporaries |
| `value_ref.hpp` | `value_ref<T>` | Non-null, non-owning reference wrapper that rejects binding to prvalue temporaries |
| `checked_value.hpp` | `checked_value<T>` | Move-only wrapper with Rust-like use-after-move checks |
| `cell.hpp` | `cell<T>`, `ref_cell<T>` | Interior-mutability wrappers matching Rust's `Cell<T>`/`RefCell<T>` |
| `non_zero.hpp` | `non_zero<T>` | Integral wrapper statically known to never be `0`, matching Rust's `NonZero*` family |
| `wrapping.hpp` | `wrapping<T>` | Integral newtype whose arithmetic operators always wrap on overflow, matching Rust's `std::num::Wrapping<T>` |
| `saturating.hpp` | `saturating<T>` | Integral newtype whose arithmetic operators always clamp on overflow, matching Rust's `std::num::Saturating<T>` |
| `checked.hpp` | `checked<T>` | Integral newtype whose arithmetic is always explicitly fallible via `try_add/sub/mul/div/rem/neg/abs` returning `result<checked<T>>` |
| `int_ops.hpp` | `checked_add/sub/mul/div/rem/neg/abs`, `wrapping_add/sub/mul`, `saturating_add/sub/mul`, `overflowing_add/sub/mul`, `overflowing_result<T>`, `checked_cast<To>` | Free-function Rust-style checked/wrapping/saturating/overflowing integer arithmetic and range-checked numeric casts |
| `allocator.hpp` | `allocator_ref`, `allocator<Tag>`, `allocator_traits<Tag>`, `mem_block`, `usage_hint` | Type-erased allocator handle and the tag-based provider pattern backing it |
| `heap_allocator.hpp` | `heap_allocator_tag` | Stateless `allocator_traits` backend over the process heap (`new`/`delete`) |
| `default_allocator.hpp` | `default_allocator()`, `reloco_global_alloc` | Process-wide default allocator, overridable like Rust's `#[global_allocator]` |
| `concepts.hpp` | `has_try_create_v`, `has_try_allocate_v`, `has_try_construct_v`, `has_try_clone_v`, `has_try_clone_at_v` (+ C++20 concepts) | Detection traits for the fallible-construction protocol |
| `construction_helpers.hpp` | `construction_helpers` | Compile-time dispatcher picking the best construction/clone strategy for a type |
| `relocatable.hpp` | `is_trivially_relocatable<T>` (+ C++20 `trivially_relocatable`) | Marks types safely movable by copying bytes and abandoning the source |
| `send_sync.hpp` | `is_send<T>`, `is_sync<T>` (+ C++20 `sendable`/`syncable`) | Marks types sound to transfer to another thread (`is_send`) or share concurrently (`is_sync`), matching Rust's `Send`/`Sync` |
| `thread.hpp` | `thread`, `thread_id`, `this_thread::get_id/yield`, `spawn`, `join_handle<R>` | Backend-selected (`pthread`/`std`/custom) OS thread primitive plus a Rust-like `spawn`/`JoinHandle<T>` layer built on `is_send`/`is_sync` |
| `channel.hpp` | `channel<T>`, `sender<T>`, `receiver<T>` | Multi-producer, single-consumer channel matching Rust's `std::sync::mpsc`, built on `mutex.hpp` + `shared_ptr` + `is_send`/`is_sync` |
| `once_lock.hpp` | `once_lock<T>` | Write-once, read-many-times cell matching Rust's `std::sync::OnceLock<T>`, usable as a plain field/local (unlike `fallible_singleton.hpp`'s static, one-per-`T` global) |
| `scope.hpp` | `scope`, `thread_scope`, `scoped_join_handle<R>` | Matches Rust's `std::thread::scope`: spawns threads guaranteed to finish before `scope()` returns, so they may safely borrow references to the caller's stack frame |
| `lifetime.hpp` | `RELOCO_LIFETIMEBOUND`, `RELOCO_OWNER`, `RELOCO_POINTER`, `RELOCO_UNSAFE_BUFFER_USAGE`, ... | Compiler-specific lifetime/ownership/safe-buffers annotation macros |
| `rvalue_safety.hpp` | `RELOCO_BLOCK_RVALUE_ACCESS` | Deletes rvalue accessors that would otherwise dangle past a temporary |
| `reloco_config.hpp` | (user override header hook) | How to override library-wide defaults from `reloco_user_config.hpp` |

## `expected<T, E>` / `result<T>`

`include/reloco/expected.hpp`, `include/reloco/error.hpp`

A `std::expected`-like, allocation-free value-or-error type, usable in
C++17 (no dependency on the standard library's own `<expected>`).
`reloco::result<T>` is `reloco::expected<T, reloco::error>` — the one error
type every fallible reloco operation returns (see
[Fallible construction](fallible-construction.md)).

```cpp
reloco::result<int> parse(std::string_view s) noexcept {
  if (s.empty())
    return reloco::unexpected(reloco::error::invalid_argument);
  return 42;
}
```

## `error`

`include/reloco/error.hpp`

reloco has exactly **one** error type in the entire library: `reloco::error`,
a plain `enum class`. Every fallible operation anywhere in reloco -- every
`try_*` container mutator, every allocator call, `weak_ptr::lock()`, the
fallible-construction protocol (`concepts.hpp`) -- returns
`reloco::result<T>` (`expected<T, error>`) or `result<void>`. No type
defines its own scoped error enum, and `concepts.hpp`'s detection traits
only recognize a `try_*` member that itself returns `reloco::result<...>`,
so this is enforced rather than just a convention.

| Member | Meaning |
|---|---|
| `allocation_failed` | The allocator failed to provide/grow/shrink a memory block. |
| `in_place_growth_failed` | `allocator_ref::expand_in_place` could not grow a block without moving it. |
| `unsupported_operation` | The operation is not supported by this concrete type/backend (e.g. a `function_ref`/`function` invoked with the wrong signature, an allocator tag that doesn't implement an optional operation). |
| `out_of_range` | A value fell outside the range required by the operation (a parsed number, a duration, a size argument) -- distinct from `out_of_bounds`, which is specifically about container indices/iterators. |
| `invalid_argument` | An argument failed a precondition check unrelated to range/bounds (a malformed string, a null callback, mismatched key/value in `flat_set`). |
| `already_exists` | Insertion failed because an equivalent key/element is already present (`flat_set::try_insert`, `flat_map::try_insert`). |
| `empty_pointer` | A smart pointer (`unique_ptr`, `shared_ptr`, `value_ptr`, ...) was empty when a non-empty one was required. |
| `pointer_expired` | A `weak_ptr::lock()` failed because the last owning `shared_ptr` has already released the object. |
| `no_owner` | An operation requiring an owning handle was attempted on a non-owning one. |
| `out_of_bounds` | A container index/iterator fell outside `[0, size())` (see `out_of_range` for non-index range checks). |
| `deadlock` | A locking operation detected it would deadlock (e.g. recursive non-recursive lock acquisition) and failed instead of blocking forever. |
| `invalid_owner` | An operation was attempted by a thread/handle that does not own the resource it is trying to operate on (e.g. unlocking a mutex you don't hold). |
| `still_locked` | An operation requiring an unlocked resource found it still locked (e.g. destroying/reclaiming a lock that is still held). |
| `not_locked` | An operation requiring a locked resource (unlocking, asserting exclusive access) found it was not locked. |
| `timed_out` | A bounded-wait operation (a timed lock acquisition) did not complete within its deadline. |
| `try_again` | The operation could not complete right now for a transient reason and may succeed if retried (contended non-blocking lock acquisition). |
| `not_initialized` | The object/subsystem was used before the initialization step its protocol requires (a two-phase `try_construct` shell that was never followed through, see `concepts.hpp`). |
| `container_empty` | An operation requiring at least one element (`front`/`back`/`pop_back`) was called on an empty container. |
| `not_found` | A lookup (`flat_set::try_find`, `flat_map::try_find`, ...) found no matching key/element. |
| `integer_overflow` | An arithmetic computation (a size/capacity calculation) would overflow its integer type. |
| `division_by_zero` | A division or remainder operation was attempted with a zero divisor (`checked_div`/`checked_rem`, see `int_ops.hpp`). |
| `capacity_exceeded` | A fixed-capacity container (`inline_vector`, `inline_flat_set`, `inline_flat_map`, `inplace_function`, ...) has no room left for another element and, unlike a heap-backed container, cannot grow. |
| `invalid_state` | The operation is not valid given the object's current state (a moved-from object, or an operation attempted in the wrong phase of a multi-step protocol). |
| `permission_denied` | An OS- or allocator-level access-control check failed (e.g. `mmap` with insufficient permissions). |
| `interrupted` | The underlying operation was interrupted (e.g. by a signal) and may be safely retried. |
| `resource_exhausted` | A system-imposed resource limit unrelated to heap memory (a handle/descriptor count, a thread count) was reached. |
| `busy` | The resource is currently in use by someone else and the operation could not proceed non-blockingly; distinct from `still_locked`/`not_locked` (lock state specifically) and `try_again` (any transient retryable failure). |
| `io_error` | A lower-level I/O operation (e.g. one performed by an allocator backend) failed for a reason not otherwise covered by a more specific member. |
| `operation_canceled` | The operation was explicitly canceled before it could complete. |

Several members (`no_owner`/`invalid_owner`, `deadlock`/`still_locked`/
`not_locked`/`timed_out`, `not_initialized`, `busy`/`interrupted`/
`io_error`/`operation_canceled`, ...) are not yet returned by any type in
this header-only core today; they exist so that locking primitives,
initialization protocols, and OS-backed allocators added later reuse the
same single enum instead of introducing their own.

### `<system_error>` interop (`error_std.hpp`)

`include/reloco/error_std.hpp` (opt-in; not included by `error.hpp` or any
other reloco header) makes `reloco::error` satisfy
`std::is_error_code_enum`, so it implicitly converts to `std::error_code`:

```cpp
#include <reloco/error_std.hpp>

std::error_code ec = reloco::error::not_found;      // implicit conversion
throw std::system_error(reloco::error::deadlock);   // ADL-found make_error_code
```

`reloco::error_category()` returns the singleton `std::error_category`
(`name() == "reloco"`) backing that conversion; its `message(int)` mirrors
the one-line descriptions in the table above. `reloco::error` also
satisfies `std::is_error_condition_enum` (via `make_error_condition`), so
it converts to `std::error_condition` too, and a `reloco::error`-based
`std::error_code` compares equal to `std::error_condition(reloco::error::x)`
directly:

```cpp
std::error_code ec = reloco::error::busy;
assert(ec == std::error_condition(reloco::error::busy));
```

`error_category_impl::equivalent()` (used for the `error_code ==
error_condition` comparison above) compares by `name()` string content
(`"reloco"`) instead of category-object identity, so the comparison still
works even if the category singleton ended up duplicated across a
shared-library boundary.

`reloco::error` is also plugged into the standard POSIX/`errno` bridge:
`error_category_impl::default_error_condition(int)` maps several members
(`allocation_failed`, `invalid_argument`, `out_of_range`, `out_of_bounds`,
`already_exists`, `deadlock`, `timed_out`, `try_again`,
`unsupported_operation`, `capacity_exceeded`, `permission_denied`,
`interrupted`, `busy`, `io_error`, `operation_canceled`,
`integer_overflow`, `division_by_zero`) onto the closest matching `std::errc` value, so a
`reloco::error`-based `std::error_code` compares equal to that generic
condition *and* to any other category's code that reports the same
`errno`-derived condition (e.g. one built from `errno` via
`std::generic_category()`):

```cpp
std::error_code ec = reloco::error::timed_out;
assert(ec == std::errc::timed_out);

std::error_code errno_ec(ETIMEDOUT, std::generic_category());
assert(ec == errno_ec);
```

Members with no sufficiently precise POSIX equivalent (e.g. `not_found`,
`pointer_expired`) keep the default identity condition and only compare
equal to themselves. This does not change
how reloco itself reports errors -- every `try_*` operation still returns
`reloco::result<T>` -- it is purely a bridge for code that also needs to
hand a `reloco::error` to, or compare it against, `std::error_code`/
`std::error_condition`-based APIs.

## `span<T>`

`include/reloco/span.hpp`

Non-owning view over a contiguous range of `T`, with the checked/`try_*`/
`unsafe_*` tri-tier convention on every element/subrange accessor
(`operator[]`/`at()`, `try_at()`/`try_first()`/`try_last()`/`try_subspan()`,
`unsafe_at()`/...). See [Hardened containers](hardened-containers.md).

Rust `slice`-inspired algorithms operate directly on the viewed buffer (no
copy): `contains(value)` does a linear scan; `sort()`/`sort_by(Compare)`
sort in place with `std::stable_sort` (preserving relative order of equal
elements); `sort_unstable()` uses `std::sort` when stability isn't needed;
`binary_search(value)`/`binary_search_by(value, less)` assume the span is
already sorted and return an `optional<std::size_t>` index of a matching
element (empty if none is found).

```cpp
int values[] = {5, 3, 1, 4, 2};
reloco::span<int> view(values);
view.sort();
assert(view.contains(3));
assert(*view.binary_search(3) == 2);
```

## `array<T, N>`

`include/reloco/array.hpp`

Fixed-size, stack- or member-embeddable owning array. Same tri-tier element
access as `span`, plus `as_span()` to hand out a borrowed, checked view
without exposing the underlying storage directly. Storage alignment is
`alignment_of<T>`-controlled (see [Over-alignment](alignment.md)) for
SIMD-friendly over-alignment.

## `basic_string_view<CharT, TraitsT>` (`string_view`, `wstring_view`)

`include/reloco/string_view.hpp`

Non-owning, checked view over character data — `std::string_view`, plus the
tri-tier convention (`front()`/`try_front()`/`unsafe_front()`, etc.),
rejection of dangling prvalue `std::basic_string` temporaries at the
constructor, and interop with both `std::basic_string_view` and
`std::basic_string`. Storage is a self-contained pointer/size pair (not a
wrapped `std::basic_string_view`); comparisons and prefix/suffix checks are
implemented directly against `TraitsT`, and only substring/character-class
search delegates to `std::search`/`std::find_first_of`.

## `basic_string<CharT, TraitsT>` (`string`, `wstring`)

`include/reloco/string.hpp`

Move-only, allocator-backed, growable character buffer — the fallible,
allocator-explicit analogue of `std::string`. No small-string optimization:
every non-empty instance holds exactly one heap allocation, obtained and
released through a bound `reloco::allocator_ref`.

```cpp
auto s = reloco::string::try_create(reloco::string_view("hello"));
if (!s)
  return; // s.error() is a reloco::error.
auto ok = s->try_append(reloco::string_view(" world"));
assert(s->view() == "hello world");
```

Construction and cloning:

| Function | Behavior |
|---|---|
| `try_create(view_type sv = {})` | Builds from `sv` using `default_allocator()` |
| `try_allocate(allocator_ref alloc, view_type sv = {})` | Builds from `sv` using an explicit allocator |
| `try_clone(allocator_ref alloc)` / `try_clone()` | Fallible deep copy, explicit or own allocator |
| `try_clone_at(allocator_ref alloc, basic_string *storage, const basic_string &source)` | Fallible deep copy directly into uninitialized storage |

Because it implements this protocol itself (see
[Fallible construction](fallible-construction.md)), `basic_string` composes
with anything built on `has_try_create_v`/`construction_helpers`:
`reloco::unique_ptr<reloco::string>::try_create(...)` just works.

Mutation: `try_reserve`, `shrink_to_fit`, `try_assign`, `try_append`,
`try_push_back`, `pop_back`/`try_pop_back`, `try_insert`,
`erase`/`try_erase`, `try_resize`, `clear`. Every fallible one returns
`reloco::result<void>`.

Element access follows the same checked/`try_*`/`unsafe_*` tri-tier
convention as `string_view`/`span`/`array`: `operator[]`/`at()`/`front()`/
`back()` assert; `try_at()`/`try_front()`/`try_back()` return
`reloco::result<...>`; `unsafe_at()`/`unsafe_front()`/`unsafe_back()`/
`unsafe_c_str()` are `RELOCO_UNSAFE_BUFFER_USAGE`-gated escape hatches.
`data()` is always safe (points at a shared static null character when
empty, never `nullptr`). `view()` and implicit conversions to
`reloco::string_view`/`std::string_view` provide borrowed access; explicit
`operator std::basic_string<CharT, TraitsT>()` converts to an owning
standard-library string when one is actually needed at a boundary.

`reloco::is_trivially_relocatable<basic_string<CharT, TraitsT>>` is always
`true` (see [Trivial relocation](relocatable.md)).

## `basic_sso_string<CharT, TraitsT>` (`sso_string`, `wsso_string`)

`include/reloco/sso_string.hpp`

Move-only, allocator-backed, growable character buffer with a **small-string
optimization (SSO)**: strings of at most `sso_capacity` characters (15 by
default) live entirely inline inside the object and never allocate; growing
past that inline capacity transparently falls back to a heap allocation,
exactly like `basic_string`. `shrink_to_fit()` moves a string that has
shrunk back down to `sso_capacity` or fewer characters back into the inline
buffer, releasing the heap allocation.

`basic_sso_string` implements the exact same API as `basic_string` --
`try_create`/`try_allocate`/`try_clone`/`try_clone_at`, the same fallible
mutators (`try_reserve`, `shrink_to_fit`, `try_assign`, `try_append`,
`try_push_back`, `pop_back`/`try_pop_back`, `try_insert`, `erase`/
`try_erase`, `try_resize`, `clear`), and the same checked/`try_*`/`unsafe_*`
element-access tiers. Additionally, `is_inline()` reports whether the
current content lives inline or on the heap, and `sso_capacity` is a
`static constexpr` member exposing the compile-time inline limit.

```cpp
auto s = reloco::sso_string::try_create(reloco::string_view("hello"));
if (!s)
  return; // s.error() is a reloco::error.
assert(s->is_inline()); // "hello" fits inline, no allocation happened.
```

The inline capacity is a single process-wide compile-time constant, not a
template parameter: define `RELOCO_SSO_STRING_CAPACITY` (via a compiler
`-D` flag or `reloco_user_config.hpp`, see
[`reloco_config.hpp`](#reloco_confighpp)) to override the default of 15
characters.

Because the inline buffer is self-referencing while a `basic_sso_string` is
small (its `data()` can point inside the object itself),
`reloco::is_trivially_relocatable<basic_sso_string<CharT, TraitsT>>` is
always `false` -- unlike `basic_string`. Containers that branch on
`is_trivially_relocatable` (`vector`, `inline_vector`, `flat_set`,
`flat_map`, ...) always use their safe, per-element move path for
`basic_sso_string` elements, never `memcpy`.

Prefer `basic_sso_string` over `basic_string` when most expected values are
short and avoiding an allocation for them matters more than keeping the
type trivially relocatable; prefer `basic_string` when relocatability
matters (e.g. as the element type of a frequently grown/erased
`vector`/`flat_set`) or values are typically longer than the inline
capacity.

## `vector<T>`

`include/reloco/vector.hpp`

Move-only, allocator-backed, growable dynamic array — the fallible,
allocator-explicit analogue of `std::vector`. Every instance holds at most
one heap allocation, obtained and released through a bound
`reloco::allocator_ref`.

```cpp
auto v = reloco::vector<int>::try_create();
if (!v)
  return; // v.error() is a reloco::error.
auto ok = v->try_push_back(1);
ok = v->try_insert_at(0, 0);
assert((*v)[0] == 0 && (*v)[1] == 1);
```

Construction and cloning:

| Function | Behavior |
|---|---|
| `try_create(size_type initial_cap = 0)` | Builds, optionally reserving capacity, using `default_allocator()` |
| `try_allocate(allocator_ref alloc, size_type initial_cap = 0)` | Builds, optionally reserving capacity, using an explicit allocator |
| `try_clone(allocator_ref alloc)` / `try_clone()` | Fallible deep copy, explicit or own allocator |
| `try_clone_at(allocator_ref alloc, vector *storage, const vector &source)` | Fallible deep copy directly into uninitialized storage |

Because it implements this protocol itself (see
[Fallible construction](fallible-construction.md)), `vector<T>` composes
with anything built on `has_try_create_v`/`construction_helpers`:
`reloco::unique_ptr<reloco::vector<int>>::try_create(...)` just works.
Element construction (`try_emplace_back`/`try_insert_at`) and cloning both
delegate to `construction_helpers`, so element types that implement their
own fallible-construction protocol compose transparently; trivially
copyable element types with no custom `try_clone` take a single-`memcpy`
fast path when cloning.

Mutation: `try_reserve`, `shrink_to_fit`, `try_emplace_back`, `try_push_back`,
`try_pop_back`, `try_resize`, `try_insert_at`, `try_erase_at`, `clear`. Every
fallible one returns `reloco::result<...>`. Growth prefers
`allocator_ref::expand_in_place` first; when that fails it either
byte-relocates the whole buffer in one `reallocate` call (when
`is_trivially_relocatable_v<T>`) or falls back to move-constructing each
element into a freshly allocated block.

`try_resize(count)` / `try_resize(count, value)` grow or shrink the vector
to exactly `count` elements, reserving storage first if growing. Shrinking
destroys the trailing elements; growing default-constructs (`try_resize`)
or copy-constructs `value` into (`try_resize(count, value)`) each new slot.
`try_resize(count)` requires `T` to be default-constructible (a
`static_assert`); use the `value`-taking overload otherwise. When `T` is
trivially default-constructible/trivially copyable, the newly added range
is bulk zero-filled with a single `std::memset` or filled via a plain
assignment loop, instead of dispatching `construction_helpers::try_construct`
per element.

Element access follows the same checked/`try_*`/`unsafe_*` tri-tier
convention as `span`/`array`/`string`: `operator[]`/`front()`/`back()`
assert (there is no separately named `at()` — `operator[]` itself is the
checked tier); `try_at()`/`try_front()`/`try_back()` return
`reloco::result<...>`; `unsafe_at()`/`unsafe_data()` are
`RELOCO_UNSAFE_BUFFER_USAGE`-gated escape hatches. `data()`/`front()`/
`back()` assert the vector is non-empty; `try_data()`/`try_front()`/
`try_back()` fail with `error::container_empty` instead.

`reloco::is_trivially_relocatable<vector<T>>` is always `true` regardless of
`T` (see [Trivial relocation](relocatable.md)): the vector's own handle is
just an `allocator_ref` plus a pointer and two sizes, with no
self-reference into its own storage.

Every allocation requests `alignment_of<T>`-controlled alignment (see
[Over-alignment](alignment.md)) rather than plain `alignof(T)`, and
`data()`/`unsafe_data()`/`begin()` carry a matching `RELOCO_ASSUME_ALIGNED`
hint for the optimizer.

## `inline_vector<T, Capacity>`

`include/reloco/inline_vector.hpp`

Move-only, fixed-capacity, allocator-free growable array — `vector<T>`'s
counterpart for when the maximum element count is known at compile time
and heap allocation must be avoided entirely. Elements live directly
inside the object, in a raw `alignas(reloco::effective_alignment_v<T>)
std::byte` buffer sized for exactly `Capacity` elements (see
[Over-alignment](alignment.md)); unlike `array<T, N>`, `T` need not be
default-constructible, and `size()` is tracked independently of
`capacity()`. `Capacity` must be greater than zero — there is no
zero-capacity specialization.

```cpp
reloco::inline_vector<int, 4> v;
auto ok = v.try_push_back(1);
ok = v.try_insert_at(0, 0);
assert(v[0] == 0 && v[1] == 1);
```

Mutation mirrors `vector<T>` (`try_emplace_back`, `try_push_back`,
`try_pop_back`, `try_resize`, `try_insert_at`, `try_erase_at`, `clear`),
except there is no growth to fall back on: `try_emplace_back`/
`try_insert_at`/`try_resize` fail with `error::capacity_exceeded` once
`size() == capacity()` (respectively `count > capacity()`), instead of
allocating more space. Element construction and cloning delegate to
`construction_helpers` exactly like `vector<T>`, so nested element types
that implement their own fallible-construction protocol compose
transparently.

Cloning follows the same dual-tier shape as `vector<T>`:
`try_clone(allocator_ref alloc)` / `try_clone()` / `try_clone_at(...)`.
`try_clone(alloc)` forwards the given allocator to nested fallible
elements even though `inline_vector` itself never allocates; trivially
copyable element types with no custom `try_clone` take a single-`memcpy`
fast path.

Element access follows the same checked/`try_*`/`unsafe_*` tri-tier
convention as `vector`/`span`/`array`.

`inline_vector<T, Capacity>` can be "upgraded" to a heap-backed `vector<T>`
via `try_to_vector`, for callers that reach the fixed `Capacity` but need
to keep growing:

| Function | Behavior |
|---|---|
| `try_to_vector(allocator_ref alloc) const &` / `try_to_vector() const &` | Clones every element into a new `vector<T>`, leaving `*this` untouched |
| `try_to_vector(allocator_ref alloc) &&` / `try_to_vector() &&` | Moves every element out into a new `vector<T>`, consuming `*this` (left empty either way) |

`reloco::is_trivially_relocatable<inline_vector<T, Capacity>>` is
conditional on `is_trivially_relocatable_v<T>` (see
[Trivial relocation](relocatable.md)): unlike `vector<T>`'s heap pointer,
`inline_vector`'s storage is embedded directly in the object, so relocating
it via `memcpy` is only safe when every contained `T` is too.

## `sso_vector<T, InlineCapacity>`

`include/reloco/sso_vector.hpp`

Move-only, allocator-backed, growable dynamic array with a small-size
optimization (SSO): up to `InlineCapacity` elements live directly inside
the object, in the same kind of raw `alignas(reloco::effective_alignment_v<T>)
std::byte` buffer `inline_vector<T, Capacity>` uses; growing past
`InlineCapacity` transparently promotes to a heap allocation obtained
through a bound `reloco::allocator_ref`, exactly like `vector<T>`, and
`shrink_to_fit` demotes back to the inline buffer once `size()` fits within
`InlineCapacity` again. It sits between `vector<T>` and `inline_vector<T,
Capacity>`: unlike `inline_vector`, there is no hard capacity ceiling --
`try_push_back`/`try_emplace_back`/`try_insert_at` never fail with
`error::capacity_exceeded`, they simply grow onto the heap instead.
`InlineCapacity` must be greater than zero, and is a required template
parameter (there is no default), since a sensible inline capacity depends
on `T`.

```cpp
reloco::sso_vector<int, 4> v; // no allocation yet
auto ok = v.try_push_back(1); // still inline
ok = v.try_push_back(2);
assert(v.is_inline());
```

Construction, cloning, mutation, and element access all follow the same
protocol/shape as `vector<T>` (`try_allocate`/`try_create`/
`try_clone`/`try_clone_at`, `try_reserve`/`shrink_to_fit`,
`try_emplace_back`/`try_push_back`/`try_pop_back`/`try_resize`/
`try_insert_at`/`try_erase_at`/`clear`, checked/`try_*`/`unsafe_*` tri-tier
element access) -- see [`vector<T>`](#vectort) above for the full
breakdown. The one addition is `is_inline()`, which reports whether
`*this` currently holds its elements in the embedded buffer rather than a
heap allocation.

Because `try_reserve` has no existing heap allocation to
`expand_in_place`/`reallocate` the first time it promotes from inline to
heap, that particular growth step always allocates fresh and
move/`memcpy`'s the (at most `InlineCapacity`) inline elements across, the
same as `inline_vector`'s relocation logic; once heap-backed, growth
behaves exactly like `vector<T>`.

`reloco::is_trivially_relocatable<sso_vector<T, InlineCapacity>>` is always
`false`, regardless of `T` (see [Trivial relocation](relocatable.md)): a
small instance's internal pointer points into its own embedded buffer, so
relocating the object via `memcpy` would leave that pointer dangling into
the old location -- the same rationale `basic_sso_string` (see
[`basic_sso_string`](#basic_sso_stringcchart-traitst-sso_string-wsso_string))
documents for its own specialization.

## `outline_vector<T>`

`include/reloco/outline_vector.hpp`

Non-owning, growable-up-to-capacity dynamic array over a caller-supplied
byte buffer -- `vector<T>`'s non-owning counterpart, for backing bytes
`reloco` doesn't control (a memory-mapped region, a hardware DMA buffer, an
arena slab the caller manages directly, ...). Unlike every other reloco
vector flavor, `outline_vector<T>` is bound to a `span<std::byte>` exactly
once, at construction, and is afterwards **immovable and non-copyable**:
there is no default constructor, no rebind/reset method, and no move
constructor or move assignment operator either, since there is no
well-defined way to "steal" a borrowed span out from under whoever actually
owns it.

```cpp
alignas(reloco::effective_alignment_v<int>) std::byte buffer[sizeof(int) * 4];
reloco::outline_vector<int> v(reloco::span<std::byte>(buffer));
auto ok = v.try_push_back(1);
ok = v.try_insert_at(0, 0);
assert(v[0] == 0 && v[1] == 1);
// buffer itself is still the caller's to reuse once `v` is destroyed.
```

The bound span's capacity (`storage.size() / sizeof(T)`, computed once at
construction) is fixed for the object's entire lifetime:
`try_reserve`/`try_emplace_back`/`try_push_back`/`try_insert_at`/
`try_resize` fail with `error::capacity_exceeded` once `size() ==
capacity()` (respectively `count > capacity()`), exactly like
`inline_vector<T, Capacity>` -- there is no allocator to grow into.
`storage.data()` must already satisfy `effective_alignment_v<T>`; this is
checked with `RELOCO_ASSERT` (active even when `NDEBUG` is defined) at
construction time, since -- unlike `allocator_ref::allocate` -- there is no
allocator here to request a specific alignment from. Element construction
delegates to `construction_helpers` exactly like `vector<T>`/
`inline_vector<T, Capacity>`, passing `default_allocator()` for any nested
`T` that implements its own fallible-construction protocol.

Element access follows the same checked/`try_*`/`unsafe_*` tri-tier
convention as `vector`/`inline_vector`/`span`/`array`.

There is no `try_create`/`try_allocate`/`try_clone`/`try_clone_at`: binding
a caller-owned span can never itself fail (there is nothing to allocate),
so the plain constructor is sufficient, and a fallible deep copy would need
a *second* caller-owned destination span the type has no way to ask for on
its own.

`reloco::is_trivially_relocatable<outline_vector<T>>` is deliberately *not*
specialized: the primary template's `std::is_trivially_copyable<T>`
fallback already evaluates to `false` (copy is deleted), correctly
reporting that `outline_vector<T>` may neither be moved nor relocated by
any means.

## `boxed_slice<T>`

`include/reloco/boxed_slice.hpp`

Fixed-size, allocator-backed owned array with no spare capacity, matching
Rust's `Box<[T]>`. The allocation-free-of-slack counterpart to `vector<T>`:
once constructed, `boxed_slice<T>` can never grow/shrink and holds exactly
`size()` elements, so it never wastes capacity headroom and is one word
smaller than `vector<T>` (no separate capacity field). Move-only, with the
same checked/`try_*`/`unsafe_*` tri-tier element access as every other
reloco container.

```cpp
auto bs = reloco::boxed_slice<int>::try_create(4, 0); // 4 elements, all 0
```

Construct via `try_allocate`/`try_create` (default-constructs every
element), their `(count, value)` overloads (copy-constructs `value` into
every element), or `try_from_vector(vector<T> &&)` (Rust's
`Vec::into_boxed_slice`), which moves an already-built `vector<T>`'s
elements into a freshly, exactly-sized allocation and drops any spare
capacity it was holding.

## `binary_heap<T, Compare = std::less<T>>`

`include/reloco/binary_heap.hpp`

Allocator-backed priority queue matching Rust's
`std::collections::BinaryHeap<T>`: a thin wrapper around `vector<T>`
maintaining the standard binary heap invariant via `<algorithm>`'s
`push_heap`/`pop_heap`/`make_heap`/`sort_heap`. With the default
`Compare = std::less<T>` it's a max-heap (`try_pop()`/`peek()` return the
greatest element first, matching both `std::priority_queue` and Rust's
`BinaryHeap` defaults); pass `std::greater<T>` for a min-heap.

```cpp
auto heap = reloco::binary_heap<int>::try_create();
heap.value().try_push(5);
heap.value().try_push(9);
assert(heap.value().peek() == 9);
auto top = heap.value().try_pop(); // 9, restoring the heap invariant
```

`try_push(value)` inserts and restores the invariant; `try_pop()` removes
and returns the top element, failing with `error::container_empty` when
empty; `peek()`/`try_peek()` are the checked/fallible tier for reading the
top without removing it. `into_sorted_vec()` consumes the heap and returns
its elements as a `vector<T>` in ascending order. Iteration
(`begin()`/`end()`) walks unspecified heap order, not sorted order,
matching Rust's `BinaryHeap::iter()`, and is `const`-only since mutating an
element in place could silently break the invariant.

## `flat_set<T, Compare = std::less<T>>`

`include/reloco/flat_set.hpp`

Sorted, unique-element set backed directly by a `vector<T>`: a contiguous,
`std::lower_bound`-searched array kept in ascending `Compare` order rather
than a node-based tree. `try_insert`/`try_remove` are the only mutators and
return `reloco::result<...>`; `contains`/`try_find` do not allocate or
mutate. `begin()`/`end()`/`cbegin()`/`cend()` are read-only (mutating
through an iterator would break the sort invariant).

```cpp
auto s = reloco::flat_set<int>::try_create();
if (!s)
  return;
auto ok = s->try_insert(2);
ok = s->try_insert(1);
assert(s->contains(1) && s->contains(2));
```

`reloco::flat_set<T, Compare>` is trivially relocatable exactly when
`Compare` is (see [Trivial relocation](relocatable.md)): it wraps a
`vector<T>`, which is unconditionally relocatable regardless of `T`, so
only the (usually stateless, hence trivially relocatable) `Compare`
matters. It has `container_ref_traits`/`collection_view_traits` adapters
(see below): as an associative `mutable_container_ref` source
(`key_type == element_type == T`), and as a read-only
(`collection_view_traits::is_mutable == false`) collection view, since
mutating an element in place could break the sort order.

## `flat_map<Key, Mapped, Compare = std::less<Key>>`

`include/reloco/flat_map.hpp`

Sorted, unique-key map backed directly by a `vector<std::pair<Key,
Mapped>>`: `flat_set`'s key/value counterpart, kept in ascending `Compare`
order over `.first` rather than a node-based tree. `flat_set` and
`flat_map` share a common base, `detail::flat_container_base<Storage,
Compare, KeyOf>` (`include/reloco/detail/flat_container_base.hpp`), which
implements insertion, removal, lookup, iteration, and cloning once for
both `vector`- and `inline_vector`-backed storage; `flat_map` only adds the
key/value-specific surface described below.

```cpp
auto m = reloco::flat_map<int, std::string>::try_create();
if (!m)
  return;
auto ok = m->try_insert(2, "two");
ok = m->try_insert(1, "one");
auto found = m->try_at(1);
assert(found && found->get() == "one");
```

`try_insert(Key key, Mapped mapped)` is a fallible-insertion convenience
that fails with `error::already_exists` if `key` is already present, or
propagates the underlying storage's allocation failure; `try_at(const
K &key)` (mutable and `const`-qualified overloads) looks up `key` and
returns a reference to just the mapped value on success or
`error::not_found` otherwise. Mutating the mapped value through the
mutable `try_at` overload is always safe: unlike mutating a key, it cannot
break the sort invariant. There is deliberately no `operator[]`: unlike
`std::map`, reloco has no way to silently insert a default-constructed
value on a missing key without either allocating fallibly or aborting, both
of which conflict with the library's explicit-fallibility design — use
`try_insert`/`try_at` instead.

`reloco::flat_map<Key, Mapped, Compare>` is trivially relocatable exactly
when `Compare` is, for the same reason as `flat_set`. It has
`container_ref_traits`/`collection_view_traits` adapters: as an
associative `mutable_container_ref` source (`key_type == Key`,
`element_type == Mapped`), and as a read-only
(`collection_view_traits::is_mutable == false`) collection view of
`(Key, Mapped)` pairs.

## `inline_flat_set<T, Capacity, Compare = std::less<T>>`

`include/reloco/inline_flat_set.hpp`

`flat_set`'s allocation-free counterpart: a sorted, unique-element set
backed directly by an `inline_vector<T, Capacity>` instead of a `vector<T>`,
built on the same `detail::flat_container_base` shared with `flat_set`.
There is no `try_allocate`/`try_create` factory to reserve extra capacity
with (`Capacity` itself is the fixed reserved capacity, fixed at compile
time) — only a default constructor is available, and `try_insert` fails
with `error::capacity_exceeded` once `size() == Capacity`. Everything else
(`try_insert`, `try_remove`, `contains`, `try_find`, `clear`, iteration,
`try_clone(alloc)` / `try_clone()`) behaves exactly like `flat_set`.

```cpp
reloco::inline_flat_set<int, 4> s;
auto ok = s.try_insert(2);
ok = s.try_insert(1);
assert(s.contains(1) && s.contains(2));
```

`reloco::is_trivially_relocatable<inline_flat_set<T, Capacity, Compare>>`
is conditional on **both** `is_trivially_relocatable_v<T>` and
`is_trivially_relocatable_v<Compare>` (unlike `flat_set`, whose `vector<T>`
storage is unconditionally relocatable, `inline_flat_set`'s
`inline_vector<T, Capacity>` storage embeds `T` directly, so its
relocatability already depends on `T` — see `inline_vector<T, Capacity>`
above). It has the same `container_ref_traits`/`collection_view_traits`
adapters as `flat_set`.

Like `inline_vector`, `inline_flat_set<T, Capacity, Compare>` can be
"upgraded" to a heap-backed `flat_set<T, Compare>` via `try_to_flat_set`,
for callers that reach the fixed `Capacity` but need to keep growing:

| Function | Behavior |
|---|---|
| `try_to_flat_set(allocator_ref alloc) const &` / `try_to_flat_set() const &` | Clones every element into a new `flat_set<T, Compare>`, leaving `*this` untouched |
| `try_to_flat_set(allocator_ref alloc) &&` / `try_to_flat_set() &&` | Moves every element out into a new `flat_set<T, Compare>`, consuming `*this` (left empty either way) |

## `inline_flat_map<Key, Mapped, Capacity, Compare = std::less<Key>>`

`include/reloco/inline_flat_map.hpp`

`flat_map`'s allocation-free counterpart: a sorted, unique-key map backed
directly by an `inline_vector<std::pair<Key, Mapped>, Capacity>`. As with
`inline_flat_set`, there is no allocator-taking factory (only a default
constructor), and `try_insert`/`try_insert_at` fail with
`error::capacity_exceeded` once `size() == Capacity`. Otherwise it exposes
the same `try_insert(key, mapped)`/`try_at(key)` surface as `flat_map`
(including the same rationale for omitting `operator[]`).

```cpp
reloco::inline_flat_map<int, std::string, 4> m;
auto ok = m.try_insert(1, "one");
auto found = m.try_at(1);
assert(found && found->get() == "one");
```

`reloco::is_trivially_relocatable<inline_flat_map<Key, Mapped, Capacity,
Compare>>` is conditional on `is_trivially_relocatable_v<Key>`,
`is_trivially_relocatable_v<Mapped>`, **and**
`is_trivially_relocatable_v<Compare>` all holding, for the same
embedded-storage reason as `inline_flat_set`. It has the same
`container_ref_traits`/`collection_view_traits` adapters as `flat_map`.

Like `inline_flat_set`, `inline_flat_map<Key, Mapped, Capacity, Compare>`
can be "upgraded" to a heap-backed `flat_map<Key, Mapped, Compare>` via
`try_to_flat_map`, mirroring `inline_vector::try_to_vector`'s dual
clone/consume tiers:

| Function | Behavior |
|---|---|
| `try_to_flat_map(allocator_ref alloc) const &` / `try_to_flat_map() const &` | Clones every (key, mapped) entry into a new `flat_map<Key, Mapped, Compare>`, leaving `*this` untouched |
| `try_to_flat_map(allocator_ref alloc) &&` / `try_to_flat_map() &&` | Moves every (key, mapped) entry out into a new `flat_map<Key, Mapped, Compare>`, consuming `*this` (left empty either way) |

## `tree_set<T, Compare = std::less<T>>`

`include/reloco/tree_set.hpp`

Sorted, unique-element set backed by an allocator-managed, unbalanced
binary search tree (`detail::tree_base<T, Compare, KeyOf>`,
`include/reloco/detail/tree_base.hpp`) rather than `flat_set`'s contiguous
`vector<T>`: every element lives in its own single-allocation node, so
insertion/removal never shifts other elements and never invalidates
references to elements that are not themselves removed — the trade-off is
`O(log n)` average (`O(n)` worst case, since it is unbalanced)
insert/find/remove instead of `flat_set`'s cache-friendly `O(n)`
insert/`O(log n)` binary-search find. See [Tree containers](
tree-containers.md) for the node layout and engine design.

```cpp
auto s = reloco::tree_set<int>::try_create();
if (!s)
  return;
auto ok = s->try_insert(2);
ok = s->try_insert(1);
assert(s->contains(1) && s->contains(2));
```

`reloco::tree_set<T, Compare>` is trivially relocatable exactly when
`Compare` is: the container's own members (a node pointer, a size, and an
`allocator_ref`) never point into `*this`, so relocating its bytes is
always safe regardless of `T` — every element lives in a separately
allocated node untouched by a byte-copy of the container object itself.
It has a `container_ref_traits` adapter (associative
`mutable_container_ref` source, `key_type == element_type == T`), but no
`collection_view_traits` adapter: unlike `flat_set`, a tree has no `O(1)`
`at(index)`/`data()` to offer.

Because the tree is always kept in ascending `Compare` order, `tree_set`
also exposes Rust `BTreeSet`-flavored `try_first`/`try_last`/
`try_pop_first`/`try_pop_last`, `retain`, `append`, and `is_subset`/
`is_superset`/`is_disjoint` — see [Tree containers](tree-containers.md#rust-btreesetbtreemap-flavored-api-surface)
for the full table.

## `tree_map<Key, Mapped, Compare = std::less<Key>>`

`include/reloco/tree_map.hpp`

`tree_set`'s key/value counterpart: a sorted, unique-key map backed by the
same `detail::tree_base` engine over `std::pair<Key, Mapped>` nodes.
Exposes the same `try_insert(key, mapped)`/`try_at(key)` (mutable and
`const` overloads) surface as `flat_map`, plus the Rust-`entry`-flavored
`try_entry_or_insert`/`try_entry_or_insert_with`/`try_entry_and_modify`; it
also has no `operator[]`, for the same fallibility rationale as `flat_map`.

```cpp
auto m = reloco::tree_map<int, std::string>::try_create();
if (!m)
  return;
auto ok = m->try_insert(2, "two");
ok = m->try_insert(1, "one");
auto found = m->try_at(1);
assert(found && found->get() == "one");
```

`reloco::tree_map<Key, Mapped, Compare>` is trivially relocatable exactly
when `Compare` is, for the same reason as `tree_set`. It has a
`container_ref_traits` adapter (associative `mutable_container_ref`
source, `key_type == Key`, `element_type == Mapped`), but no
`collection_view_traits` adapter, for the same reason as `tree_set`.

Because the tree is always kept in ascending `Compare` order over `Key`,
`tree_map` also exposes Rust `BTreeMap`-flavored `try_first_key_value`/
`try_last_key_value`/`try_pop_first`/`try_pop_last`, `retain(pred)` (where
`pred` takes `(const Key &, Mapped &)`), and `append` — see [Tree
containers](tree-containers.md#rust-btreesetbtreemap-flavored-api-surface)
for the full table.

## `flat_hash_set<T, Hash = std::hash<T>, KeyEqual = std::equal_to<T>>`

`include/reloco/flat_hash_set.hpp`

Unordered, unique-element set backed by an open-addressing, linear-probing
hash table over a single `vector<optional<T>>` (`detail::flat_hash_base<T,
Hash, KeyEqual, KeyOf>`, `include/reloco/detail/flat_hash_base.hpp`),
matching Rust's `HashSet<T>`. Unlike `tree_set` (ordered by `Compare`,
node-based storage), iteration order is unspecified and depends on hash
values and insertion/removal history, but lookup/insert/remove are `O(1)`
average case instead of `O(log n)`. See [Flat hash containers](
flat-hash-containers.md) for the storage/deletion/growth design.

```cpp
auto s = reloco::flat_hash_set<int>::try_create();
if (!s)
  return;
auto ok = s->try_insert(2);
ok = s->try_insert(1);
assert(s->contains(1) && s->contains(2));
```

`reloco::flat_hash_set<T, Hash, KeyEqual>` is trivially relocatable exactly
when both `Hash` and `KeyEqual` are: its own members (a `vector<optional<
T>>`, a size, a mask, and `Hash`/`KeyEqual` themselves) are trivially
relocatable regardless of `T`, since `vector<U>` is unconditionally
trivially relocatable for any `U`. It has a `container_ref_traits` adapter
(associative `mutable_container_ref` source, `key_type == element_type ==
T`), but no `collection_view_traits` adapter: like a tree, a hash table has
no `O(1)` `at(index)`/`data()` to offer (elements are not laid out in
insertion order).

`flat_hash_set` also exposes `try_take` (Rust `HashSet::take` equivalent:
removes and returns the matching element), `retain`, and `is_subset`/
`is_superset`/`is_disjoint` — see [Flat hash containers](
flat-hash-containers.md#rust-hashsethashmap-flavored-api-surface) for the
full table.

## `flat_hash_map<Key, Mapped, Hash = std::hash<Key>, KeyEqual = std::equal_to<Key>>`

`include/reloco/flat_hash_map.hpp`

`flat_hash_set`'s key/value counterpart: an unordered, unique-key map
backed by the same `detail::flat_hash_base` engine over `std::pair<Key,
Mapped>` slots, matching Rust's `HashMap<K, V>`. Exposes the same
`try_insert(key, mapped)`/`try_at(key)` (mutable and `const` overloads)
surface as `tree_map`/`flat_map`, plus the Rust-`entry`-flavored
`try_entry_or_insert`/`try_entry_or_insert_with`/`try_entry_and_modify` and
`try_remove_entry` (Rust `HashMap::remove_entry` equivalent); it also has
no `operator[]`, for the same fallibility rationale as `flat_map`/
`tree_map`.

```cpp
auto m = reloco::flat_hash_map<int, std::string>::try_create();
if (!m)
  return;
auto ok = m->try_insert(2, "two");
ok = m->try_insert(1, "one");
auto found = m->try_at(1);
assert(found && found->get() == "one");
```

`reloco::flat_hash_map<Key, Mapped, Hash, KeyEqual>` is trivially
relocatable exactly when both `Hash` and `KeyEqual` are, for the same
reason as `flat_hash_set`. It has a `container_ref_traits` adapter
(associative `mutable_container_ref` source, `key_type == Key`,
`element_type == Mapped`), but no `collection_view_traits` adapter, for
the same reason as `flat_hash_set`.

`flat_hash_map` also exposes `retain(pred)` (where `pred` takes `(const
Key &, Mapped &)`) — see [Flat hash containers](
flat-hash-containers.md#rust-hashsethashmap-flavored-api-surface) for the
full table.

## `sso_flat_set<T, InlineCapacity, Compare = std::less<T>>`

`include/reloco/sso_flat_set.hpp`

A sorted, unique-element set backed by `sso_vector<T, InlineCapacity>`
instead of `vector<T>`: it holds up to `InlineCapacity` elements inline
without allocating, and transparently promotes to heap storage once that
capacity is exceeded. Unlike `inline_flat_set`, `try_insert` never fails
with `error::capacity_exceeded` — growth past `InlineCapacity` just
allocates, exactly like `flat_set`. Otherwise it exposes the same surface
as `flat_set` (`try_insert`, `try_find`, `contains`, `try_remove`,
`try_clone`, etc.), including the same allocator-taking `try_allocate`/
`try_create` factories.

```cpp
auto set = reloco::sso_flat_set<int, 4>::try_create();
auto ok = set->try_insert(42); // stays inline
auto grown = set->try_insert(1000); // may promote to heap once size > 4
assert(set->contains(42));
```

`reloco::is_trivially_relocatable<sso_flat_set<T, InlineCapacity, Compare>>`
is unconditionally `false`, since it wraps `sso_vector`, which is itself
never trivially relocatable (its inline buffer is self-referencing). It has
the same `container_ref_traits`/`collection_view_traits` adapters as
`flat_set`.

## `sso_flat_map<Key, Mapped, InlineCapacity, Compare = std::less<Key>>`

`include/reloco/sso_flat_map.hpp`

`flat_map`'s SSO-backed counterpart: a sorted, unique-key map backed by
`sso_vector<std::pair<Key, Mapped>, InlineCapacity>`. Like `sso_flat_set`,
it never fails with `error::capacity_exceeded` — it stays inline up to
`InlineCapacity` entries and transparently promotes to heap storage beyond
that. It exposes the same `try_insert(key, mapped)`/`try_at(key)` surface
as `flat_map` (including the same rationale for omitting `operator[]`).

```cpp
auto map = reloco::sso_flat_map<int, std::string, 4>::try_create();
auto ok = map->try_insert(1, "one");
auto found = map->try_at(1);
assert(found && found->get() == "one");
```

`reloco::is_trivially_relocatable<sso_flat_map<Key, Mapped, InlineCapacity,
Compare>>` is unconditionally `false`, for the same reason as
`sso_flat_set`. It has the same `container_ref_traits`/
`collection_view_traits` adapters as `flat_map`.

## `basic_inline_string<Capacity, CharT, TraitsT>` (`inline_string<Capacity>`, `inline_wstring<Capacity>`)

`include/reloco/inline_string.hpp`

Fixed-capacity, inline-allocated character buffer mirroring
`reloco::basic_string`'s fallible-mutation and tri-tier access convention,
without ever allocating or throwing. Unlike `basic_string`, it is trivially
copyable and movable (it holds no heap resource), and
`reloco::is_trivially_relocatable<basic_inline_string<...>>` is
unconditionally `true`.

```cpp
auto s = reloco::inline_string<32>::try_create(reloco::string_view("hello"));
if (!s)
  return; // s.error() is a reloco::error.
auto ok = s->try_append(reloco::string_view(" world"));
assert(s->view() == "hello world");
```

Every mutating operation that can exceed `Capacity` (`try_assign`,
`try_append`, `try_insert`, ...) returns `reloco::result<void>` instead of
trapping or reallocating — there is no growth path, unlike `basic_string`.
Self-aliasing appends/inserts (e.g. `s.try_append(s.view())`) are handled
correctly by temporarily materializing the overlapping source view, exactly
as `basic_string` does. Read-only access follows the same checked/`try_*`/
`unsafe_*` convention as `basic_string`/`string_view`, including a
`RELOCO_UNSAFE_BUFFER_USAGE`-gated `unsafe_c_str()` for interop with
null-terminated-string APIs.

Like the other fixed-capacity types, `basic_inline_string<Capacity, CharT,
TraitsT>` can be "upgraded" to a heap-backed `basic_string<CharT,
TraitsT>` via `try_to_string(allocator_ref alloc)` / `try_to_string()`,
which copy the current content into a new growable string, leaving `*this`
untouched (there is no consuming `&&` overload: unlike `vector`/
`inline_flat_set`/`inline_flat_map`, `basic_inline_string` is trivially
copyable, so there is no move-vs-clone distinction to make).

## `optional<T>`

`include/reloco/optional.hpp`

Zero-allocation, conditionally-present value wrapper replacing
`std::optional<T>` by eliminating undefined behavior on empty access.
Storage is inline (no heap allocation), and construction/assignment/`swap`
compose with `is_trivially_relocatable<T>` for zero-overhead moves where `T`
permits it.

```cpp
reloco::optional<int> maybe;
assert(!maybe.has_value());
maybe = 42;
assert(maybe.value() == 42);
```

Access follows the checked/fallible/unsafe convention used throughout
reloco: `value()`/`operator*`/`operator->` use `RELOCO_ASSERT` to trap on
empty access; `try_value()` returns `result<std::reference_wrapper<T>>` and
`ok_or(error)` bridges directly into a `result<T>` pipeline; `unsafe_value()`/
`unsafe_ptr()` are `RELOCO_UNSAFE_BUFFER_USAGE`-gated, debug-only-checked
escape hatches. `emplace(...)` destroys any held value and constructs a new
one in place, returning a borrowed reference to it.
`reloco::is_trivially_relocatable<optional<T>>` follows `T`'s own
relocatability.

Rust `Option`-inspired methods round out the fallible/`&`-qualified
mutation surface: `take()` moves the value out, leaving `*this` empty, and
returns it as a fresh `optional<T>`; `replace(value)` moves `value` in and
returns whatever was previously held (empty or not); `get_or_insert(value)`/
`get_or_insert_with(factory)` insert only if empty (the latter's `factory`
is never invoked when a value is already present) and return a reference to
the now-present value.

```cpp
reloco::optional<int> opt;
int &v = opt.get_or_insert_with([] { return 42; });
assert(v == 42);
reloco::optional<int> old = opt.replace(7);
assert(*old == 42 && *opt == 7);
```

Two free functions mirror Rust's `bool::then`/`bool::then_some`:
`then(condition, f)` invokes `f()` only if `condition` is `true`, wrapping
its result in an `optional`; `then_some(condition, value)` always evaluates
`value` (an ordinary argument) but only keeps it if `condition` is `true` --
prefer `then()` when constructing the value has a cost worth skipping.

```cpp
reloco::optional<int> maybe = reloco::then(x > 0, [&] { return compute(x); });
```

## `function_ref<R(Args...)>`

`include/reloco/function_ref.hpp`

Non-owning, zero-allocation, type-erased borrow of any callable (lambdas,
function pointers, functors, member-invocable objects), storing exactly two
pointers (a context payload and a trampoline function). It has no default
constructor and can never be null: the single converting constructor is
`RELOCO_LIFETIMEBOUND`-annotated so Clang rejects binding it to a temporary
callable (e.g. an inline lambda) that would immediately dangle.

```cpp
void call_it(reloco::function_ref<int(int)> f) { assert(f(1) == 2); }
call_it([](int x) { return x + 1; });
```

Prefer `function_ref` over `reloco::function<R(Args...)>` at a call boundary
that does not need to store the callable past the call: it never allocates
and has no ownership overhead, at the cost of the caller owning the bound
callable's lifetime for the duration of the call.

## `inplace_function<Signature, Capacity = 32>`

`include/reloco/inplace_function.hpp`

Zero-allocation, fixed-capacity, move-only callable wrapper: a deterministic
alternative to `std::function` that stores the type-erased callable entirely
inline in a `Capacity`-byte buffer. If a functor exceeds `Capacity` or its
alignment requirement, construction fails to compile via `static_assert`
rather than silently falling back to heap allocation.

```cpp
reloco::inplace_function<int(int)> f = [](int x) { return x + 1; };
assert(f(1) == 2);
```

`operator()` is the checked tier (`RELOCO_ASSERT`s if empty);
`unsafe_invoke()` is the `RELOCO_UNSAFE_BUFFER_USAGE`-gated unsafe tier that
skips the empty check. The class carries `-Wconsumed` typestate annotations
(`RELOCO_CONSUMABLE`/`RELOCO_CALLABLE_WHEN`/`RELOCO_SET_TYPESTATE`) so Clang
flags invoking a default-constructed or moved-from instance. Relocation
(move/swap) uses `is_trivially_relocatable<Functor>` to skip the
move-constructor/destructor pair for the stored callable when possible.

## `stack_allocator` / `stack_allocator_tag` / `stack_allocator_context`

`include/reloco/stack_allocator.hpp`

`allocator_traits<stack_allocator_tag>` backend implementing a bump-pointer
(arena) allocator over a single caller-owned buffer: `allocate` advances an
offset into the buffer and never frees individual blocks, only the whole
arena at once via `stack_allocator_context::reset()`. `reallocate` and
`advise` are intentionally unimplemented; `allocator_ref::can_reallocate()`/
`can_advise()` detect their absence and report `false`/
`error::unsupported_operation` automatically.

```cpp
alignas(std::max_align_t) std::byte buffer[1024];
reloco::stack_allocator arena(reloco::stack_allocator_context(buffer, sizeof(buffer)));
auto vec = reloco::vector<int>::try_allocate(arena.ref());
arena.context()->reset(); // Reclaim the entire arena at once.
```

Use `stack_allocator` for a scoped or per-frame arena backing any reloco
container that takes an `allocator_ref` (`vector<T>`, `basic_string`,
`flat_set<T>`, ...), instead of `heap_allocator_tag`, when allocation must
be deterministic and bounded to a caller-owned buffer.

## `unique_ptr<T>`

`include/reloco/unique_ptr.hpp`

Move-only, allocator-backed smart pointer. `try_allocate(allocator_ref,
Args...)`/`try_create(Args...)` allocate the box and then resolve the best
available construction strategy for `T` via
`construction_helpers::try_construct` (see
[Fallible construction](fallible-construction.md)) — so any `T` that
implements `try_construct`/`try_allocate`/`try_create`, or is simply
nothrow-constructible from `Args...`, works with no extra glue code.

```cpp
auto ptr = reloco::unique_ptr<widget>::try_create(arg1, arg2);
if (ptr)
  (*ptr)->do_something();
```

`operator*`/`operator->`/`get()` are checked (`RELOCO_ASSERT` on null);
`unsafe_get()` skips that check and is `RELOCO_UNSAFE_BUFFER_USAGE`-gated.
No copy constructor: use `T`'s own `try_clone`/`try_clone_at` explicitly for
a deep copy. `reloco::is_trivially_relocatable<unique_ptr<T>>` is always
`true`, regardless of `T` (see [Trivial relocation](relocatable.md)).

## `shared_ptr<T>` / `weak_ptr<T>` / `enable_shared_from_this<T>`

`include/reloco/shared_ptr.hpp`

Reference-counted, allocator-backed smart pointer. `shared_ptr<T>` is
copyable (sharing ownership, incrementing a refcount) and movable;
`weak_ptr<T>` observes without extending the object's lifetime, and
`lock()`s back into a `shared_ptr<T>` (or `error::pointer_expired` if the
object is already gone). Object construction goes through
`construction_helpers::try_construct`, exactly like `unique_ptr`, so the
same `try_construct`/`try_allocate`/`try_create`/nothrow-constructible tiers
apply.

Two allocation layouts are available, mirroring `std::allocate_shared` vs.
`std::make_shared`:

```cpp
// Single allocation (object + control block together); recommended default.
auto ptr = reloco::try_create_combined_shared<widget>(arg1, arg2);

// Object and control block allocated separately: the object's storage is
// freed as soon as the last shared_ptr releases it, independently of any
// surviving weak_ptr (which only keeps the small control block alive).
auto ptr2 = reloco::try_create_shared<widget>(arg1, arg2);
```

`try_allocate_shared`/`try_allocate_combined_shared` take an explicit
`allocator_ref`; `try_create_shared`/`try_create_combined_shared` use
`default_allocator()`. `static_pointer_cast`/`dynamic_pointer_cast`/
`const_pointer_cast`/`reinterpret_pointer_cast` mirror the `std::shared_ptr`
casts. `operator*`/`operator->`/`get()` are checked; `unsafe_get()` skips
the null check and is `RELOCO_UNSAFE_BUFFER_USAGE`-gated; `try_get()`
returns `result<T *>`. `reloco::is_trivially_relocatable<shared_ptr<T>>`
and `<weak_ptr<T>>` are always `true`, regardless of `T` (see
[Trivial relocation](relocatable.md)).

## `rc<T>` / `weak_rc<T>` / `enable_rc_from_this<T>`

`include/reloco/rc.hpp`

Single-threaded reference-counted, allocator-backed smart pointer,
matching Rust's `std::rc::Rc<T>`/`std::rc::Weak<T>`. Structurally identical
to `shared_ptr<T>`/`weak_ptr<T>` -- same two allocation layouts
(`try_allocate_rc`/`try_create_rc` for a separate control block,
`try_allocate_combined_rc`/`try_create_combined_rc` for a single
allocation), same checked/fallible/unsafe access tiers, same
`static_pointer_cast`/`dynamic_pointer_cast`/`const_pointer_cast`/
`reinterpret_pointer_cast` overloads, same `enable_rc_from_this<T>`/
`rc_from_this()` self-borrow idiom -- except its refcounts are plain
`std::size_t` increments/decrements instead of `std::atomic<std::size_t>`
operations, which is unsound if an `rc<T>`/`weak_rc<T>` is ever shared
across threads but noticeably cheaper when it never is.

```cpp
auto ptr = reloco::try_create_combined_rc<widget>(arg1, arg2);
reloco::rc<widget> shared = ptr.value();
reloco::weak_rc<widget> observer = shared;
```

Pick `rc<T>` over `shared_ptr<T>` purely for performance, whenever the
shared object only ever lives on one thread; switch to `shared_ptr<T>` the
moment it might cross a thread boundary. The two are unrelated types with
independent control blocks and cannot share ownership of the same object.

## `bytes` / `bytes_mut`

`include/reloco/bytes.hpp`

An immutable, reference-counted, cheaply-cloneable byte buffer (`bytes`)
and its growable, exclusively-owned mutable counterpart (`bytes_mut`),
matching the shape of Rust's `bytes` crate `Bytes`/`BytesMut`. Both are
allocator-backed and hold no self-references, so `is_trivially_relocatable`
is `true` for each.

`bytes_mut` is a move-only, `std::byte` buffer that grows the same way
`vector<T>` does for trivially relocatable `T` -- prefer
`allocator_ref::expand_in_place`, then `allocator_ref::reallocate`, and
only fall back to a fresh allocation + `memcpy` + deallocate:

```cpp
auto buf = reloco::bytes_mut::try_create(); // or try_allocate(alloc, initial_cap)
if (buf) {
  buf->try_put_u8(0xFF);
  buf->try_put_u32_be(0xDEADBEEF);
  buf->try_put_slice(reloco::span<const std::byte>(payload, payload_len));
}
```

`try_push`, `try_put_slice`/`try_extend_from_slice`, and the endian-aware
`try_put_u8`/`try_put_u16_le`/`try_put_u16_be`/`try_put_u32_le`/
`try_put_u32_be`/`try_put_u64_le`/`try_put_u64_be` (Rust `BufMut`-flavored)
all grow the buffer on demand via `try_reserve`, failing with
`result<void>` on allocation failure rather than throwing. `data()`/
`as_span()` expose the current contents; `clear()` resets the length
without releasing the allocation.

`bytes_mut::try_freeze() &&` consumes the buffer and converts it into an
immutable `bytes` without copying the byte payload: it moves the existing
heap pointer into a freshly allocated `rc<detail::bytes_storage>` control
block (that small control-block allocation is itself unavoidable -- it is
what makes the buffer shareable -- but the potentially large payload is
never copied, unlike a plain `vector<T>`-to-owned-copy conversion):

```cpp
reloco::result<reloco::bytes> frozen = std::move(*buf).try_freeze();
```

`bytes` clones by bumping an `rc<T>` refcount (never copying the payload),
and slices are the same O(1) operation, sharing the same backing
allocation while only adjusting a local pointer/length pair:

```cpp
reloco::bytes clone = *frozen;              // refcount bump, no copy
reloco::bytes view = frozen->slice(1, 4);    // shares storage
reloco::bytes front = frozen->split_to(2);   // Rust Bytes::split_to
reloco::bytes rest = frozen->split_off(2);   // Rust Bytes::split_off
```

`try_copy_from(span<const std::byte>, allocator_ref)` allocates a fresh
`bytes` and copies the given span into it (the usual entry point when data
does not already live in a `bytes_mut`). Checked `operator[]` and fallible
`try_at`/`try_slice`/`try_split_to`/`try_split_off` round out bounds-safe
access; `operator==`/`operator!=` compare contents, not identity.

Unlike the real `bytes` crate, `bytes_mut` does not support
`split_to`/`split_off`/`unsplit` (those would require the same shared,
refcounted storage `bytes` uses, turning every mutation into an
alias-check), and `bytes` has no `Buf`-style stateful advancing-cursor
reader -- only the `BufMut`-style `try_put_*` writers on `bytes_mut`. Both
gaps can be layered on top of `as_span()` if/when needed.

## `function<R(Args...)>`

`include/reloco/function.hpp`

Type-erased, allocator-backed callable wrapper (reloco's `std::function`
counterpart). `try_allocate(allocator_ref, F)`/`try_create(F)` wrap any
callable convertible to `R(Args...)`, choosing the cheapest storage tier at
construction time: a bare function pointer (or captureless lambda) stored
directly with no allocation, a small-object-optimization inline buffer
(`function<R(Args...)>::soo_capacity` bytes, alignment up to
`alignof(std::max_align_t)`), or a single heap allocation for anything
larger.

```cpp
auto fn = reloco::function<int(int)>::try_create([captured](int v) noexcept {
  return captured + v;
});
if (fn)
  int result = (*fn)(41);
```

Move-only: `try_clone()` performs an explicit fallible deep copy, failing
with `error::unsupported_operation` if the captured callable is not
`std::is_nothrow_copy_constructible_v` (or if the function is empty).
`operator()` asserts non-empty; `try_call(Args...)` is the checked
alternative, failing with `error::container_empty` instead -- and if `R` is
itself a `result<U>`, `try_call`'s own result is flattened rather than
double-wrapped. `reloco::is_trivially_relocatable<function<R(Args...)>>` is
always `false`: the captured callable may live inline in the SOO buffer, so
relocating the wrapper by copying bytes is only as safe as the (erased)
captured type itself (see [Trivial relocation](relocatable.md)).

## `type_id` / `type_id_of<T>()`

`include/reloco/type_id.hpp`

Opaque, process-wide type identity established without RTTI, matching
Rust's `std::any::TypeId`. `type_id::of<T>()` (or the free-function alias
`type_id_of<T>()`) returns the address of a per-instantiation static data
member as `T`'s identity -- no `typeid`/`<typeinfo>`, no mangled-name
string, no runtime registration. `T` is never decayed for you (unlike
`any::is<T>()`), so `type_id::of<int>()` and `type_id::of<const int>()` are
distinct identities.

```cpp
auto id = reloco::type_id::of<int>();
if (id == reloco::type_id::of<int>())
  // ...
```

A default-constructed `type_id` is a distinct "no type" sentinel
(`operator bool()` is `false`), never equal to `type_id::of<T>()` for any
`T` -- what a type-erased container like `any` returns from its own
`type_id()` accessor when empty. Unlike Rust's own `TypeId` (deliberately
`Eq`/`Hash` only, no `Ord`), reloco also provides
`operator<`/`operator<=`/`operator>`/`operator>=` (via `std::less<const
void *>`, so ordering is at least well-defined and total within a process)
so a `type_id` can be used directly as a `flat_set`/`flat_map` key;
`std::hash<reloco::type_id>` is specialized too, for
`std::unordered_map`/`unordered_set` interop.

`type_id::name()` returns an optional, game-engine-style (Unreal/EnTT
convention) human-readable debug name for `T`, or `nullptr` if none is
registered -- registering a name is always opt-in, never automatic (no
`__PRETTY_FUNCTION__`/`__FUNCSIG__` string-mangling trick), and never
affects equality/ordering/hashing, which are always based solely on the
identity, not the name:

```cpp
reloco::type_id::of<int>().name();          // "int"
reloco::type_id::of<my_widget>().name();    // nullptr, unless registered
```

Register a name for a type with `RELOCO_TYPE_ID_NAME(T, "name")` (usable
at namespace scope, inside or outside `namespace reloco`; does not end in
`;` -- add one at the call site):

```cpp
RELOCO_TYPE_ID_NAME(my_widget, "my_widget");
```

Names are already registered out of the box for every fundamental type
(`bool`, the integer/character types, `float`/`double`/`long double`,
`std::nullptr_t`), for `reloco::type_id` itself, and for a handful of
"classic" reloco types: `reloco::error`, `reloco::ordering`,
`reloco::string`/`wstring`, `reloco::string_view`/`wstring_view`, and
`reloco::sso_string`/`wsso_string` (each registered alongside its own
type's definition, in its own header, not centralized in `type_id.hpp`,
matching how this codebase's `std::hash<reloco::X>` specializations are
likewise defined alongside each `X`).

The identity trick relies on every translation unit that instantiates
`type_id_tag<T>` for the same `T` sharing one symbol; a consumer
building a shared library with `-fvisibility=hidden` can otherwise end up
with a separate, non-merged copy per shared object, silently breaking
`type_id`/`any::is<T>()` equality for a `T` shared across that boundary.
Define `RELOCO_ENABLE_EXPORT` (see `reloco_config.hpp`) to opt in to
keeping that symbol at default visibility regardless of the ambient
`-fvisibility` setting; it is opt-in, and a no-op on backends without an
equivalent attribute (e.g. MSVC), since it only matters when `type_id`/
`any` values for a shared `T` actually cross a shared-object boundary.
This only takes effect for a `T` that itself has default visibility (a
fundamental type like `int` always qualifies; an ordinary consumer class
under `-fvisibility=hidden` needs its own explicit default-visibility
annotation too), since a template instantiation's visibility is the
minimum of the template's own and each argument's.

## `any`

`include/reloco/any.hpp`

Type-erased, allocator-backed single-value container (reloco's `std::any`
counterpart), with no dependency on RTTI: type identity is established via
`reloco::type_id` (see `type_id.hpp` above) instead of
`typeid`/`<typeinfo>`. `try_allocate(allocator_ref, T)`/`try_create(T)` wrap
a copy/move of any decayed, constructible type, choosing the cheapest
storage tier at construction time: a small-object-optimization inline
buffer (`any::soo_capacity` bytes, alignment up to
`alignof(std::max_align_t)`), or a single heap allocation for anything
larger. `try_allocate(allocator_ref, std::in_place_type<T>, args...)`/
`try_create(std::in_place_type<T>, args...)` construct `T` in place instead,
avoiding the extra move/copy.

```cpp
auto value = reloco::any::try_create(42);
if (value && value->is<int>())
  int n = value->get<int>();
```

Move-only: `try_clone()` performs an explicit fallible deep copy, dispatching
through `construction_helpers::try_clone_at` (see `construction_helpers.hpp`)
-- the held type's own `try_clone`/`try_allocate`/`try_create` if it
implements one, falling back to plain nothrow copy-construction -- and
failing with `error::unsupported_operation` if none of those apply (or if
the instance is empty). `is<T>()` reports whether the instance is non-empty
and holds exactly `std::decay_t<T>`. Access follows the usual tri-tier
convention: `get<T>()` asserts non-empty and type-matching; `try_get<T>()` is
the checked alternative, returning `result<std::reference_wrapper<T>>`
(failing with `error::container_empty` if empty, or `error::invalid_argument`
on a type mismatch); `unsafe_get<T>()` skips the check entirely (only a
`RELOCO_DEBUG_ASSERT`). `reloco::is_trivially_relocatable<any>` is always
`false`: the held value may live inline in the SOO buffer, so relocating
the wrapper by copying bytes is only as safe as the (erased) held type
itself (see [Trivial relocation](relocatable.md)).

A parallel, Rust-flavored surface mirrors Rust's `std::any::Any` trait on
top of the same dispatch: `type_id()` (Rust's `Any::type_id`) returns the
held value's `reloco::type_id` (the "no type" sentinel if empty) --
`type_id().name()` additionally gives an optional debug name if one was
registered for the held type (see [`type_id`](#type_id--type_id_oft)
above), `nullptr` otherwise; `downcast_ref<T>()`/`downcast_mut<T>()` (Rust's `Any::downcast_ref`/
`downcast_mut`) return a nullable `const T *`/`T *` instead of asserting --
reloco's usual analog of Rust's `Option<&T>`/`Option<&mut T>` for a
checked-but-non-asserting accessor (compare `flat_map::find`); and
`downcast<T>() &&` (Rust's `Any::downcast`, consuming) moves the held `T`
out into a `result<T>` (`error::container_empty`/`error::invalid_argument`
on failure) rather than Rust's `Result<Box<T>, Box<dyn Any>>`, since every
fallible reloco operation returns `reloco::result<T>` and handing back the
original, differently-typed `any` on failure would require a second error
type.

## `collection_view<T>` / `mutable_collection_view<T>` / `collection_view_traits<Container>`

`include/reloco/collection_view.hpp`

Type-erased, non-owning views over an *adapted* sequence container
(`reloco::span<T>`, `reloco::array<T, N>`, `std::array<T, N>`,
`std::span<T>`, ...), matching `allocator_ref`'s customization-point shape:
a two-word handle (an untyped context pointer plus a `const vtable *`), no
virtual base class, no RTTI, no allocation of its own. There is
deliberately no structural detection ("any type that happens to have
`size()`/`begin()`/`end()`"): a container type is only usable through these
views once someone specializes `collection_view_traits<Container>` for it,
exactly like `allocator_ref` requires an explicit `allocator_traits<Tag>`
specialization. Every required accessor must be supplied explicitly by the
adapter author; optional capabilities (contiguous `data()` access, mutable
access) are switched on by required `static constexpr bool` flags
(`has_data`, `is_mutable`) on the traits specialization, mirroring how
`allocator_ref::can_expand_in_place()`/`can_reallocate()`/`can_advise()`
report an optional backend operation.

`collection_view<T>` is *unconditionally read-only*: every accessor
(`size`/`empty`/`at`/`try_at`/`unsafe_at`/`data`/`try_data`/`unsafe_data`/
`for_each`) returns or visits `const T &`/`const T *`, regardless of
whether the bound container or `T` itself is `const`-qualified.
`mutable_collection_view<T>`, derived from `collection_view<T>`, is the
*only* way to obtain write access: its converting constructor additionally
requires the adapter's `is_mutable` flag and a non-`const` lvalue container,
and it adds `T &`/`T *`-returning overloads of `at`/`try_at`/`unsafe_at`/
`data`/`try_data`/`unsafe_data`/`for_each` alongside the read-only ones it
inherits.

```cpp
reloco::array<int, 3> a{1, 2, 3};

reloco::collection_view<int> view(a);       // read-only
int total = 0;
view.for_each([&](const int &v) { total += v; });

reloco::mutable_collection_view<int> mview(a); // requires a non-const array
mview.at(0) = 100;                             // a[0] == 100
```

Both converting constructors are `explicit`: binding a container is always
a deliberate, visible step at the call site, never an implicit conversion.
Neither view allocates, copies, or owns the underlying container: like
`span`/`allocator_ref`, the referenced container must outlive every view
built from it.

Built-in `collection_view_traits` adapters ship for `reloco::span<T>`,
`reloco::array<T, N>`, `std::array<T, N>`, and `std::span<T>` (the last
gated behind `RELOCO_HAS_STD_SPAN`). No adapters for owning/node-based
containers (`std::vector`, `std::list`, `std::map`, ...) are provided yet;
see the `collection_view_traits` primary template's documentation for the
full contract required to add one.

## `mutable_container_ref<T, Key = void>` / `container_ref_traits<Container>`

`include/reloco/container_ref.hpp` (core, no built-in adapters),
`include/reloco/container_ref_std.hpp` (opt-in `std::vector`/`std::map`
adapters)

Type-erased, non-owning handle allowing *structural* mutation of an adapted
container (growing, inserting, erasing, clearing) as well as
indexed/keyed access to its existing elements -- unlike
`mutable_collection_view`, which only mutates elements already present and
never resizes the container. Same customization-point shape as
`allocator_ref`/`collection_view`: a container is only usable here once
someone specializes `container_ref_traits<Container>` for it; there is no
structural detection.

`mutable_container_ref<T, Key = void>` is an alias picking between two
implementations based on whether `Key` is `void`:

- `mutable_container_ref<T>` (`Key = void`): a *sequence* container handle
  (`container_ref_traits<Container>::is_associative == false`). Offers
  `try_push_back`/`try_push_front`/`try_insert_at(index,
  value)`/`try_erase_at(index)`/`clear()`, `for_each(Fn)` (implemented
  generically over `at`/`size` -- no separate trait hook needed, since this
  contract targets index-addressable "vector-like" containers), and
  `at`/`try_at`/`unsafe_at(index)` for existing elements.
- `mutable_container_ref<T, Key>` (`Key` other than `void`): an
  *associative* container handle (`is_associative == true`). Offers
  `try_insert_at(key, value)`/`try_erase(key)`/`clear()`, `for_each(Fn)`
  (via a required trait-level iteration hook, since there is no index
  concept to fall back on), and `at`/`try_at`/`unsafe_at(key)` for existing
  entries.

```cpp
std::vector<int> v{1, 2, 3};
reloco::mutable_container_ref<int> ref(v);
ref.try_push_back(4);          // v == {1, 2, 3, 4}
ref.try_insert_at(0, 0);       // v == {0, 1, 2, 3, 4}
ref.at(0) = 100;                // v == {100, 1, 2, 3, 4}
```

Every fallible trait function the adapted container cannot support (e.g. a
container with no efficient front-insertion, or inserting a key that
already exists) must still be implemented by the traits specialization --
it simply reports that through its own `result<void>` (e.g.
`unexpected(error::unsupported_operation)`, `error::already_exists`,
`error::not_found`) rather than being gated by a separate capability flag.

`container_ref_std.hpp` is deliberately a separate header from
`container_ref.hpp`: its `std::vector<T>`/`std::map<Key, Value>` adapters
wrap standard-library operations that allocate and may throw, which the
rest of reloco avoids; only a caller who explicitly `#include`s this header
opts into that behavior. Every adapter function in it wraps the
underlying call in `try`/`catch (...)`, converting any thrown exception
into `unexpected(error::allocation_failed)` -- except under
`-fno-exceptions` (`RELOCO_HAS_EXCEPTIONS == 0`), where the call is made
unguarded instead, since `try`/`catch` isn't valid syntax in that mode.

## `value_ptr<T>` / `value_ref<T>`

`include/reloco/value_ptr.hpp`, `include/reloco/value_ref.hpp`

Non-owning pointer/reference wrappers that document a borrowed relationship
without taking ownership, and reject binding to prvalue temporaries at
compile time so a borrow can never quietly outlive its source. `value_ptr`
is nullable (`operator bool`, defaults to null); `value_ref` is always
non-null and convertible to/from `value_ptr`. See
[Lifetime safety](lifetime-safety.md).

## `checked_value<T>`

`include/reloco/checked_value.hpp`

Move-only wrapper giving Rust-like use-after-move checking to any nothrow
move-constructible `T` (plus a `T *` partial specialization with an
additional null check on dereference). A moved-from instance is poisoned:
further access asserts and traps, on every compiler; under Clang, `
-Wconsumed` additionally flags use-after-move at compile time. See
[Lifetime safety](lifetime-safety.md). `reloco::is_trivially_relocatable<
checked_value<T>>` mirrors `T`'s own relocatability; `checked_value<T *>` is
always relocatable (see [Trivial relocation](relocatable.md)).

## `cell<T>` / `ref_cell<T>`

`include/reloco/cell.hpp`

Interior-mutability wrappers matching Rust's `std::cell::Cell<T>`/
`std::cell::RefCell<T>`: both allow mutating the wrapped value through a
shared (`const`) reference, deliberately opting out of the ordinary
`const`/non-`const` reference rules every other reloco container enforces.
Both are single-threaded only (`!Sync`, in Rust's terms); see `mutex.hpp`
for a thread-safe alternative.

`cell<T>` has no runtime bookkeeping: `set(T)`/`replace(T)` only ever move
the value in and out, so they work for any `T`, callable through a `const
cell<T>&`:

```cpp
const reloco::cell<int> c(1);
c.set(2);                 // mutation through a const reference
const int old = c.replace(3);
assert(old == 2 && c.get() == 3);
```

`get()` additionally requires a `noexcept` copy constructor (Rust's `T:
Copy` bound), since it is the only accessor that hands back a copy rather
than moving; `take()` requires `T` to be default constructible (Rust's `T:
Default` bound on `Cell::take`) and replaces the value with `T()`,
returning the previous one.

`ref_cell<T>` instead borrows a reference to `T` in place, tracking the
borrow state at runtime: at most one exclusive borrow (`mut_guard`), or any
number of concurrent shared borrows (`ref_guard`), may be outstanding at
once. `try_borrow()`/`try_borrow_mut()` are the fallible tier, returning
`result<ref_guard>`/`result<mut_guard>` with `error::busy` on conflict;
`borrow()`/`borrow_mut()` are the checked tier that `RELOCO_ASSERT` instead,
matching Rust's own panicking `RefCell::borrow()`/`borrow_mut()`. Both guard
types are move-only RAII types that release their borrow on destruction,
and are `RELOCO_CONSUMABLE`-tagged so Clang's `-Wconsumed` flags
use-after-move of a guard at compile time, same as `checked_value<T>`.
`get_mut()` bypasses borrow tracking entirely for callers that already hold
an exclusive `ref_cell&` (Rust's `RefCell::get_mut()`, which borrows `&mut
self` at compile time instead).

```cpp
reloco::ref_cell<std::string> rc(std::string("hello"));
{
  auto shared = rc.try_borrow();
  assert(shared.has_value() && **shared == "hello");
  auto conflict = rc.try_borrow_mut();
  assert(!conflict.has_value() && conflict.error() == reloco::error::busy);
}
auto exclusive = rc.borrow_mut(); // checked tier: traps instead of failing
*exclusive = "world";
```

## `cow<T>` / `cow_traits<T>`

`include/reloco/cow.hpp`

Clone-on-write wrapper matching Rust's `std::borrow::Cow<'a, T>`: holds
either a borrowed reference to a `T` it doesn't own, or an owned `T` it
does, and defers the (fallible) clone until the borrowed case is actually
mutated. Move-only -- copying a `cow<T>` would either have to silently
clone (fallible, surprising for a copy constructor) or silently alias
(unsound), so `try_clone()` provides an explicit fallible deep copy
instead.

Which state a `cow<T>` starts in is selected by overload resolution, not a
tag type:

```cpp
std::string original = "hello";
reloco::cow<std::string> borrowed(original);            // const T&: borrowed
reloco::cow<std::string> owned(std::string("owned"));   // T&&: owned

assert(borrowed.is_borrowed());
assert(owned.is_owned());

auto &mutated = borrowed.to_mut(); // clones on first mutation, then owned
mutated += " world";
assert(borrowed.is_owned());
```

`get()`/`operator*`/`operator->` read through either state without
cloning. `to_mut()` clones-if-borrowed and returns a mutable reference,
transitioning `*this` to owned. `into_owned()` is rvalue-qualified and
consumes `*this`, returning a `T` by value (cloning only if still
borrowed). `try_clone()` performs an explicit deep copy regardless of
state.

The clone step itself is a customization point: `cow_traits<T>::try_clone`
defaults to forwarding to `construction_helpers::try_clone<T>` (the same
tiered `try_clone(alloc)`/`try_clone()`/copy-construct dispatch used
elsewhere in reloco), but can be specialized per-`T` to override how (or
whether) a borrowed value is cloned into an owned one:

```cpp
template <>
struct reloco::cow_traits<my_type> {
  static reloco::result<my_type> try_clone(reloco::allocator_ref alloc,
                                            const my_type &source) {
    return my_custom_clone(alloc, source);
  }
};
```

## `non_zero<T>`

`include/reloco/non_zero.hpp`

Rust `NonZeroU8`/`NonZeroI32`/... equivalent: wraps an integral `T` that is
statically known to never be `0`. The invariant is checked once, at
construction, rather than re-checked on every use, so it also documents
"this parameter is never zero" directly in a function signature. Follows
reloco's tri-tier construction convention: `try_create(value)` returns
`result<non_zero<T>>`, failing with `error::invalid_argument` for a `0`
argument; `unsafe_create(value)` is a debug-only-checked escape hatch for a
caller that has already proven the value is non-zero. `get()` and the
implicit conversion to `T` are always sound and `constexpr`.

```cpp
auto nz = reloco::non_zero<int>::try_create(4);
assert(nz.has_value());
int quotient = 100 / nz.value(); // implicit conversion to T
assert(!reloco::non_zero<int>::try_create(0).has_value());
```

## `int_ops.hpp`

`include/reloco/int_ops.hpp`

Free functions porting Rust's four explicit integer-arithmetic families to
`result<T>`/plain-`T` return types, plus a generic range-checked numeric
cast. No new class -- these mirror Rust's inherent integer methods
(`i32::checked_add`, `checked_div`, ...) directly, the same way
`wrapping<T>`/`saturating<T>` (below) build on top of them for
operator-overloaded newtypes:

- `checked_add/sub/mul/div/rem(a, b)` return `result<T>`, failing with
  `error::integer_overflow` (or `error::division_by_zero` for `div`/`rem`
  with a zero divisor) instead of invoking undefined behavior (signed) or
  silently wrapping (unsigned).
- `checked_neg(a)` returns `result<T>`, failing for signed
  `numeric_limits<T>::min()` and for any nonzero unsigned `T`.
- `checked_abs(a)` (signed `T` only) returns `result<T>`, failing for
  `numeric_limits<T>::min()`.
- `wrapping_add/sub/mul(a, b)` always return a `T`, with well-defined
  modulo-2^N wraparound (applied to signed types via a two's-complement
  bit-pattern reinterpretation, matching Rust's `wrapping_*`).
- `saturating_add/sub/mul(a, b)` always return a `T`, clamped to
  `[numeric_limits<T>::min(), numeric_limits<T>::max()]`.
- `overflowing_add/sub/mul(a, b)` always return an `overflowing_result<T>`
  (a `{T value; bool overflowed;}` pair).
- `checked_cast<To>(from)` converts an integral value to a different
  integral type `To`, failing with `error::integer_overflow` if it doesn't
  fit, matching Rust's `TryFrom`/`TryInto` for integers. Correct across
  every signed/unsigned and differing-width combination -- comparisons are
  widened to `intmax_t`/`uintmax_t` rather than narrowed into `From`/`To`
  directly, which would itself be able to overflow.

There is deliberately no `checked_div`/`checked_rem`/`checked_cast`
*operator* overload anywhere in reloco -- only `wrapping<T>`/`saturating<T>`
overload `+`/`-`/`*` (see below), since division and casting have no
sensible implicit wrap/saturate fallback.

```cpp
auto sum = reloco::checked_add<int32_t>(a, b);
if (!sum.has_value()) { /* handle reloco::error::integer_overflow */ }

auto q = reloco::checked_div<int>(10, 0);
assert(q.error() == reloco::error::division_by_zero);

auto narrowed = reloco::checked_cast<int8_t>(int32_t{300});
assert(!narrowed.has_value()); // doesn't fit in int8_t
```

Note: like `wrapping<T>`/`saturating<T>` below, every function here is
marked `constexpr` (always legal), but a call that goes through
`result<T>`'s `expected<T, error>` internals can't actually be *evaluated*
as a constant expression under C++17 specifically (`result<T>` isn't a
C++17 literal type) -- only C++20 and later can `static_assert` on the
outcome of e.g. `checked_add`.

## `wrapping<T>` / `saturating<T>`

`include/reloco/wrapping.hpp`, `include/reloco/saturating.hpp`

Rust `std::num::Wrapping<T>`/`std::num::Saturating<T>` equivalents: integral
newtypes whose `+`/`-`/`*`, unary `-`, and `++`/`--` operators always
resolve overflow a specific way, instead of relying on the caller
remembering to call the right free function from `int_ops.hpp` at every
arithmetic expression:

- `wrapping<T>` always wraps modulo-2^N (`wrapping_add`/`wrapping_sub`/
  `wrapping_mul`), exactly like plain unsigned arithmetic, but also for a
  signed `T` (avoiding the undefined behavior plain `T` arithmetic would
  invoke on signed overflow).
- `saturating<T>` always clamps to `[numeric_limits<T>::min(),
  numeric_limits<T>::max()]` (`saturating_add`/`saturating_sub`/
  `saturating_mul`) instead of overflowing.

Both get `get()` and an implicit conversion to `T` (so they compare/print
like a plain integer), a `std::hash` specialization, and are usable in a
`constexpr` context for values within range (an overflowing/saturating
operation internally goes through `checked_add`/`checked_sub`/
`checked_mul`'s `result<T>`, which isn't a literal type under C++17, so
only the non-overflowing, `get()`-only paths are guaranteed `constexpr`
under C++17 specifically). Neither type overloads `/`/`%` (division by
zero has no wrapping/saturating equivalent to fall back to) — use
`int_ops.hpp`'s free functions directly for division.

```cpp
reloco::wrapping<uint8_t> counter(250);
counter += reloco::wrapping<uint8_t>(10);
assert(counter.get() == 4); // wrapped, not UB or clamped

reloco::saturating<uint8_t> health(200);
health -= reloco::saturating<uint8_t>(255);
assert(health.get() == 0); // clamped to the type's minimum
```

## `checked<T>`

`include/reloco/checked.hpp`

A third integral newtype alongside `wrapping<T>`/`saturating<T>`, for
arithmetic that has no defined "always succeeds" answer worth baking into
an operator overload. `checked<T>` wraps a `T` and exposes
`try_add`/`try_sub`/`try_mul`/`try_div`/`try_rem`/`try_neg`/`try_abs`
(mirroring `int_ops.hpp`'s `checked_*` free functions), each returning
`result<checked<T>>` instead of a plain `checked<T>` -- deliberately named
methods rather than operator overloads, so a caller can't silently drop
the failure the way `a + b` discarding a `result<T>` return value would
(reloco's `[[nodiscard]]` on `expected<T, E>` still catches that mistake,
but a named `try_*` method reads as fallible at the call site the same way
every other `try_*` operation in reloco does). `try_div`/`try_rem` fail
with `error::division_by_zero` for a zero divisor; `try_abs` is signed-`T`
only, matching Rust (which has no `checked_abs` for unsigned integers).

```cpp
reloco::checked<int32_t> a(std::numeric_limits<int32_t>::max());
auto sum = a.try_add(reloco::checked<int32_t>(1));
assert(!sum.has_value());
assert(sum.error() == reloco::error::integer_overflow);

auto chained = reloco::checked<int>(10).try_div(reloco::checked<int>(2)).and_then(
    [](reloco::checked<int> half) { return half.try_mul(reloco::checked<int>(3)); });
assert(chained.value().get() == 15);
```

## `allocator_ref` / `allocator<Tag>` / `allocator_traits<Tag>`

`include/reloco/allocator.hpp`

Type-erased, two-word handle (`allocator_ref`) to an allocator backend
described by a `Tag` + `allocator_traits<Tag>` specialization — no virtual
base class, no vtable-carrying inheritance. Every operation
(`allocate`/`deallocate`/`expand_in_place`/`reallocate`/`advise`) returns
`reloco::result<T>` and is `RELOCO_UNSAFE_BUFFER_USAGE`-gated (raw sized
pointers, no bounds-tracked alternative). See
[Extending reloco](extending.md) for how to add a new backend.

## `heap_allocator_tag` / `default_allocator()`

`include/reloco/heap_allocator.hpp`, `include/reloco/default_allocator.hpp`

`heap_allocator_tag` is the stateless, built-in backend over the process
heap. `default_allocator()` is the process-wide default `allocator_ref`
(similar to Rust's `#[global_allocator]`): out of the box it returns the
heap backend, but an application can replace it wholesale by defining
`RELOCO_DEFAULT_ALLOCATOR_CUSTOM` — see the header's own documentation for
the exact override recipe (a customization-header include cycle makes it
slightly more involved than a plain `reloco_user_config.hpp` define).

`reloco_global_alloc` is `RELOCO_EXPORT`-annotated so a custom hook's
function-local `static` state stays one shared instance across a
`-fvisibility=hidden` shared-library boundary — see the
[`type_id`](#type_id--type_id_oft) note above and `RELOCO_ENABLE_EXPORT` in
`reloco_config.hpp`; the built-in, stateless heap-backed default has no
such state to share, so this only matters for a custom hook shared across
that boundary.

## Fallible construction: `concepts.hpp` / `construction_helpers.hpp`

`include/reloco/concepts.hpp`, `include/reloco/construction_helpers.hpp`

`has_try_create_v<T, Args...>`, `has_try_allocate_v<T, Args...>`,
`has_try_construct_v<T, Args...>`, `has_try_clone_v<T>` (and its
`_allocator_aware`/`_self_contained` halves), and `has_try_clone_at_v<T>`
detect which of the five fallible-construction functions a type implements
(plus matching C++20 `concept`s). `construction_helpers::try_construct`/
`try_allocate`/`try_clone`/`try_clone_at` pick the most efficient available
strategy at compile time so generic code never hand-writes the `if
constexpr` dispatch itself. See
[Fallible construction](fallible-construction.md) for the full protocol,
and `unique_ptr.hpp`/`string.hpp` for two complete, real-world examples.

## `fallible_singleton<T>` / `atomic_fallible_singleton<T, LockTraits>`

`include/reloco/fallible_singleton.hpp`

Lazily, fallibly initialized singletons built on
`construction_helpers::try_construct`, avoiding the static initialization
order fiasco for globals with non-trivial, potentially-failing setup.
`fallible_singleton<T>::instance()` (or `instance(allocator_ref)`)
constructs `T` in static storage on first call and returns the same `T *`
thereafter; it is **not thread-safe**. `atomic_fallible_singleton<T,
LockTraits>::instance(lock)` is the thread-safe counterpart, guarded by a
caller-supplied `LockTraits::lock_type &` (any type satisfying
`has_lock_traits_v<LockTraits>`/the C++20 `lock_traits` concept) only while
initialization hasn't completed yet. See
[Fallible construction](fallible-construction.md#lazy-singletons-fallible_singleton-atomic_fallible_singleton)
for the full explanation. `LockTraits::lock_type` can be any of the
`mutex`/`recursive_mutex`/`error_checking_mutex` types below (via a thin
adapter satisfying `has_lock_traits_v`, since it requires `static void
lock(lock_type &)`/`static void unlock(lock_type &)` free functions, not
member functions) or an application's own mutex/spinlock.

Both classes are `RELOCO_EXPORT`-annotated so their storage stays one
shared, process-wide instance per `T` even across a `-fvisibility=hidden`
shared-library boundary -- see the [`type_id`](#type_id--type_id_oft) note
above and `RELOCO_ENABLE_EXPORT` in `reloco_config.hpp`; opt in with that
macro if `instance()` for a shared `T` is called from more than one shared
object.

## `mutex` / `recursive_mutex` / `error_checking_mutex` / `shared_mutex` / `condition_variable`

`include/reloco/mutex.hpp`

Backend-selectable synchronization primitives. `mutex`/`recursive_mutex`/
`shared_mutex` have an infallible `void lock()`/`unlock()` shape (`void
lock_shared()`/`unlock_shared()` for `shared_mutex`): any underlying OS/library
failure indicates a programming error (relocking a non-recursive mutex
already held by the calling thread, unlocking one not held, ...) -- undefined
behavior per the standard in the first place -- and is reported via
`RELOCO_ASSERT` rather than a `result<void>`, so they drop in cleanly as
`std::unique_lock<T>`'s `Mutex` template parameter (whose destructor calls
`unlock()` unconditionally, with no way to check a return value). Plus
`[[nodiscard]] bool try_lock()` and a `native_handle()` escape hatch.
Two built-in backends are selected automatically -- `RELOCO_MUTEX_BACKEND_PTHREAD`
(POSIX `pthread_mutex_t`/`pthread_rwlock_t`/`pthread_cond_t`) when
`<pthread.h>` is available, else `RELOCO_MUTEX_BACKEND_STD`
(`std::mutex`/`std::shared_mutex`/`std::condition_variable`, which covers
Windows out of the box) -- or forced by defining exactly one of them in
`reloco_user_config.hpp`. `error_checking_mutex` is the deliberate exception:
implemented once, generically, on top of whichever `mutex` backend is active
plus an `std::atomic<std::thread::id>` owner tag, so it's fully portable (no
glibc-specific `PTHREAD_MUTEX_ERRORCHECK`/`_NP` initializer macros), its
`[[nodiscard]] result<void> lock()` returns `error::deadlock` when the
calling thread already owns it, `unlock()` returns `error::invalid_owner`
from a non-owning thread, and `try_lock()` returns `false` (not an error)
when self-owned -- turning exactly the misuses that `mutex` asserts on into
a reportable, recoverable error instead.

Under `-fno-exceptions`, the `RELOCO_MUTEX_BACKEND_STD` backend's internal
`std::system_error` translation is compiled out (detected via the
`RELOCO_HAS_EXCEPTIONS` macro in `detail/compat.hpp`): the underlying
`std::mutex`/`std::shared_mutex` calls are made unguarded instead, since
`try`/`catch` isn't valid syntax in that mode and the standard library's own
throwing paths become terminating calls anyway. `RELOCO_MUTEX_BACKEND_PTHREAD`
is unaffected -- POSIX mutex calls never throw.

Defining `RELOCO_MUTEX_BACKEND_CUSTOM` suppresses both built-in backends
(including the generic `error_checking_mutex`) entirely: the application
must then supply its own `mutex`/`recursive_mutex`/`error_checking_mutex`/
`shared_mutex`/`condition_variable` matching the same public API, e.g. for
Win32 `SRWLOCK`/`CRITICAL_SECTION`/`CONDITION_VARIABLE` or an RTOS's native
primitives (not ported here by design -- see the header's file-level doc
comment). See `RELOCO_MUTEX_BACKEND_STD`/`_PTHREAD`/`_CUSTOM` in
`reloco_config.hpp` for the exact selection mechanism.

`mutex`/`recursive_mutex`/`error_checking_mutex`/`shared_mutex` (both
backends) are annotated for
[Clang Thread Safety Analysis](https://clang.llvm.org/docs/ThreadSafetyAnalysis.html)
via the `RELOCO_CAPABILITY`/`RELOCO_ACQUIRE`/`RELOCO_RELEASE`/
`RELOCO_TRY_ACQUIRE` (and `_SHARED` variants) macros in `detail/compat.hpp`:
building with `-Wthread-safety` on Clang statically flags mismatched
lock/unlock pairs (e.g. a `lock()` with no matching `unlock()` on some
path, or an `unlock()` on a mutex not currently held) at compile time. On
GCC/MSVC, `RELOCO_HAS_ATTRIBUTE` reports these attributes as unsupported,
so the macros expand to nothing there -- zero cost, zero portability
impact. `detail/compat.hpp` also exposes `RELOCO_GUARDED_BY`/
`RELOCO_PT_GUARDED_BY`/`RELOCO_REQUIRES`/`RELOCO_REQUIRES_SHARED`/
`RELOCO_LOCKS_EXCLUDED`/`RELOCO_ASSERT_CAPABILITY`/
`RELOCO_ASSERT_SHARED_CAPABILITY`/`RELOCO_SCOPED_CAPABILITY`/
`RELOCO_NO_THREAD_SAFETY_ANALYSIS` for annotating application code that
guards its own data with these mutex types.

## `guarded_mutex<T, MutexT = mutex>`

`include/reloco/guarded_mutex.hpp`

A mutex that owns the value it protects, matching Rust's
`std::sync::Mutex<T>`. `mutex`/`recursive_mutex`/`shared_mutex` above
protect nothing by themselves -- there is nothing stopping code from
touching a separately-declared guarded variable without holding the lock
at all. `guarded_mutex<T>` closes that gap: the `T` lives inside the
`guarded_mutex<T>` itself, and the only way to reach it is through the
RAII `guard` returned by `lock()`/`try_lock()`.

```cpp
reloco::guarded_mutex<int> counter(0);
{
  auto g = counter.lock(); // blocks until acquired
  *g += 1;
} // lock released automatically here

auto g = counter.try_lock(); // result<guard>, fails with error::busy if held
if (g)
  **g += 1;
```

`lock()` blocks until acquired and returns a `guard` (the checked tier: it
cannot fail, matching Rust's own `Mutex::lock()` once poisoning is
disregarded, which reloco has no equivalent of since it never unwinds
through a held lock). `try_lock()` is the fallible tier, returning
`result<guard>` and failing with `error::busy` if already held elsewhere
-- matching Rust's own `Mutex::try_lock() -> Result<MutexGuard<T>,
TryLockError<...>>` more closely than an `optional<guard>` would (and
sidesteps `optional<T>`'s Clang consumed-state typestate tracking, which
does not mix with a guard's branching acquire-or-fail control flow).
`get_mut()` bypasses locking entirely for callers that already hold an
exclusive `guarded_mutex&` (Rust's `Mutex::get_mut()`, which borrows `&mut
self` at compile time instead). `guard` is move-only and releases the
lock automatically on destruction, matching Rust's `MutexGuard<'a, T>`.

The lock backend is a template parameter (`MutexT = mutex` by default);
any type providing `lock()`/`unlock()`/`try_lock()` with the same shape as
`reloco::mutex` works, including `recursive_mutex`. Only exclusive access
is modeled -- `shared_mutex`'s `lock_shared()`/`unlock_shared()` are not
exposed through `guarded_mutex<T>`; use `shared_mutex` directly for
reader/writer locking without an owned value. This is the thread-safe
counterpart of [`cell<T>`/`ref_cell<T>`](#celltref_cellt) above, which are
`!Sync`-equivalent (single-threaded only) by design.

## `is_trivially_relocatable<T>`

`include/reloco/relocatable.hpp`

Customization-point trait marking a type whose object representation can be
relocated by copying its bytes to a new address and abandoning the old one,
without running a move constructor or destructor at either address. See
[Trivial relocation](relocatable.md) for the full explanation and the
built-in specializations (`unique_ptr<T>`, `basic_string<CharT, TraitsT>`,
`checked_value<T>`/`checked_value<T *>`).

`include/reloco/relocatable_std.hpp` is a separate, opt-in header adding
specializations for `std::pair<T1, T2>`, `std::tuple<Ts...>`,
`std::optional<T>`, and `std::variant<Ts...>`, each forwarding to the
relocatability of their contained type(s) -- see
[Trivial relocation](relocatable.md#standard-library-wrapper-types-relocatable_stdhpp).

## `is_send<T>` / `is_sync<T>`

`include/reloco/send_sync.hpp`

Customization-point traits matching Rust's `Send`/`Sync` auto traits:
`is_send<T>` asks whether it is sound to transfer ownership of a `T` to
another thread, `is_sync<T>` whether it is sound to share a `T` across
threads through a `const T &`. Both default to `true`; reloco specializes
both to `false` (unconditionally) for `rc<T>`/`weak_rc<T>` (non-atomic
refcount), specializes `is_sync` to `false` (`is_send` forwarding to `T`)
for `cell<T>`/`ref_cell<T>` (unsynchronized interior mutability), and
specializes both to `is_send<T> && is_sync<T>` for `shared_ptr<T>`/
`weak_ptr<T>` (atomic refcount, but the shared `T` still needs to tolerate
concurrent access). `guarded_mutex<T, MutexT>` (see `guarded_mutex.hpp`)
`static_assert`s `is_send_v<T>`, matching Rust's `Mutex<T: Send>` bound. See
[Thread-transfer/-sharing safety](send-sync.md) for the full rationale and
table.

## `thread` / `thread::spawn` / `join_handle<R>`

`include/reloco/thread.hpp`

An OS thread primitive with the same backend-selection shape as
`mutex.hpp`: `RELOCO_THREAD_BACKEND_PTHREAD` (wraps `<pthread.h>`
directly) or `RELOCO_THREAD_BACKEND_STD` (wraps `<thread>`), auto-selected
via `RELOCO_HAS_INCLUDE(<pthread.h>)`, or `RELOCO_THREAD_BACKEND_CUSTOM` to
suppress both -- for an application/kernel supplying its own
`thread`/`thread_id`/`this_thread::get_id()`/`this_thread::yield()`
matching the same public surface (an RTOS task API, a freestanding target,
...).

`thread::try_spawn(function<void()> &&, allocator_ref)` is the raw,
fallible primitive (`result<thread>`, failing with
`error::resource_exhausted` if the OS refuses to create a new thread).
Move-only; the destructor/move-assignment `RELOCO_ASSERT`s if still
joinable, matching `std::thread`'s "must join or detach first" contract
as an assertion trap instead of an unconditional `std::terminate`.

```cpp
auto handle = reloco::spawn([]() noexcept -> int {
  return 42;
});
if (handle) {
  int result = std::move(*handle).join(); // blocks, returns 42
}
```

`spawn(F &&, allocator_ref = default_allocator())` is the Rust-facing
layer matching `std::thread::spawn`/`JoinHandle<T>`, built generically on
top of `thread` + `function<void()>` (see `function.hpp`) so it needs no
backend-specific code of its own. It `static_assert`s that `F` (and
everything it captures) is `is_send_v`, and that `F`'s return type is too
(see [`is_send<T>` / `is_sync<T>`](#is_sendtis_synct) above) --
matching Rust's `F: Send + 'static, F::Output: Send` bound (reloco has no
lifetime tracking to enforce the `'static` half). Unlike Rust (where
dropping a `JoinHandle` silently detaches the thread), `join_handle<R>`'s
destructor blocks and joins if still joinable, matching C++20
`std::jthread`'s safer default; call `detach()` explicitly to opt in to
Rust's original behavior.

## `channel<T>` / `sender<T>` / `receiver<T>`

`include/reloco/channel.hpp`

A multi-producer, single-consumer channel matching Rust's
`std::sync::mpsc::channel`. `channel<T>(allocator_ref =
default_allocator())` returns a `result<std::pair<sender<T>, receiver<T>>>`
sharing one heap-allocated, intrusively-linked-list queue guarded by one
`mutex` + `condition_variable` (see `mutex.hpp`), kept alive by an
atomically-refcounted `shared_ptr` (not `rc<T>`, which is deliberately
`!Send`/`!Sync` -- see `send_sync.hpp`).

```cpp
auto ends = reloco::channel<int>();
if (ends) {
  auto &[tx, rx] = *ends;
  tx.try_send(42);              // result<void>
  auto value = rx.recv();       // blocks; result<int>
}
```

`sender<T>` is `Clone`-like via an ordinary copy constructor (matching
`rc<T>`/`shared_ptr<T>`'s own copy-is-clone convention, see above) -- each
clone increments a shared count and may call `try_send` from any thread,
independently. `try_send(T)` never blocks the sender: it fails with the
allocator's own error if the node allocation fails, or
`error::invalid_state` if the receiver has already been dropped (matching
Rust's `SendError<T>`, minus recovering the un-sent value -- reloco's
single `error` enum carries no payload).

`receiver<T>` is move-only: exactly one consumer is ever meant to call
`recv()`/`try_recv()`. `recv()` blocks until a value is sent or every
`sender<T>` clone has been dropped, failing with `error::container_empty`
in the latter case (matching Rust's `RecvError`). `try_recv()` never
blocks: it fails with `error::try_again` if the queue is momentarily empty
but at least one sender remains (matching `TryRecvError::Empty`), or
`error::container_empty` if it is empty and every sender has already been
dropped (matching `TryRecvError::Disconnected`).

`T` must be `std::is_nothrow_move_constructible_v`, like every other
reloco container element requirement. `is_send<sender<T>>`/
`is_send<receiver<T>>` forward to `is_send<T>`; `is_sync<sender<T>>` is
`is_send<T>` too (every access is fully mutex-guarded, matching current
Rust where `mpsc::Sender<T>: Sync` when `T: Send`); `is_sync<receiver<T>>`
is always `false`, deliberately matching Rust's single-consumer API
contract rather than the implementation's own (looser) actual guarantee.

## `once_lock<T>`

`include/reloco/once_lock.hpp`

A cell that can be written at most once and read many times after that,
matching Rust's `std::sync::OnceLock<T>`. Unlike `fallible_singleton<T>`/
`atomic_fallible_singleton<T, LockTraits>` (`fallible_singleton.hpp`),
which each provide exactly one static, process-wide instance per `T`,
`once_lock<T>` is an ordinary value type -- usable as a struct field, a
local, or a container element -- so a program can have as many
independently-initialized cells as it needs.

```cpp
reloco::once_lock<reloco::string> config_path;

// From any thread, any number of times:
auto entry = config_path.get_or_try_init([]() -> reloco::result<reloco::string> {
  return load_config_path(); // returns result<string>
});
if (entry)
  use_path(**entry);
```

- `try_set(T)` -> `result<void>`: fails with `error::already_exists` if
  the cell is already initialized (matching Rust's `OnceLock::set`, minus
  recovering the rejected value -- reloco's single `error` enum carries no
  payload).
- `get_or_try_init(F)` -> `result<T *>`, where `F` is invocable as
  `result<T>()`: returns the existing value if already initialized,
  otherwise blocks concurrent callers while exactly one of them runs `F`
  and stores its result. If `F` fails, the cell reverts to empty so a
  later call (from any thread) may retry, matching Rust's
  `OnceLock::get_or_try_init`.
- `get()`/`get_mut()` -> `const T *`/`T *`: `nullptr` if not yet
  initialized, never blocking.
- `take()` -> `result<T>`: resets the cell to empty and returns the
  previous value, failing with `error::not_initialized` if the cell was
  already empty (matching Rust's `OnceLock::take(&mut self)`, which
  returns `Option<T>` -- reloco represents "nothing to take" via the
  file's own error case instead, consistent with every other fallible
  reloco operation). Like Rust's `&mut self` requirement, the caller must
  ensure no other thread concurrently reads/writes the cell.

Internally, an `std::atomic<int>` state gives `get()`/`get_mut()`, and the
fast path of `try_set`/`get_or_try_init`, a lock-free acquire-load once
initialized; the slow path is serialized by one `mutex` +
`condition_variable` pair (see `mutex.hpp`), matching `channel.hpp`'s own
locking approach. `T` must be `std::is_nothrow_move_constructible_v`, like
every other reloco container element requirement. `once_lock<T>` is
neither copyable nor movable (it embeds a `mutex` + `condition_variable`).

`is_send<once_lock<T>>` forwards to `is_send<T>`. `is_sync<once_lock<T>>`
requires both `is_send<T>` and `is_sync<T>`, matching Rust's `unsafe impl
<T: Send + Sync> Sync for OnceLock<T>`: unlike `guarded_mutex<T>`
(`Mutex<T>`, only ever reached through an exclusive lock), a `const
once_lock<T> &` hands out a bare `const T *` once ready, so concurrent
readers need `T` itself to tolerate concurrent shared access.

## `scope` / `thread_scope` / `scoped_join_handle<R>`

`include/reloco/scope.hpp`

Matches Rust's `std::thread::scope`: runs a closure that may spawn
threads borrowing references to the caller's own stack frame, guaranteed
to have all finished running before `scope()` itself returns. `thread.hpp`'s
`spawn(F, allocator_ref)` requires `F: Send + 'static` (documented, not
statically enforced -- reloco has no lifetime tracking, so nothing stops
a captured reference from actually outliving the spawned thread if the
caller gets it wrong); `scope()` closes that gap the same way Rust does.

```cpp
int local = 0;
auto result = reloco::scope([&](reloco::thread_scope &s) {
  auto handle = s.spawn([&local]() noexcept { local = 42; });
  if (handle)
    std::move(*handle).join();
});
// result: result<void>; local == 42 here, guaranteed.
```

- `reloco::scope(body, allocator_ref)` -> `result<R>`, where `R` is
  `body`'s own return type (invoked as `body(thread_scope &)`): fails
  only if allocating the internal shared completion-tracking state
  fails; `body` is otherwise invoked unconditionally and its result is
  forwarded as-is.
- `thread_scope::spawn(F, allocator_ref)` -> `result<scoped_join_handle<R>>`,
  where `R = std::invoke_result_t<F &>`, matching Rust's `Scope::spawn`.
  `F` (and everything it captures) must be `is_send_v` (see
  `send_sync.hpp`), same as `reloco::spawn` -- but, unlike
  `reloco::spawn`, `F` may capture a reference to any value that outlives
  the enclosing `scope()` call, since every thread spawned through a
  `thread_scope` is guaranteed to have finished before `scope()` returns.
- `scoped_join_handle<R>::join() &&` blocks until the thread finishes and
  returns its result (consuming `*this`), matching Rust's
  `ScopedJoinHandle::join()`. A handle the caller never explicitly joins
  is still safely joined on its own destruction, exactly like
  `join_handle<R>` (see `thread.hpp`).

Internally, `scope()` allocates one shared, atomically-refcounted
completion counter (guarded by a `mutex` + `condition_variable` pair, see
`mutex.hpp`) that every `thread_scope::spawn()` call increments before
handing its closure to `reloco::spawn()`, and decrements (waking up
`~thread_scope()`, which blocks until it reaches zero) right after the
closure returns, still running on the spawned thread. This matches Rust's
own `std::thread::scope` implementation, which also does not literally
join every spawned thread to know when it is safe to return, just waits
for this kind of completion signal -- the OS-level join
`scoped_join_handle<R>` performs is a separate, purely
resource-reclamation concern from this borrow-safety guarantee.

`is_send<scoped_join_handle<R>>` forwards to `is_send<R>`;
`is_sync<scoped_join_handle<R>>` is always `true`, both matching
`join_handle<R>`'s own specializations.

## `alignment_of<T>`

`include/reloco/alignment.hpp`

Customization-point trait controlling the allocation/storage alignment
`vector<T>`, `array<T, N>`, and `inline_vector<T, Capacity>` request for `T`,
independent of `alignof(T)`. Defaults to `alignof(T)`; specialize it (once,
per element type, like `is_trivially_relocatable<T>`) to request SIMD-width
over-alignment (e.g. 32 bytes for AVX) without redeclaring `T` itself with
`alignas(...)`. `effective_alignment_v<T>` is `std::max(alignof(T),
alignment_of_v<T>)` -- the value containers actually use -- and never lets a
specialization weaken alignment below `alignof(T)`. See
[Over-alignment](alignment.md).

## Lifetime and safety annotation macros

`include/reloco/lifetime.hpp`, `include/reloco/rvalue_safety.hpp`

Compiler-feature-detected macros used throughout the library:
`RELOCO_LIFETIMEBOUND` (Clang `[[clang::lifetimebound]]`),
`RELOCO_OWNER`/`RELOCO_POINTER` (GSL owner/pointer annotations for static
analyzers), `RELOCO_UNSAFE_BUFFER_USAGE`/`RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/
`RELOCO_END_UNSAFE_BUFFER_USAGE` (Clang `-Wunsafe-buffer-usage` opt-in
gating), and `RELOCO_BLOCK_RVALUE_ACCESS(Type)` (deletes a container's
rvalue accessors so a borrow can never outlive a temporary). See
[Lifetime safety](lifetime-safety.md).

## `reloco_config.hpp`

`include/reloco/reloco_config.hpp`

The library-wide user-override mechanism: define
`RELOCO_HAS_USER_CONFIG`/create `reloco_user_config.hpp` on the include path
to override compile-time defaults (e.g. `RELOCO_DEFAULT_ALLOCATOR_CUSTOM`,
assertion-handling behavior) before any other reloco header is processed.
See the header's own documentation for the exact mechanism and its
constraints (why it cannot itself include headers like `allocator.hpp`).
