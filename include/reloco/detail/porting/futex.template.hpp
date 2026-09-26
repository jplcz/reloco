// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file futex.template.hpp
 * @brief Documentation-only scaffold for `RELOCO_FUTEX_BACKEND_CUSTOM`.
 *
 * Never `#include`d by anything -- copy this file to
 * `detail/porting/futex.hpp` (dropping `.template`), fill it in for your
 * actual target, and define `RELOCO_FUTEX_BACKEND_CUSTOM` (see
 * `reloco/futex.hpp`/`reloco/reloco_config.hpp`/`docs/futex.md`), or
 * point the `JPLCZ_RELOCO_PORTING_HEADERS` CMake variable at a directory
 * containing your finished `futex.hpp` and let the build do both for
 * you.
 *
 * Sketches, loosely, what a **FreeBSD kernel** port might look like --
 * `msleep(9)`/`wakeup(9)`, the kernel's own wait-channel primitive,
 * instead of `RELOCO_FUTEX_BACKEND_FREEBSD`'s *userspace* `_umtx_op(2)`.
 * This is illustrative, not exact or complete (`futex_kernel_lock` below
 * stands in for whatever single kernel `mtx` the real integration
 * serializes `msleep`/`wakeup` calls with -- a real implementation would
 * likely hash `&word` across a small table of such locks, exactly like
 * `futex_std.ipp`'s userspace "parking lot", rather than a single global
 * one), and is not compiled or exercised by this repository (which
 * targets hosted userspace, not the FreeBSD kernel proper).
 */

#include <sys/param.h>

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>

namespace reloco {

using futex_word = std::atomic<std::uint32_t>;

inline struct mtx futex_kernel_lock{};

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
