// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file keyed_intrusive_registry.hpp
 * @brief `keyed_intrusive_registry<T, OwnerKey, Tag, Lock>`: a locked,
 * self-allocating "one `T` per `OwnerKey`" registry, built on top of
 * `intrusive_hash_table` -- the building block a `RELOCO_TLS_MODEL_OS`
 * port (`detail/porting/tls_provider.hpp`, see `tls_provider.hpp`) needs
 * to implement genuine per-thread/per-task storage without hand-rolling
 * its own locking, node allocation, or (as bad as a linear scan)
 * O(n)-per-lookup data structure.
 *
 * `detail/porting/tls_provider.template.hpp`'s own scaffold sketches a
 * `mtx(9)`-guarded singly-linked list scanned linearly on every `get()`/
 * `set()`, and says outright that "a real port should index by something
 * cheaper ... or a proper hash table". This file *is* that proper hash
 * table, generalized: `OwnerKey` is deliberately not "the current
 * thread" (`curthread`, a task pointer, a CPU id, ...) or any other
 * fixed identity concept -- reloco has no opinion on what identifies
 * "the calling context" on any given target, and never will (see
 * `tls_provider.hpp`'s own file docs: the "TLS register policy" is
 * entirely the porting caller's business). Every lookup here takes an
 * explicit `OwnerKey` the caller already obtained however it likes
 * (`curthread`, `PCPU_GET(cpuid)`, ...); this file only ever stores,
 * finds, and removes a `T` per distinct key.
 *
 * **Node ownership**: unlike bare `intrusive_hash_table` (which never
 * allocates -- every node is caller-owned, see that file's own docs),
 * `keyed_intrusive_registry` allocates/frees each `(OwnerKey, T)` node
 * itself, through the `allocator_ref` passed to whichever call first
 * needs one (`get_or_create`/`set`) -- lazily, on first use per key, not
 * up front. Every node additionally stores the exact `allocator_ref` it
 * was allocated with (mirroring `tls_provider.hpp`'s own
 * `RELOCO_TLS_MODEL_PTHREAD` heap-allocated specialization's `record`),
 * so `erase()` always deallocates through the right allocator, even if
 * some other call site later passes a different one.
 *
 * **Bucket array ownership**: matches `intrusive_hash_table` exactly --
 * still entirely caller-owned (a `static node *buckets[N];`, or a
 * `span<node *>` obtained however the caller likes), and growing it is
 * still a manual, caller-driven two-step protocol (allocate a bigger
 * `span<node *>`, e.g. sized via `bucket_growth.hpp` against
 * `bucket_count()`/`size()`, *without* holding this registry's lock,
 * then call `rehash(new_buckets)` -- see `intrusive_hash_table.hpp`'s own
 * "Growing" section for the exact rationale, which applies unchanged
 * here). `keyed_intrusive_registry` deliberately never grows its own
 * bucket array automatically: doing so from inside `get_or_create`/`set`
 * would mean calling into a (possibly slow, possibly sleeping) allocator
 * while this registry's own `Lock` is held, exactly the hazard the
 * "unlock, allocate, relock" discipline below exists to avoid in the
 * first place.
 *
 * **Locking**: `Lock` is a template parameter (default `null_mutex`,
 * see `pool_allocator.hpp`), not a fixed backend -- pass `reloco::mutex`
 * or `reloco::spin_lock` (or a target-specific kernel lock exposing the
 * same `lock()`/`unlock()`/`try_lock()` surface, e.g. a `struct mtx`
 * wrapper) for genuine multi-threaded/multi-core use. Every call that
 * touches the table takes @c Lock for the duration of the table
 * operation only: `get_or_create`/`set` release it *before* calling
 * `alloc.allocate` (which may block/sleep on a real kernel target) and
 * only reacquire it to publish the newly allocated node -- the exact
 * same "unlock, allocate, relock" discipline `pool_allocator`'s own
 * `refill()` uses, including a re-check immediately after reacquiring
 * the lock in case another caller raced in and created the same key's
 * node in the meantime (whichever node loses that race is simply
 * discarded/deallocated, never leaked).
 *
 * A per-thread `OwnerKey` (e.g. `curthread`) is usually reached from an
 * ordinary, preemptible context, so `reloco::mutex` (blocking, may
 * sleep while contended) is a fine choice there. A per-CPU `OwnerKey`
 * (e.g. `PCPU_GET(cpuid)`) is a different story: real kernels read/write
 * per-CPU state with preemption disabled specifically so nothing else
 * can migrate the calling context to another CPU mid-access, and
 * blocking/sleeping while preemption is disabled is illegal on most
 * targets (FreeBSD panics; Linux's `might_sleep()` debug checks exist
 * for exactly this). `Lock` must therefore be `reloco::spin_lock` (or an
 * equivalent non-blocking kernel spinlock), never `reloco::mutex`, for
 * any registry a preemption-disabled context reaches -- this file's own
 * "unlock, allocate, relock" discipline already keeps its half of the
 * bargain (the lock, whichever kind it is, is never held across
 * `alloc.allocate`), but the allocator call itself still must not happen
 * while the *caller's* preemption is disabled. A port whose fast path
 * (`try_find`) runs with preemption disabled and only needs to fall back
 * to `get_or_create` on a miss must do so *after* re-enabling
 * preemption, then retry the fast path once the value exists -- exactly
 * the same "look up locked, allocate unlocked, then loop back" shape
 * `get_or_create` itself already uses one level down, just with an outer
 * preemption-enable/disable pair added around it instead of another
 * lock.
 *
 * - `keyed_intrusive_registry(buckets)`: adopts @p buckets exactly like
 *   `intrusive_hash_table::try_create` (`RELOCO_ASSERT`s it is
 *   non-empty, rather than returning a `result`, since this type embeds
 *   a non-movable @c Lock and so cannot itself be returned by value from
 *   a fallible factory function -- construct it once, in place, like
 *   `pool_allocator`).
 * - `try_find(owner)` -> `result<reference_wrapper<T>>`: fails with
 *   `error::not_found` if @p owner has no node yet.
 * - `get_or_create(owner, alloc, initial)` -> `result<reference_wrapper<T>>`:
 *   returns the existing node's value if present, otherwise allocates
 *   and links a new one seeded with @p initial. Fails only if @p alloc's
 *   allocation fails.
 * - `set(owner, value, alloc)` -> `result<void>`: same lazy-allocate
 *   behavior as `get_or_create`, but always overwrites the value.
 * - `erase(owner)` -> `result<void>`: unlinks and deallocates @p owner's
 *   node (through the very `allocator_ref` it was created with). Fails
 *   with `error::not_found` if absent. This is what a kernel port's
 *   thread/task-exit hook calls to reclaim storage, matching
 *   `RELOCO_TLS_MODEL_PTHREAD`'s own `pthread_key_create` destructor.
 * - `rehash(new_buckets)`, `size()`, `bucket_count()`,
 *   `load_factor_permille()`, `suggest_bucket_count_for_insert`/
 *   `_for_remove`: locked pass-throughs to the same-named
 *   `intrusive_hash_table` members -- see that file's docs.
 *
 * Neither copyable nor movable (a live @c Lock, e.g. `reloco::mutex`,
 * is not either), matching `pool_allocator`'s own decision for the same
 * reason.
 */

#include "allocator.hpp"
#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "intrusive_hash_table.hpp"
#include "lifetime.hpp"
#include "pool_allocator.hpp" // for null_mutex
#include "span.hpp"

#include <cstddef>
#include <functional>
#include <new>
#include <utility>

// allocate_node()/erase() below call allocator_ref::allocate/deallocate,
// which are RELOCO_UNSAFE_BUFFER_USAGE-marked -- see allocator.hpp/tls_provider.hpp.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/**
 * @brief See the file-level doc comment.
 * @tparam T Value type stored per `OwnerKey`.
 * @tparam OwnerKey Whatever identifies "the calling context" on the
 *   target (a thread pointer, a CPU id, ...) -- entirely the caller's
 *   choice; reloco has no opinion on it.
 * @tparam Tag Unique tag distinguishing one registry's node type from
 *   another's, exactly like `tls_provider<T, Tag>`'s own `Tag` -- only
 *   relevant if several `keyed_intrusive_registry`s happen to share the
 *   same `T, OwnerKey` pair and must not be confused with one another at
 *   the type level (e.g. by `intrusive_hash_iterator`/ADL); otherwise
 *   free to leave defaulted where a using-declaration provides one.
 * @tparam Lock Anything satisfying `lock()`/`unlock()`/`try_lock()`
 *   (`reloco::mutex`, `reloco::spin_lock`, a custom kernel lock, ...).
 *   Defaults to `null_mutex` -- a no-op, matching `pool_allocator`'s own
 *   default -- for single-threaded use or external synchronization.
 * @tparam Hash `Hash{}(const OwnerKey &) -> size_t`, defaults to
 *   `std::hash<OwnerKey>`.
 * @tparam KeyEqual `KeyEqual{}(const OwnerKey &, const OwnerKey &) ->
 *   bool`, defaults to `std::equal_to<OwnerKey>`.
 */
template <typename T, typename OwnerKey, typename Tag = void, typename Lock = null_mutex,
          typename Hash = std::hash<OwnerKey>, typename KeyEqual = std::equal_to<OwnerKey>>
class keyed_intrusive_registry {
public:
  /**
   * @brief The node type this registry's `intrusive_hash_table` links --
   * exposed publicly only so a caller can size a `span<node *>` bucket
   * array (e.g. `static node *buckets[64];`) to pass to the constructor/
   * `rehash`; nothing about its layout is otherwise meant to be touched
   * directly.
   */
  struct node {
    intrusive_hash_hook<node> hook;
    OwnerKey owner;
    allocator_ref alloc; ///< The exact allocator this node was allocated with -- see `erase()`.
    T value;
  };

  using size_type = std::size_t;

  /**
   * @brief Adopts @p buckets exactly like
   * `intrusive_hash_table::try_create` -- see the file-level doc comment
   * for why this is a plain, `RELOCO_ASSERT`-guarded constructor rather
   * than a fallible `try_create` factory.
   */
  explicit keyed_intrusive_registry(span<node *> buckets) noexcept : table_(make_table(buckets)) {}

  keyed_intrusive_registry(const keyed_intrusive_registry &) = delete;
  keyed_intrusive_registry &operator=(const keyed_intrusive_registry &) = delete;
  keyed_intrusive_registry(keyed_intrusive_registry &&) = delete;
  keyed_intrusive_registry &operator=(keyed_intrusive_registry &&) = delete;

  ~keyed_intrusive_registry() noexcept {
    // While a destructor inherently assumes exclusive access (the object is dying,
    // so no other thread should be calling methods on it), we acquire the lock
    // defensively to ensure memory visibility and consistency.
    lock_.lock();
    for (auto it = table_.begin(); it != table_.end();) {
      node &n = *it;
      // Advance the iterator before unlinking the node to prevent invalidation.
      auto next = it;
      ++next;

      table_.remove(n);
      destroy_node(n);

      it = next;
    }
    lock_.unlock();
  }

  [[nodiscard]] size_type size() & noexcept {
    lock_.lock();
    size_type n = table_.size();
    lock_.unlock();
    return n;
  }

  [[nodiscard]] size_type bucket_count() & noexcept {
    lock_.lock();
    size_type n = table_.bucket_count();
    lock_.unlock();
    return n;
  }

  [[nodiscard]] size_type load_factor_permille() & noexcept {
    lock_.lock();
    size_type v = table_.load_factor_permille();
    lock_.unlock();
    return v;
  }

  /** @brief Locked pass-through to `intrusive_hash_table::suggest_bucket_count_for_insert`. */
  [[nodiscard]] size_type suggest_bucket_count_for_insert(size_type n, size_type min_buckets, size_type max_buckets,
                                                          size_type elements_per_bucket = 1) & noexcept {
    lock_.lock();
    size_type v = table_.suggest_bucket_count_for_insert(n, min_buckets, max_buckets, elements_per_bucket);
    lock_.unlock();
    return v;
  }

  /** @brief Locked pass-through to `intrusive_hash_table::suggest_bucket_count_for_remove`. */
  [[nodiscard]] size_type suggest_bucket_count_for_remove(size_type n, size_type min_buckets, size_type max_buckets,
                                                          size_type elements_per_bucket = 1) & noexcept {
    lock_.lock();
    size_type v = table_.suggest_bucket_count_for_remove(n, min_buckets, max_buckets, elements_per_bucket);
    lock_.unlock();
    return v;
  }

  /**
   * @brief See the file-level doc comment's "Bucket array ownership"
   * section: @p new_buckets must be allocated by the caller *before*
   * calling this, without holding any lock this registry does not
   * already provide -- this call takes `Lock` only for the O(n) relink
   * itself, exactly like bare `intrusive_hash_table::rehash`.
   */
  [[nodiscard]] result<void> rehash(span<node *> new_buckets) & noexcept {
    lock_.lock();
    auto rehashed = table_.rehash(new_buckets);
    lock_.unlock();
    return rehashed;
  }

  /** @brief Fails with `error::not_found` if @p owner has no node yet. */
  [[nodiscard]] result<std::reference_wrapper<T>> try_find(const OwnerKey &owner) & noexcept {
    lock_.lock();
    auto found = table_.try_find(owner);
    if (!found) {
      lock_.unlock();
      return unexpected(found.error());
    }
    T &value = found->get().value;
    lock_.unlock();
    return std::ref(value);
  }

  /**
   * @brief Returns @p owner's existing value if present, otherwise
   * allocates (through @p alloc, not `new`) and links a new node seeded
   * with @p initial. See the file-level doc comment's "Locking" section
   * for the unlock/allocate/relock discipline this follows. Fails only
   * if @p alloc's allocation fails.
   */
  [[nodiscard]] result<std::reference_wrapper<T>>
  get_or_create(const OwnerKey &owner, allocator_ref alloc = default_allocator(), T initial = T{}) & noexcept {
    lock_.lock();
    auto found = table_.try_find(owner);
    if (found) {
      T &value = found->get().value;
      lock_.unlock();
      return std::ref(value);
    }
    lock_.unlock(); // never call into alloc (may block/sleep) while holding lock_.

    auto created = allocate_node(owner, alloc, std::move(initial));
    if (!created)
      return unexpected(created.error());
    node *n = *created;

    lock_.lock();
    auto raced = table_.try_find(owner); // another caller may have created one while we were unlocked.
    if (raced) {
      T &value = raced->get().value;
      lock_.unlock();
      destroy_node(*n);
      return std::ref(value);
    }
    auto inserted = table_.try_insert(*n);
    RELOCO_ASSERT(inserted.has_value(), "reloco::keyed_intrusive_registry::get_or_create: unexpected duplicate key");
    T &value = n->value;
    lock_.unlock();
    return std::ref(value);
  }

  /**
   * @brief Same lazy-allocate behavior as `get_or_create`, but always
   * overwrites @p owner's value (creating a node first if absent).
   */
  [[nodiscard]] result<void> set(const OwnerKey &owner, T value, allocator_ref alloc = default_allocator()) & noexcept {
    lock_.lock();
    auto found = table_.try_find(owner);
    if (found) {
      found->get().value = std::move(value);
      lock_.unlock();
      return {};
    }
    lock_.unlock();

    auto created = allocate_node(owner, alloc, std::move(value));
    if (!created)
      return unexpected(created.error());
    node *n = *created;

    lock_.lock();
    auto raced = table_.try_find(owner);
    if (raced) {
      raced->get().value = std::move(n->value);
      lock_.unlock();
      destroy_node(*n);
      return {};
    }
    auto inserted = table_.try_insert(*n);
    RELOCO_ASSERT(inserted.has_value(), "reloco::keyed_intrusive_registry::set: unexpected duplicate key");
    lock_.unlock();
    return {};
  }

  /**
   * @brief Unlinks and deallocates @p owner's node, through the exact
   * `allocator_ref` it was created with (see the `node::alloc` field
   * docs above) -- the caller does not need to remember/pass one. Fails
   * with `error::not_found` if absent.
   */
  [[nodiscard]] result<void> erase(const OwnerKey &owner) & noexcept {
    lock_.lock();
    auto found = table_.try_find(owner);
    if (!found) {
      lock_.unlock();
      return unexpected(found.error());
    }
    node &n = found->get();
    table_.remove(n);
    lock_.unlock();
    destroy_node(n);
    return {};
  }

  size_type size() && = delete;
  size_type bucket_count() && = delete;
  size_type load_factor_permille() && = delete;
  size_type suggest_bucket_count_for_insert(size_type n, size_type min_buckets, size_type max_buckets,
                                            size_type elements_per_bucket = 1) && = delete;
  size_type suggest_bucket_count_for_remove(size_type n, size_type min_buckets, size_type max_buckets,
                                            size_type elements_per_bucket = 1) && = delete;
  result<void> rehash(span<node *> new_buckets) && = delete;
  result<std::reference_wrapper<T>> try_find(const OwnerKey &owner) && = delete;
  result<std::reference_wrapper<T>> get_or_create(const OwnerKey &owner, allocator_ref alloc = default_allocator(),
                                                  T initial = T{}) && = delete;
  result<void> set(const OwnerKey &owner, T value, allocator_ref alloc = default_allocator()) && = delete;
  result<void> erase(const OwnerKey &owner) && = delete;

private:
  /** @brief Extracts the owner key from a registry node. */
  struct owner_of {
    const OwnerKey &operator()(const node &n) const noexcept { return n.owner; }
  };

  using table_type = intrusive_hash_table<node, &node::hook, owner_of, Hash, KeyEqual>;

  static table_type make_table(span<node *> buckets) noexcept {
    auto created = table_type::try_create(buckets);
    RELOCO_ASSERT(created.has_value(), "reloco::keyed_intrusive_registry: buckets must not be empty");
    return std::move(*created);
  }

  [[nodiscard]] static result<node *> allocate_node(const OwnerKey &owner, allocator_ref alloc, T value) noexcept {
    auto block = alloc.allocate(sizeof(node), alignof(node));
    if (!block)
      return unexpected(block.error());
    auto *n = ::new (block->ptr) node{{}, owner, alloc, std::move(value)};
    return n;
  }

  static void destroy_node(node &n) noexcept {
    allocator_ref alloc = n.alloc;
    n.~node();
    alloc.deallocate(&n, sizeof(node));
  }

  RELOCO_NO_UNIQUE_ADDRESS Lock lock_{};
  table_type table_;
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
