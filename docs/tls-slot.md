# `tls_slot<T>`

`include/reloco/tls_slot.hpp`

A thread-local **cell** for an object that is more than plain bytes: it has a constructor that may allocate and
fail, and a destructor that must run when the thread exits. It is the C++ side of kif's `static thread` cells
(`docs/kif-proc-language.md` §4.17), and works by itself in any C++ code.

## Why it exists

Thread-local storage in a kernel cannot rely on the C++ runtime: there are no TLS constructors, memory may be
exhausted, and a thread may start with a block of zeroes. `tls_slot` therefore has three rules:

- **Zero is valid.** A slot in all-zero memory is *empty*. Put it in a zero-filled block and it works, with no
  constructor having run.
- **Built lazily, fallibly.** `try_with` builds the object on first use. The initialiser returns `result<T>`, may
  allocate, and its error is returned to the caller; the slot stays empty and can be retried.
- **Destroyed by its owner.** The destructor of the slot is trivial. The block that owns it calls `destroy()` at
  thread exit (kif generates that call).

## API

| Member | Meaning |
|---|---|
| `try_with(init, f)` / `try_with_mut(init, f)` | Build if empty, then run `f(const T &)` / `f(T &)`. `RELOCO_MIGHT_SLEEP`. Returns `result<R>`; `error::busy` if called from inside `init` for the same slot. |
| `with_ready(f)` / `with_ready_mut(f)` | Run `f` only if the slot is ready, never initialises. Returns `optional<R>`, or `bool` for a `void` closure. Usable where sleeping is not allowed. |
| `destroy()` | Run `T`'s destructor if ready, then become empty. Idempotent. Must not run while a closure of the slot executes. |
| `is_ready()` | Whether the object exists. |

Closures must not return a reference or a pointer (the object is only borrowed for the call).

## Example

```cpp
#include <reloco/tls_provider.hpp>
#include <reloco/tls_slot.hpp>
#include <reloco/vector.hpp>

// A thread's scratch buffer: it allocates, so it is built lazily and can fail.
using scratch = reloco::vector<int>;

reloco::result<scratch> make_scratch() noexcept {      // init_fn: result<T>(*)() noexcept
  scratch v;
  RELOCO_TRY(v.try_reserve(64));                       // may fail with resource_exhausted
  return v;
}

// One block per module: every slot of the module lives here, so the OS needs a single TLS key per module.
struct module_block {
  reloco::tls_slot<scratch> scratch_slot;              // zero == empty
};
struct module_tag {};                                  // distinguishes this module's block

reloco::result<void> record(int sample) noexcept {
  auto block = reloco::tls_provider<module_block, module_tag>::get();   // zero-filled per thread on first use
  RELOCO_TRY(block);
  return block->get().scratch_slot.try_with_mut(&make_scratch,          // init: runs only when empty
      [&](scratch &v) noexcept { return v.try_push_back(sample); });    // f: runs with the object
}

void thread_exit() noexcept {                          // the owner of the block destroys its slots
  if (auto block = reloco::tls_provider<module_block, module_tag>::get(); block.has_value()) {
    block->get().scratch_slot.destroy();
  }
}
```

## Integration

- **`tls_provider`** supplies the per-thread, zero-filled block (`RELOCO_TLS_MODEL`: `thread_local`, pthread keys,
  a custom OS backend, or single-threaded). `tls_slot` adds no provider of its own.
- **Thread exit.** Call `destroy()` on every slot of the block from the thread-exit hook of your target (on FreeBSD,
  an `osd_thread_register(9)` destructor). The hook of a *new* thread must not rely on any slot being ready.
- **kif.** `static thread` cells lower to a `module_block` per module plus a `tls_slot<T>` field per cell, and a
  generated exit hook that calls `destroy()`. A `drop` of a TLS cell must not reach another TLS cell (KIFP183).

## Tests

`tests/test_tls_slot.cpp`.
