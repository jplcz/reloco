<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# demos

Small, standalone `main()` programs showing how `reloco`'s safe threading
primitives fit together in practice. Each one is a single `.cpp` file with
its own file-level doc comment explaining what it demonstrates and why;
read the source directly for the full commentary.

| Demo | Shows |
|---|---|
| [`scoped_workers.cpp`](scoped_workers.cpp) | `scope()` + `guarded_mutex<T>` + `barrier`: a fixed worker pool that borrows stack-local state directly (no heap capture needed) and synchronizes in lock-step waves |
| [`channel_pipeline.cpp`](channel_pipeline.cpp) | `channel<T>()` + `receiver<T>::recv_timeout()`: a producer/consumer pipeline, including a bounded-wait receive and clean shutdown when every `sender<T>` is dropped |
| [`once_lock_config.cpp`](once_lock_config.cpp) | `once_lock<T>::get_or_init`: many threads racing to lazily initialize one shared value, with the initializer closure guaranteed to run exactly once |
| [`park_ping_pong.cpp`](park_ping_pong.cpp) | `this_thread::current()`/`park()`/`park_timeout()`/`unpark()`: a direct handshake between two threads with no channel/mutex/condvar involved, plus a deliberate `park_timeout()` timeout |

See [`docs/reference.md`](../docs/reference.md) for full API documentation
of every type used here, and [`docs/futex.md`](../docs/futex.md) for the
low-level primitive (`futex_word`/`futex_wait`/`futex_wake_*`) that
`barrier`, `park.hpp`, and `scope()` are all built on.

## Building

Demos are built by default alongside the test suite in a development
checkout. To build only the demos:

```sh
cmake -B build -DJPLCZ_RELOCO_BUILD_TESTS=OFF -DJPLCZ_RELOCO_BUILD_HEADER_CHECKS=OFF -DJPLCZ_RELOCO_BUILD_DEMOS=ON
cmake --build build --target jplcz_reloco_demos
```

Each demo is also its own individually buildable target (`scoped_workers`,
`channel_pipeline`, `once_lock_config`, `park_ping_pong`).
