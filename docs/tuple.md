<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# `reloco::tuple`

`include/reloco/tuple.hpp`

`reloco::tuple<Ts...>` **is a `std::tuple<Ts...>`** (it derives from it) with two additions:

* **Rust-like combinators**: `map`, `zip`, `append`, `prepend`, `concat`, `first`, `last`, `for_each`, `apply`.
* **reloco-style safety**: reference-returning accessors cannot dangle on temporaries, and a runtime index lookup
  (`visit_at`) reports failure through `result<T>` instead of undefined behaviour.

## Why it exists

`std::tuple` is only a bag of values: building a new tuple from an old one needs `std::tuple_cat`, `std::apply` and
index sequences. Writing `t.map(f)` or `a.zip(b)` says what you mean. And `std::get<0>(make_tuple())` hands you an
`T &&` into a temporary, which is easy to bind to a dangling reference; `reloco::tuple` returns such elements by value.

Because it derives from `std::tuple`, nothing you already have breaks: it converts to `std::tuple` implicitly, inherits
all constructors, comparisons and `swap`, works with `std::get`, `std::apply`, `std::tuple_size` and structured bindings,
and is trivially relocatable when all elements are.

## Example

```cpp
#include <reloco/tuple.hpp>
#include <string>

void demo() {
  // Class template argument deduction works: this is reloco::tuple<int, std::string>.
  reloco::tuple t(1, std::string("one"));

  // get<I>() is the element access. On an lvalue it returns a reference; on an rvalue it returns the element BY VALUE
  // (moved out), so `make_tuple().get<0>()` can never dangle.
  t.get<0>() = 2;
  int a = t.first();            // same as get<0>()
  const std::string &s = t.last(); // same as get<size() - 1>()

  // Structured bindings work (reloco::tuple provides std::tuple_size / std::tuple_element).
  auto &[num, name] = t;

  // map: apply a function to every element, collect the results in a new tuple. The function receives T& on an
  // lvalue tuple, const T& on a const one, and a moved T on an rvalue (so move-only elements are fine).
  auto lengths = t.map([](const auto &v) { return sizeof(v); }); // tuple<size_t, size_t>

  // zip: pair elements by position. Both tuples must have the same size (compile-time check).
  reloco::tuple<char, double> b('x', 2.0);
  auto z = t.zip(b); // tuple<tuple<int, char>, tuple<std::string, double>>

  // append / prepend / concat build longer tuples without index sequences.
  auto longer = t.append(3.5);                       // tuple<int, std::string, double>
  auto both = t.concat(reloco::tuple<char>('c'));    // tuple<int, std::string, char>

  // for_each runs a function per element, in order; apply calls f(elements...).
  t.for_each([](auto &v) { /* inspect or mutate v */ });
  int total = reloco::tuple<int, int>(1, 2).apply([](int x, int y) { return x + y; });

  // visit_at: choose the element with a RUNTIME index. Every element must give the same return type (otherwise the
  // result type is unnameable, which is a compile-time error). Out-of-range index -> error::out_of_bounds.
  reloco::tuple<int, short, long> n(1, 2, 3);
  auto r = n.visit_at(1, [](auto &v) { return static_cast<long>(v); }); // result<long>
  if (!r) {
    // r.error() == reloco::error::out_of_bounds
  }

  // Interop: into_std() moves the elements into a plain std::tuple; as_std() borrows it.
  std::tuple<int, std::string> plain = std::move(t).into_std();
}
```

## API summary

| Group | Members |
|---|---|
| Size | `size()`, `empty()` (static, constexpr) |
| Access | `get<I>()` (`&`, `const &`, `&&` by value), `first()`, `last()` |
| Calling | `apply(f)`, `for_each(f)`, `visit_at(index, f)` *(fallible: `result<R>`)* |
| Transforming | `map(f)`, `zip(other)`, `append(v)`, `prepend(v)`, `concat(other)` |
| std interop | implicit conversion to `std::tuple`, `as_std()`, `into_std()` |
| Free functions | `make_tuple`, `tie`, `forward_as_tuple`, deduction guides (incl. from `std::tuple` / `std::pair`) |

Notes:

* Each combinator comes in `&`, `const &` and `&&` flavours that decide how elements reach your function or the new
  tuple (reference, const reference, or moved). The `const &&` overloads are deleted.
* `map`'s function must not return `void` (use `for_each`); returning a reference gives a tuple of references, so
  make sure the referent outlives the result.
* `get<I>() &&` is `noexcept` only when moving the element is.
* Compile-time errors: `first()`/`last()`/`visit_at` on an empty tuple, `zip` with different sizes, `visit_at` with a
  function whose return type differs between elements.

## Integrating with other subsystems

* **`result<T>` / `error`**: `visit_at` returns `result<R>` with `error::out_of_bounds`; chain it like any other
  reloco fallible call.
* **Relocation**: `is_trivially_relocatable<tuple<Ts...>>` is true iff all `Ts` are, so containers such as `vector` can
  relocate tuples with `memcpy`.
* **std**: pass a `reloco::tuple` anywhere a `std::tuple` is expected, e.g. `std::apply(f, t)` or `std::tuple_cat`.
