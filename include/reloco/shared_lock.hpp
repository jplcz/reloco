// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file shared_lock.hpp
 * @brief `reloco`'s own, non-owning, movable RAII shared-lock wrapper
 * over an externally-owned shared-mutex reference -- matching
 * `std::shared_lock` in API and semantics (default-constructible,
 * `defer_lock`/`try_to_lock`/`adopt_lock` tagged construction, movable,
 * `lock()`/`try_lock()`/`unlock()`, `owns_lock()`/`operator bool`,
 * `release()`), but declared entirely in terms of `reloco`'s own
 * infrastructure: no `<shared_mutex>`, no other STL header, and no
 * dependency on any specific `reloco` mutex type -- `SharedMutexT` is a
 * template parameter satisfied by any type providing `lock_shared()`/
 * `unlock_shared()`/`try_lock_shared()` with the same signatures as
 * `reloco::shared_mutex` (`mutex.hpp`), exactly like
 * `std::shared_lock<SharedMutex>` itself places no requirement beyond
 * "SharedLockable".
 *
 * See `unique_lock.hpp`'s file-level documentation for the rationale
 * behind this being a thin, non-owning proxy rather than an owning
 * wrapper like `rw_lock.hpp`'s `rw_lock<T>::read_guard` -- the same
 * reasoning applies here with "exclusive" replaced by "shared"
 * throughout, including the note on why the `defer_lock`/`try_to_lock`
 * constructors carry no Clang Thread Safety Analysis annotation.
 */

#include "detail/assert.hpp"
#include "detail/compat.hpp"
#include "lock_tags.hpp"

#include <utility>

namespace reloco {

/**
 * @brief Non-owning, movable RAII shared-lock wrapper over a
 * `SharedMutexT &`, matching `std::shared_lock<SharedMutexT>`.
 * `SharedMutexT` must provide `lock_shared()`/`unlock_shared()`/
 * `try_lock_shared()` with the same signatures as `reloco::shared_mutex`
 * (the common case).
 */
template <typename SharedMutexT> class RELOCO_SCOPED_CAPABILITY shared_lock {
public:
  using mutex_type = SharedMutexT;

  /** @brief Owns no mutex; `owns_lock()` is `false` until a later
   * `lock()`/`try_lock()`/swap/move-assignment brings one in. */
  constexpr shared_lock() noexcept = default;

  /** @brief Blocks until @p m is acquired for shared access. */
  explicit shared_lock(mutex_type &m) noexcept RELOCO_ACQUIRE_SHARED(m) : mutex_(&m) {
    mutex_->lock_shared();
    owns_ = true;
  }

  /** @brief Does not touch @p m at all -- a later `lock()`/`try_lock()`
   * acquires it. See `unique_lock.hpp` for why this constructor carries
   * no Clang TSA annotation. */
  shared_lock(mutex_type &m, defer_lock_t) noexcept : mutex_(&m) {}

  /** @brief Assumes the calling thread already holds a shared lock on
   * @p m (e.g. acquired directly, or adopted from another `shared_lock`
   * via `release()`); records ownership without calling `lock_shared()`
   * again. */
  shared_lock(mutex_type &m, adopt_lock_t) noexcept RELOCO_ASSERT_SHARED_CAPABILITY(m) : mutex_(&m), owns_(true) {}

  /** @brief Attempts `m.try_lock_shared()` non-blockingly; check
   * `owns_lock()` to see whether it succeeded. See `unique_lock.hpp` for
   * why this constructor carries no Clang TSA annotation. */
  shared_lock(mutex_type &m, try_to_lock_t) noexcept : mutex_(&m), owns_(m.try_lock_shared()) {}

  shared_lock(shared_lock &&other) noexcept : mutex_(other.mutex_), owns_(other.owns_) {
    other.mutex_ = nullptr;
    other.owns_ = false;
  }

  shared_lock &operator=(shared_lock &&other) noexcept {
    if (this != &other) {
      if (owns_)
        mutex_->unlock_shared();
      mutex_ = other.mutex_;
      owns_ = other.owns_;
      other.mutex_ = nullptr;
      other.owns_ = false;
    }
    return *this;
  }

  shared_lock(const shared_lock &) = delete;
  shared_lock &operator=(const shared_lock &) = delete;

  /** @brief Releases the shared lock, if still owned. Annotated with the
   * generic `RELOCO_RELEASE()` rather than `RELOCO_RELEASE_SHARED()` --
   * Clang's Thread Safety Analysis models a `RELOCO_SCOPED_CAPABILITY`'s
   * own destructor as releasing whatever single capability its
   * constructor acquired, regardless of shared/exclusive kind, exactly
   * like Abseil's `ReaderMutexLock::~ReaderMutexLock()`; annotating it
   * `RELEASE_SHARED()` instead causes a spurious "releasing mutex using
   * shared access, expected exclusive access" diagnostic at every call
   * site. */
  ~shared_lock() noexcept RELOCO_RELEASE() {
    if (owns_)
      mutex_->unlock_shared();
  }

  /** @brief Blocks until the wrapped mutex is acquired for shared access.
   * Asserts that no lock is already owned by this `shared_lock` (matching
   * `std::shared_lock::lock()`'s precondition, enforced here rather than
   * reported as an error, since violating it is always a programming
   * error -- never a runtime race). */
  void lock() & noexcept RELOCO_ACQUIRE_SHARED() {
    RELOCO_ASSERT(mutex_ != nullptr, "shared_lock::lock() called without a wrapped mutex");
    RELOCO_ASSERT(!owns_, "shared_lock::lock() called while already owning the lock");
    mutex_->lock_shared();
    owns_ = true;
  }

  /** @brief Attempts to acquire the wrapped mutex for shared access
   * non-blockingly. Same preconditions as `lock()`. */
  [[nodiscard]] bool try_lock() & noexcept RELOCO_TRY_ACQUIRE_SHARED(true) {
    RELOCO_ASSERT(mutex_ != nullptr, "shared_lock::try_lock() called without a wrapped mutex");
    RELOCO_ASSERT(!owns_, "shared_lock::try_lock() called while already owning the lock");
    owns_ = mutex_->try_lock_shared();
    return owns_;
  }

  /** @brief Releases the wrapped shared lock. Asserts it is currently
   * owned. Annotated with the generic `RELOCO_RELEASE()` rather than
   * `RELOCO_RELEASE_SHARED()` -- see the destructor above for why. */
  void unlock() & noexcept RELOCO_RELEASE() {
    RELOCO_ASSERT(owns_, "shared_lock::unlock() called without owning the lock");
    mutex_->unlock_shared();
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
   * `adopt_lock`). Matches `std::shared_lock::release()`. */
  [[nodiscard]] mutex_type *release() noexcept {
    mutex_type *m = mutex_;
    mutex_ = nullptr;
    owns_ = false;
    return m;
  }

  void swap(shared_lock &other) noexcept {
    std::swap(mutex_, other.mutex_);
    std::swap(owns_, other.owns_);
  }

private:
  mutex_type *mutex_ = nullptr;
  bool owns_ = false;
};

template <typename SharedMutexT> void swap(shared_lock<SharedMutexT> &lhs, shared_lock<SharedMutexT> &rhs) noexcept {
  lhs.swap(rhs);
}

} // namespace reloco
