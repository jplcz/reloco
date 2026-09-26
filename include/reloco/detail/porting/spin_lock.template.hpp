// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file spin_lock.template.hpp
 * @brief Documentation-only scaffold for `RELOCO_SPIN_LOCK_BACKEND_CUSTOM`.
 *
 * Never `#include`d by anything -- copy this file to
 * `detail/porting/spin_lock.hpp` (dropping `.template`), fill it in for
 * your actual target, and define `RELOCO_SPIN_LOCK_BACKEND_CUSTOM` (see
 * `reloco/spin_lock.hpp`/`reloco/reloco_config.hpp`), or point the
 * `JPLCZ_RELOCO_PORTING_HEADERS` CMake variable at a directory containing
 * your finished `spin_lock.hpp` and let the build do both for you.
 *
 * Sketches, loosely, what a **FreeBSD kernel** port might look like --
 * backed by `mtx(9)`'s `MTX_SPIN` variant, which additionally disables
 * interrupts on the current CPU for as long as the lock is held (so it
 * is safe to take from an interrupt handler / with interrupts otherwise
 * masked, matching this header's own doc-comment rationale for when a
 * spinlock, not a blocking mutex, is required) and integrates with
 * `WITNESS`'s lock-order verification. This is illustrative, not exact or
 * complete, and is not compiled or exercised by this repository (which
 * targets hosted userspace, not the FreeBSD kernel proper). A Linux
 * kernel module would do the same wrapping `raw_spin_lock`/
 * `raw_spin_unlock`/`raw_spin_trylock` (`<linux/spinlock.h>`) instead.
 */

#include <sys/param.h>

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>

namespace reloco {

class RELOCO_CAPABILITY("mutex") spin_lock {
public:
  spin_lock() noexcept { mtx_init(&handle_, "reloco::spin_lock", nullptr, MTX_SPIN); }
  ~spin_lock() noexcept { mtx_destroy(&handle_); }

  spin_lock(const spin_lock &) = delete;
  spin_lock &operator=(const spin_lock &) = delete;

  void lock() & noexcept RELOCO_ACQUIRE() { mtx_lock_spin(&handle_); }
  void unlock() & noexcept RELOCO_RELEASE() { mtx_unlock_spin(&handle_); }
  [[nodiscard]] bool try_lock() & noexcept RELOCO_TRY_ACQUIRE(true) { return mtx_trylock_spin(&handle_) != 0; }

  /** @brief Best-effort, racy snapshot -- see `reloco::spin_lock::
   * is_locked()`'s own doc comment (`reloco/spin_lock.hpp`) for the exact
   * contract a replacement must keep. */
  [[nodiscard]] bool is_locked() const noexcept { return mtx_owned(&handle_) != 0; }

private:
  mutable struct mtx handle_{};
};

} // namespace reloco
