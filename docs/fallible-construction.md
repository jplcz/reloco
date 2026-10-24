<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Fallible construction: `try_create`, `try_allocate`, `try_construct`, `try_clone`

Ordinary C++ construction has no room for failure that isn't an exception or
undefined behavior: a constructor either succeeds or throws, and there is no
standard way to report "allocation failed" or "the requested state is
invalid" from one without unwinding. That is unusable on the same class of
targets `reloco` targets everywhere else — firmware, RTOS tasks, interrupt
paths, kernel-adjacent code — so `reloco` never puts fallible work inside a
constructor. Instead, every type that can fail to come into existence
exposes one (or more) of five static or member functions, and reports the
outcome through `reloco::result<T>` (`reloco::expected<T, reloco::error>`,
see [Hardened containers](hardened-containers.md) and `error.hpp`) instead
of throwing. `reloco::error` is the one error enum every fallible reloco
operation uses; no type defines its own scoped error enum.

`include/reloco/concepts.hpp` defines a `has_try_*_v<T, ...>` compile-time
detection trait for each of these functions (plus a matching C++20
`concept` of the same name, when available), so generic code — containers,
factory helpers, test fixtures — can check which construction protocol a
type supports and dispatch accordingly, without every type needing to
inherit from a common base or register itself anywhere.

| Function | Detected by | Use when |
|---|---|---|
| `T::try_create(Args...)` | `has_try_create_v<T, Args...>` | The type can use the process-wide `reloco::default_allocator()` (or needs no allocator at all) |
| `T::try_allocate(reloco::allocator_ref, Args...)` | `has_try_allocate_v<T, Args...>` | The caller must control which allocator backs the new instance |
| `storage->try_construct(Args...)` | `has_try_construct_v<T, Args...>` | Two-phase, in-place construction into caller-owned storage |
| `source.try_clone(reloco::allocator_ref)` / `source.try_clone()` | `has_try_clone_v<T>` | An existing instance needs an independent, fallible copy |
| `T::try_clone_at(reloco::allocator_ref, T *storage, const T &source)` | `has_try_clone_at_v<T>` | An optimized in-place clone into caller-owned storage |

`vector<T>` and `string` follow the protocol with these extra factories, which are not part of the detected set:
`vector<T>::try_create(cap)` only *reserves* capacity (`size() == 0`, and `data()` asserts on an empty vector), while
`vector<T>::try_create_sized(count)` / `try_allocate_sized(alloc, count)` return `count` value-initialized elements,
ready to be filled in place (for example by a C API writing into `data()`):

```cpp
// A buffer of exactly `n` zeroed bytes for a C read function to fill in; fails with an error instead of throwing.
auto buffer = reloco::vector<std::byte>::try_create_sized(n);
if (!buffer) { return reloco::unexpected(buffer.error()); }
read_into(buffer->data(), buffer->size()); // n == 0 would trip data()'s non-empty assertion, so check it first
```

### Converting between views and owners

Borrowed types convert to their owning counterparts with a fallible copy. The conversion is a member of the *view*, but
it is only instantiated where you call it, so the owner's header must be included there (the view headers only
forward-declare the owner, which keeps them free of include cycles):

| From | To | Call | Include |
|---|---|---|---|
| `string_view` | `string` | `sv.try_to_string()` / `sv.try_to_string(alloc)` | `string.hpp` |
| `span<T>` / `span<const T>` | `vector<T>` | `sp.try_to_vector()` / `sp.try_to_vector(alloc)` | `vector.hpp` |
| `span<const T>` | any existing `vector`, `sso_vector`, `inline_vector`, `outline_vector` | `v.try_append(sp)`, `v.try_assign(sp)` | the container's header |
| `span<const T>` | new `vector`, `sso_vector`, `inline_vector`, `secure_vector`, `boxed_slice` | `C::try_create_from(sp)` / `C::try_allocate_from(alloc, sp)` (`inline_vector` has no allocator: only `try_create_from`) | the container's header |
| `span<T>` | any of the above | `sp.try_to_vector<C>()` (e.g. `sp.try_to_vector<reloco::inline_vector<int, 8>>()`) | the container's header |
| `inline_vector` / `inline_string` | heap `vector` / `string` | `v.try_to_vector()`, `s.try_to_string()` | (already existed) |

```cpp
#include <reloco/string.hpp>
#include <reloco/vector.hpp>

// Own a copy of a borrowed name so it outlives the buffer it was parsed from.
reloco::result<reloco::string> keep(reloco::string_view name) noexcept { return name.try_to_string(); }

// Append bytes received from a C API. The span may even point into `buf` itself; that case is handled.
reloco::result<void> add(reloco::vector<std::byte> &buf, reloco::span<const std::byte> chunk) noexcept {
  return buf.try_append(chunk); // on allocation failure `buf` is unchanged
}
```

`try_append` reserves capacity first, so on failure (allocation failure, or `error::capacity_exceeded` for
`inline_vector`/`outline_vector`) the container is unchanged; `try_assign` does the same. Both accept a span aliasing the
container's own elements, even across an `sso_vector` spilling from inline to heap storage. They require a
copy-constructible element type. The deque family (`vec_deque` and friends) does not have these yet.

This is a convention, not an interface: implement whichever of the five
functions make sense for a given type (most types only need one or two), and
generic code that wants to work with any of them checks for each function
with the matching trait. Ported from `reloco_legacy`'s `concepts.hpp`, with
two changes to fit this repository's conventions (see below). Rather than
hand-writing the `if constexpr` dispatch every time, most code should reach
for `reloco::construction_helpers` (see below and
`include/reloco/construction_helpers.hpp`), which already implements it once
for every type that follows this protocol; `reloco::unique_ptr<T>` (see
`unique_ptr.hpp`) is a small, complete example of a type built entirely on
top of it. `reloco::string` (see `string.hpp`) is a real-world example that
implements the protocol itself -- `try_create`, `try_allocate`, `try_clone`,
and `try_clone_at` -- rather than consuming it, which is why
`reloco::unique_ptr<reloco::string>::try_create(...)` composes automatically
through `has_try_create_v`.

## 1. `try_create`: the default-allocator factory

```cpp
struct widget {
  int value;

  static reloco::result<widget> try_create(int v) noexcept {
    if (v < 0)
      return reloco::unexpected(reloco::error::invalid_argument);
    return widget{v};
  }
};

reloco::result<widget> made = widget::try_create(3);
```

Implement `try_create` when a type can be instantiated without the caller
specifying an allocator, typically because it delegates internally to
`reloco::default_allocator()` (see [Extending reloco](extending.md)) or
because it needs no allocation at all. This is the common case for
application code that has not opted into explicit allocator control.

## 2. `try_allocate`: the explicit-allocator factory

```cpp
static reloco::result<widget>
try_allocate(reloco::allocator_ref alloc, int v) noexcept {
  if (v < 0)
    return reloco::unexpected(reloco::error::invalid_argument);
  // ... use `alloc` for any nested allocation the type needs ...
  return widget{v};
}
```

Implement `try_allocate` instead of (or alongside) `try_create` when memory
residency must be strictly controlled by the caller — kernel subsystems
choosing between paged and non-paged pools, an arena scoped to a single
request, a caller-supplied test allocator that tracks leaks. `alloc` is a
`reloco::allocator_ref`, the type-erased handle from
[Extending reloco](extending.md); it is a cheap, non-owning two-word value,
so take it by value.

## 3. `try_construct`: two-phase in-place construction

```cpp
struct handle {
  int fd = -1; // noexcept-default-constructible "shell" state

  reloco::result<void> try_construct(const char *path) noexcept {
    fd = open_device(path);
    if (fd < 0)
      return reloco::unexpected(reloco::error::invalid_argument);
    return {};
  }
};

alignas(handle) unsigned char storage[sizeof(handle)];
auto *h = new (storage) handle(); // noexcept shell construction
if (auto result = h->try_construct("/dev/example"); !result) {
  h->~handle(); // caller destroys the shell before reclaiming storage
  report(result.error());
}
```

Implement `try_construct` for zero-copy initialization directly into
caller-owned storage: placement-new a `noexcept`-default-constructible
"shell" object first, then call `try_construct` to perform the fallible part
(resource acquisition, validation). **If `try_construct` fails, the caller
is responsible for invoking the destructor on the shell object** before
reclaiming the raw memory — the shell is a live object from the moment it
is placement-newed, whether or not `try_construct` later succeeds.

## 4. `try_clone`: fallible copying

```cpp
// Allocator-aware: for types owning allocator-backed nested resources.
reloco::result<widget> try_clone(reloco::allocator_ref alloc) const noexcept;

// Self-contained: for types with no allocator dependency.
reloco::result<widget> try_clone() const noexcept;
```

Implement one of these two shapes — never both — when a type needs an
independent copy that can fail (allocation failure, a resource that cannot
be duplicated). `has_try_clone_v<T>` is satisfied by either shape, so
generic code does not need to know which one a given type chose; if generic
code specifically needs to tell them apart (as `construction_helpers::
try_clone` does, to know whether to pass an allocator), it checks
`has_try_clone_allocator_aware_v<T>`/`has_try_clone_self_contained_v<T>`
individually instead.

## 5. `try_clone_at`: optimized in-place clone

```cpp
static reloco::result<void>
try_clone_at(reloco::allocator_ref alloc, widget *storage, const widget &source) noexcept {
  new (storage) widget{source.value};
  return {};
}
```

Implement `try_clone_at` alongside (or instead of) `try_clone` when cloning
directly into caller-owned, uninitialized storage avoids a move that
`try_clone` would otherwise require — the same in-place-construction
tradeoff `try_construct` makes for first-time initialization.

## Dispatching generically over the protocol

Generic code that wants to construct or clone a type without knowing in
advance which functions it implements checks the traits with
`if constexpr`. `reloco::construction_helpers` (see
`include/reloco/construction_helpers.hpp`) already does this once, choosing
the most efficient available tier for a given `T`:

```cpp
// Picks try_allocate, falling back to try_create, falling back to a plain
// noexcept constructor call -- whichever T actually implements.
reloco::result<widget> made =
    reloco::construction_helpers::try_allocate<widget>(alloc, 3);

// Two-phase, in-place construction into caller-owned storage: picks
// T::try_construct if available, otherwise placement-news T directly.
reloco::result<void> constructed =
    reloco::construction_helpers::try_construct<widget>(alloc, storage, 3);

// Fallible copy, dispatching to try_clone/try_clone_at (allocator-aware or
// self-contained, whichever T implements) or a plain copy constructor.
reloco::result<widget> cloned = reloco::construction_helpers::try_clone(alloc, source);
```

Under C++20, the underlying traits are also available as real `concept`s
(`reloco::has_try_create<T, Args...>`, etc.), usable directly in a
`template` constraint or `requires` clause instead of `if constexpr`:

```cpp
template <reloco::has_try_clone T>
void register_clonable_type(); // only compiles for T satisfying has_try_clone
```

Both forms check exactly the same thing; `include/reloco/concepts.hpp`
defines the `_v` trait once and derives the C++20 `concept` from it, so
there is no risk of the two disagreeing.

## Lazy singletons: `fallible_singleton`, `atomic_fallible_singleton`

`include/reloco/fallible_singleton.hpp` builds `reloco::construction_helpers
::try_construct` into two ready-made lazy-singleton wrappers, useful for
globals whose setup is non-trivial and fallible (so they cannot be plain
`static T instance;` globals) without reintroducing C++'s static
initialization order fiasco:

```cpp
struct config {
  static reloco::result<config> try_create() noexcept { /* parse, validate, ... */ }
};

// Not thread-safe: construct explicitly, once, from a single thread
// (e.g. early in main()) before any other thread can call instance().
reloco::result<config *> cfg = reloco::fallible_singleton<config>::instance();
```

`reloco::fallible_singleton<T>::instance()` (optionally
`instance(reloco::allocator_ref)`) constructs `T` in static storage on its
first call, via whichever `try_construct`/`try_allocate`/`try_create`/plain
nothrow tier `T` implements (exactly the tiers `construction_helpers`
already resolves), and returns the same `T *` on every later call. It is
**not thread-safe**: only use it from a single, controlled initialization
path.

`reloco::atomic_fallible_singleton<T>::instance()` is the thread-safe
counterpart: a `futex.hpp` `futex_word` state (`empty`/`initializing`/
`ready`) lets every thread skip locking once initialization has
completed (a lock-free acquire-load), falling back -- only for the
first, contended call -- to a `compare_exchange` claim of the `empty` ->
`initializing` transition. The single winner performs construction while
every other concurrent caller blocks via `futex_wait` until the winner
publishes the outcome and wakes them via `futex_wake_all`, exactly like
`once_lock<T>`'s slow path. If construction fails, the state reverts to
`empty` so a later call (from any thread) may retry. No lock is embedded
or caller-supplied: `futex.hpp` itself is the customization point for a
freestanding/kernel target with no `std::mutex` (via
`RELOCO_FUTEX_BACKEND_CUSTOM`, see `docs/futex.md`), so
`atomic_fallible_singleton` no longer needs its own separate escape
hatch for that case.

## Differences from `reloco_legacy`

`reloco_legacy/include/reloco/concepts.hpp` is the origin of this pattern,
but one thing changed to fit this repository's conventions: legacy took a
`fallible_allocator &`, a virtual base class. `reloco` has no virtual
allocator interface; `has_try_allocate_v`, `has_try_clone_v`, and
`has_try_clone_at_v` instead expect a `reloco::allocator_ref`, the
type-erased, non-virtual handle described in
[Extending reloco](extending.md).

Both legacy and `reloco` require every one of these functions to report its
outcome through the library's one error type — `reloco::error`/
`reloco::result<T>` here (see `error.hpp`) — rather than an arbitrary
per-type error enum. This is enforced by `concepts.hpp`'s detection traits
themselves: `has_try_create_v<T, Args...>` (and the other four) only holds
if `T::try_create(Args...)` returns exactly `reloco::result<T>`, so a type
that reports failure through some other type is treated as not implementing
the protocol at all.

`reloco_legacy/include/reloco/fallible_singleton.hpp` is the origin of
`fallible_singleton`/`atomic_fallible_singleton`. Legacy dispatched
construction through its own `is_fallible_initializable`/
`fallible_constructed` protocol — a `T::try_init(constructor_key)` member
gated by a friend-only key type, entirely separate from
`try_create`/`try_allocate`/`try_construct`. `reloco` instead builds
directly on `construction_helpers::try_construct`, so any `T` that already
works with `unique_ptr<T>` or the other helpers above works with
`fallible_singleton`/`atomic_fallible_singleton` too, with no dedicated
protocol or key type needed. As above, the allocator parameter is a
`reloco::allocator_ref` rather than a `fallible_allocator &`.

## Building strings from parts

```cpp
// One allocation for the whole result; on failure nothing is leaked and the error says why.
auto path = reloco::string::try_concat(dir, "/", name);            // "dir/name"
auto csv  = reloco::string::try_join(fields, ",");                 // any range of string_view-convertibles
// Append several parts to an existing string; it grows at most once and is unchanged on failure.
// Views that alias the string itself (s.view()) are allowed.
auto ok   = path->try_append_all("/", ext);
// Explicit allocator: reloco::string::try_allocate_concat(alloc, a, b, ...)
```

## More sequence helpers

```cpp
reloco::vector<int> v = /* 1 2 5 */;
// Insert a range before index 2 (reserves first: unchanged on failure). The span may alias v itself.
auto ok = v.try_insert_span(2, reloco::span<const int>(mid, 2));
// Remove `count` elements starting at `first` in one pass.
ok = v.try_erase_range(1, 3);
// O(1) unordered removal: the last element takes the removed one's place.
ok = v.try_swap_remove(0);
v.truncate(2);                                   // shorten; no-op if already shorter
auto tail = v.try_split_off(1);                  // vector<int> with the elements from index 1; v keeps the first 1
// On a span: first index matching a value or predicate, and a fallible map into an owner.
auto at   = sp.position(8);                      // optional<size_t>
auto big  = sp.position_if([](int x) { return x > 10; });
auto twice = sp.try_map_to([](int x) { return x * 2; });   // result<vector<int>>
// Convert between owner flavours (copies; fails with capacity_exceeded for fixed-capacity targets).
auto small = v.try_convert_to<reloco::sso_vector<int, 8>>();
auto text  = reloco::string::try_create("x")->try_convert_to<reloco::sso_string<16>>();
```

`vec_deque`, `sso_vec_deque` and `inline_vec_deque` also have `try_create_from(span)`, `try_append(span)` and
`try_assign(span)`. Unlike the vectors they **reject** a span that points into the deque's own ring storage with
`error::invalid_argument` (the ring can wrap and reallocate, so supporting aliasing would be fragile); clone first.

## Spans, string views and byte reinterpretation

```cpp
#include <reloco/span.hpp>
#include <reloco/string_view.hpp>   // string_view.hpp only forward-declares span; include span.hpp where you convert

// string_view <-> span<const char>, both implicit.
reloco::string_view sv = "hello";
reloco::span<const char> chars = sv;
reloco::string_view again = chars;                 // also from span<char>
auto text = reloco::string_view::from_bytes(reloco::span<const uint8_t>(raw, n));   // bytes as text, no validation

// "Downcast" to a smaller unit. sizeof(U) must divide sizeof(T), alignof(U) <= alignof(T), const is preserved,
// and U must be byte-like (char, signed/unsigned char, uint8_t, char8_t, std::byte) so strict aliasing holds.
reloco::span<uint32_t> words = /* ... */;
reloco::span<uint8_t> u8 = words.cast_to<uint8_t>();   // 4x the elements
auto c  = words.as_chars();                            // span<char>      (span<const char> for const T)
auto u  = words.as_uint8();                            // span<uint8_t>
auto wb = words.as_writable_bytes();                   // span<std::byte> (non-const only)
auto s8 = sv.as_uint8();                               // also on string_view; sv.as_bytes() -> span<const std::byte>
// char <-> uint8_t is the identity-size case: span<char>::as_uint8() just retypes the span.

// Any other trivially copyable U (e.g. uint64_t -> uint32_t) needs the explicit opt-in, because it can violate
// strict aliasing:
auto halves = words64.unsafe_cast_to<uint32_t>();

// The checked direction for parsing: bytes -> typed records. Fails (invalid_argument) if misaligned or the byte
// count is not a multiple of sizeof(U).
auto records = packet_bytes.try_cast_to<const Header>();
```

Header layout: `byte_like.hpp` (the `is_byte_like_v` trait) has no dependencies; `string_view.hpp` depends on it and
forward-declares `span`, while `span.hpp` never includes `string_view.hpp`, so there is no include cycle (and
`string.hpp` does not pull in `span.hpp`'s iterator machinery).
