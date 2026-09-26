// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file mutex.template.hpp
 * @brief Documentation-only scaffold for `RELOCO_MUTEX_BACKEND_CUSTOM`.
 *
 * Never `#include`d by anything -- copy this file to
 * `detail/porting/mutex.hpp` (dropping `.template`), fill it in for your
 * actual target, and define `RELOCO_MUTEX_BACKEND_CUSTOM` (see
 * `reloco/mutex.hpp`/`reloco/reloco_config.hpp`), or point the
 * `JPLCZ_RELOCO_PORTING_HEADERS` CMake variable at a directory containing
 * your finished `mutex.hpp` and let the build do both for you.
 *
 * Sketches, loosely, what a **FreeBSD kernel** port might look like --
 * `sx(9)` (a sleepable shared/exclusive lock) backing `mutex`/
 * `shared_mutex`, `mtx(9)` (an adaptive mutex, the closest kernel
 * counterpart to a recursive `pthread_mutex_t`) backing `recursive_mutex`,
 * and `cv(9)` backing `condition_variable`. This is illustrative, not
 * exact or complete -- a real port has its own naming/lock-order/witness
 * conventions to fold in (see `sys/lock.h`/`sys/mutex.h`/`sys/sx.h`/
 * `sys/condvar.h`), and is not compiled or exercised by this repository
 * (which targets hosted userspace, not the FreeBSD kernel proper).
 * `reloco::error_checking_mutex` is omitted here (kernel code rarely
 * needs a fallible relock/unlock-by-non-owner check -- a kernel `mtx`/
 * `sx` typically already panics on exactly those misuses via `WITNESS`),
 * but a real port that wants one can still write it generically on top
 * of the `mutex` below plus an `_Atomic(struct thread *)` owner tag,
 * exactly like this file's built-in `RELOCO_EXPORT RELOCO_CAPABILITY(
 * "mutex") error_checking_mutex` (`reloco/mutex.hpp`) does atop `mutex`.
 */

#include <sys/param.h>

#include <sys/condvar.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/systm.h>

namespace reloco {

class RELOCO_CAPABILITY("mutex") mutex {
public:
  using native_handle_type = struct sx *;

  mutex() noexcept { sx_init(&handle_, "reloco::mutex"); }
  ~mutex() noexcept { sx_destroy(&handle_); }

  mutex(const mutex &) = delete;
  mutex &operator=(const mutex &) = delete;

  void lock() & noexcept RELOCO_ACQUIRE() { sx_xlock(&handle_); }
  void unlock() & noexcept RELOCO_RELEASE() { sx_xunlock(&handle_); }
  [[nodiscard]] bool try_lock() & noexcept RELOCO_TRY_ACQUIRE(true) { return sx_try_xlock(&handle_) != 0; }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  struct sx handle_{};
};

class RELOCO_CAPABILITY("mutex") recursive_mutex {
public:
  using native_handle_type = struct mtx *;

  recursive_mutex() noexcept { mtx_init(&handle_, "reloco::recursive_mutex", nullptr, MTX_DEF | MTX_RECURSE); }
  ~recursive_mutex() noexcept { mtx_destroy(&handle_); }

  recursive_mutex(const recursive_mutex &) = delete;
  recursive_mutex &operator=(const recursive_mutex &) = delete;

  void lock() & noexcept RELOCO_ACQUIRE() { mtx_lock(&handle_); }
  void unlock() & noexcept RELOCO_RELEASE() { mtx_unlock(&handle_); }
  [[nodiscard]] bool try_lock() & noexcept RELOCO_TRY_ACQUIRE(true) { return mtx_trylock(&handle_) != 0; }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  struct mtx handle_{};
};

class RELOCO_CAPABILITY("mutex") shared_mutex {
public:
  using native_handle_type = struct sx *;

  shared_mutex() noexcept { sx_init(&handle_, "reloco::shared_mutex"); }
  ~shared_mutex() noexcept { sx_destroy(&handle_); }

  shared_mutex(const shared_mutex &) = delete;
  shared_mutex &operator=(const shared_mutex &) = delete;

  void lock() & noexcept RELOCO_ACQUIRE() { sx_xlock(&handle_); }
  void unlock() & noexcept RELOCO_RELEASE() { sx_xunlock(&handle_); }
  [[nodiscard]] bool try_lock() & noexcept RELOCO_TRY_ACQUIRE(true) { return sx_try_xlock(&handle_) != 0; }

  void lock_shared() & noexcept RELOCO_ACQUIRE_SHARED() { sx_slock(&handle_); }
  void unlock_shared() & noexcept RELOCO_RELEASE_SHARED() { sx_sunlock(&handle_); }
  [[nodiscard]] bool try_lock_shared() & noexcept RELOCO_TRY_ACQUIRE_SHARED(true) {
    return sx_try_slock(&handle_) != 0;
  }

  [[nodiscard]] native_handle_type native_handle() & noexcept { return &handle_; }

private:
  struct sx handle_{};
};

// A real port would back condition_variable with cv(9) (cv_init/cv_wait/
// cv_timedwait/cv_signal/cv_broadcast), paired with the `mutex` above
// (sx(9)'s xlock, held by the caller, standing in for pthread_cond_t's
// paired mutex). Omitted here -- see `reloco::condition_variable`'s
// public API in `reloco/mutex.hpp` for the exact wait()/wait_for()/
// notify_one()/notify_all() shape to match, and cv(9)'s own manual page
// for the FreeBSD-kernel-specific wiring (cv_wait_sig()/cv_timedwait_sig()
// for interruptible variants a kernel port may also want to expose).

} // namespace reloco
