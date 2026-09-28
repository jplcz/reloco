<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Fault injection: `fault_injection.hpp`

## What it is for

Concurrency bugs -- a torn read, a stale pointer, a value that changed
"impossibly" between two instructions -- are notoriously hard to reproduce
on demand: the actual race window is often a handful of CPU cycles wide,
and a test that merely spins up real threads and hopes to hit it is
inherently flaky. `include/reloco/fault_injection.hpp` sidesteps that
entirely: it lets a **single-threaded** test deterministically simulate
"what if a concurrent CPU/ISR had mutated this exact piece of state right
here" by running an arbitrary hook, synchronously, at one specific,
named location in the code under test.

It is a small, C++-only (not usable from C, unlike `reloco_ipc_ring.h`),
header-based mechanism built directly on `tls_provider<T, Tag>`, and is
opt-in at compile time: a normal build of code that uses it compiles the
entire mechanism away to nothing. Its own thread-local bookkeeping
storage picks one of two designs automatically, based on `RELOCO_TLS_
MODEL` -- see "One TLS slot for the whole program, or one per `Tag`"
below.

```cpp
// Production code: mark one location as an injectable fault point.
struct commit_race_point {};

void producer_commit(std::uint64_t &write_idx, std::uint64_t count) {
  std::uint64_t w = write_idx;
  RELOCO_FAULT_POINT_ARGS(commit_race_point, w); // hook may mutate `w` right here
  write_idx = w + count;
}
```

```cpp
// Test code: arm that fault point and assert on the resulting behavior.
#define RELOCO_ENABLE_FAULT_INJECTION
#include <reloco/fault_injection.hpp>

TEST(Commit, SurvivesConcurrentIndexMutation) {
  auto hook = [](std::uint64_t &w) { w = 0xDEADBEEF; }; // named local: must outlive `fi`
  reloco::fault_injector<commit_race_point, std::uint64_t> fi(hook);

  std::uint64_t write_idx = 10;
  producer_commit(write_idx, 4);
  // producer_commit() observed write_idx == 0xDEADBEEF, not 10, at the
  // fault point -- assert it produced the correct (or correctly-detected
  // as corrupted) result regardless.
}
```

## Compile-time opt-in: `RELOCO_ENABLE_FAULT_INJECTION`

Every fault point and helper compiles to a true no-op unless the consumer
defines `RELOCO_ENABLE_FAULT_INJECTION` before the *first* inclusion of
`fault_injection.hpp` (the same "define before first include" contract
`RELOCO_TLS_MODEL`/`RELOCO_MUTEX_BACKEND_*`/etc. use elsewhere in reloco):

- `RELOCO_FAULT_POINT(Tag)`/`RELOCO_FAULT_POINT_ARGS(Tag, ...)` expand to
  `((void)0)` -- the arguments to `RELOCO_FAULT_POINT_ARGS` are not even
  evaluated (matching `RELOCO_ASSERT`'s own disabled-mode convention), so
  pass only side-effect-free lvalue expressions (ordinarily just bare
  variable names) at a fault point call site.
- `fault_armed<Tag, Args...>()` always returns `false`.
- `fault_injector<Tag, Args...>` still exists and is still constructible
  (so test code that arms fault points compiles either way without
  `#ifdef` clutter), but its constructor/destructor do nothing beyond
  storing the hook -- no `tls_provider.hpp` machinery is even
  instantiated.

This means production code can sprinkle `RELOCO_FAULT_POINT_ARGS(...)`
calls freely with zero runtime cost in a normal build, and test binaries
that want to actually exercise them define
`RELOCO_ENABLE_FAULT_INJECTION` (typically once, near the top of the test
file that needs it -- see `tests/test_fault_injection.cpp`) before
including any reloco header that transitively pulls in
`fault_injection.hpp`.

## Core API

```cpp
template <typename Tag, typename... Args> class fault_injector {
public:
  using hook_type = function_ref<void(Args &...)>;

  explicit fault_injector(hook_type hook) noexcept;
  ~fault_injector() noexcept;

  void invoke(Args &...args) const noexcept; // direct, macro-free trigger
};

template <typename Tag, typename... Args> bool fault_armed() noexcept;
```

```cpp
RELOCO_FAULT_POINT(Tag)             // no arguments
RELOCO_FAULT_POINT_ARGS(Tag, ...)   // one or more lvalue arguments, passed by reference
```

- **`Tag`** is a caller-defined, otherwise-unused type identifying one
  fault point, exactly like the `Tag` template parameter
  `tls_provider<T, Tag>`/`pool_allocator_tag<Lock>`/`allocator<Tag>`
  already use elsewhere in reloco (`struct my_race_point {};`). Every
  `RELOCO_FAULT_POINT`/`RELOCO_FAULT_POINT_ARGS` call site for a given
  `Tag`, and every `fault_injector<Tag, Args...>` that arms it, must agree
  on the exact same `Args...` (see "The `Tag` <-> `Args...` contract"
  below).
- **`Args...`** are the fault point's exposed local variables, always
  received by the hook as `Args &...` -- a mutation the hook makes is
  visible to the code under test immediately after `RELOCO_FAULT_POINT_
  ARGS` returns, exactly as if it had happened concurrently just before
  that point.
- **`fault_injector::invoke(args...)`** runs the hook directly, without
  going through the macro or checking whether this instance is actually
  the currently active one for `Tag` -- an escape hatch for generic test
  helpers that already know which `fault_injector` they mean to drive.

## Convenience macros for the common case

Declaring a `Tag` and arming a `fault_injector` for it are both a small,
repetitive ritual once a codebase has more than a couple of fault points.
Two macros shrink that ritual to one line each:

```cpp
RELOCO_FAULT_TAG(name)                 // struct name {};
RELOCO_FAULT_INJECTOR(var, Tag, ...)   // declares var##_hook, then var
```

- **`RELOCO_FAULT_TAG(name)`** just expands to `struct name {};` -- purely
  a naming convention, identical to writing the `struct` by hand.
- **`RELOCO_FAULT_INJECTOR(var, Tag, ...)`** declares a hook local named
  `var##_hook` (bound to whatever callable expression the `...` argument
  is, ordinarily a lambda literal), then declares `var` as a
  `reloco::fault_injector<Tag, Args...>` armed with it -- deducing
  `Args...` automatically from the hook's own call-operator signature, so
  they never need to be spelled out a second time. Because the macro
  always declares the hook as a named local immediately before the
  `fault_injector` that borrows it, using it structurally rules out the
  dangling-inline-lambda pitfall described above -- there is no way to
  misuse it into passing a bare temporary.

```cpp
RELOCO_FAULT_TAG(commit_race_point);
// ...
TEST(Commit, SurvivesConcurrentIndexMutation) {
  RELOCO_FAULT_INJECTOR(fi, commit_race_point, [](std::uint64_t &w) { w = 0xDEADBEEF; });
  // equivalent to:
  //   auto fi_hook = [](std::uint64_t &w) { w = 0xDEADBEEF; };
  //   reloco::fault_injector<commit_race_point, std::uint64_t> fi(fi_hook);
  // ... call producer_commit() and assert on the resulting (mis)behavior ...
}
```

`RELOCO_FAULT_INJECTOR`'s deduction only supports a hook whose call
operator is not itself a template -- a plain function pointer, a
non-generic lambda, or an ordinary function object all work; a *generic*
lambda (one using `auto` parameters) does not, because its `operator()`
has no single address to introspect. Construct a `fault_injector<Tag,
Args...>` directly (naming `Args...` explicitly) for that case.

A separate header, `fault_injection_patterns.hpp`, builds further,
single-line macros on top of these two for even more common and
advanced arming patterns (unconditional overwrite, toggle, nudge by a
delta, counting, firing only the first *N* times or only after skipping
the first *N*, on exactly one hit, or periodically, and conditional
firing) -- see its own file-level doc comment and [the API
reference](reference.md#fault_injection_patternshpp) for the full list.

## Caller-owned, allocation-free, stackable

The framework's only piece of managed state is a thread-local,
pointer-sized slot (or one such slot per `Tag` -- see the next section for
which design applies and why), backed directly by `tls_provider<void *,
...>`, which always resolves to its zero-allocation raw-pointer
specialization for a pointer-typed `T` on every `RELOCO_TLS_MODEL`
(`thread_local`, `pthread`, a custom `RELOCO_TLS_MODEL_OS` port, or the
single-threaded fallback -- see
[API reference](reference.md#tls_providert-tag)). Arming/disarming a
fault point therefore never allocates, on any backend.

Critically, that thread-local slot is **unowned**: it only ever points at
`fault_injector`s the caller constructed and still owns (plain stack
locals, in the overwhelmingly common case) -- the framework does not, and
cannot, allocate, copy, or extend the lifetime of a `fault_injector` on
the caller's behalf. `fault_injector` is accordingly:

- **Non-copyable and non-movable.** Its own address is linked directly
  into its stack; relocating it (by move or copy) would leave that stack
  pointing at stale memory. Construct it directly where it is meant to
  live -- never as a temporary, and never returned by value.
- **Stackable (nestable).** Constructing a `fault_injector<Tag, Args...>`
  while another instance is already active on this thread nests it on
  top, and destroying it restores the stack to exactly what it was
  before, in strict LIFO order -- exactly like nested `scope_guard`s
  composing via `RELOCO_DEFER`. Under the default shared-stack design
  (see below), *any* `Tag`'s `fault_injector` can be that "another
  instance" -- all of them, across every `Tag`, share one stack, so
  *all* active `fault_injector`s on a thread must be destroyed in strict
  reverse-construction order (`RELOCO_ASSERT`ed in debug builds; ordinary
  nested scopes already guarantee this automatically). Under
  `RELOCO_FAULT_INJECTION_UNLIMITED_TLS`, each `Tag` has its own
  independent stack, so only same-`Tag` nesting order is constrained.

```cpp
reloco::fault_injector<my_point> outer(outer_hook);
{
  reloco::fault_injector<other_point> inner(inner_hook); // a *different* Tag: still nests fine
  RELOCO_FAULT_POINT(other_point); // inner_hook fires
  RELOCO_FAULT_POINT(my_point);    // outer_hook still fires too
} // inner destroyed: outer_hook's own arming is unaffected either way
RELOCO_FAULT_POINT(my_point);   // outer_hook fires
```

## Thread-locality is deliberate

Arming is thread-local by construction: a `fault_injector` armed on one
thread has **no effect** on a `RELOCO_FAULT_POINT`/`RELOCO_FAULT_POINT_
ARGS` reached on a different thread. This is not a limitation to work
around -- it is what makes the whole mechanism deterministic and
repeatable in the first place, instead of racing a real second thread to
land its mutation in the right instruction-wide window.

To simulate a fault as observed by a specific worker thread (e.g. one
spawned via `reloco::spawn`/`thread_scope::spawn`), arm the
`fault_injector` from *inside* that thread's own closure, before it
reaches the fault point:

```cpp
reloco::spawn([&] {
  reloco::fault_injector<worker_point, int> fi(worker_hook); // armed on the worker thread
  worker_function(); // reaches RELOCO_FAULT_POINT_ARGS(worker_point, ...) on this same thread
});
```

## One TLS slot for the whole program, or one per `Tag`

A design that gave every fault-point `Tag` its own `tls_provider<void *,
Tag>` slot would spend one TLS key per `Tag` -- fine for `RELOCO_TLS_
MODEL_THREAD_LOCAL`/`RELOCO_TLS_MODEL_SINGLE` (each such slot is just an
ordinary `thread_local`/`static` variable, effectively unlimited), but not
for every backend: `RELOCO_TLS_MODEL_PTHREAD`'s `pthread_key_create()` is
only *guaranteed* `PTHREAD_KEYS_MAX` keys process-wide (as few as 128 on
some libcs, shared with every other user of `pthread_key_create` in the
whole process), and a custom `RELOCO_TLS_MODEL_OS` port on an RTOS/kernel
typically backs per-task storage with a small, fixed-size slot table. A
codebase with more than a handful of fault points -- entirely plausible
once the pattern proves useful -- would risk exhausting that budget on
those backends.

`fault_injection.hpp` therefore auto-selects between two designs, based
on `RELOCO_TLS_MODEL`, unless the consumer defines `RELOCO_FAULT_
INJECTION_UNLIMITED_TLS` itself (to `0` or `1`) before including this
header:

- **Shared stack, one TLS slot total** -- the default under
  `RELOCO_TLS_MODEL_PTHREAD`/`RELOCO_TLS_MODEL_OS`. Exactly **one** TLS
  slot, shared by every `Tag`, for the lifetime of the program, holds the
  head of an intrusive singly linked stack of every `fault_injector`
  currently active on the calling thread, across every `Tag`; arming
  pushes, disarming pops, in strict LIFO order. Finding the right
  `fault_injector` for a given `Tag`/`Args...` means walking that shared
  stack from the innermost entry outward looking for the first one whose
  recorded signature matches (see "The `Tag` <-> `Args...` contract"
  below) -- `O(number of currently active fault_injectors on this
  thread)`, rather than `O(1)`. In practice that count is a handful at
  most (however many fault points a single test exercises at once), so
  trading a small, bounded linear scan for a constant, backend-independent
  TLS budget is a clear win on these backends.
- **One TLS slot per `Tag`** -- the default under `RELOCO_TLS_MODEL_
  THREAD_LOCAL`/`RELOCO_TLS_MODEL_SINGLE`, since a slot there costs
  nothing scarce. Each `Tag` gets its own private `tls_provider<void *,
  Tag>` slot holding its own private per-`Tag` stack -- `O(1)` lookup, no
  signature check needed or performed (see below).

The public API (`fault_injector`, `fault_armed`, the two macros) and every
caller-visible behavior -- caller ownership, no allocation, stackable,
strict LIFO, thread-local scope -- are identical either way; only the
TLS-slot budget vs. lookup complexity trade-off, and whether cross-`Tag`
nesting order is constrained, changes. A codebase that defines a very
large number of fault points and wants `O(1)` lookup even under
`RELOCO_TLS_MODEL_PTHREAD`/`_OS` can force it by defining `RELOCO_FAULT_
INJECTION_UNLIMITED_TLS` to `1` itself (accepting the one-slot-per-`Tag`
budget risk that describes); conversely `0` forces the shared-stack design
even under `RELOCO_TLS_MODEL_THREAD_LOCAL`/`_SINGLE`.

## The `Tag` <-> `Args...` contract, and how it is checked

Under the default shared-stack design (`RELOCO_TLS_MODEL_PTHREAD`/`_OS`),
every `fault_injector<Tag, Args...>` records its own signature -- both
`Tag` and the exact `Args...` list, in order -- as a `reloco::type_id`
(see [`type_id`](reference.md#type_id--type_id_oft)), computed without
RTTI and without any allocation (the same "address of a per-instantiation
static data member" trick `type_id_of<T>()` always uses). A fault point
looks for the first (innermost) stack entry whose recorded signature
matches its own `Tag`/`Args...`; a mismatched `Args...` for the same `Tag`
simply never matches (the fault point behaves as if nothing were armed)
rather than being silently mis-cast.

Under `RELOCO_FAULT_INJECTION_UNLIMITED_TLS` (the default under
`RELOCO_TLS_MODEL_THREAD_LOCAL`/`_SINGLE`), each `Tag` has its own private
slot instead, so this check is unnecessary and not performed -- a fault
point unconditionally trusts the `Args...` it was written with match every
`fault_injector<Tag, ...>` ever constructed for that `Tag`.

Either way, give each fault point its own, single-purpose `Tag` type --
exactly the convention every other reloco `Tag` template parameter already
follows (`tls_provider<T, Tag>`, `pool_allocator_tag<Lock>`,
`allocator<Tag>`) -- and reuse exactly one `Args...` signature per `Tag`
throughout the program; this is always good practice, and is
safety-critical specifically when the per-`Tag`-slot design is in effect.

## Argument evaluation and side effects

`RELOCO_FAULT_POINT_ARGS(Tag, ...)`'s arguments are ordinary lvalue
expressions passed by reference to whichever hook is currently armed
(if any). When `RELOCO_ENABLE_FAULT_INJECTION` is not defined, they are
**not evaluated at all** -- pass only side-effect-free expressions
(ordinarily just bare variable names already computed by the surrounding
code), matching the disabled-mode convention `RELOCO_ASSERT`/`RELOCO_
ASSERT_MSG` already use for their own condition/message arguments.

## When not to use it

Fault injection is a testing tool, not a runtime resilience mechanism: it
should never appear in a code path a production, `RELOCO_ENABLE_FAULT_
INJECTION`-undefined build cannot simply compile out, and it deliberately
provides no way to inject a fault "from the outside" on a thread that
did not itself arm a `fault_injector` first. For deterministically
exercising a lock-free/wait-free algorithm's actual concurrent
interleavings (rather than simulating one interleaving's outcome
directly), reach for a proper interleaving-exploration tool (e.g.
ThreadSanitizer, a model checker, or a stress test with many real
threads) instead -- `fault_injection.hpp` complements that kind of tool by
letting a *specific*, already-diagnosed interleaving be turned into a
fast, deterministic regression test, not by replacing broader concurrency
testing.

## See also

- [API reference](reference.md#fault_injectortag-args--reloco_fault_point--reloco_fault_point_args) --
  quick summary alongside every other reloco header.
- [`tls_provider<T, Tag>`](reference.md) -- the thread-local storage
  framework `fault_injector` is built on, and its `RELOCO_TLS_MODEL`
  backend selection.
- [`scope_guard<Callable>` / `RELOCO_DEFER`](reference.md) -- the simpler
  RAII scope-exit primitive `fault_injector`'s own nesting/restore
  discipline mirrors.
- `tests/test_fault_injection.cpp` -- the full behavioral test suite
  (no-op-when-unarmed, hook mutation, nesting/LIFO restore, multiple
  arguments, distinct tags, and direct `invoke()` use).
- `tests/test_fault_injection_patterns.cpp` -- the behavioral test suite
  for `fault_injection_patterns.hpp`'s own convenience macros.
- `tests/test_fault_injection_backends.cpp` -- the same core behaviors
  re-run under each `RELOCO_TLS_MODEL` backend (thread-local, single,
  and pthread), one dedicated test binary per backend.
- [`reloco_ipc_ring.h`/`.hpp`](reference.md#reloco_ipc_producer--reloco_ipc_consumer--ipc_producer--ipc_consumer) --
  a real-world, production-header usage example: two built-in fault
  points (`reloco::ipc_fault::producer_read_idx_refresh`/
  `consumer_write_idx_refresh`) guard the exact instant a cross-process
  ring buffer trusts the *other* side's shared-memory index, for
  index-spoofing and bit-flip attack-surface testing. See
  `tests/test_ipc_ring_fault_injection.cpp` -- including its own,
  dedicated CMake target, required to avoid an ODR violation against
  other translation units that use the same header without fault
  injection enabled.
