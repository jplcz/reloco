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
bool futex_wait_timeout(const futex_word &word, std::uint32_t expected, duration timeout) noexcept;
void futex_wake_one(futex_word &word) noexcept;
void futex_wake_all(futex_word &word) noexcept;
```

This is the building block a handful of higher-level reloco primitives can
use instead of a full `mutex` + `condition_variable` pair (see
[`mutex.hpp`](reference.md)) when all they actually need is "block until
this word changes" / "wake whoever is blocked on this word":
`barrier.hpp`, `scope.hpp`, `once_lock.hpp`, `fallible_singleton.hpp`'s
`atomic_fallible_singleton<T>`, `tls_provider.hpp`'s
`RELOCO_TLS_MODEL_PTHREAD` backend, and `park.hpp`'s `detail::parker`/
`this_thread::sleep_for` (backing `this_thread::park`/`park_timeout`/
`sleep_for`) all use it today.

`futex_wait` tolerates spurious wakeups (it may return even though `word`
still equals `expected`), exactly like `condition_variable::wait` --
callers must always re-check their own condition in a loop, never assume a
single `futex_wait` call implies the word actually changed (see
`barrier.hpp` for the expected calling pattern).

`futex_wait_timeout(word, expected, timeout)` is `futex_wait`'s bounded
counterpart, matching `condition_variable::wait_for`'s/`park_timeout`'s
own "may return early" contract: it returns `true` if it returned for any
reason *other* than the timeout definitely elapsing (a genuine wake, or a
spurious wakeup -- `word` may still equal `expected`), and `false` only
once `timeout` has definitely elapsed with no wake observed (`duration`
from [`duration.hpp`](reference.md)). Exactly like `futex_wait`, callers
must always re-check their own condition afterward regardless of the
return value.

## Backend selection

Backend selection mirrors `mutex.hpp`'s `RELOCO_MUTEX_BACKEND_*`
customization point -- select **at most one**:

- **`RELOCO_FUTEX_BACKEND_LINUX`**: raw `futex(2)` syscall
  (`FUTEX_WAIT_PRIVATE`/`FUTEX_WAKE_PRIVATE`, via a direct `syscall()` call
  -- no glibc futex wrapper is used or required; `futex_wait_timeout`
  passes `FUTEX_WAIT_PRIVATE`'s own *relative* `struct timespec` timeout
  argument directly, via `duration_cast<struct timespec>`). Implemented in
  `futex_linux.ipp`. **Auto-selected by default on Linux** (see below).
- **`RELOCO_FUTEX_BACKEND_FREEBSD`**: `_umtx_op(2)`
  (`UMTX_OP_WAIT_UINT`/`UMTX_OP_WAKE`; `futex_wait_timeout` additionally
  passes a relative `struct _umtx_time` timeout, per `uaddr1`/`uaddr2`'s
  size/pointer convention). Implemented in `futex_freebsd.ipp`.
  **Auto-selected by default on FreeBSD** (see below); not exercised by
  this repository's test suite (no FreeBSD CI runner) -- review before
  relying on it in production.
- **`RELOCO_FUTEX_BACKEND_STD`**: forces the portable "parking lot"
  fallback described below even on Linux/FreeBSD, overriding the
  auto-selected native backend -- the explicit opt-out this task asked
  for.
- **`RELOCO_FUTEX_BACKEND_CUSTOM`**: suppresses the built-in
  declarations/definitions below entirely; `futex.hpp` instead
  `#include`s a fixed path, `detail/porting/futex.hpp`, right where the
  built-in backend would otherwise appear -- so correctness never
  depends on where else the application/kernel includes its replacement
  from, unlike relying on include order. That file does not ship in this
  repository (only `detail/porting/futex.template.hpp`, an unused
  documentation-only scaffold, does); supply your own
  `reloco::futex_word`/`futex_wait`/`futex_wait_timeout`/
  `futex_wake_one`/`futex_wake_all` matching this exact API, most
  conveniently via the `JPLCZ_RELOCO_PORTING_HEADERS` CMake variable (see
  `CMakeLists.txt`), which copies it into that exact path and defines
  `RELOCO_FUTEX_BACKEND_CUSTOM` for you. This is the intended path for a
  target `futex.hpp` has no built-in backend for at all, e.g. **FreeBSD
  kernel** code (as opposed to `RELOCO_FUTEX_BACKEND_FREEBSD`'s
  *userspace* `_umtx_op(2)`), which has its own kernel-primitive wait
  channel API (`msleep(9)`/`wakeup(9)`) instead --
  `detail/porting/futex.template.hpp` sketches exactly that:

```cpp
// reloco_user_config.hpp
#define RELOCO_FUTEX_BACKEND_CUSTOM

// include/reloco/detail/porting/futex.hpp (this exact path/name).
// Documentation/reference only: never compiled or exercised by this
// repository (which targets hosted userspace, not the FreeBSD kernel
// proper) -- review and adapt before relying on it.
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

inline bool futex_wait_timeout(const futex_word &word, std::uint32_t expected, duration timeout) noexcept {
  // msleep_sbt(9) takes a relative sbintime_t (32.32 fixed-point seconds)
  // rather than msleep(9)'s coarser, hz-scaled tick count -- the same
  // sbintime_t conversion instant.hpp's own FreeBSD kernel example uses
  // for sbinuptime(). Passing pr == 0 asks for exact (no coalescing)
  // timer precision; flags == 0 keeps the timeout relative, matching
  // FUTEX_WAIT/UMTX_OP_WAIT_UINT's own relative-timeout convention in
  // userspace. EWOULDBLOCK means the timeout elapsed first, exactly like
  // ETIMEDOUT there.
  sbintime_t sbt = (static_cast<sbintime_t>(timeout.as_secs()) << 32) |
                   static_cast<sbintime_t>((static_cast<std::uint64_t>(timeout.subsec_nanos()) << 32) / 1'000'000'000ULL);
  mtx_lock(&futex_kernel_lock);
  int error = 0;
  if (word.load(std::memory_order_acquire) == expected)
    error = msleep_sbt(&word, &futex_kernel_lock, PPAUSE, "rlfutex", sbt, 0, 0);
  mtx_unlock(&futex_kernel_lock);
  return error != EWOULDBLOCK;
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

- **None defined -- default on any other target (not Linux/FreeBSD, or
  `RELOCO_FUTEX_BACKEND_STD` was defined)**: a portable "parking lot"
  fallback -- implemented in `futex_std.ipp` entirely on top of
  `mutex.hpp`'s `mutex`/`condition_variable` (a small fixed table of
  buckets, each a `mutex` + `condition_variable`, selected by hashing
  `&word`; see `futex_std.ipp`'s file-level comment for the
  collision-safety argument) -- always available on any hosted target
  `mutex.hpp` itself supports, no OS-specific futex-like syscall
  required.

### Auto-detected default

If none of `RELOCO_FUTEX_BACKEND_LINUX`/`_FREEBSD`/`_STD`/`_CUSTOM` is
defined, `futex.hpp` auto-selects the native backend by target OS --
`RELOCO_FUTEX_BACKEND_LINUX` when `__linux__` is defined,
`RELOCO_FUTEX_BACKEND_FREEBSD` when `__FreeBSD__` is defined -- exactly
like `mutex.hpp` auto-selects `RELOCO_MUTEX_BACKEND_PTHREAD` whenever
`<pthread.h>` is available. On any other target, or when
`RELOCO_FUTEX_BACKEND_STD` is explicitly defined, the portable "parking
lot" fallback above is used regardless of platform. Define
`RELOCO_FUTEX_BACKEND_STD` to opt out of the native backend on
Linux/FreeBSD without giving up the built-in implementation entirely
(e.g. to sidestep the "not exercised on FreeBSD CI" caveat above, or to
keep behavior identical across all your deployment targets).

## Shared-library participation

Every `futex_wait`/`futex_wait_timeout`/`futex_wake_one`/`futex_wake_all` declaration is
`RELOCO_API`-decorated (see `detail/compat.hpp`) so the built-in fallback
backend participates in reloco's header-only/`RELOCO_SHARED` split exactly
like `mutex.hpp`'s own classes: out-of-line definitions live in
`futex_std.ipp`/`futex_linux.ipp`/`futex_freebsd.ipp`, included from
`futex.hpp` only when `RELOCO_SHARED_PROVIDE_DEFINITIONS` is `1`.

See [Shared-library deployments](shared-library.md) for the general
`RELOCO_SHARED`/`RELOCO_API` mechanism this reuses.
