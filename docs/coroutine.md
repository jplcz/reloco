<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Coroutines: `coroutine.hpp`

C++20+ only, GCC/Clang only (on older standards the header is empty and
`RELOCO_HAS_COROUTINES` is 0). No exceptions: failures travel as
`reloco::result`, and an exception escaping a coroutine traps.

```cpp
#include <reloco/coroutine.hpp>

// Fallible helper returning a plain result<int>.
reloco::result<int> parse(int v);

// `allocator_arg, <allocator_ref>` as the first parameters selects where the
// coroutine frame lives; omit them to use reloco::default_allocator().
reloco::task<int> work(reloco::allocator_arg_t, reloco::allocator_ref alloc, int v) {
  // co_await on a result<U> yields U; on error the coroutine ends with that
  // error (like RELOCO_TRY), and its locals are destroyed normally.
  int parsed = co_await parse(v);

  // Awaiting another task yields its result<T>; await that again to unwrap/propagate.
  auto inner = co_await work(reloco::allocator_arg, alloc, parsed - 1);
  int sub = co_await std::move(inner);

  // co_return a value, or an error via reloco::unexpected (task<void> uses
  // `co_await reloco::unexpected(e)` instead).
  if (sub > 100)
    co_return reloco::unexpected(reloco::error::out_of_range);
  co_return sub;
}

// Drive a top-level task: tasks are lazy, so resume() starts it. Resume again
// whenever an awaitable it is blocked on completes, until done().
auto t = work(reloco::allocator_arg, alloc, 5);
t.resume();
if (t.done()) {
  reloco::result<int> r = t.take(); // value or error, taken once
}
```

## Notes

- **Allocation failure** is not fatal: the returned task is already `done()`
  and `take()` yields `error::allocation_failed`; awaiting it propagates it.
- **Member coroutines** take `allocator_arg, alloc` right after the implicit
  object, same as free functions.
- The allocator is stored in a header before the frame and used to free it, so
  its backing context must outlive the task.
- Write coroutines as functions, not capturing lambdas: a temporary lambda dies
  before the frame that refers to it.
- Tested with Clang 24 and GCC 15 (ASan/UBSan clean).
