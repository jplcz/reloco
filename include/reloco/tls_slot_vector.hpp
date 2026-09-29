// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file tls_slot_vector.hpp
 * @brief The classic pthread-key design (a global slot table + an
 * atomic generation counter, a per-context growable pointer vector) as
 * an alternative to `keyed_intrusive_registry.hpp`'s hash-table-per-
 * owner-key model -- both exist to back a `RELOCO_TLS_MODEL_OS` port
 * (`detail/porting/tls_provider.hpp`, see `tls_provider.hpp`); pick
 * whichever shape fits the target better.
 *
 * `keyed_intrusive_registry` pays a hash + lock on *every* `get()`/
 * `set()`, for every distinct `Tag`, because it has no cheaper way to
 * find "this context's node" than walking a bucket chain. This file
 * takes the opposite trade: assign every distinct `(T, Tag)` pair used
 * anywhere in the program a small integer *slot index*, once, the very
 * first time it is touched (`tls_slot_index<T, Tag>`, guarded by a
 * `once_lock<std::size_t>` -- never a function-local "magic" static,
 * which some bare-metal C++ runtimes cannot support at all without extra
 * guard-variable machinery). Every calling context then only ever needs
 * *one* growable `void *` array, indexed directly by that slot number --
 * no hashing, no bucket-chain walk, `O(1)` either way.
 *
 * - `detail::tls_slot_table`: process-wide (there is exactly one, ever),
 *   fixed-capacity (`RELOCO_TLS_MAX_SLOTS`, default `256` -- define it
 *   before including this header to raise it) table of every registered
 *   slot's destroy thunk, plus an atomic slot count read lock-free by
 *   every `tls_slot_vector` as its "generation" signal: `slot_count()`
 *   only ever increases (once per distinct `(T, Tag)` pair, ever, across
 *   the whole program's lifetime -- registration itself is rare enough
 *   that guarding it with a plain `reloco::spin_lock` is fine). That
 *   type name is itself the OS customization point (see
 *   `RELOCO_SPIN_LOCK_BACKEND_CUSTOM` in `spin_lock.hpp`): a kernel/RTOS
 *   port that defines it gets its own native spinlock here automatically,
 *   the same way every other reloco component that spells `spin_lock`
 *   does, with no separate template parameter needed on this file's own
 *   process-wide, always-a-singleton table. Any vector whose own
 *   capacity has fallen behind the generation knows exactly how far it
 *   needs to grow to catch up.
 * - `tls_slot_index<T, Tag>::get()`: the slot index for this `(T, Tag)`
 *   pair, assigning + registering it (via `detail::tls_slot_table::
 *   register_slot`) the first time it is ever called, for any `T, Tag`.
 * - `tls_slot_vector<Lock, Growth>`: one growable `void *` array,
 *   representing *one calling context's* storage (a thread, a task,
 *   whatever `Lock` is chosen to protect against -- see below). Grows
 *   (via its own `allocator_ref`, given once at construction and reused
 *   for every allocation this vector ever makes -- unlike
 *   `keyed_intrusive_registry`, which accepts a fresh `allocator_ref` per
 *   call, this file deliberately does not, since a real per-thread/
 *   per-task slot array is essentially always sized/freed by the same
 *   allocator throughout its life) by a `Growth` strategy
 *   (`bucket_growth.hpp`, default `bucket_growth::doubling_then_ratio`)
 *   sized against `detail::tls_slot_table::slot_count()`, not merely the
 *   one slot index that triggered the grow -- so catching up to the
 *   current generation in one grow leaves room for whatever other
 *   `(T, Tag)` pairs some other thread has *already* registered, instead
 *   of growing one slot at a time as each is individually first touched
 *   by *this* context.
 * - `tls_local_state_traits<Tag>`: the one trait an OS/porting layer
 *   must specialize to plug `tls_local_slots` into its own per-thread/
 *   per-task storage -- deliberately not the full `tls_provider<T, Tag>`
 *   machinery (`tls_provider.hpp`), whose `result<>`-returning, four-
 *   backend-model shape only complicates gluing through a single,
 *   always-present, never-failing raw pointer. The contract is exactly
 *   two functions, `void *get() noexcept` / `void set(void *) noexcept`,
 *   returning/storing one pointer-sized slot per calling context,
 *   nothing else -- semantically identical to what a `pthread_key_t`
 *   backend already looks like internally, or a single dedicated field
 *   in a kernel's own TCB/task struct. A hosted, `thread_local`-backed
 *   default specialization (`Tag = void`) is provided below, but only
 *   when `RELOCO_KERNEL` is not defined -- a kernel/bare-metal build
 *   must always supply its own.
 * - `tls_local_slots<Lock, Growth, Tag>`: ties the two together --
 *   lazily allocates (through whatever `allocator_ref` the first
 *   `get_or_create`/`set` call provides) exactly one
 *   `tls_slot_vector<Lock, Growth>` per calling context the first time
 *   it is needed, publishing it through `tls_local_state_traits<Tag>`,
 *   then forwards every call to it. `clear_current()` runs every
 *   populated slot's destructor and frees the vector itself -- **the
 *   caller is entirely responsible for calling this at whatever point it
 *   already detects the underlying thread/task is going away**; nothing
 *   in this file hooks into thread/task exit on its own (there is no
 *   portable, allocation-free way to do so in a kernel/bare-metal
 *   target, which is this whole file's reason to exist in the first
 *   place).
 *
 * `Lock` defaults to `null_mutex` (`pool_allocator.hpp`) throughout,
 * matching `keyed_intrusive_registry.hpp`'s own default -- appropriate
 * whenever a `tls_slot_vector`/`tls_local_slots` instance is only ever
 * reached from the one context it belongs to (the common case: nothing
 * else should ever be touching *this* thread's slots); pass
 * `reloco::mutex`/`reloco::spin_lock` only if some other context
 * (e.g. a reaper cleaning up a dead thread's state from a different
 * thread) may reach the same instance concurrently. If that other
 * context reaches it with preemption disabled (e.g. a per-CPU vector
 * indexed by `PCPU_GET(cpuid)` instead of a per-thread one -- see
 * `keyed_intrusive_registry.hpp`'s own "Locking" section for the exact
 * same distinction), `Lock` must be `reloco::spin_lock`, never
 * `reloco::mutex`: blocking while preemption is disabled is illegal on
 * most kernels. `ensure_capacity`/`get_or_create`/`set` already keep the
 * allocator call itself outside `lock_` (the same "unlock, allocate,
 * relock" discipline `keyed_intrusive_registry.hpp` uses), but a caller
 * whose own preemption is disabled still must not call into any of them
 * while it stays disabled if the call might allocate -- re-enable
 * preemption first, call, then retry a preemption-disabled-only
 * `try_find` afterwards. A per-CPU vector's final capacity (the CPU
 * count) is typically already known before any AP starts, though, in
 * which case `tls_slot_vector::reserve(cpu_count)` called once, single-
 * threaded, during that same boot window sidesteps the whole concern:
 * no AP-side `get_or_create`/`set` call ever needs to grow (or allocate)
 * again.
 */

#include "allocator.hpp"
#include "bucket_growth.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "once_lock.hpp"
#include "pool_allocator.hpp" // for null_mutex
#include "spin_lock.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <new>
#include <utility>

#if !defined(RELOCO_TLS_MAX_SLOTS)
#define RELOCO_TLS_MAX_SLOTS 256
#endif

// allocate_slots()/allocate_value()/etc. below call allocator_ref::allocate/
// deallocate, which are RELOCO_UNSAFE_BUFFER_USAGE-marked -- see
// allocator.hpp/tls_provider.hpp/keyed_intrusive_registry.hpp.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

namespace detail {

/** @brief `void (*)(void *value, allocator_ref alloc) noexcept`: destroys
 * and deallocates one slot's value, exactly like `keyed_intrusive_
 * registry.hpp`'s `node::alloc`-based `destroy_node` -- generated
 * automatically per `(T, Tag)` pair by `tls_slot_index`, never supplied
 * by the caller. */
using tls_destroy_fn = void (*)(void *, allocator_ref) noexcept;

/**
 * @brief Process-wide (exactly one instance, ever -- every member is
 * `static`) fixed-capacity table of every `(T, Tag)` slot index ever
 * registered by `tls_slot_index`. See the file-level doc comment.
 */
class tls_slot_table {
public:
  tls_slot_table() = delete; // namespacing only -- every member is static, never instantiated.

  /**
   * @brief Registers a brand-new slot, returning its index.
   * `RELOCO_ASSERT`s `RELOCO_TLS_MAX_SLOTS` has not been exceeded --
   * raise that macro (defined before this header is first included) if
   * the program genuinely uses more than the default `256` distinct
   * `(T, Tag)` pairs across every `tls_slot_index`/`tls_local_slots` use
   * anywhere.
   */
  [[nodiscard]] static std::size_t register_slot(tls_destroy_fn destroy) noexcept {
    lock_.lock();
    std::size_t index = count_.load(std::memory_order_relaxed);
    RELOCO_ASSERT(index < RELOCO_TLS_MAX_SLOTS,
                  "reloco::tls_slot_table::register_slot: RELOCO_TLS_MAX_SLOTS exceeded -- define a bigger "
                  "RELOCO_TLS_MAX_SLOTS before including <reloco/tls_slot_vector.hpp>");
    destroy_fns_[index] = destroy;
    // Release-store publishes destroy_fns_[index] (and every earlier
    // slot's, transitively) to any thread that subsequently observes the
    // new slot_count() via an acquire load below.
    count_.store(index + 1, std::memory_order_release);
    lock_.unlock();
    return index;
  }

  /** @brief Monotonically increasing "generation": the number of slots
   * registered so far, process-wide. Lock-free -- this is the hot path
   * every `tls_slot_vector::ensure_capacity` call reads. */
  [[nodiscard]] static std::size_t slot_count() noexcept { return count_.load(std::memory_order_acquire); }

  /** @brief `RELOCO_ASSERT`s `index < slot_count()`. */
  [[nodiscard]] static tls_destroy_fn destroy_for(std::size_t index) noexcept {
    RELOCO_ASSERT(index < slot_count(), "reloco::tls_slot_table::destroy_for: index out of range");
    return destroy_fns_[index];
  }

private:
  static inline std::atomic<std::size_t> count_{0};
  static inline tls_destroy_fn destroy_fns_[RELOCO_TLS_MAX_SLOTS] = {};
  // `reloco::spin_lock` -- already the OS-customizable backend (see
  // `RELOCO_SPIN_LOCK_BACKEND_CUSTOM` in spin_lock.hpp), not hardwired to
  // the freestanding `std::atomic<bool>` default; no separate Lock
  // template parameter is needed on this process-wide singleton table.
  static inline spin_lock lock_{};
};

} // namespace detail

/**
 * @brief See the file-level doc comment. Assigns @p T, @p Tag's global
 * slot index exactly once -- via `once_lock<std::size_t>`, never a
 * function-local "magic" static (some bare-metal C++ runtimes have no
 * guard-variable support at all) -- registering the `~T()` + deallocate
 * cleanup thunk `detail::tls_slot_table` needs into it at the same time.
 */
template <typename T, typename Tag> class tls_slot_index {
public:
  [[nodiscard]] static std::size_t get() noexcept {
    return slot_.get_or_init([]() noexcept { return detail::tls_slot_table::register_slot(&destroy); });
  }

private:
  static void destroy(void *ptr, allocator_ref alloc) noexcept {
    auto *p = static_cast<T *>(ptr);
    p->~T();
    alloc.deallocate(p, sizeof(T));
  }

  static inline once_lock<std::size_t> slot_{};
};

/**
 * @brief See the file-level doc comment. One calling context's growable
 * `void *` slot array, indexed directly by `tls_slot_index<T, Tag>::
 * get()`. Neither copyable nor movable (a live @p Lock, e.g.
 * `reloco::mutex`, is not either -- matching `keyed_intrusive_registry`'s
 * own decision for the same reason).
 * @tparam Lock Anything satisfying `lock()`/`unlock()`/`try_lock()`.
 *   Defaults to `null_mutex` -- see the file-level doc comment for when
 *   a real lock is actually needed here.
 * @tparam Growth A `bucket_growth.hpp` strategy (any type exposing
 *   `next_bucket_count(current_buckets, projected_elements,
 *   elements_per_bucket, min_buckets, max_buckets)`), used to size each
 *   grow against `detail::tls_slot_table::slot_count()`. Defaults to
 *   `bucket_growth::doubling_then_ratio`.
 */
template <typename Lock = null_mutex, typename Growth = bucket_growth::doubling_then_ratio> class tls_slot_vector {
public:
  /** @param alloc Allocator used for every allocation this vector ever
   * makes (both its own backing array and every slot value) -- see the
   * file-level doc comment for why this is fixed at construction rather
   * than accepted again per call. */
  explicit constexpr tls_slot_vector(allocator_ref alloc = default_allocator()) noexcept : alloc_(alloc) {}

  tls_slot_vector(const tls_slot_vector &) = delete;
  tls_slot_vector &operator=(const tls_slot_vector &) = delete;
  tls_slot_vector(tls_slot_vector &&) = delete;
  tls_slot_vector &operator=(tls_slot_vector &&) = delete;

  ~tls_slot_vector() noexcept { clear(); }

  /** @brief The allocator this vector (and every value it currently
   * holds) was created with -- what a caller tearing this vector down
   * from the outside (see `tls_local_slots::clear_current`) needs to
   * free the vector object itself through the correct allocator. */
  [[nodiscard]] constexpr allocator_ref allocator() const & noexcept { return alloc_; }

  /**
   * @brief Runs every currently populated slot's destroy thunk (via
   * `detail::tls_slot_table::destroy_for`), then frees this vector's own
   * backing array. Safe to call more than once (a no-op once already
   * empty/never populated). See the file-level doc comment: the caller
   * decides when this runs -- nothing here is triggered automatically.
   */
  void clear() & noexcept {
    lock_.lock();
    void **slots = slots_;
    std::size_t capacity = capacity_;
    slots_ = nullptr;
    capacity_ = 0;
    lock_.unlock();

    if (slots == nullptr)
      return;
    for (std::size_t i = 0; i < capacity; ++i) {
      if (slots[i] != nullptr)
        detail::tls_slot_table::destroy_for(i)(slots[i], alloc_);
    }
    alloc_.deallocate(slots, capacity * sizeof(void *));
  }

  /**
   * @brief Grows this vector's slot array, if needed, so it can hold at
   * least @p min_capacity slots -- without any later `get_or_create`/
   * `set` call needing to grow (and therefore allocate) at all, as long
   * as every `(T, Tag)` pair ever used against this vector was already
   * registered (via `tls_slot_index<T, Tag>::get()`, e.g. by touching it
   * through any `tls_slot_vector`/`tls_local_slots` instance at least
   * once) before this call.
   *
   * Exists for a per-CPU `tls_slot_vector` (see the file-level doc
   * comment's per-CPU/`reloco::spin_lock` guidance): its final capacity
   * -- the number of CPUs -- is already known once the boot processor
   * has discovered the topology, and can be reserved once, single-
   * threaded, before any AP (secondary CPU) is started. Doing so here
   * means no `get_or_create`/`set` call reached later from an AP running
   * with preemption disabled ever needs to grow this array (and
   * therefore never needs to call into `allocator()`, however briefly
   * unlocked, from that context) -- entirely sidestepping the
   * preemption-disabled/allocation-ordering concern that guidance
   * describes, rather than merely handling it correctly.
   */
  [[nodiscard]] result<void> reserve(std::size_t min_capacity) & noexcept {
    if (min_capacity == 0)
      return {};
    return ensure_capacity(min_capacity - 1);
  }

  /** @brief Fails with `error::not_found` if this context has no value
   * for `(T, Tag)` yet. */
  template <typename T, typename Tag> [[nodiscard]] result<std::reference_wrapper<T>> try_find() & noexcept {
    std::size_t index = tls_slot_index<T, Tag>::get();
    lock_.lock();
    if (index >= capacity_ || slots_[index] == nullptr) {
      lock_.unlock();
      return unexpected(error::not_found);
    }
    T &value = *static_cast<T *>(slots_[index]);
    lock_.unlock();
    return std::ref(value);
  }

  /**
   * @brief Returns this context's existing `(T, Tag)` value if present,
   * otherwise grows (if needed) and allocates/constructs one seeded with
   * @p initial. Fails only if this vector's own `allocator()` fails to
   * grow the slot array or allocate the new value.
   */
  template <typename T, typename Tag>
  [[nodiscard]] result<std::reference_wrapper<T>> get_or_create(T initial = T{}) & noexcept {
    std::size_t index = tls_slot_index<T, Tag>::get();
    if (auto grown = ensure_capacity(index); !grown)
      return unexpected(grown.error());

    lock_.lock();
    if (slots_[index] != nullptr) {
      T &value = *static_cast<T *>(slots_[index]);
      lock_.unlock();
      return std::ref(value);
    }
    lock_.unlock(); // never call into alloc_ (may block/sleep) while holding lock_.

    auto created = allocate_value(std::move(initial));
    if (!created)
      return unexpected(created.error());
    T *p = *created;

    lock_.lock();
    if (slots_[index] != nullptr) {
      // Another caller reaching this same tls_slot_vector (only possible
      // under a real Lock -- see the class docs) raced in and created
      // one first; discard ours.
      T &value = *static_cast<T *>(slots_[index]);
      lock_.unlock();
      destroy_value(p);
      return std::ref(value);
    }
    slots_[index] = p;
    lock_.unlock();
    return std::ref(*p);
  }

  /** @brief Same lazy-allocate behavior as `get_or_create`, but always
   * overwrites the value. */
  template <typename T, typename Tag> [[nodiscard]] result<void> set(T value) & noexcept {
    std::size_t index = tls_slot_index<T, Tag>::get();
    if (auto grown = ensure_capacity(index); !grown)
      return unexpected(grown.error());

    lock_.lock();
    if (slots_[index] != nullptr) {
      *static_cast<T *>(slots_[index]) = std::move(value);
      lock_.unlock();
      return {};
    }
    lock_.unlock();

    auto created = allocate_value(std::move(value));
    if (!created)
      return unexpected(created.error());
    T *p = *created;

    lock_.lock();
    if (slots_[index] != nullptr) {
      *static_cast<T *>(slots_[index]) = std::move(*p);
      lock_.unlock();
      destroy_value(p);
      return {};
    }
    slots_[index] = p;
    lock_.unlock();
    return {};
  }

  /** @brief Destroys and deallocates this context's `(T, Tag)` value.
   * Fails with `error::not_found` if absent. */
  template <typename T, typename Tag> [[nodiscard]] result<void> erase() & noexcept {
    std::size_t index = tls_slot_index<T, Tag>::get();
    lock_.lock();
    if (index >= capacity_ || slots_[index] == nullptr) {
      lock_.unlock();
      return unexpected(error::not_found);
    }
    void *p = slots_[index];
    slots_[index] = nullptr;
    lock_.unlock();
    destroy_value(static_cast<T *>(p));
    return {};
  }

  allocator_ref allocator() const && = delete;
  void clear() && = delete;
  result<void> reserve(std::size_t min_capacity) && = delete;
  template <typename T, typename Tag> result<std::reference_wrapper<T>> try_find() && = delete;
  template <typename T, typename Tag> result<std::reference_wrapper<T>> get_or_create(T initial = T{}) && = delete;
  template <typename T, typename Tag> result<void> set(T value) && = delete;
  template <typename T, typename Tag> result<void> erase() && = delete;

private:
  // Grows slots_ (plain allocate + copy + free -- see the file docs for
  // why this never bothers with allocator_ref::reallocate) so that
  // index < capacity_, sizing the new capacity through Growth against
  // detail::tls_slot_table::slot_count() (the global "generation"), not
  // merely index + 1, so catching up leaves room for every other
  // (T, Tag) pair already registered by any other context too. Manages
  // its own locking (unlike a plain private helper assumed to run under
  // a lock the caller already holds) so that `alloc_.allocate` --
  // possibly slow/blocking -- is never called with `lock_` held, exactly
  // like `get_or_create`/`set` themselves; callers must not wrap this in
  // their own `lock_.lock()`/`unlock()` pair.
  [[nodiscard]] result<void> ensure_capacity(std::size_t index) noexcept {
    lock_.lock();
    if (index < capacity_) {
      lock_.unlock();
      return {};
    }
    std::size_t old_capacity = capacity_;
    lock_.unlock(); // never call into alloc_ (may block/sleep) while holding lock_.

    std::size_t needed = detail::tls_slot_table::slot_count();
    if (needed <= index)
      needed = index + 1;
    std::size_t new_capacity = growth_.next_bucket_count(old_capacity, needed, /*elements_per_bucket=*/1,
                                                         /*min_buckets=*/1, /*max_buckets=*/RELOCO_TLS_MAX_SLOTS);
    auto block = alloc_.allocate(new_capacity * sizeof(void *), alignof(void *));
    if (!block)
      return unexpected(block.error());
    auto *new_slots = static_cast<void **>(block->ptr);

    lock_.lock();
    if (index < capacity_) {
      // Another caller reaching this same tls_slot_vector (only possible
      // under a real Lock) already grew far enough while we were
      // allocating unlocked; discard ours.
      lock_.unlock();
      alloc_.deallocate(new_slots, new_capacity * sizeof(void *));
      return {};
    }
    for (std::size_t i = 0; i < capacity_; ++i)
      new_slots[i] = slots_[i];
    for (std::size_t i = capacity_; i < new_capacity; ++i)
      new_slots[i] = nullptr;
    void **old_slots = slots_;
    std::size_t old_slots_capacity = capacity_;
    slots_ = new_slots;
    capacity_ = new_capacity;
    lock_.unlock();

    if (old_slots != nullptr)
      alloc_.deallocate(old_slots, old_slots_capacity * sizeof(void *));
    return {};
  }

  template <typename T> [[nodiscard]] result<T *> allocate_value(T value) noexcept {
    auto block = alloc_.allocate(sizeof(T), alignof(T));
    if (!block)
      return unexpected(block.error());
    return ::new (block->ptr) T(std::move(value));
  }

  template <typename T> void destroy_value(T *p) noexcept {
    p->~T();
    alloc_.deallocate(p, sizeof(T));
  }

  allocator_ref alloc_;
  RELOCO_NO_UNIQUE_ADDRESS Lock lock_{};
  RELOCO_NO_UNIQUE_ADDRESS Growth growth_{};
  void **slots_ = nullptr;
  std::size_t capacity_ = 0;
};

/**
 * @brief See the file-level doc comment: the trait an OS/porting layer
 * must specialize (for its own `Tag`) to plug `tls_local_slots<..., Tag>`
 * into its own per-thread/per-task storage -- exactly one pointer-sized
 * slot per calling context, nothing else.
 */
template <typename Tag> struct tls_local_state_traits;

#if !defined(RELOCO_KERNEL)
/**
 * @brief Hosted default specialization (`Tag = void`): a plain C++11
 * `thread_local void *`. Only defined when `RELOCO_KERNEL` is not set --
 * a kernel/bare-metal port (which this whole file exists for) has no
 * business getting a hosted `thread_local` fallback handed to it for
 * free; it must always supply its own `tls_local_state_traits<Tag>`
 * specialization, for whatever `Tag` it passes to `tls_local_slots`,
 * against its own per-thread/per-task storage.
 */
template <> struct tls_local_state_traits<void> {
  [[nodiscard]] static void *get() noexcept { return ptr_; }
  static void set(void *ptr) noexcept { ptr_ = ptr; }

private:
  static inline thread_local void *ptr_ = nullptr;
};
#endif // !RELOCO_KERNEL

/**
 * @brief See the file-level doc comment. Lazily creates exactly one
 * `tls_slot_vector<Lock, Growth>` per calling context (published through
 * `tls_local_state_traits<Tag>`) and forwards every call to it.
 * @tparam Tag Selects which `tls_local_state_traits<Tag>` specialization
 *   provides this calling context's storage slot; defaults to `void`
 *   (the hosted `thread_local` specialization above).
 */
template <typename Lock = null_mutex, typename Growth = bucket_growth::doubling_then_ratio, typename Tag = void>
class tls_local_slots {
public:
  using slot_vector = tls_slot_vector<Lock, Growth>;

  tls_local_slots() = delete; // namespacing only -- every member is static, never instantiated.

  /** @brief Fails with `error::not_found` if this context never created
   * a `(T, Tag2)` value (or never created a vector at all yet). */
  template <typename T, typename Tag2> [[nodiscard]] static result<std::reference_wrapper<T>> try_find() noexcept {
    slot_vector *vec = current();
    if (vec == nullptr)
      return unexpected(error::not_found);
    return vec->template try_find<T, Tag2>();
  }

  /** @brief See `tls_slot_vector::get_or_create`; lazily creates this
   * context's vector itself (through @p alloc) first, if needed. */
  template <typename T, typename Tag2>
  [[nodiscard]] static result<std::reference_wrapper<T>> get_or_create(allocator_ref alloc = default_allocator(),
                                                                       T initial = T{}) noexcept {
    auto vec = ensure_vector(alloc);
    if (!vec)
      return unexpected(vec.error());
    return (*vec)->template get_or_create<T, Tag2>(std::move(initial));
  }

  /** @brief See `tls_slot_vector::set`; lazily creates this context's
   * vector itself (through @p alloc) first, if needed. */
  template <typename T, typename Tag2>
  [[nodiscard]] static result<void> set(T value, allocator_ref alloc = default_allocator()) noexcept {
    auto vec = ensure_vector(alloc);
    if (!vec)
      return unexpected(vec.error());
    return (*vec)->template set<T, Tag2>(std::move(value));
  }

  /** @brief Fails with `error::not_found` if this context never created
   * a `(T, Tag2)` value (or never created a vector at all yet). */
  template <typename T, typename Tag2> [[nodiscard]] static result<void> erase() noexcept {
    slot_vector *vec = current();
    if (vec == nullptr)
      return unexpected(error::not_found);
    return vec->template erase<T, Tag2>();
  }

  /**
   * @brief Runs every populated slot's destructor for *this calling
   * context's* vector, then frees the vector object itself (through the
   * exact allocator it was created with -- `slot_vector::allocator()`,
   * so the caller does not need to remember/pass one) and resets
   * `tls_local_state_traits<Tag>`'s stored pointer to `nullptr`. A no-op
   * if this context never created a vector in the first place.
   *
   * **The caller is entirely responsible for calling this at whatever
   * point it already detects the underlying thread/task is going away**
   * -- see the file-level doc comment.
   */
  static void clear_current() noexcept {
    slot_vector *vec = current();
    if (vec == nullptr)
      return;
    allocator_ref alloc = vec->allocator();
    vec->clear();
    vec->~slot_vector();
    alloc.deallocate(vec, sizeof(slot_vector));
    tls_local_state_traits<Tag>::set(nullptr);
  }

private:
  [[nodiscard]] static slot_vector *current() noexcept {
    return static_cast<slot_vector *>(tls_local_state_traits<Tag>::get());
  }

  [[nodiscard]] static result<slot_vector *> ensure_vector(allocator_ref alloc) noexcept {
    slot_vector *vec = current();
    if (vec != nullptr)
      return vec;
    auto block = alloc.allocate(sizeof(slot_vector), alignof(slot_vector));
    if (!block)
      return unexpected(block.error());
    auto *created = ::new (block->ptr) slot_vector(alloc);
    tls_local_state_traits<Tag>::set(created);
    return created;
  }
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
