<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Low-level word-wait/wake: `futex.hpp`

## What it is for

`include/reloco/futex.hpp` defines a "futex"-like word-wait/wake
primitive: `reloco::futex_word` (a plain `std::atomic<std::uint32_t>`)
plus

```cpp
void futex_wait(const futex_word &word, std::uint32_t expected) noexcept;
void futex_wake_one(futex_word &word) noexcept;
void futex_wake_all(futex_word &word) noexcept;
```

This is the building block a handful of higher-level reloco primitives can
use instead of a full `mutex` + `condition_variable` pair (see
[`mutex.hpp`](reference.md)) when all they actually need is "block until
this word changes" / "wake whoever is blocked on this word":
`barrier.hpp`, `scope.hpp`, and `once_lock.hpp` all use it today;
`fallible_singleton.hpp` and `tls_provider.hpp`'s
`RELOCO_TLS_MODEL_PTHREAD` backend are natural future users of the same
primitive.

`futex_wait` tolerates spurious wakeups (it may return even though `word`
still equals `expected`), exactly like `condition_variable::wait` --
callers must always re-check their own condition in a loop, never assume a
single `futex_wait` call implies the word actually changed (see
`barrier.hpp` for the expected calling pattern).

## Backend selection

Backend selection mirrors `mutex.hpp`'s `RELOCO_MUTEX_BACKEND_*`
customization point -- select **at most one**:

- **`RELOCO_FUTEX_BACKEND_LINUX`**: raw `futex(2)` syscall
  (`FUTEX_WAIT_PRIVATE`/`FUTEX_WAKE_PRIVATE`, via a direct `syscall()` call
  -- no glibc futex wrapper is used or required). Implemented in
  `futex_linux.ipp`. **Not exercised by this repository's test suite --
  review before relying on it in production.**
- **`RELOCO_FUTEX_BACKEND_FREEBSD`**: `_umtx_op(2)`
  (`UMTX_OP_WAIT_UINT`/`UMTX_OP_WAKE`). Implemented in
  `futex_freebsd.ipp`. **Also not exercised by this repository's test
  suite -- review before relying on it in production.**
- **`RELOCO_FUTEX_BACKEND_CUSTOM`**: suppresses the built-in
  declarations/definitions below entirely; the application/kernel
  supplies its own `reloco::futex_word`/`futex_wait`/`futex_wake_one`/
  `futex_wake_all` matching this exact API, in its own header, included
  through the normal path -- the same escape hatch
  `RELOCO_MUTEX_BACKEND_CUSTOM`/`RELOCO_DEFAULT_ALLOCATOR_CUSTOM` provide
  elsewhere. This is the intended path for a target `futex.hpp` has no
  built-in backend for at all, e.g. **FreeBSD kernel** code (as opposed to
  `RELOCO_FUTEX_BACKEND_FREEBSD`'s *userspace* `_umtx_op(2)`), which has
  its own kernel-primitive wait channel API (`msleep(9)`/`wakeup(9)`)
  instead:

```cpp
// reloco_user_config.hpp
#define RELOCO_FUTEX_BACKEND_CUSTOM

// my_freebsd_kernel_futex.hpp -- included normally elsewhere, e.g.
// from a source file, before any use of reloco::futex_wait/
// futex_wake_one/futex_wake_all. Documentation/reference only: never
// compiled or exercised by this repository (which targets hosted
// userspace, not the FreeBSD kernel proper) -- review and adapt before
// relying on it.
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>

namespace reloco {

using futex_word = std::atomic<std::uint32_t>;

inline void futex_wait(const futex_word &word, std::uint32_t expected) noexcept {
  // msleep(9) re-checks `word` under `lock` immediately before sleeping,
  // so a wakeup() racing just ahead of this call is never lost -- the
  // same guarantee _umtx_op(2)/futex(2) give in userspace, just expressed
  // via the kernel's own wait-channel API instead of a syscall.
  mtx_lock(&futex_kernel_lock);
  if (word.load(std::memory_order_acquire) == expected)
    msleep(&word, &futex_kernel_lock, PPAUSE, "rlfutex", 0);
  mtx_unlock(&futex_kernel_lock);
}

inline void futex_wake_one(futex_word &word) noexcept {
  mtx_lock(&futex_kernel_lock);
  wakeup_one(&word);
  mtx_unlock(&futex_kernel_lock);
}

inline void futex_wake_all(futex_word &word) noexcept {
  mtx_lock(&futex_kernel_lock);
  wakeup(&word);
  mtx_unlock(&futex_kernel_lock);
}

} // namespace reloco
```

(`futex_kernel_lock` above stands in for whatever single kernel `mtx` the
real integration serializes `msleep`/`wakeup` calls with -- `msleep(9)`
requires one already held on entry; a real implementation would likely
hash `&word` across a small table of such locks, exactly like
`futex_std.ipp`'s userspace "parking lot", rather than a single global
one.)

- **None defined (default)**: a portable "parking lot" fallback --
  implemented in `futex_std.ipp` entirely on top of `mutex.hpp`'s
  `mutex`/`condition_variable` (a small fixed table of buckets, each a
  `mutex` + `condition_variable`, selected by hashing `&word`; see
  `futex_std.ipp`'s file-level comment for the collision-safety argument)
  -- always available on any hosted target `mutex.hpp` itself supports, no
  OS-specific futex-like syscall required.

## Shared-library participation

Every `futex_wait`/`futex_wake_one`/`futex_wake_all` declaration is
`RELOCO_API`-decorated (see `detail/compat.hpp`) so the built-in fallback
backend participates in reloco's header-only/`RELOCO_SHARED` split exactly
like `mutex.hpp`'s own classes: out-of-line definitions live in
`futex_std.ipp`/`futex_linux.ipp`/`futex_freebsd.ipp`, included from
`futex.hpp` only when `RELOCO_SHARED_PROVIDE_DEFINITIONS` is `1`.

See [Shared-library deployments](shared-library.md) for the general
`RELOCO_SHARED`/`RELOCO_API` mechanism this reuses.
