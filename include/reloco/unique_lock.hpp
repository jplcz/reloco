// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file unique_lock.hpp
 * @brief `reloco`'s own, non-owning, movable RAII exclusive-lock wrapper
 * over an externally-owned mutex reference -- matching `std::unique_lock`
 * in API and semantics (default-constructible, `defer_lock`/`try_to_lock`/
 * `adopt_lock` tagged construction, movable, `lock()`/`try_lock()`/
 * `unlock()`, `owns_lock()`/`operator bool`, `release()`), but declared
 * entirely in terms of `reloco`'s own infrastructure: no `<mutex>`, no
 * other STL header, and no dependency on any specific `reloco` mutex type
 * -- `MutexT` is a template parameter satisfied by any type providing
 * `lock()`/`unlock()`/`try_lock()` with the same signatures as
 * `reloco::mutex` (`mutex.hpp`), exactly like `std::unique_lock<Mutex>`
 * itself places no requirement beyond "Lockable".
 *
 * Unlike `guarded_mutex.hpp`'s `guarded_mutex<T>` (a Rust `Mutex<T>`
 * equivalent that *owns* the value it protects), `unique_lock<MutexT>`
 * owns nothing -- it is the thin, non-owning proxy C++ has always used to
 * pair a bare mutex object with RAII acquisition/release, for the cases
 * `guarded_mutex<T>`/`rw_lock<T>` don't fit (e.g. `condition_variable::
 * wait`, which must be able to unlock and relock an existing mutex
 * reference mid-wait -- something an owning wrapper's API has no room
 * for). `mutex.hpp`'s `condition_variable` is written directly against
 * this type instead of `std::unique_lock<mutex>`.
 *
 * Carries the same Clang Thread Safety Analysis (`-Wthread-safety`)
 * annotations as every other lock type in `reloco` (see `detail/
 * compat.hpp`): `unique_lock` itself is `RELOCO_SCOPED_CAPABILITY`, so
 * constructing one with @p m directly (the common case), later calling
 * `unlock()`, and then `lock()`/`try_lock()` again on that *same* object
 * are all understood by the analyzer as acquiring/releasing @p m itself
 * -- a guarded variable protected by @p m is checked correctly across all
 * of that, the same way Clang's own canonical `MutexLocker` idiom works
 * (verified against Clang 21 and Clang 24).
 *
 * The `defer_lock`/`try_to_lock`/`adopt_lock` constructors are
 * deliberately left without an acquiring annotation, and the `lock()`/
 * `try_lock()` call that necessarily follows a `defer_lock`-constructed
 * object's *first* real acquisition is not reliably tracked either --
 * this is an inherent limitation of Clang's capability model, not a
 * missing annotation: a scoped-capability object is only ever aliased to
 * the external mutex expression it wraps at the exact constructor call
 * where that expression is spelled out as an argument (e.g. `unique_lock
 * lk(m);`); no later no-argument method call (`lock()`), and no
 * `assert_capability`-based "trust me" annotation (which is what
 * `adopt_lock`'s constructor uses), can retroactively establish or
 * transfer that alias for release-tracking purposes. Empirically (Clang
 * 21/24), this means: a manually-locked mutex later adopted via
 * `adopt_lock` is not recognized as released by `unique_lock`'s
 * destructor, and a `defer_lock`/`try_to_lock`-constructed object's first
 * `lock()`/`try_lock()` does not mark the wrapped mutex as held. Code
 * relying on any of those three tags must fall back to runtime
 * `owns_lock()` checks instead of static checking for that specific
 * acquisition -- exactly like `std::unique_lock`, which carries no Clang
 * TSA annotations at all upstream, for the same reason.
 *
 * ## Caller-location forwarding
 *
 * The acquiring constructor, the `try_to_lock_t` constructor, `lock()`,
 * `try_lock()`, and `unlock()` each come in a paired
 * `debug_call_location_ref`/`release_call_location_ref` overload (see
 * `call_location.hpp`), exactly like that header's own `mutex::lock`
 * worked example: `RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG`/
 * `RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE` give exactly one of the pair
 * a default argument (selected build-wide by `RELOCO_CALL_LOCATION_
 * DEBUG`), so an omitted-argument call (`lk.lock()`) is never ambiguous,
 * while an explicit caller on either side of an ABI boundary can still
 * pick a specific overload directly (`lk.lock(release_call_location_ref
 * ::none())`). Whether the captured location actually reaches the
 * wrapped `MutexT` depends entirely on `MutexT` itself, detected via
 * SFINAE (`detail/lock_location_traits.hpp`): if `MutexT` provides a
 * `call_location_ref` overload of the method being called
 * (`lock(call_location_ref)`, `try_lock(call_location_ref)`,
 * `unlock(call_location_ref)`), `unique_lock` forwards the captured
 * location into it (`call_location_ref::none()` for the
 * `release_call_location_ref` overload, which never carries one);
 * otherwise it silently falls back to the plain, location-less overload
 * every `MutexT` is already required to provide. No change is needed at
 * a `unique_lock` call site either way -- `lk.lock();` always compiles,
 * and automatically starts carrying a real location the moment its
 * `MutexT` opts in by adding the overload.
 */

#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "detail/lock_location_traits.hpp"
#include "lock_tags.hpp"

#include <utility>

namespace reloco {

/**
 * @brief Non-owning, movable RAII exclusive-lock wrapper over a
 * `MutexT &`, matching `std::unique_lock<MutexT>`. `MutexT` must provide
 * `lock()`/`unlock()`/`try_lock()` with the same signatures as
 * `reloco::mutex` (the common case; `recursive_mutex`/`shared_mutex`/
 * `error_checking_mutex` -- used exclusive-only here -- all satisfy this
 * too).
 */
template <typename MutexT> class RELOCO_SCOPED_CAPABILITY unique_lock {
public:
  using mutex_type = MutexT;

  /** @brief Owns no mutex; `owns_lock()` is `false` until a later
   * `lock()`/`try_lock()`/swap/move-assignment brings one in. */
  constexpr unique_lock() noexcept = default;

  /** @brief Blocks until @p m is acquired, capturing @p where (see the
   * file-level documentation's caller-location forwarding section). */
  explicit unique_lock(mutex_type &m, debug_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG) noexcept
      RELOCO_ACQUIRE(m)
      : mutex_(&m) {
    do_lock(where);
    owns_ = true;
  }

  /** @brief Blocks until @p m is acquired. The `release_call_location_ref`
   * side of the paired overload above -- see the file-level
   * documentation's caller-location forwarding section. */
  explicit unique_lock(mutex_type &m, release_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE) noexcept
      RELOCO_ACQUIRE(m)
      : mutex_(&m) {
    (void)where;
    do_lock(call_location_ref::none());
    owns_ = true;
  }

  /** @brief Does not touch @p m at all -- a later `lock()`/`try_lock()`
   * acquires it. See the file-level documentation for why this
   * constructor carries no Clang TSA annotation. */
  unique_lock(mutex_type &m, defer_lock_t) noexcept : mutex_(&m) {}

  /** @brief Assumes the calling thread already holds @p m (e.g. acquired
   * directly, or adopted from another `unique_lock` via `release()`);
   * records ownership without calling `lock()` again. */
  unique_lock(mutex_type &m, adopt_lock_t) noexcept RELOCO_ASSERT_CAPABILITY(m) : mutex_(&m), owns_(true) {}

  /** @brief Attempts `m.try_lock()` non-blockingly, capturing @p where;
   * check `owns_lock()` to see whether it succeeded. See the file-level
   * documentation for why this constructor carries no Clang TSA
   * annotation. */
  unique_lock(mutex_type &m, try_to_lock_t,
              debug_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG) noexcept
      : mutex_(&m) {
    owns_ = do_try_lock(where);
  }

  /** @brief Attempts `m.try_lock()` non-blockingly; check `owns_lock()`
   * to see whether it succeeded. The `release_call_location_ref` side of
   * the paired overload above -- see the file-level documentation's
   * caller-location forwarding section. */
  unique_lock(mutex_type &m, try_to_lock_t,
              release_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE) noexcept
      : mutex_(&m) {
    (void)where;
    owns_ = do_try_lock(call_location_ref::none());
  }

  unique_lock(unique_lock &&other) noexcept : mutex_(other.mutex_), owns_(other.owns_) {
    other.mutex_ = nullptr;
    other.owns_ = false;
  }

  unique_lock &operator=(unique_lock &&other) noexcept {
    if (this != &other) {
      if (owns_)
        mutex_->unlock();
      mutex_ = other.mutex_;
      owns_ = other.owns_;
      other.mutex_ = nullptr;
      other.owns_ = false;
    }
    return *this;
  }

  unique_lock(const unique_lock &) = delete;
  unique_lock &operator=(const unique_lock &) = delete;

  /** @brief Releases the mutex, if still owned. */
  ~unique_lock() noexcept RELOCO_RELEASE() {
    if (owns_)
      mutex_->unlock();
  }

  /** @brief Blocks until the wrapped mutex is acquired, capturing
   * @p where. Asserts that no mutex is already owned by this
   * `unique_lock` (matching `std::unique_lock::lock()`'s precondition,
   * enforced here rather than reported as an error, since violating it
   * is always a programming error -- never a runtime race), attributed
   * to @p where's captured location (`RELOCO_ASSERT_LOC`) when it
   * carries one. */
  void lock(debug_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG) & noexcept RELOCO_ACQUIRE() {
    const call_location loc = assert_location(where);
    RELOCO_ASSERT_LOC(mutex_ != nullptr, loc.file, loc.line, "unique_lock::lock() called without a wrapped mutex");
    RELOCO_ASSERT_LOC(!owns_, loc.file, loc.line, "unique_lock::lock() called while already owning the mutex");
    do_lock(where);
    owns_ = true;
  }

  /** @brief Blocks until the wrapped mutex is acquired. The
   * `release_call_location_ref` side of the paired overload above -- see
   * the file-level documentation's caller-location forwarding section.
   * Same preconditions as the `debug_call_location_ref` overload. */
  void lock(release_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE) & noexcept RELOCO_ACQUIRE() {
    (void)where;
    RELOCO_ASSERT(mutex_ != nullptr, "unique_lock::lock() called without a wrapped mutex");
    RELOCO_ASSERT(!owns_, "unique_lock::lock() called while already owning the mutex");
    do_lock(call_location_ref::none());
    owns_ = true;
  }

  /** @brief Attempts to acquire the wrapped mutex non-blockingly,
   * capturing @p where. Same preconditions as `lock()`, attributed to
   * @p where's captured location the same way. */
  [[nodiscard]] bool try_lock(debug_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG) & noexcept
      RELOCO_TRY_ACQUIRE(true) {
    const call_location loc = assert_location(where);
    RELOCO_ASSERT_LOC(mutex_ != nullptr, loc.file, loc.line, "unique_lock::try_lock() called without a wrapped mutex");
    RELOCO_ASSERT_LOC(!owns_, loc.file, loc.line, "unique_lock::try_lock() called while already owning the mutex");
    owns_ = do_try_lock(where);
    return owns_;
  }

  /** @brief Attempts to acquire the wrapped mutex non-blockingly. The
   * `release_call_location_ref` side of the paired overload above -- see
   * the file-level documentation's caller-location forwarding section.
   * Same preconditions as `lock()`. */
  [[nodiscard]] bool try_lock(release_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE) & noexcept
      RELOCO_TRY_ACQUIRE(true) {
    (void)where;
    RELOCO_ASSERT(mutex_ != nullptr, "unique_lock::try_lock() called without a wrapped mutex");
    RELOCO_ASSERT(!owns_, "unique_lock::try_lock() called while already owning the mutex");
    owns_ = do_try_lock(call_location_ref::none());
    return owns_;
  }

  /** @brief Releases the wrapped mutex, capturing @p where. Asserts it
   * is currently owned, attributed to @p where's captured location the
   * same way as `lock()`. */
  void unlock(debug_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG) & noexcept RELOCO_RELEASE() {
    const call_location loc = assert_location(where);
    RELOCO_ASSERT_LOC(owns_, loc.file, loc.line, "unique_lock::unlock() called without owning the mutex");
    do_unlock(where);
    owns_ = false;
  }

  /** @brief Releases the wrapped mutex. The `release_call_location_ref`
   * side of the paired overload above -- see the file-level
   * documentation's caller-location forwarding section. Asserts it is
   * currently owned. */
  void unlock(release_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE) & noexcept RELOCO_RELEASE() {
    (void)where;
    RELOCO_ASSERT(owns_, "unique_lock::unlock() called without owning the mutex");
    do_unlock(call_location_ref::none());
    owns_ = false;
  }

  [[nodiscard]] bool owns_lock() const noexcept { return owns_; }

  explicit operator bool() const noexcept { return owns_; }

  /** @brief The wrapped mutex, or `nullptr` if default-constructed or
   * moved-from. Does not imply ownership -- check `owns_lock()`. */
  [[nodiscard]] mutex_type *mutex() const noexcept { return mutex_; }

  /** @brief Detaches from the wrapped mutex without unlocking it,
   * returning it to the caller, who becomes responsible for eventually
   * unlocking it (typically by adopting it into another lock wrapper via
   * `adopt_lock`). Matches `std::unique_lock::release()`. */
  [[nodiscard]] mutex_type *release() noexcept {
    mutex_type *m = mutex_;
    mutex_ = nullptr;
    owns_ = false;
    return m;
  }

  void swap(unique_lock &other) noexcept {
    std::swap(mutex_, other.mutex_);
    std::swap(owns_, other.owns_);
  }

private:
  /** @brief Forwards @p where into `mutex_->lock(call_location_ref)` if
   * `mutex_type` provides that overload, otherwise calls the plain
   * `mutex_->lock()`. Shared by both the `debug_call_location_ref` and
   * `release_call_location_ref` overloads of the acquiring constructor
   * and `lock()`. */
  void do_lock(call_location_ref where) noexcept {
    if constexpr (detail::has_location_lock<mutex_type>::value) {
      mutex_->lock(where);
    } else {
      (void)where;
      mutex_->lock();
    }
  }

  /** @brief Same as `do_lock()`, for `try_lock(call_location_ref)`/
   * `try_lock()`. Shared by both the `debug_call_location_ref` and
   * `release_call_location_ref` overloads of the `try_to_lock_t`
   * constructor and `try_lock()`. */
  [[nodiscard]] bool do_try_lock(call_location_ref where) noexcept {
    if constexpr (detail::has_location_try_lock<mutex_type>::value) {
      return mutex_->try_lock(where);
    } else {
      (void)where;
      return mutex_->try_lock();
    }
  }

  /** @brief Same as `do_lock()`, for `unlock(call_location_ref)`/
   * `unlock()`. Shared by both the `debug_call_location_ref` and
   * `release_call_location_ref` overloads of `unlock()`. */
  void do_unlock(call_location_ref where) noexcept {
    if constexpr (detail::has_location_unlock<mutex_type>::value) {
      mutex_->unlock(where);
    } else {
      (void)where;
      mutex_->unlock();
    }
  }

  /** @brief Converts @p where into a plain `call_location`, substituting
   * `{nullptr, 0}` when @p where carries none -- `{nullptr, 0}` is
   * exactly what `RELOCO_ASSERT_LOC` treats as "no location", so the
   * resulting `file`/`line` pair can always be passed to it directly,
   * with no separate branch needed for the no-location case. Used to
   * attribute `lock()`/`try_lock()`/`unlock()`'s precondition assertions
   * to @p where's real caller site instead of a line inside this
   * wrapper, whenever one is available. */
  static constexpr call_location assert_location(call_location_ref where) noexcept {
    return where.has_value() ? where.value() : call_location{nullptr, 0};
  }

  mutex_type *mutex_ = nullptr;
  bool owns_ = false;
};

template <typename MutexT> void swap(unique_lock<MutexT> &lhs, unique_lock<MutexT> &rhs) noexcept { lhs.swap(rhs); }

} // namespace reloco
