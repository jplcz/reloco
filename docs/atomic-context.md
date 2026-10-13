# Atomic-context checking (`reloco-atomic-context`)

`reloco-atomic-context` is a reloco-tidy check that finds **sleeping inside atomic context** and
**atomic-only calls made outside it**, statically. Annotations live in
`<reloco/atomic_context.hpp>`; every macro expands to nothing when `clang::annotate` is unavailable.

## Rules

1. A call to a `RELOCO_MIGHT_SLEEP` function (or a function that transitively calls one, through
   visible bodies) is reported while atomic depth may be > 0 on any path reaching it.
2. A call to a `RELOCO_ATOMIC_ONLY` function is reported when atomic depth is 0 on some path.
3. A function marked both `RELOCO_MIGHT_SLEEP` and `RELOCO_ATOMIC_ONLY`/`RELOCO_ATOMIC_ENTRY` is
   reported at its declaration.
4. Atomic depth rises when a scope object comes into existence (constructor, by-value return, copy/move)
   and falls when it is destroyed; `RELOCO_ENTER_ATOMIC` / `RELOCO_LEAVE_ATOMIC` calls do the same
   explicitly (leave clamps at 0).
5. The body of an `RELOCO_ATOMIC_ONLY`/`RELOCO_ATOMIC_ENTRY` function, or of one taking a
   `RELOCO_ATOMIC_TOKEN` parameter, starts at depth 1.
6. A lambda is analysed in the state where it is created; a lambda passed to a
   `RELOCO_ATOMIC_CALLBACK` parameter gets one extra level.

## Macros

| Macro | Placed on | Meaning |
|---|---|---|
| `RELOCO_MIGHT_SLEEP` | function / member | May block. Annotate individual members, not whole mutex classes (`try_lock`/`unlock` don't sleep). |
| `RELOCO_ATOMIC_ONLY` | function | Callable only in atomic context. |
| `RELOCO_ATOMIC_ENTRY` | function | Body runs atomic (e.g. interrupt handler); may not sleep. |
| `RELOCO_ATOMIC_SCOPE` | class | Live object = atomic context (irq/preemption guards). |
| `RELOCO_ATOMIC_LOCK` | class | Lock type; guards over it become scopes. |
| `RELOCO_ATOMIC_GUARD` | class template | Specialisation over an `ATOMIC_LOCK` type is a scope. |
| `RELOCO_ENTER_ATOMIC` / `RELOCO_LEAVE_ATOMIC` | function | Explicit depth +1 / -1 (`lock()` / `unlock()`). |
| `RELOCO_ATOMIC_CALLBACK` | parameter | Callable argument runs atomic. |
| `RELOCO_ATOMIC_TOKEN` | class | Proof-of-atomic-context parameter type. |
| `RELOCO_ATOMIC_EXEMPT` | function | Suppress findings and sleep inference. |
| `RELOCO_ACQUIRES(n)` / `RELOCO_RELEASES(n)` / `RELOCO_TRY_ACQUIRES(n)` / `RELOCO_LEGACY_LOCK` | function / type | Free-function and legacy lock support (see [lock-annotations.md](lock-annotations.md)). |
| `RELOCO_LOCK_IMPL` | function | Lock implementation: may return with a lock held (see [lock-discipline.md](lock-discipline.md)). |

## Example

```cpp
#include <reloco/atomic_context.hpp>

// Sleeps: waits for a mutex.
RELOCO_MIGHT_SLEEP void wait_for_io();

// Scope class: while an object lives, we are atomic.
struct RELOCO_ATOMIC_SCOPE irq_off { irq_off(); ~irq_off(); };

// Needs atomic context (e.g. touches CPU-local data).
RELOCO_ATOMIC_ONLY void touch_cpu_local();

void f() {
  touch_cpu_local();      // reported: not atomic here
  {
    irq_off g;            // depth 1
    touch_cpu_local();    // ok
    wait_for_io();        // reported: sleeping in atomic context
  }
  wait_for_io();          // ok: scope ended
}

// Token parameter: caller must prove atomic context; body starts at depth 1.
struct RELOCO_ATOMIC_TOKEN irq_token {};
void g(irq_token);

// Callback runs atomic; a sleeping call inside the lambda is reported.
template <class F> void with_irq_off(F &&fn RELOCO_ATOMIC_CALLBACK);
```

## Exemptions

`RELOCO_ATOMIC_EXEMPT` on the function, `// NOLINT(reloco-atomic-context)` on the line, or a
reloco unsafe region.

## Limits

- One atomic level; IRQ-off, preemption-off and held spinlock are not distinguished. `core_pin_guard`
  (migration only) is deliberately **not** a scope, and `per_cpu_ptr::get()` is not atomic-only.
- Joins are path-insensitive: `if (f) lock(); ... if (f) unlock();` leaves depth possibly 1 and may
  produce a false positive (use NOLINT).
- Lambdas are judged where created, not where invoked.
- `try_lock` is not an enter.
- Sleep inference only follows visible bodies and does not descend into lambdas.

## Integration

reloco annotates: mutex/recursive_mutex/shared_mutex `lock`/`lock_shared`, `condition_variable::wait*`,
`thread::join` (`MIGHT_SLEEP`); `spin_lock` (`ATOMIC_LOCK` with enter/leave); `unique_lock`
(`ATOMIC_GUARD`). structo annotates `irq_guard`, `preemption_guard`, `spinlock_entry_guard` and the
spinlock guards (`ATOMIC_SCOPE`), their tokens (`ATOMIC_TOKEN`), `with_*` helpers
(`ATOMIC_CALLBACK`) and the kernel spinlocks (`ATOMIC_LOCK`). Run with
`clang-tidy --checks='-*,reloco-atomic-context'`.
