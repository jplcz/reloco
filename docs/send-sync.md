<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Thread-transfer/-sharing safety: `is_send<T>` / `is_sync<T>`

## What `Send`/`Sync` mean

Rust's compiler tracks two independent, automatically-derived facts about
every type:

- **`Send`**: is it sound to *move ownership* of a value to another thread
  and keep using it only from that thread from then on?
- **`Sync`**: is it sound to *share* a value across threads through a
  `&T` (i.e. concurrent read access, or access the caller has externally
  synchronized some other way)?

C++ has no equivalent compiler-tracked ownership/thread-affinity model, so
these are properties every C++ type already has implicitly, whether or not
anything checks them — a type built on non-atomic shared/mutable state
(a hand-rolled refcount, an unsynchronized cache) is unsound to hand to
another thread or share concurrently, exactly as it would be in Rust,
whether or not the language notices. `reloco::is_send<T>`/`reloco::is_sync<T>`
give that already-true property a name and a place to record it, so it can
be checked at compile time (`static_assert`) at the specific API boundaries
that actually cross a thread, instead of being left as an unchecked
assumption in a doc comment.

Rust derives both traits automatically, per field, at compile time; C++ has
no equivalent reflection, so both traits follow the same manually-curated
customization-point shape [`is_trivially_relocatable`](relocatable.md)
already established: default to the safe-looking case (`true` — an
ordinary value type with no hidden non-atomic shared state is fine to move
to another thread or place behind external synchronization), and have each
type that is deliberately *not* thread-transferable/-shareable specialize
itself to `false` in its own header, right next to its
`is_trivially_relocatable` specialization if it has one.

## `reloco::is_send<T>` / `reloco::is_sync<T>`

`include/reloco/send_sync.hpp` defines both customization points:

```cpp
template <typename T> struct is_send : std::true_type {};
template <typename T> struct is_sync : std::true_type {};

template <typename T>
inline constexpr bool is_send_v = is_send<T>::value;
template <typename T>
inline constexpr bool is_sync_v = is_sync<T>::value;

#if RELOCO_CXX20
template <typename T> concept sendable = is_send_v<T>;
template <typename T> concept syncable = is_sync_v<T>;
#endif
```

Every ordinary value type (a plain `struct`, `int`, `reloco::vector<T>`,
...) is both `Send` and `Sync` by default, matching how most Rust types are
auto-derived `Send`/`Sync` unless a field opts out. reloco itself opts out
the specific types it ships that are unsound to transfer or share:

| Type | `Send`? | `Sync`? | Why |
|---|---|---|---|
| `reloco::rc<T>` / `reloco::weak_rc<T>` | Never, regardless of `T` | Never, regardless of `T` | Non-atomic refcount; even transferring a single handle to another thread is unsound while a clone might still be dropped concurrently from the original thread — matches Rust's `Rc<T>`/`Weak<T>` |
| `reloco::cell<T>` | Same as `T` | Never, regardless of `T` | `set()`/`replace()` mutate through a `const cell<T>&` with no synchronization at all — matches Rust's `Cell<T>` |
| `reloco::ref_cell<T>` | Same as `T` | Never, regardless of `T` | Its runtime borrow flag is a plain, unsynchronized counter — matches Rust's `RefCell<T>` |
| `reloco::shared_ptr<T>` / `reloco::weak_ptr<T>` | Same as `is_send<T> && is_sync<T>` | Same as `is_send<T> && is_sync<T>` | Atomic refcount, so the smart pointer itself is always safe to transfer/share, but the `T` it protects is reachable concurrently through any live clone — matches Rust's `Arc<T>`/`Weak<T>` (`Send`/`Sync` only when `T: Send + Sync`) |

> **Note:** composition is *not* automatic beyond these specific
> specializations. `is_send_v<reloco::vector<reloco::rc<T>>>` is `true`
> even though it should not be, because nothing walks `vector<T>`'s element
> type — exactly the same limitation `is_trivially_relocatable` accepts
> (see [Trivial relocation](relocatable.md)). `is_send<cell<T>>`/
> `is_send<ref_cell<T>>` are the only specializations here that forward to
> another type's trait at all; every other reloco container is left at the
> default `true` until a container is found that genuinely needs
> otherwise.

## Where it is actually checked

Neither trait is consulted automatically by any reloco container or
algorithm — like `is_trivially_relocatable`, they exist as a customization
point for *callers* to assert on at whatever API boundary crosses a thread.
`reloco::guarded_mutex<T, MutexT>` (see `guarded_mutex.hpp`) is the one
place reloco itself does this today:

```cpp
template <typename T, typename MutexT = mutex> class guarded_mutex {
  static_assert(is_send_v<T>, "guarded_mutex<T>: T must be Send ...");
  // ...
};
```

This matches Rust's own bound on `std::sync::Mutex<T>`: the mutex
synchronizes every access to the guarded value, so only `T: Send` is
required — not `T: Sync` — because only one thread ever touches `T` at a
time (whichever thread currently holds the lock). A `T` that is unsound to
*transfer* to another thread at all (`reloco::rc<U>`/`reloco::weak_rc<U>`)
is still unsound to guard this way, since `lock()`/`try_lock()` hand
ownership of that access to whichever thread acquires the lock next; the
`static_assert` above rejects `guarded_mutex<rc<U>>` at compile time instead
of leaving a latent data race for the guarded non-atomic refcount.

## Opting a type in or out

Specialize `is_send`/`is_sync` for your own type once you have verified
whether it holds non-atomic state shared across clones/handles (opt out,
`false`) or is a plain value/composition of already-verified `Send`/`Sync`
types (the default `true` already covers this; no specialization needed):

```cpp
class single_threaded_cache {
  mutable std::size_t hit_count_; // Not atomic: only safe from one thread.
public:
  // ...
};

template <> struct reloco::is_send<single_threaded_cache> : std::true_type {};  // Fine to hand to another thread...
template <> struct reloco::is_sync<single_threaded_cache> : std::false_type {}; // ...but never to share concurrently.
```

Do **not** leave a type at the default `true` for `is_sync` if any of its
`const`-qualified operations mutate unsynchronized shared state (a mutable
cache, a lazily-computed field written without a lock/atomic) — that default
is only correct for types with no such hidden interior mutability at all.

## Why this matters

`reloco` targets exactly the kind of code where an unsound cross-thread
handoff is catastrophic and hard to reproduce (a data race on a refcount
that only manifests under contention): embedded firmware, RTOS tasks, and
interrupt paths, per the project's own goals (see the root
[README](../README.md)). `is_send<T>`/`is_sync<T>` give any future
thread-crossing API (a thread pool's `submit`, a channel's `send`, or a new
container) a single, already-verified customization point to
`static_assert` against, exactly like `guarded_mutex<T>` already does,
instead of every author inventing (and getting slightly wrong) their own ad
hoc thread-safety convention.
