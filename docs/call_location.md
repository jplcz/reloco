<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# `call_location` / `call_location_ref`

`include/reloco/call_location.hpp`

An optional, compile-time-stripable `file:line` caller location, for
passing into assert/fault-reporting-style functions without forcing
every build (or every call site) to pay for it.

`call_location` is the plain `{file, line}` pair. `call_location_ref` is
the thin (pointer plus `int`), trivially-copyable wrapper actually
passed around *by value* -- a default-constructed ref (or
`call_location_ref::none()`) means "no location is available";
`call_location_ref::current()` captures the location of its own caller.

Every location-aware API takes exactly one `call_location_ref`
parameter rather than inventing a bespoke optional-pair convention per
call site. Some call sites architecturally cannot supply a location at
all -- a fixed-signature function pointer slot (an `extern "C"`
callback table entry, a trap handler vector, anything crossing an ABI
boundary the kernel does not control the shape of) cannot grow an extra
parameter just because one caller happens to know its own source
location. For exactly these cases, a location-aware kernel API ships a
location-*less* export alongside the location-aware one, both forwarding
into one shared implementation that takes a `call_location_ref` --
`call_location_ref::none()` for the former, a captured location for the
latter. See the header's `@file` block for the full worked example.

## `current()` vs. a `__FILE__`/`__LINE__` macro

A macro expanding to `call_location{__FILE__, __LINE__}` looks like the
obvious way to capture a caller's location, but silently breaks the
moment it is used as a *default argument*: default-argument expressions
are bound once, at the point a function is *declared*, so a
`where = SOME_MACRO()` default would always report `call_location.hpp`'s
own declaration site, never the real caller's. `current()` instead uses
the GCC/Clang `__builtin_FILE()`/`__builtin_LINE()` intrinsics (the same
mechanism `std::source_location::current()` is built on, available
before C++20 and without pulling in `<source_location>`) as its own
parameters' default values -- these are specially re-evaluated at each
omitted-argument call site, including transitively through a chain of
default arguments, so `current()` correctly reports whichever real call
site ultimately triggered it. `RELOCO_CALL_LOCATION_HAS_BUILTINS`
(defined by the header itself, from `__has_builtin`) is `1` when this is
available; on a compiler without these intrinsics, `current()` falls
back to plain `__FILE__`/`__LINE__`, which is still correct when
`current()` is called directly/explicitly at the real call site, but
not when relied upon as some *other* function's default argument.

`RELOCO_ENABLE_CALL_LOCATION` (see `reloco_config.hpp`) is the
build-time switch controlling whether `current()` actually captures a
location, or defaults to `none()` instead -- set it to `0` to strip
every caller-location string this header would otherwise contribute to
the binary's rodata, for size-constrained builds (early boot stages,
ROM-resident firmware) that cannot justify the cost.

## `debug_call_location_ref` vs. `release_call_location_ref`

`debug_call_location_ref` is just an alias for `call_location_ref`.
`release_call_location_ref` is its permanently empty companion type (no
data members, `has_value()` unconditionally `false`, no `value()`) for
an inter-module ABI boundary -- a stable shared-library export, a
versioned driver/plugin interface -- that must never change shape
depending on `RELOCO_ENABLE_CALL_LOCATION`/
`RELOCO_CALL_LOCATION_HAS_BUILTINS`, or any field `call_location` ever
grows in the future. An API that wants both ships one overload per
type. `RELOCO_CALL_LOCATION_DEBUG` (see `reloco_config.hpp`) selects
which overload's parameter actually gets a default argument --
`RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG`/
`RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE` are a complementary pair of
default-argument clauses (each already includes its own leading `=`,
written directly after the parameter name), exactly one of which is
ever non-empty -- so an omitted-argument call is never ambiguous
between the two overloads:

```cpp
class mutex {
public:
  // Debug builds (RELOCO_CALL_LOCATION_DEBUG != 0): the default
  // argument lives here, so an omitted-argument call resolves to this
  // overload and captures a real location.
  void lock(reloco::debug_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG) {
    lock_impl(where);
  }

  // Release builds (RELOCO_CALL_LOCATION_DEBUG == 0): the default
  // argument lives here instead, so an omitted-argument call resolves
  // to this overload -- always zero-sized, frozen shape.
  void lock(reloco::release_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE) {
    (void)where;
    lock_impl(reloco::call_location_ref::none());
  }

private:
  void lock_impl(reloco::call_location_ref where);
};
```

See the header's `@file` block for the full worked version of this
example.
