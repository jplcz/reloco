// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/**
 * @file per_cpu_slot.hpp
 * @brief `per_cpu_slot<T>`: a per-CPU cell holding an object with non-trivial initialisation but no allocation.
 *
 * The per-CPU counterpart of `tls_slot.hpp`. A per-CPU block is zero-filled by `per_cpu_provider`; a slot in it is
 * **empty** (all-zero) until the module's CPU bring-up code calls `init()` on every CPU's block. After that,
 * access is `with`/`with_mut`, in atomic context only.
 *
 * Rules (the kif checker enforces the same ones, KIFP182/186):
 * - The initialiser returns `T` directly and **cannot fail and cannot allocate**: per-CPU data has no allocator. It runs
 *   in whatever context CPU bring-up provides, before the CPU runs module code.
 * - The object is **never destroyed**: a CPU does not exit, so `T`'s destructor does not run (use a trivially
 *   destructible `T` or accept the leak at shutdown). There is deliberately no `destroy()`.
 * - Access is scoped, like every cell: the closure gets `T &` / `const T &`, whose result must not borrow from it.
 * - Accessing a slot that was not initialised is a bug (`RELOCO_ASSERT`), not a recoverable error.
 *
 * @code
 * struct counters { std::uint64_t rx = 0; std::uint64_t tx = 0; };
 * static counters make_counters() noexcept { return {}; }            // no allocation, cannot fail
 *
 * void cpu_up(per_cpu_slot<counters> &mine) noexcept { mine.init(&make_counters); }   // once per CPU
 * void on_rx(per_cpu_slot<counters> &mine) noexcept RELOCO_ATOMIC_ONLY {
 *   mine.with_mut([](counters &c) { c.rx++; });                      // atomic context: no migration, no lock
 * }
 * @endcode
 */

#include "atomic_context.hpp"
#include "detail/assert.hpp"
#include "lifetime.hpp"

#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace reloco {

/**
 * @brief Zero-valid storage for one `T` in a CPU's block; built once by `init()`, never destroyed.
 *
 * Not copyable or movable: its address is the address of the CPU's storage.
 *
 * @tparam T nothrow move constructible (the initialiser's result is moved into place).
 */
template <typename T> class RELOCO_OWNER per_cpu_slot {
  static_assert(std::is_nothrow_move_constructible_v<T>, "per_cpu_slot<T>: T must be nothrow move constructible");

public:
  /** @brief Initialiser signature: cannot fail and must not allocate. */
  using init_fn = T (*)() noexcept;

  per_cpu_slot() noexcept = default;
  per_cpu_slot(const per_cpu_slot &) = delete;
  per_cpu_slot &operator=(const per_cpu_slot &) = delete;
  per_cpu_slot(per_cpu_slot &&) = delete;
  per_cpu_slot &operator=(per_cpu_slot &&) = delete;
  ~per_cpu_slot() = default; // trivial on purpose: the object is never destroyed (see the file comment)

  /** @brief True after `init()`. */
  [[nodiscard]] bool is_ready() const noexcept { return ready_ != 0; }

  /** @brief Builds the object with `make`; a second call is a no-op (CPU bring-up may be retried). */
  void init(init_fn make) & noexcept {
    if (ready_ == 0) {
      ::new (static_cast<void *>(storage_)) T(make());
      ready_ = 1;
    }
  }

  /** @brief Runs `f(const T &)`; atomic context only. */
  template <typename F> [[nodiscard]] RELOCO_ATOMIC_ONLY auto with(F &&f) & noexcept {
    return access<const T>(std::forward<F>(f));
  }

  /** @brief Runs `f(T &)`; atomic context only. */
  template <typename F> [[nodiscard]] RELOCO_ATOMIC_ONLY auto with_mut(F &&f) & noexcept {
    return access<T>(std::forward<F>(f));
  }

private:
  template <typename Access, typename F> auto access(F &&f) & noexcept {
    using R = std::invoke_result_t<F, Access &>;
    static_assert(!std::is_reference_v<R> && !std::is_pointer_v<R>,
                  "per_cpu_slot: the closure must not return a reference or pointer");
    RELOCO_ASSERT(ready_ != 0, "per_cpu_slot used before init()");
    // NOLINTNEXTLINE(reloco-banned-construct): storage_ holds a T once ready_ is set.
    return std::forward<F>(f)(static_cast<Access &>(*std::launder(reinterpret_cast<T *>(storage_))));
  }

  // NOLINTNEXTLINE(reloco-no-c-array): the slot must be zero-valid and constructible in raw zeroed memory.
  alignas(T) unsigned char storage_[sizeof(T)] = {};
  std::uint8_t ready_ = 0;
};

} // namespace reloco
