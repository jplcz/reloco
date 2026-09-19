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
outcome through `reloco::expected<T, E>` (see
[Hardened containers](hardened-containers.md)) instead of throwing.

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

This is a convention, not an interface: implement whichever of the five
functions make sense for a given type (most types only need one or two), and
generic code that wants to work with any of them checks for each function
with the matching trait. Ported from `reloco_legacy`'s `concepts.hpp`, with
two changes to fit this repository's conventions (see below).

## 1. `try_create`: the default-allocator factory

```cpp
enum class widget_error { invalid_argument };

struct widget {
  int value;

  static reloco::expected<widget, widget_error> try_create(int v) noexcept {
    if (v < 0)
      return reloco::unexpected(widget_error::invalid_argument);
    return widget{v};
  }
};

reloco::expected<widget, widget_error> made = widget::try_create(3);
```

Implement `try_create` when a type can be instantiated without the caller
specifying an allocator, typically because it delegates internally to
`reloco::default_allocator()` (see [Extending reloco](extending.md)) or
because it needs no allocation at all. This is the common case for
application code that has not opted into explicit allocator control.

## 2. `try_allocate`: the explicit-allocator factory

```cpp
static reloco::expected<widget, widget_error>
try_allocate(reloco::allocator_ref alloc, int v) noexcept {
  if (v < 0)
    return reloco::unexpected(widget_error::invalid_argument);
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

  reloco::expected<void, widget_error> try_construct(const char *path) noexcept {
    fd = open_device(path);
    if (fd < 0)
      return reloco::unexpected(widget_error::invalid_argument);
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
reloco::expected<widget, widget_error> try_clone(reloco::allocator_ref alloc) const noexcept;

// Self-contained: for types with no allocator dependency.
reloco::expected<widget, widget_error> try_clone() const noexcept;
```

Implement one of these two shapes — never both — when a type needs an
independent copy that can fail (allocation failure, a resource that cannot
be duplicated). `has_try_clone_v<T>` is satisfied by either shape, so
generic code does not need to know which one a given type chose.

## 5. `try_clone_at`: optimized in-place clone

```cpp
static reloco::expected<void, widget_error>
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
`if constexpr`:

```cpp
template <typename T, typename... Args>
auto make(reloco::allocator_ref alloc, Args &&...args) {
  static_assert(reloco::has_try_allocate_v<T, Args...> || reloco::has_try_create_v<T, Args...>,
                "T must implement try_allocate or try_create");
  if constexpr (reloco::has_try_allocate_v<T, Args...>) {
    return T::try_allocate(alloc, std::forward<Args>(args)...);
  } else {
    return T::try_create(std::forward<Args>(args)...);
  }
}
```

Under C++20, the same detection is also available as a real `concept`
(`reloco::has_try_create<T, Args...>`, etc.), usable directly in a
`template` constraint or `requires` clause instead of `if constexpr`:

```cpp
template <reloco::has_try_clone T>
void register_clonable_type(); // only compiles for T satisfying has_try_clone
```

Both forms check exactly the same thing; `include/reloco/concepts.hpp`
defines the `_v` trait once and derives the C++20 `concept` from it, so
there is no risk of the two disagreeing.

## Differences from `reloco_legacy`

`reloco_legacy/include/reloco/concepts.hpp` is the origin of this pattern,
but two things changed to fit this repository's conventions:

* Legacy checked for one fixed `result<T>` alias (`expected<T, error>` with
  a single global `error` enum). `reloco` uses a scoped, per-feature error
  enum for every fallible type (`span_error`, `string_view_error`,
  `allocator_error`, and so on — see [Hardened containers](hardened-containers.md)),
  so every `has_try_*_v` trait instead accepts *any*
  `reloco::expected<T, E>` return, whatever `E` the implementing type
  chooses.
* Legacy took a `fallible_allocator &`, a virtual base class. `reloco` has
  no virtual allocator interface; `has_try_allocate_v`, `has_try_clone_v`,
  and `has_try_clone_at_v` instead expect a `reloco::allocator_ref`, the
  type-erased, non-virtual handle described in
  [Extending reloco](extending.md).
