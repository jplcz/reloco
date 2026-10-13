<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Lock discipline checks

Two `reloco-tidy` checks keep lock handling analysable:

| Check | Enforces |
|---|---|
| `reloco-lock-balance` | Every `lock`/`lock_shared`/`write_lock`/`read_lock` is released on every path out of the function. |
| `reloco-lock-alias` | Locks are not aliased through pointers or `&lock`, so the balance check can follow them. |

Both are enabled by default. Like the other checks they skip tests, demos and
third-party paths (`ExemptPaths` option; set it to `none` to lint those too).

## What is a "lock"

A class is a lock when it, a base class, or (for classes exposing `lock()`-style members) a by-value member carries clang's `capability` attribute (e.g. via
`RELOCO_CAPABILITY`) or `RELOCO_ATOMIC_LOCK`. Calls are classified by:

1. Clang `acquire_capability` / `release_capability` / `try_acquire_capability`.
2. `RELOCO_ENTER_ATOMIC` / `RELOCO_LEAVE_ATOMIC`.
3. Otherwise, on lock types only, by name: `lock`, `write_lock`, `lock_shared`,
   `read_lock` (acquire); `unlock`, `unlock_shared`, `read_unlock`,
   `write_unlock` (release); `try_lock`, `try_write_lock`, `try_lock_shared`,
   `try_read_lock` (conditional acquire).

## `reloco-lock-balance` rules

| # | Rule | Why |
|---|---|---|
| 1 | A lock acquired in a function must be released on **every** path out (returns, `break`, early exits). Reported at the acquire. "not unlocked on every path" = leaked on all paths; "may still be held" = leaked on some. | A leaked lock deadlocks the next caller. |
| 2 | Locking a lock the function definitely already holds is reported (not for shared locks or types whose name contains `recursive`). | Self-deadlock. |
| 3 | Unlocking a lock the function already released is reported. | Double unlock corrupts lock state. |
| 4 | `try_lock` counts as held only on the branch where its result is true: `if (l.try_lock())`, `if (!l.try_lock()) return;`. An unchecked result is ignored. | Matches real semantics. |
| 5 | Paths ending in a noreturn call (`abort`, trap, `RELOCO_ASSERT` failure) are not leaks. | The function never returns there. |
| 6 | RAII guards and `scoped_*()` objects are never tracked; they release in their destructor. Calls that return a guard (`guarded_mutex::lock()`) are not acquires. | Preferred style. |
| 7 | An unlock with no lock in the same function is allowed (ownership handed in). | Hand-off patterns. |
| 8 | Lambdas are analysed as their own functions. | They run separately. |

Untracked places (lock reached through a call result, array index, etc.) are
silently skipped; the check prefers missing a bug to a false positive.
References to locks are resolved to what they bind to.

### Opting out

Only lock *implementations* should return with a lock held (e.g. a wrapper
whose destructor or sibling `unlock()` releases it). These are exempt
automatically: functions with acquire/release attributes, members of lock,
guard (`RELOCO_ATOMIC_SCOPE`/`RELOCO_ATOMIC_GUARD`) or `ScopedLockable` classes.
Everything else:

```cpp
// This function intentionally returns with `l` locked; the caller unlocks.
RELOCO_LOCK_IMPL void lock_for_caller(spin_lock &l) noexcept { l.lock(); }

// Or, for a single line:
l.lock(); // NOLINT(reloco-lock-balance)
```

### Example

```cpp
void bad(spin_lock &l, bool early) noexcept {
  l.lock();               // flagged: not unlocked on the `early` path
  if (early)
    return;
  l.unlock();
}

void good(spin_lock &l, bool early) noexcept {
  auto g = l.scoped_lock();   // guard releases on every path
  if (early)
    return;
}
```

### Free-function lock APIs

Legacy APIs such as `mtx_lock(&m)` are tracked when the functions are annotated
`RELOCO_ACQUIRES(n)`, `RELOCO_ACQUIRES_SHARED(n)`, `RELOCO_RELEASES(n)`,
`RELOCO_TRY_ACQUIRES(n)` or `RELOCO_TRY_ACQUIRES_SHARED(n)` (`n` = index of the lock
parameter). All balance rules above apply unchanged. See
[lock-annotations.md](lock-annotations.md) for the shim-header pattern.

## `reloco-lock-alias` rules

| # | Rule | Why |
|---|---|---|
| 1 | No variable, field or parameter of type pointer-to-lock (or array of them). | A pointer is a second name the balance check cannot follow. |
| 2 | No `&lock` expressions. | Same. |
| 3 | References (`Lock &`) are fine. | They never rebind, and are tracked. |
| 4 | Types marked `RELOCO_LEGACY_LOCK` are exempt. | C APIs pass lock pointers everywhere; migrate first, tighten later. |

Lock implementations, `RELOCO_LOCK_IMPL` code and `RELOCO_LEGACY_LOCK` types are exempt (a guard must be
able to refer to its lock). Fix: pass `Lock &`, or reach the lock through its
owner (`owner.m_lock.lock()`).

```cpp
void bad(spin_lock *l) noexcept;     // flagged: pointer parameter
void good(spin_lock &l) noexcept;    // fine
```

## Limits

- Per-function analysis; no cross-function tracking (use clang's
  `-Wthread-safety` with `RELOCO_REQUIRES` for that).
- Joins are path-insensitive apart from `try_lock` results.
- Only the lock expressions listed above are tracked (variables, `this`,
  member chains).
