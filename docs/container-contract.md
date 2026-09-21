<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Container contract: lifetime, rvalue safety, and tri-tier access

Every container or view reloco ships (`array`, `span`, `string`/`string_view`,
`vector`, `inline_vector`, `flat_set`, `basic_inline_string`, ...) satisfies
three orthogonal, mechanically-checkable contracts:

1. **Lifetime annotations** (`RELOCO_OWNER`/`RELOCO_POINTER`,
   `RELOCO_LIFETIMEBOUND`, `RELOCO_LIFETIME_CAPTURE_BY_THIS`/
   `RELOCO_LIFETIME_CAPTURE_BY`) — see
   [Lifetime safety](lifetime-safety.md) for the full macro reference.
2. **Rvalue protection** (`RELOCO_BLOCK_RVALUE_ACCESS`, and explicit
   `... const && = delete;` for anything the macro does not name) so a
   pointer/reference/iterator can never be bound to a temporary container's
   storage.
3. **The checked/`try_*`/`unsafe_*` tri-tier accessor convention** — see
   [Hardened containers](hardened-containers.md) for the user-facing
   rationale.

This page is the copy-paste template and checklist for applying all three to
a *new* container or when auditing an existing one — not another
explanation of *why* (the two guides above cover that). Read the "Tri-tier
accessor template" section literally: adapt names/types, keep every
attribute.

## Header include list

```cpp
#include "detail/assert.hpp"   // RELOCO_ASSERT / RELOCO_DEBUG_ASSERT
#include "error.hpp"            // reloco::error
#include "expected.hpp"         // reloco::result<T>
#include "lifetime.hpp"         // RELOCO_LIFETIMEBOUND / RELOCO_OWNER / RELOCO_POINTER / RELOCO_UNSAFE_BUFFER_USAGE
#include "rvalue_safety.hpp"    // RELOCO_BLOCK_RVALUE_ACCESS
#include <functional>            // std::reference_wrapper / std::ref / std::cref
```

Wrap the raw pointer-arithmetic implementation of the container's
`namespace reloco` body in
`RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/`RELOCO_END_UNSAFE_BUFFER_USAGE` (see
`array.hpp`, `span.hpp`, `string.hpp`, `vector.hpp`); this is orthogonal to
the per-accessor `RELOCO_UNSAFE_BUFFER_USAGE` attribute below.

## Class declaration template

```cpp
// Owning container (embeds or allocates its own storage):
template <typename T> class RELOCO_OWNER my_container {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T &;
  using const_reference = const T &;
  using pointer = T *;
  using const_pointer = const T *;
  using iterator = T *;
  using const_iterator = const T *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  // First line in `public:` for every container/view — see below.
  RELOCO_BLOCK_RVALUE_ACCESS(T);

  // ...
};

// Non-owning view/reference wrapper (borrows external storage):
template <typename T> class RELOCO_POINTER my_view {
  // Same member-type aliases and RELOCO_BLOCK_RVALUE_ACCESS(T) as above.
};
```

Use `RELOCO_OWNER` when the type manages the lifetime of its storage
(`array`, `vector`, `inline_vector`, `basic_string`, `basic_inline_string`,
`flat_set`).
Use `RELOCO_POINTER` when the type is a non-owning handle over storage it
does not manage (`span`, `function_ref`, `mutable_container_ref`,
`stack_allocator_context`).

## Tri-tier accessor template

Copy this block verbatim for any indexable/dereferenceable container backed
by a contiguous `T *data_` + `size_type size_`; rename `data_`/`size_` and
swap the bounds check as needed (e.g. non-empty vs. in-range).

```cpp
  // ---- checked tier: RELOCO_ASSERT, active even when NDEBUG is defined ----

  [[nodiscard]] T &operator[](size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < size_, "my_container index out of bounds");
    return data_[index];
  }
  [[nodiscard]] const T &operator[](size_type index) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(index < size_, "my_container index out of bounds");
    return data_[index];
  }

  [[nodiscard]] T &front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "my_container is empty");
    return data_[0];
  }
  [[nodiscard]] const T &front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "my_container is empty");
    return data_[0];
  }

  [[nodiscard]] T &back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "my_container is empty");
    return data_[size_ - 1];
  }
  [[nodiscard]] const T &back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "my_container is empty");
    return data_[size_ - 1];
  }

  // `data()` asserts non-empty; it is the checked tier's raw-pointer
  // accessor and returns a (pointer, size()) pair, never an unbounded,
  // null-terminated pointer (that is `unsafe_c_str()`/`c_str()`-style
  // access below, always unsafe-tier only).
  [[nodiscard]] T *data() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "my_container is empty");
    return data_;
  }
  [[nodiscard]] const T *data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "my_container is empty");
    return data_;
  }

  // ---- fallible tier: reloco::result instead of trapping ----

  [[nodiscard]] result<std::reference_wrapper<T>> try_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    if (index >= size_)
      return unexpected(error::out_of_bounds);
    return std::ref(data_[index]);
  }
  [[nodiscard]] result<std::reference_wrapper<const T>> try_at(size_type index) const & noexcept
      RELOCO_LIFETIMEBOUND {
    if (index >= size_)
      return unexpected(error::out_of_bounds);
    return std::cref(data_[index]);
  }

  [[nodiscard]] result<std::reference_wrapper<T>> try_front() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::ref(data_[0]);
  }
  [[nodiscard]] result<std::reference_wrapper<const T>> try_front() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return std::cref(data_[0]);
  }

  [[nodiscard]] result<T *> try_data() & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return data_;
  }
  [[nodiscard]] result<const T *> try_data() const & noexcept RELOCO_LIFETIMEBOUND {
    if (empty())
      return unexpected(error::container_empty);
    return data_;
  }

  // ---- explicitly-unsafe tier: RELOCO_UNSAFE_BUFFER_USAGE, RELOCO_DEBUG_ASSERT only ----

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T &unsafe_at(size_type index) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < size_, "my_container index out of bounds");
    return data_[index];
  }
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T &unsafe_at(size_type index) const & noexcept
      RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(index < size_, "my_container index out of bounds");
    return data_[index];
  }

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE T *unsafe_data() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "my_container has no data");
    return data_;
  }
  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const T *unsafe_data() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_DEBUG_ASSERT(!empty(), "my_container has no data");
    return data_;
  }

  // ---- iteration: same RELOCO_LIFETIMEBOUND rule applies uniformly ----

  [[nodiscard]] iterator begin() & noexcept RELOCO_LIFETIMEBOUND { return data_; }
  [[nodiscard]] iterator end() & noexcept RELOCO_LIFETIMEBOUND { return data_ + size_; }
  [[nodiscard]] const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND { return data_; }
  [[nodiscard]] const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return data_ + size_; }
  [[nodiscard]] const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return data_; }
  [[nodiscard]] const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return data_ + size_; }
```

Rules baked into every line above, and non-negotiable for a new container:

* Every accessor that hands back a reference, pointer, or iterator into the
  container's own storage carries `RELOCO_LIFETIMEBOUND` — in **all three**
  tiers alike (checked, `try_*`, `unsafe_*`), not just `data()`/`begin()`/
  `end()`. This applies uniformly to plain reference-returning accessors
  (`operator[]`/`front()`/`back()`/`unsafe_at()`) too, not only to
  pointer/iterator ones; `array.hpp`, `span.hpp`, `vector.hpp`,
  `basic_inline_string`, `flat_set`, and `collection_view`/
  `mutable_collection_view` all follow this uniformly.
* A raw, unbounded, null-terminated pointer accessor (`c_str()`-style) is
  **never** part of the checked tier. Name it `unsafe_c_str()` (see
  `string.hpp`/`inline_string.hpp`) and gate it the same way as
  `unsafe_data()` above; there is no checked or `try_*` counterpart because
  there is no safe bounded alternative to it.
* `try_*` accessors that return a borrow use
  `result<std::reference_wrapper<T>>` (never `result<T &>` — references are
  not assignable, so `expected` cannot store one directly) or a raw pointer
  for `try_data()`; both still need `RELOCO_LIFETIMEBOUND`.
* `unsafe_*` accessors use `RELOCO_DEBUG_ASSERT`, never `RELOCO_ASSERT` —
  the entire point of the unsafe tier is that the precondition is the
  caller's responsibility in release builds.

## Rvalue protection template

```cpp
public:
  RELOCO_BLOCK_RVALUE_ACCESS(T); // first line after `public:`
```

`RELOCO_BLOCK_RVALUE_ACCESS(Type)` (see `rvalue_safety.hpp`) deletes the
`&&`-qualified overload of every accessor named above
(`operator[]`, `front`/`back`, `at`/`try_at`/`unsafe_at`,
`data`/`try_data`/`unsafe_data`, `begin`/`end`/`rbegin`/`rend`/`c*`,
`operator*`/`operator->`/`get`/`unsafe_get`) in one line, so a caller cannot
do `my_container{...}.data()` and keep the pointer past the full expression.

Any accessor the shared macro does not name — because it is specific to one
container (`view()`, `as_span()`, `unsafe_c_str()`, a bespoke
`try_substr()`) — still needs its own explicit deletion if it can return a
borrow:

```cpp
  [[nodiscard]] view_type view() const & noexcept RELOCO_LIFETIMEBOUND { return view_type(data_, size_); }
  view_type view() const && = delete;

  [[nodiscard]] RELOCO_UNSAFE_BUFFER_USAGE const_pointer unsafe_c_str() const & noexcept RELOCO_LIFETIMEBOUND {
    return data_;
  }
  const_pointer unsafe_c_str() const && = delete;
```

Move-only owning containers (`vector`, `basic_string`) get this "for free"
in the sense that there is no copy constructor to produce a throwaway
prvalue in the first place, but the macro still matters for the common
`try_create(...).value()...` chaining mistake, since the `result<T>`
temporary's `.value()` returns an rvalue reference to the container.

## Constructors/setters that capture a borrow

Anything that stores a caller-supplied pointer/reference *inside* the
object (type-erased handles like `function_ref`, `mutable_container_ref`,
`stack_allocator_context` — as opposed to a container copying/owning the
value) needs `RELOCO_LIFETIME_CAPTURE_BY_THIS` on the captured parameter, in
addition to (or, per the equivalence noted in
[Lifetime safety](lifetime-safety.md#lifetime-annotations), instead of on a
void-returning setter) `RELOCO_LIFETIMEBOUND`:

```cpp
  template <typename Container, std::enable_if_t<is_container_ref_source<Container, T>::value, int> = 0>
  constexpr explicit my_view(Container &c RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(std::addressof(c)) {}
```

## Move-only/use-after-move wrappers

If the type has a hazardous "used after move" or "used after take/reset"
state (`checked_value`, `optional`, `inplace_function`), add the
consumed-state annotations from `lifetime.hpp` so Clang's `-Wconsumed`
tracks it: `RELOCO_CONSUMABLE(unconsumed)` on the class,
`RELOCO_RETURN_TYPESTATE(unconsumed)`/`RELOCO_RETURN_TYPESTATE(consumed)` on
constructors/`reset()`, `RELOCO_CALLABLE_WHEN("unconsumed")` on every
accessor that traps on a moved-from/empty object, and
`RELOCO_SET_TYPESTATE(consumed)` on `take()`/`reset()`. See
`checked_value.hpp`'s `as_known()` for the `RELOCO_RETURN_TYPESTATE`-only
`&`-qualified escape hatch used at reference/pointer boundaries where Clang
would otherwise infer `"unknown"`.

## Checklist

- [ ] Class tagged `RELOCO_OWNER` (owns storage) or `RELOCO_POINTER`
      (non-owning view/handle).
- [ ] `RELOCO_BLOCK_RVALUE_ACCESS(ElementType)` is the first line after
      `public:`.
- [ ] Every reference/pointer/iterator-returning accessor, in **all three**
      tiers, carries `RELOCO_LIFETIMEBOUND`.
- [ ] Every borrow-returning accessor not named by the shared macro has its
      own explicit `... const && = delete;` (or non-`const &&`, as
      applicable).
- [ ] Any unbounded/null-terminated raw-pointer accessor is `unsafe_*`-only,
      with no checked or `try_*` counterpart.
- [ ] Constructors/setters that capture a borrow into the object use
      `RELOCO_LIFETIME_CAPTURE_BY_THIS`/`RELOCO_LIFETIME_CAPTURE_BY(...)`.
- [ ] `unsafe_*` accessors use `RELOCO_DEBUG_ASSERT`, never `RELOCO_ASSERT`,
      and are `RELOCO_UNSAFE_BUFFER_USAGE`-gated.
- [ ] Move-only/use-after-move wrappers carry the `-Wconsumed` typestate
      annotations.
- [ ] Validate with `./scripts/check-unsafe-buffer-usage.sh` (0 diagnostics
      across C++17/20/23) and a `clang++-24` build/test run — annotation
      correctness only shows up as Clang diagnostics, never as a GCC error.
