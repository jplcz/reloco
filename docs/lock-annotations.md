<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Annotating locks: where each annotation applies

How the lock-related annotations behave on **derived** and **wrapped** locks (the
typical kernel pattern: a `lock_object` for lockdep/witness, `spinlock_enter()` /
`spinlock_exit()`, then a call into the underlying structo spin lock). The checks
involved are `reloco-atomic-context` ([atomic-context.md](atomic-context.md)),
`reloco-lock-balance` and `reloco-lock-alias` ([lock-discipline.md](lock-discipline.md)).

## 1. Annotation reference

| Annotation | Put it on | Meaning | Seen through derivation? | Seen through a by-value member? |
|---|---|---|---|---|
| `RELOCO_ATOMIC_LOCK` | the innermost lock class | Spin-style lock: holding it is atomic context; guards over it become atomic scopes. | Yes, any derived class counts. | Yes, if the wrapper exposes `lock()`/`lock_shared()`/`read_lock()`/`write_lock()`. |
| `RELOCO_CAPABILITY(...)` (clang `capability`) | a sleeping/other lock class | Marks a lock for balance/alias checks and `-Wthread-safety`. Not atomic. | Yes | Yes (same rule) |
| `RELOCO_ENTER_ATOMIC` / `RELOCO_LEAVE_ATOMIC` | a **function** (`lock`, `spinlock_enter`, ...) | The call raises/lowers atomic depth and counts as an acquire/release for balance. | Inherited members keep their annotation. | No: annotate the wrapper's own `lock()`/`unlock()`. |
| `RELOCO_ATOMIC_SCOPE` | RAII guard class | A live object is atomic context. | Yes | n/a |
| `RELOCO_ATOMIC_GUARD` | guard class **template** | A specialisation over an atomic lock is a scope. | n/a | n/a |
| `RELOCO_MIGHT_SLEEP`, `RELOCO_ATOMIC_ONLY`, `RELOCO_ATOMIC_ENTRY`, `RELOCO_ATOMIC_EXEMPT` | functions | See atomic-context.md. | Per function; **not** inherited by overrides: repeat on overrides. | n/a |
| `RELOCO_ACQUIRES(n)`, `RELOCO_ACQUIRES_SHARED(n)`, `RELOCO_RELEASES(n)`, `RELOCO_TRY_ACQUIRES(n)`, `RELOCO_TRY_ACQUIRES_SHARED(n)` | a free function | Argument `n` is the lock; pairs like `lock()`/`unlock()` for lock-balance. See section 4. | n/a | n/a |
| `RELOCO_LEGACY_LOCK` | a legacy lock type | Pointers to it are accepted by lock-alias. | Yes | n/a |
| `RELOCO_LOCK_IMPL` | a function or class | Deliberately returns with a lock held / hands one over. Exempt from lock-balance and lock-alias. | Functions: no. Classes: yes. | n/a |

Recognition rules (what counts as "a lock"):

1. The class, its template pattern, or **any base class** carries the annotation.
2. Or the class exposes a lock-style member (`lock`, `lock_shared`, `read_lock`,
   `write_lock`) and has a **by-value** member (arrays included) that is a lock by rule 1 or 2.
   Pointer and reference members never count: those are guards borrowing a lock.

Members of any lock, guard or `RELOCO_LOCK_IMPL` class are exempt from
`reloco-lock-balance` and `reloco-lock-alias`, because implementing a lock means
returning with it held. Callers are checked normally.

## 2. Deriving from a spin lock

```cpp
// Derived from a structo lock: the base's RELOCO_ATOMIC_LOCK is seen through the base
// list, so this needs no class annotation of its own.
class debug_spin_lock : public structo::ticket_spin_lock {
public:
  // Override that adds bookkeeping before delegating. Annotations on virtual or
  // hidden members are per function, so repeat the ones the callers rely on:
  // ENTER_ATOMIC makes callers atomic after this call, and lets lock-balance
  // pair it with unlock(). Without it, callers would not be atomic.
  RELOCO_ENTER_ATOMIC void lock() noexcept {
    lockdep_acquire(this);                 // bookkeeping only, no sleeping
    structo::ticket_spin_lock::lock();     // base member; already ENTER_ATOMIC
  }
  RELOCO_LEAVE_ATOMIC void unlock() noexcept {
    structo::ticket_spin_lock::unlock();
    lockdep_release(this);
  }
};

// Callers get full checking with no further annotation:
void use(debug_spin_lock &l) noexcept {
  auto g = std::lock_guard<debug_spin_lock>(l);  // RELOCO_ATOMIC_GUARD template over an atomic lock: scope
  sleepy();   // reloco-atomic-context: sleeping under a spin lock
}
```

If you only *add* members and keep the base's `lock()`/`unlock()`, you need no
annotation at all.

## 3. Wrapping a spin lock (FreeBSD-style)

```cpp
// Wrapper owning a lock by value and exposing lock()/unlock(): recognised as an
// atomic lock because m_lock is one. Nothing is annotated on the class itself.
class mtx_spin {
public:
  // spinlock_enter() is declared RELOCO_ENTER_ATOMIC in the kernel headers
  // (critical section + interrupts off), spinlock_exit() RELOCO_LEAVE_ATOMIC. State the same
  // contract on the wrapper's own API, since callers cannot see the body.
  RELOCO_ENTER_ATOMIC void lock() noexcept {
    spinlock_enter();
    lock_object_acquire(&m_lo);   // lock_object: lockdep/witness record, a plain struct
    m_lock.lock();
  }
  RELOCO_LEAVE_ATOMIC void unlock() noexcept {
    m_lock.unlock();
    lock_object_release(&m_lo);
    spinlock_exit();
  }

private:
  lock_object m_lo;                       // not a lock: no lock-style API, ignored
  structo::ticket_spin_lock m_lock;       // innermost lock, carries RELOCO_ATOMIC_LOCK
};
```

A wrapper that only exposes accessors (no `lock()` and friends) is **not** a lock,
and neither is a class holding a lock by pointer or reference.

If the wrapper deliberately hands the held lock to something else (a guard
adopting it after `try_lock()`), mark that function `RELOCO_LOCK_IMPL`:

```cpp
// Returns an engaged guard that now owns the lock; the lock is intentionally held on return.
RELOCO_LOCK_IMPL [[nodiscard]] reloco::optional<guard> try_lock() & noexcept;
```

## 4. Legacy locks and incremental migration

Real code replaces legacy locks piece by piece, so legacy types and C-style APIs
(`struct mtx`, `mtx_lock(&m)`) have to be checkable *without* editing the legacy
headers. Put the annotations in a **shim header** that you include instead of the
legacy header:

```cpp
// kernel_locks_shim.hpp: the only file that knows about both worlds.

// 1. Forward-declare the legacy type WITH its attributes BEFORE the real header is included.
//    The later definition inherits them (clang drops attributes added after a definition).
//    capability: the type is a lock for lock-balance / lock-alias / -Wthread-safety.
//    RELOCO_LEGACY_LOCK: C APIs pass `struct mtx *` everywhere, so lock-alias accepts pointers to it.
struct __attribute__((capability("mutex"))) RELOCO_LEGACY_LOCK mtx;
// For a spin-style lock that makes the holder atomic, use RELOCO_ATOMIC_LOCK instead of capability.
struct RELOCO_ATOMIC_LOCK RELOCO_LEGACY_LOCK mtx_spin;

#include <sys/mutex.h>  // the unmodified legacy header

// 2. Describe the free-function API. The number is the zero-based parameter that names the lock.
//    ACQUIRES / RELEASES pair like m.lock() / m.unlock(); TRY_ACQUIRES holds the lock only when the
//    function returns non-zero. Add RELOCO_ENTER_ATOMIC / RELOCO_LEAVE_ATOMIC to say the call enters
//    or leaves atomic context (spin mutexes, spinlock_enter()).
RELOCO_ACQUIRES(0)     void mtx_lock(struct mtx *m);
RELOCO_RELEASES(0)     void mtx_unlock(struct mtx *m);
RELOCO_TRY_ACQUIRES(0) int  mtx_trylock(struct mtx *m);
RELOCO_ENTER_ATOMIC RELOCO_ACQUIRES(0) void mtx_lock_spin(struct mtx_spin *m);
RELOCO_LEAVE_ATOMIC RELOCO_RELEASES(0) void mtx_unlock_spin(struct mtx_spin *m);
```

Rules for free-function locks:

- The lock expression is the argument at that index, with a leading `&` removed:
  `mtx_lock(&sc->lock)` and `mtx_lock(p)` are tracked per function, as places.
  `mtx_lock(&a); mtx_unlock(p)` where `p` aliases `a` is **not** matched (different places).
- Functions carrying these annotations are lock implementations: their own bodies are not
  checked for balance, so a wrapper like `my_lock(m) { mtx_lock(m); }` is fine when annotated
  the same way.
- Pointers to a `RELOCO_LEGACY_LOCK` type never trigger `reloco-lock-alias`. As code migrates,
  drop `RELOCO_LEGACY_LOCK` from the type to get the strict rule.
- To leave a legacy lock entirely unchecked, simply do not annotate it.

## 5. Checklist for a new lock class

1. Innermost lock: `RELOCO_ATOMIC_LOCK` (spin-style) or `RELOCO_CAPABILITY` (sleeping),
   `RELOCO_ENTER_ATOMIC`/`RELOCO_LEAVE_ATOMIC` on `lock`/`unlock`; add `RELOCO_MIGHT_SLEEP`
   on blocking acquires.
2. Derived or wrapper class: add nothing to the class; repeat function annotations on
   every `lock`/`unlock`/`try_lock` you redefine.
3. RAII guard: `RELOCO_ATOMIC_SCOPE`, or `RELOCO_ATOMIC_GUARD` for a template.
4. Anything that returns with a lock held on purpose: `RELOCO_LOCK_IMPL`.
5. Kernel primitives like `spinlock_enter()`: `RELOCO_ENTER_ATOMIC`/`RELOCO_LEAVE_ATOMIC`.

## 6. Worked example: a FreeBSD-style `mtx_spin` from reloco/structo parts

A kernel spin mutex layered on structo building blocks: `spinlock_enter()`/`spinlock_exit()`, a witness/lockdep record (`lock_object`), a FIFO ticket lock,
RAII guard, proof token, call wrapper and the legacy C API on top. This was compiled
and linted with `reloco-lock-balance`, `reloco-lock-alias` and `reloco-atomic-context`
(the `bad_*` functions at the end are reported, everything else is clean).

```cpp
#include <cstdint>
#include <reloco/atomic_context.hpp>       // RELOCO_ENTER_ATOMIC, RELOCO_ACQUIRES, RELOCO_LEGACY_LOCK, ...
#include <reloco/lifetime.hpp>             // RELOCO_LIFETIMEBOUND
#include <structo/sync/lock_guards.hpp>    // spin_guard, lock_held_token, detail::invoke_with_token
#include <structo/sync/ticket_spin_lock.hpp>

namespace kern {

// Platform layer. This is the Traits interface structo's lockers are parameterised on
// (spinlock_entry_guard<Traits> and ticket_spin_lock<Traits> use exactly these members).
// owner_type/current_owner(): identifies the CPU/thread for the ticket lock's owner stamp (never 0).
// spinlock_enter()/spinlock_exit(): FreeBSD's pair. Enter = critical section + interrupts off, with the
// previous interrupt state kept by the platform (per-thread, nesting aware), so the lock needs no
// saved-flags member. They make the code between them atomic, so they carry the same annotations as a
// lock: no sleeping until spinlock_exit() (reloco-atomic-context).
struct platform_traits {
  using owner_type = std::uintptr_t;
  static owner_type current_owner() noexcept;
  RELOCO_ENTER_ATOMIC static void spinlock_enter() noexcept;
  RELOCO_LEAVE_ATOMIC static void spinlock_exit() noexcept;
};

// Witness/lockdep record. A plain struct with no lock()/unlock(), so it is not a lock.
struct lock_object {
  unsigned class_id = 0;
  unsigned flags = 0;
};
void witness_checkorder(lock_object &lo) noexcept;  // lock-order check before acquiring
void witness_lock(lock_object &lo) noexcept;        // record acquisition
void witness_unlock(lock_object &lo) noexcept;      // record release

// RELOCO_LEGACY_LOCK: legacy C callers pass `mtx_spin *` around, so lock-alias accepts pointers
// to it. Drop it once the last such caller is migrated.
// No RELOCO_ATOMIC_LOCK is needed on this class: it holds a ticket_spin_lock by value (which is
// one) and exposes lock(), so the checks treat it as an atomic lock automatically.
class RELOCO_LEGACY_LOCK mtx_spin {
  using raw_lock = structo::sync::ticket_spin_lock<platform_traits>;

public:
  mtx_spin() noexcept = default;
  mtx_spin(const mtx_spin &) = delete;             // locks are not copyable
  mtx_spin &operator=(const mtx_spin &) = delete;

  // RELOCO_ENTER_ATOMIC: callers are in atomic context after this returns (reloco-atomic-context)
  // and the call counts as an acquire (reloco-lock-balance). Both are stated here because callers
  // cannot see the body. The `&` qualifier blocks calls on temporaries.
  RELOCO_ENTER_ATOMIC void lock() & noexcept {
    platform_traits::spinlock_enter();  // critical section + interrupts off; state kept by the platform
    witness_checkorder(m_lo);
    m_lock.lock();                      // ticket lock: FIFO, softlock-detected; already ENTER_ATOMIC
    witness_lock(m_lo);
  }

  // RELOCO_LEAVE_ATOMIC: ends the atomic region started by lock().
  RELOCO_LEAVE_ATOMIC void unlock() & noexcept {
    witness_unlock(m_lo);
    m_lock.unlock();
    platform_traits::spinlock_exit();   // reverse order of lock(): leave the spinlock section last
  }

  // No RELOCO_ENTER_ATOMIC: try_lock() only holds the lock when it returns true, which the balance
  // check understands from the `if (m.try_lock())` shape. [[nodiscard]] forces callers to look.
  [[nodiscard]] bool try_lock() & noexcept {
    platform_traits::spinlock_enter();
    if (!m_lock.try_lock()) {
      platform_traits::spinlock_exit();  // failed: undo the section, hold nothing
      return false;
    }
    witness_lock(m_lo);
    return true;
  }

  // RAII guard. spin_guard<L> is a RELOCO_ATOMIC_SCOPE: while it lives the code is atomic, and the
  // balance check ignores it because its destructor releases. RELOCO_LIFETIMEBOUND: the guard
  // refers to *this and must not outlive the mutex. g.token() mints a lock_held_token.
  [[nodiscard]] structo::sync::spin_guard<mtx_spin> scoped_lock() & noexcept RELOCO_LIFETIMEBOUND {
    return structo::sync::spin_guard<mtx_spin>(*this);
  }

  // Call wrapper. RELOCO_ATOMIC_CALLBACK: the callable runs atomic, so a sleeping lambda is
  // reported. It receives (or ignores) a lock_held_token, same as every structo lock.
  template <typename F> decltype(auto) with_lock(F &&f RELOCO_ATOMIC_CALLBACK) & noexcept {
    auto g = scoped_lock();
    return structo::sync::detail::invoke_with_token(f, g.token());
  }

private:
  lock_object m_lo;   // witness record
  raw_lock m_lock;    // innermost lock: carries RELOCO_ATOMIC_LOCK, so this class counts as one
};

// ---- Legacy C API, kept during the migration (declared in the shim header, section 4) ----
// ENTER/LEAVE_ATOMIC: callers become / stop being atomic. ACQUIRES(0)/RELEASES(0): argument 0 is
// the lock, so mtx_lock_spin(&m) pairs with mtx_unlock_spin(&m) exactly like m.lock()/m.unlock().
// These bodies are lock implementations, so they may return with the lock held.
RELOCO_ENTER_ATOMIC RELOCO_ACQUIRES(0) void mtx_lock_spin(mtx_spin *m) noexcept { m->lock(); }
RELOCO_LEAVE_ATOMIC RELOCO_RELEASES(0) void mtx_unlock_spin(mtx_spin *m) noexcept { m->unlock(); }

// ---- Callers ----
RELOCO_MIGHT_SLEEP void sleepy() noexcept;                         // e.g. waits on a condvar
void touch_run_queue(structo::sync::lock_held_token held) noexcept; // demands proof a spin lock is held

void ok(mtx_spin &m) noexcept {
  auto g = m.scoped_lock();      // preferred: released on every path
  touch_run_queue(g.token());    // the token proves the lock is held; callers cannot forge one
}

void ok_with(mtx_spin &m) noexcept {
  m.with_lock([](structo::sync::lock_held_token held) noexcept { touch_run_queue(held); });
}

void legacy_ok(mtx_spin &m) noexcept {
  mtx_lock_spin(&m);             // free-function API is tracked too
  mtx_unlock_spin(&m);
}

// A bare spinlock section, e.g. around per-CPU data. Atomic: no sleeping until spinlock_exit().
void touch_per_cpu() noexcept;
void ok_spinlock_section() noexcept {
  platform_traits::spinlock_enter();
  touch_per_cpu();
  platform_traits::spinlock_exit();
}

// ---- What the checks report ----
void bad_leak(mtx_spin &m, bool early) noexcept {
  m.lock();                      // reloco-lock-balance: not unlocked on the `early` path
  if (early)
    return;
  m.unlock();
}

void bad_sleep(mtx_spin &m) noexcept {
  auto g = m.scoped_lock();
  sleepy();                      // reloco-atomic-context: sleeping under a spin lock
}

void bad_sleep_spinlock_section() noexcept {
  platform_traits::spinlock_enter();
  sleepy();                      // reloco-atomic-context: sleeping inside a spinlock section
  platform_traits::spinlock_exit();
}

void bad_sleep_legacy(mtx_spin &m) noexcept {
  mtx_lock_spin(&m);
  sleepy();                      // reloco-atomic-context: ENTER_ATOMIC on the legacy call
  mtx_unlock_spin(&m);
}

} // namespace kern
```

### Plugging `mtx_spin` into the RAII lockers

Because `mtx_spin` has the standard `lock()` / `unlock()` / `try_lock()` surface, every
locker in structo and reloco works with it unchanged. No adapter is needed:

```cpp
#include <reloco/guarded_mutex.hpp>
#include <reloco/unique_lock.hpp>
#include <structo/sync/guarded_spin_mutex.hpp>

// Data that can only be reached while the lock is held. The third template parameter (IrqLocker)
// stays at its default: mtx_spin already enters a spinlock section (interrupts off) itself.
// gm.lock() returns a guard (an atomic scope); `*g` is the protected int.
void use_guarded(structo::sync::guarded_spin_mutex<int, kern::mtx_spin> &gm) noexcept {
  auto g = gm.lock();
  *g += 1;
}

// reloco's generic locker: works on anything with lock()/try_lock()/unlock(). It can unlock early.
void use_unique(kern::mtx_spin &m) noexcept {
  reloco::unique_lock<kern::mtx_spin> l(m);
  l.unlock();
}

// reloco's guarded_mutex accepts any lockable as its MutexT.
void use_guarded_reloco(reloco::guarded_mutex<int, kern::mtx_spin> &gm) noexcept {
  auto g = gm.lock();
  *g += 1;
}
```

The guards over `mtx_spin` are atomic scopes (the class counts as an atomic lock), the
balance check ignores them (their destructors release), and none of these functions
produce findings.

What each piece buys you:

| Piece | Provided by | Checked by |
|---|---|---|
| Atomic region | `RELOCO_ENTER_ATOMIC` / `RELOCO_LEAVE_ATOMIC` on `lock`/`unlock`, `RELOCO_ATOMIC_SCOPE` on the guard | `reloco-atomic-context` |
| Pairing on every path | acquire/release functions and guard destructors | `reloco-lock-balance` |
| Proof of holding | `lock_held_token` from `g.token()` | the compiler (token has a private constructor) |
| No dangling guard | `RELOCO_LIFETIMEBOUND`, `&`-qualified members | clang lifetime checks |
| Legacy API | `RELOCO_ACQUIRES(n)`, `RELOCO_RELEASES(n)`, `RELOCO_LEGACY_LOCK` | both lock checks |

For a sleeping `mtx` (FreeBSD `MTX_DEF`), do the same around `reloco::mutex`: put
`RELOCO_MIGHT_SLEEP` on `lock()`, omit `RELOCO_ENTER_ATOMIC`, and use `RELOCO_CAPABILITY`
instead of relying on a spin lock member.

## 7. Limits

- Wrapper recognition is a heuristic (lock-style member plus by-value lock member);
  when it is wrong, annotate the wrapper class explicitly with `RELOCO_ATOMIC_LOCK`
  or `RELOCO_CAPABILITY`.
- A wrapper's `lock()` is not inferred to enter atomic context from its body; annotate it.
- Function annotations are not inherited by overrides.
- Free-function tracking is per function and per place; locks reached through a call result or array index are not tracked.
