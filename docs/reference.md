<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# API reference

Quick, per-type reference for every public reloco header. Start with a
[guide](../README.md#guides) for the concepts behind these types (fallible
construction, the checked/`try_*`/`unsafe_*` tri-tier convention, lifetime
annotations, trivial relocation); this page is a map of what exists and
where, not a tutorial.

| Header | Type(s) | One-line summary |
|---|---|---|
| `expected.hpp` | `expected<T, E>`, `unexpected<E>` | Allocation-free value-or-error result |
| `error.hpp` | `error`, `result<T>` | The one error enum every fallible reloco operation returns, and its `expected<T, error>` alias |
| `error_std.hpp` | `error_category()`, `make_error_code(error)`, `make_error_condition(error)` | Opt-in `<system_error>` binding: makes `reloco::error` convert to `std::error_code`/`std::error_condition` |
| `span.hpp` | `span<T>` | Non-owning, checked view over a contiguous range |
| `array.hpp` | `array<T, N>` | Fixed-size owning array with hardened element access |
| `string_view.hpp` | `basic_string_view<CharT, TraitsT>` (`string_view`, `wstring_view`) | Non-owning, checked view over character data |
| `string.hpp` | `basic_string<CharT, TraitsT>` (`string`, `wstring`) | Move-only, allocator-backed, growable character buffer |
| `inline_string.hpp` | `basic_inline_string<Capacity, CharT, TraitsT>` | Fixed-capacity, trivially-copyable, allocation-free character buffer |
| `vector.hpp` | `vector<T>` | Move-only, allocator-backed, growable dynamic array |
| `flat_set.hpp` | `flat_set<T, Compare>` | Sorted, unique-element set backed by `vector<T>`, with fallible insertion |
| `flat_map.hpp` | `flat_map<Key, Mapped, Compare>` | Sorted, unique-key map backed by `vector<std::pair<Key, Mapped>>`, with fallible insertion |
| `inline_flat_set.hpp` | `inline_flat_set<T, Capacity, Compare>` | Allocation-free counterpart of `flat_set<T, Compare>`, backed by `inline_vector<T, Capacity>` |
| `inline_flat_map.hpp` | `inline_flat_map<Key, Mapped, Capacity, Compare>` | Allocation-free counterpart of `flat_map<Key, Mapped, Compare>`, backed by `inline_vector<std::pair<Key, Mapped>, Capacity>` |
| `tree_set.hpp` | `tree_set<T, Compare>` | Sorted, unique-element set backed by an allocator-managed, unbalanced binary search tree, with stable references and fallible insertion |
| `tree_map.hpp` | `tree_map<Key, Mapped, Compare>` | Sorted, unique-key map backed by an allocator-managed, unbalanced binary search tree over `(Key, Mapped)` pairs, with stable references and fallible insertion |
| `flat_hash_set.hpp` | `flat_hash_set<T, Hash, KeyEqual>` | Unordered, unique-element set backed by an open-addressing `vector<optional<T>>` hash table, with fallible insertion |
| `flat_hash_map.hpp` | `flat_hash_map<Key, Mapped, Hash, KeyEqual>` | Unordered, unique-key map backed by an open-addressing `vector<optional<std::pair<Key, Mapped>>>` hash table, with fallible insertion |
| `optional.hpp` | `optional<T>`, `nullopt_t`, `nullopt` | Zero-allocation, conditionally-present value wrapper with tri-tier access |
| `variant.hpp` | `variant<Ts...>`, `overloaded<Fs...>` | `std::variant<Ts...>` with Rust-style `match()` and tri-tier access on top |
| `function_ref.hpp` | `function_ref<R(Args...)>` | Non-owning, zero-allocation borrow of any callable |
| `inplace_function.hpp` | `inplace_function<Signature, Capacity>` | Zero-allocation, fixed-capacity callable wrapper |
| `stack_allocator.hpp` | `stack_allocator`, `stack_allocator_tag`, `stack_allocator_context` | Bump-pointer `allocator_traits` backend over a caller-owned buffer |
| `pool_allocator.hpp` | `pool_allocator<Lock>`, `pool_allocator_tag<Lock>`, `pool_allocator_context<Lock>`, `null_mutex` | Fixed-block-size `allocator_traits` backend carving blocks out of slabs obtained from an upstream allocator, with kernel-style "unlock, allocate, relock" refill |
| `bucket_allocator.hpp` | `bucket_allocator<Lock, BucketSizes...>`, `bucket_allocator_tag<Lock, BucketSizes...>`, `bucket_allocator_context<Lock, BucketSizes...>` | General-purpose `allocator_traits` backend combining a compile-time list of `pool_allocator`s, one per size bucket, routing each request to the smallest bucket that fits |
| `malloc_allocator.hpp` | `malloc_allocator<Lock>`, `malloc_allocator_tag<Lock>`, `malloc_allocator_context<Lock>` | Variable-size, boundary-tag `malloc`/`free`/`memalign`/`realloc`-style `allocator_traits` backend carving blocks out of arenas obtained on demand from an upstream allocator, releasing a whole arena back to upstream as soon as it's fully idle again |
| `bucket_growth.hpp` | `bucket_growth::linear`, `bucket_growth::doubling_then_ratio`, `bucket_growth::fixed_ratio`, `bucket_growth::power_of_two`, `bucket_growth::prime_growth`, `bucket_growth::chunked`, `bucket_growth::sqrt_curve` | Optional, integer-only bucket-count growth curves for hash containers whose bucket array is caller-sized (e.g. `intrusive_hash_table`); each exposes the same `next_bucket_count(current_buckets, projected_elements, elements_per_bucket, min_buckets, max_buckets)` interface |
| `keyed_intrusive_registry.hpp` | `keyed_intrusive_registry<T, OwnerKey, Tag, Lock, Hash, KeyEqual>` | Locked, self-allocating "one `T` per `OwnerKey`" registry built on `intrusive_hash_table`; the hash-table-per-owner-key building block for a `RELOCO_TLS_MODEL_OS` kernel/RTOS port (see `tls_provider.hpp`) |
| `tls_slot_vector.hpp` | `tls_slot_vector<Lock, Growth>`, `tls_slot_index<T, Tag>`, `tls_local_slots<Lock, Growth, Tag>`, `tls_local_state_traits<Tag>` | The classic pthread-key design (global slot table + atomic generation counter, per-context growable pointer vector) as an alternative to `keyed_intrusive_registry.hpp` for the same `RELOCO_TLS_MODEL_OS` use case; `O(1)` slot indexing instead of a hash lookup per `get()`/`set()` |
| `unique_ptr.hpp` | `unique_ptr<T>` | Move-only, allocator-backed smart pointer with fallible construction |
| `shared_ptr.hpp` | `shared_ptr<T>`, `weak_ptr<T>`, `enable_shared_from_this<T>` | Reference-counted, allocator-backed smart pointer with fallible construction |
| `rc.hpp` | `rc<T>`, `weak_rc<T>`, `enable_rc_from_this<T>` | Single-threaded (non-atomic) reference-counted smart pointer, matching Rust's `Rc<T>`/`Weak<T>` |
| `bytes.hpp` | `bytes`, `bytes_mut` | Immutable, reference-counted, cheaply-cloneable byte buffer and its growable, exclusively-owned mutable counterpart, matching Rust's `bytes::Bytes`/`bytes::BytesMut` |
| `masked_byte_region.hpp` | `masked_byte_region<Size, NoncePolicy>`, `security::inline_nonce_storage` | Fixed-size byte region stored behind a nonce-based masking policy |
| `tamper.hpp` | `masked_integral<T>`, `tamper_proof_state<EnumT>`, `tamper_bool_impl` | Tamper-detecting integral, enum-state, and boolean storage wrappers |
| `obfuscated_string.hpp` | `obfuscated_string<N>`, `obfuscated_string_ref`, `RELOCO_OBFUSCATED_STR(str)`, `RELOCO_DECLARE_OBFUSCATED_STR`/`RELOCO_DEFINE_OBFUSCATED_STR` | Compile-time XOR-masked string literals; a type-erased, forward-declarable handle; and a byte-at-a-time, no-storage decode path |
| `binary_heap.hpp` | `binary_heap<T, Compare>` | Allocator-backed priority queue matching Rust's `BinaryHeap<T>`, built on `vector<T>` |
| `digraph.hpp` | `digraph` | Allocator-backed directed graph over dense node indices, rejecting any edge that would close a cycle -- for lock-order/witness-style (`witness(4)`/lockdep) dependency tracking |
| `intrusive_hash_table.hpp` | `intrusive_hash_hook<T>`, `intrusive_hash_table<T, Hook, KeyOf, Hash, KeyEqual>` | Non-owning, unique-key hash table over caller-owned intrusive nodes |
| `intrusive_rbtree.hpp` | `intrusive_rbtree_hook<T>`, `intrusive_rbtree<T, Hook, KeyOf, Compare>` | Non-owning, unique-key, worst-case `O(log n)` red-black tree over caller-owned intrusive nodes |
| `intrusive_splay_tree.hpp` | `intrusive_splay_tree_hook<T>`, `intrusive_splay_tree<T, Hook, KeyOf, Compare>` | Non-owning, unique-key, self-adjusting (amortized `O(log n)`) splay tree over caller-owned intrusive nodes |
| `intrusive_c_list.hpp` | `c_list_hook_layout<T>`, `c_list_hook_access<T, Hook>`, `c_list_iterator<T, Hook, IsConst>`, `c_list<T, Hook>` | Intrusive adapter for a BSD-style doubly linked C list |
| `intrusive_c_list_head.hpp` | `c_list_head_node`, `c_linux_hook_access<T, Hook>`, `c_list_head_iterator<T, Hook, IsConst>`, `c_list_head<T, Hook>` | Intrusive adapter for a Linux-style list-head doubly linked list |
| `intrusive_c_slist.hpp` | `c_slist_hook_access<T, Hook>`, `c_slist_iterator<T, Hook, IsConst>`, `c_slist<T, Hook>` | Intrusive adapter for a singly linked C list |
| `intrusive_c_stailq.hpp` | `c_stailq_hook_access<T, Hook>`, `c_stailq_iterator<T, Hook, IsConst>`, `c_stailq<T, Hook>` | Intrusive adapter for a singly linked tail queue |
| `intrusive_c_tailq.hpp` | `c_tailq_hook_layout<T>`, `c_tailq_hook_access<T, Hook>`, `c_tailq_iterator<T, Hook, IsConst>`, `c_tailq<T, Hook>` | Intrusive adapter for a doubly linked tail queue |
| `contiguous_iterator.hpp` | `static_bounds_policy<T>`, `dynamic_bounds_policy<Container, T>`, `contiguous_iterator<T, BoundsPolicy>` | Bounds-aware random-access iterator over contiguous storage |
| `boxed_slice.hpp` | `boxed_slice<T>` | Fixed-size, allocator-backed owned array with no spare capacity, matching Rust's `Box<[T]>` |
| `cow.hpp` | `cow<T>`, `cow_traits<T>` | Clone-on-write wrapper matching Rust's `Cow<'a, T>`, with a user-specializable clone customization point |
| `function.hpp` | `function<R(Args...)>` | Type-erased, allocator-backed callable wrapper with fallible construction |
| `type_id.hpp` | `type_id`, `type_id_of<T>()`, `RELOCO_TYPE_ID_NAME` | Process-wide type identity established without RTTI, matching Rust's `std::any::TypeId`, with optional debug names |
| `any.hpp` | `any` | Type-erased, allocator-backed single-value container with fallible construction and no RTTI dependency |
| `collection_view.hpp` | `collection_view<T>`, `mutable_collection_view<T>`, `collection_view_traits<Container>` | Type-erased, non-owning views over an adapted sequence container |
| `container_ref.hpp` | `mutable_container_ref<T, Key = void>`, `container_ref_traits<Container>` | Type-erased handle for structurally mutating (growing/inserting/erasing) an adapted sequence or associative container |
| `container_ref_std.hpp` | `container_ref_traits<std::vector<T>>`, `container_ref_traits<std::map<Key, Value>>` | Opt-in `container_ref_traits` adapters for `std::vector`/`std::map` |
| `fmt.hpp` | `sink`, `Display<T>`, `Debug<T>`, `has_display_v<T>`, `has_debug_v<T>` | Type-erased output sink plus opt-in `Display`/`Debug` customization points, matching Rust's `std::fmt::Display`/`std::fmt::Debug`; reloco never specializes either for its own types |
| `value_ptr.hpp` | `value_ptr<T>` | Nullable, non-owning pointer that rejects binding to prvalue temporaries |
| `value_ref.hpp` | `value_ref<T>` | Non-null, non-owning reference wrapper that rejects binding to prvalue temporaries |
| `checked_value.hpp` | `checked_value<T>` | Move-only wrapper with Rust-like use-after-move checks |
| `cell.hpp` | `cell<T>`, `ref_cell<T>` | Interior-mutability wrappers matching Rust's `Cell<T>`/`RefCell<T>` |
| `non_zero.hpp` | `non_zero<T>` | Integral wrapper statically known to never be `0`, matching Rust's `NonZero*` family |
| `wrapping.hpp` | `wrapping<T>` | Integral newtype whose arithmetic operators always wrap on overflow, matching Rust's `std::num::Wrapping<T>` |
| `saturating.hpp` | `saturating<T>` | Integral newtype whose arithmetic operators always clamp on overflow, matching Rust's `std::num::Saturating<T>` |
| `checked.hpp` | `checked<T>` | Integral newtype whose arithmetic is always explicitly fallible via `try_add/sub/mul/div/rem/neg/abs` returning `result<checked<T>>` |
| `int_ops.hpp` | `checked_add/sub/mul/div/rem/neg/abs`, `wrapping_add/sub/mul`, `saturating_add/sub/mul`, `overflowing_add/sub/mul`, `overflowing_result<T>`, `checked_cast<To>` | Free-function Rust-style checked/wrapping/saturating/overflowing integer arithmetic and range-checked numeric casts |
| `fixed_point.hpp` | `fixed_point<Rep, FracBits>`, `taylor_eval<Rep, FracBits>` | Floating-point-free `Q(bits(Rep)-FracBits).FracBits` binary fixed-point number, with `pow` (exponentiation by squaring), exact integer `sqrt`, range-reduced Taylor-series `exp`, and a generic Horner's-method `taylor_eval(coefficients, x)` for any caller-supplied polynomial/series |
| `fixed_int.hpp` | `fixed_int<N, Signed>`, `fixed_uint<N>` | Arbitrary fixed-precision integer, `N` bits wide (power of two, >= 8) -- a plain alias for a native/compiler-extension integer type up to 128 bits, falling back to a portable software bignum (`detail::wide_int<N, Signed>`) beyond that; feedable to `fixed_point<Rep, FracBits>` as `Rep` |
| `atomic_ops.hpp` | `atomic::fetch_max`, `atomic::fetch_min`, `atomic::fetch_update` | Free functions filling the gaps between C++17 `std::atomic<T>` and Rust's `std::sync::atomic::Atomic*` API |
| `allocator.hpp` | `allocator_ref`, `allocator<Tag>`, `allocator_traits<Tag>`, `mem_block`, `usage_hint` | Type-erased allocator handle and the tag-based provider pattern backing it |
| `heap_allocator.hpp` | `heap_allocator_tag` | Stateless `allocator_traits` backend over the process heap (`new`/`delete`) |
| `default_allocator.hpp` | `default_allocator()`, `reloco_global_alloc` | Process-wide default allocator, overridable like Rust's `#[global_allocator]` |
| `concepts.hpp` | `has_try_create_v`, `has_try_allocate_v`, `has_try_construct_v`, `has_try_clone_v`, `has_try_clone_at_v` (+ C++20 concepts) | Detection traits for the fallible-construction protocol |
| `construction_helpers.hpp` | `construction_helpers` | Compile-time dispatcher picking the best construction/clone strategy for a type |
| `relocatable.hpp` | `is_trivially_relocatable<T>` (+ C++20 `trivially_relocatable`) | Marks types safely movable by copying bytes and abandoning the source |
| `send_sync.hpp` | `is_send<T>`, `is_sync<T>` (+ C++20 `sendable`/`syncable`) | Marks types sound to transfer to another thread (`is_send`) or share concurrently (`is_sync`), matching Rust's `Send`/`Sync` |
| `duration.hpp` | `duration`, `duration_converter<T>`, `duration_cast<T>` | Integer-only (no floating point), Rust `std::time::Duration`-like time span, convertible to `timespec`/`timeval`/kernel-specific types via a customization point |
| `instant.hpp` | `instant`, `instant_clock_traits<Tag>` | Opaque, monotonically non-decreasing point in time built on `duration`, matching Rust's `std::time::Instant`; clock source selectable (`RELOCO_INSTANT_CLOCK_TAG`) between built-in `clock_gettime` and a custom OS/kernel backend |
| `tls_provider.hpp` | `tls_provider<T, Tag>` | Tag-differentiated, fallible, allocator-aware thread-local storage, selectable (`RELOCO_TLS_MODEL`) between `thread_local`, pthread keys, a custom OS/kernel backend, or a single-threaded global |
| [`call_location.hpp`](call_location.md) | `call_location`, `call_location_ref`/`debug_call_location_ref`, `release_call_location_ref` | Optional, compile-time-stripable `file:line` caller location for assert/fault-reporting APIs: `call_location_ref` is the thin, pointer-plus-`int` by-value currency (`none()` means "no location"), `current()` captures the real caller's location through any number of default-argument hops via `__builtin_FILE()`/`__builtin_LINE()` (or expands to `none()` when `RELOCO_ENABLE_CALL_LOCATION` is off); `release_call_location_ref` is a permanently empty companion type for an inter-module ABI boundary that must never vary in shape, with `RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG`/`RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE` giving exactly one of a paired overload's two parameters a default (selected build-wide by `RELOCO_CALL_LOCATION_DEBUG`) so an omitted-argument call is never ambiguous between the two; `unique_lock`/`shared_lock` detect and forward it to a `MutexT`/`SharedMutexT` that opts in |
| `mutex.hpp` | `mutex`, `recursive_mutex`, `error_checking_mutex`, `shared_mutex`, `condition_variable` | Backend-selected (`pthread`/`std`/custom) mutex/reader-writer-lock/condvar primitives, annotated for compiler thread-safety analysis |
| `guarded_mutex.hpp` | `guarded_mutex<T, MutexT>` | A mutex that owns the value it protects, matching Rust's `std::sync::Mutex<T>` |
| `rw_lock.hpp` | `rw_lock<T, SharedMutexT>` | A reader-writer lock that owns the value it protects, matching Rust's `std::sync::RwLock<T>` |
| `thread.hpp` | `thread`, `thread_id`, `this_thread::get_id/yield`, `spawn`, `join_handle<R>`, `thread_builder` | Backend-selected (`pthread`/`std`/custom) OS thread primitive plus a Rust-like `spawn`/`JoinHandle<T>` layer built on `is_send`/`is_sync`, and a `thread_builder` for requesting a thread name/stack size, matching Rust's `std::thread::Builder` |
| `spin_lock.hpp` | `spin_lock` | Busy-wait lock that never parks/blocks/syscalls, matching the `spin` crate's `spin::Mutex` (not part of Rust `std`); drop-in `MutexT` for `guarded_mutex<T, MutexT>`; no OS dependency, usable in interrupt handlers/before a scheduler exists/in a freestanding build |
| `channel.hpp` | `channel<T>`, `sender<T>`, `receiver<T>`, `sync_channel<T>`, `sync_sender<T>` | Multi-producer, single-consumer channel matching Rust's `std::sync::mpsc`, plus a bounded/rendezvous `sync_channel<T>` counterpart, built on `mutex.hpp` + `shared_ptr` + `is_send`/`is_sync` |
| `park.hpp` | `thread_handle`, `this_thread::current/park/park_timeout/sleep_for` | Rust-like `thread::park`/`park_timeout`/`sleep`/`Thread`, built on `tls_provider.hpp` + `futex.hpp` + `instant.hpp` + `shared_ptr` |
| `once_lock.hpp` | `once_lock<T>` | Write-once, read-many-times cell matching Rust's `std::sync::OnceLock<T>`, usable as a plain field/local (unlike `fallible_singleton.hpp`'s static, one-per-`T` global) |
| `once.hpp` | `once` | Runs a closure exactly once across racing callers, matching Rust's `std::sync::Once`; no heap allocation and no `mutex.hpp` dependency, usable in a freestanding/bare-kernel build |
| `lazy_lock.hpp` | `lazy_lock<T, F>` | Value lazily initialized at most once from a closure captured at construction, matching Rust's stable `std::sync::LazyLock<T, F>`; built directly on `once_lock<T>` |
| `hint.hpp` | `hint::spin_loop()` | Architecture spin-wait hint (`pause`/`yield`/...) for busy-wait loops, matching Rust's `std::hint::spin_loop()`; no OS dependency, usable in a freestanding/bare-kernel build |
| `wait_group.hpp` | `wait_group` | Waits for an unknown-in-advance number of cloned handles to all be dropped, matching crossbeam-utils's `WaitGroup`; built on `shared_ptr` + `futex.hpp` |
| `scope.hpp` | `scope`, `thread_scope`, `scoped_join_handle<R>` | Matches Rust's `std::thread::scope`: spawns threads guaranteed to finish before `scope()` returns, so they may safely borrow references to the caller's stack frame |
| `scope_guard.hpp` | `scope_guard<Callable>`, `RELOCO_DEFER(fn)` | Zero-allocation RAII scope-exit guard matching Rust's `scopeguard`/Go's `defer`; runs a captured callable exactly once on destruction unless `cancel()`ed |
| `commit.hpp` | `tx_guard<T, RollbackFn>`, `shadow_tx<T>`, `checkpoint_guard<T>` | Zero-allocation transactional RAII guards: rollback-on-drop (`tx_guard`), deferred shadow-copy write-back (`shadow_tx`), and live-value checkpoint/restore (`checkpoint_guard`) |
| `epoch.hpp` | `epoch_trackable`, `epoch_handle<T>`, `epoch_guard<T>`, `epoch_t` | O(1), 8-byte generation-counter "weak pointer" for statically-allocated/RTOS memory pools, detecting stale handles after a slot is recycled; the resolved `epoch_guard<T>` is typestate-checked (`-Wconsumed`) so `get()`/`operator->` require `is_alive()` to have been checked first |
| `uninit.hpp` | `uninit<T>` | Typestate-tracked (`-Wconsumed`) wrapper over raw uninitialized storage, preventing reads-before-write and double-initialization at compile time; `assume_init()` is the escape hatch for externally-filled memory (e.g. DMA) |
| `seqlock.hpp` | `guarded_seqlock<T, MutexT = mutex>` | Fuses `guarded_mutex`-style hardware exclusion for writers with a lock-free, sequence-counter-verified snapshot read path for readers, matching the Linux kernel's `seqlock(9)` |
| `intrusive_iteration.hpp` | `isolated_node_tx<Container, T>`, `extract_if_iterator<Container, Pred>` | Typestate-enforced RAII handle for safely detaching a node from one intrusive container (e.g. `boost::intrusive::list`) and relinking/disposing it elsewhere, plus the `iterator_adaptor`-based `extract_if()` pipeline that produces one |
| `fault_injection.hpp` | `fault_injector<Tag, Args...>`, `fault_armed<Tag, Args...>()`, `RELOCO_FAULT_POINT(Tag)`, `RELOCO_FAULT_POINT_ARGS(Tag, ...)`, `RELOCO_FAULT_TAG(name)`, `RELOCO_FAULT_INJECTOR(var, Tag, ...)` | Header-based, opt-in (`RELOCO_ENABLE_FAULT_INJECTION`) fault injection framework for deterministically reproducing concurrency races in single-threaded tests; caller-owned, stackable scoped control blocks, backed by unowned `tls_provider<void *, ...>` pointer(s) -- one shared TLS slot for the whole program by default, or one per `Tag` under `RELOCO_FAULT_INJECTION_UNLIMITED_TLS` -- the framework itself never allocates |
| `fault_injection_patterns.hpp` | `RELOCO_FAULT_MUTATE`, `RELOCO_FAULT_SET`, `RELOCO_FAULT_SPY`, `RELOCO_FAULT_FIRE_N`, `RELOCO_FAULT_FIRE_ONCE`, `RELOCO_FAULT_WHEN`, `RELOCO_FAULT_SKIP_N`, `RELOCO_FAULT_NTH`, `RELOCO_FAULT_EVERY_N`, `RELOCO_FAULT_TOGGLE`, `RELOCO_FAULT_INCREMENT` | Convenience macros, built entirely on `fault_injection.hpp`'s own public/`detail` API, for common and more advanced fault-arming patterns: overwrite/mutate/toggle/nudge a single exposed value, count firings, fire only the first *N* times (or once) or only after skipping the first *N*, fire on exactly one or every *N*'th hit, or fire only when a predicate over the exposed arguments holds |
| `lifetime.hpp` | `RELOCO_LIFETIMEBOUND`, `RELOCO_OWNER`, `RELOCO_POINTER`, `RELOCO_UNSAFE_BUFFER_USAGE`, ... | Compiler-specific lifetime/ownership/safe-buffers annotation macros |
| `rvalue_safety.hpp` | `RELOCO_BLOCK_RVALUE_ACCESS` | Deletes rvalue accessors that would otherwise dangle past a temporary |
| `reloco_config.hpp` | (user override header hook) | How to override library-wide defaults from `detail/porting/reloco_user_config.hpp` |

## `expected<T, E>` / `result<T>`

`include/reloco/expected.hpp`, `include/reloco/error.hpp`

A `std::expected`-like, allocation-free value-or-error type, usable in
C++17 (no dependency on the standard library's own `<expected>`).
`reloco::result<T>` is `reloco::expected<T, reloco::error>` — the one error
type every fallible reloco operation returns (see
[Fallible construction](fallible-construction.md)).

```cpp
reloco::result<int> parse(std::string_view s) noexcept {
  if (s.empty())
    return reloco::unexpected(reloco::error::invalid_argument);
  return 42;
}
```

Beyond `has_value()`/`value()`/`error()`/`value_or()` and the chaining
methods `transform`/`map`, `map_err`, `and_then`, `or_else`, both
`expected<T, E>` and the `expected<void, E>` specialization also provide
Rust `Result<T, E>`-parity methods:

- `is_ok()`/`is_err()` — boolean queries, aliases of `has_value()`/`!has_value()`.
- `unwrap()`/`unwrap_err()` — like `value()`/`error()`, but named to match
  Rust; asserts (traps) on the wrong state.
- `expect(msg)`/`expect_err(msg)` — like `unwrap()`/`unwrap_err()`, but
  `msg` (a runtime `const char *`, not required to be a string literal) is
  used as the assertion failure message for a more actionable trap site.
- `is_ok_and(f)`/`is_err_and(f)` — `true` if the value/error is present
  *and* `f` applied to it returns `true`; `f` is not invoked otherwise.
- `map_or(default, f)`/`map_or_else(default_fn, f)` — apply `f` to the
  value if present, else return `default`/invoke `default_fn()`.
- `inspect(f)`/`inspect_err(f)` — invoke `f` with the value/error for a
  side effect (e.g. logging) if present, otherwise do nothing. Unlike
  Rust's consuming, chainable `inspect`, these are `void`-returning and
  non-chaining: `expected<T, E>` has no dedicated copy/move constructor
  (only a generic converting one), and `expected<void, E>` has neither, so
  a faithful consume-and-return-`Self` signature isn't available uniformly
  across both specializations.
- `unwrap_or(fallback)` — alias of `value_or(fallback)`.
- `unwrap_or_default()` — returns the value, or a default-constructed `T`
  if in the error state (SFINAE-disabled unless `T` is nothrow default
  constructible; not available on `expected<void, E>`, which has no `T`).

## `error`

`include/reloco/error.hpp`

reloco has exactly **one** error type in the entire library: `reloco::error`,
a plain `enum class`. Every fallible operation anywhere in reloco -- every
`try_*` container mutator, every allocator call, `weak_ptr::lock()`, the
fallible-construction protocol (`concepts.hpp`) -- returns
`reloco::result<T>` (`expected<T, error>`) or `result<void>`. No type
defines its own scoped error enum, and `concepts.hpp`'s detection traits
only recognize a `try_*` member that itself returns `reloco::result<...>`,
so this is enforced rather than just a convention.

| Member | Meaning |
|---|---|
| `allocation_failed` | The allocator failed to provide/grow/shrink a memory block. |
| `in_place_growth_failed` | `allocator_ref::expand_in_place` could not grow a block without moving it. |
| `unsupported_operation` | The operation is not supported by this concrete type/backend (e.g. a `function_ref`/`function` invoked with the wrong signature, an allocator tag that doesn't implement an optional operation). |
| `out_of_range` | A value fell outside the range required by the operation (a parsed number, a duration, a size argument) -- distinct from `out_of_bounds`, which is specifically about container indices/iterators. |
| `invalid_argument` | An argument failed a precondition check unrelated to range/bounds (a malformed string, a null callback, mismatched key/value in `flat_set`). |
| `already_exists` | Insertion failed because an equivalent key/element is already present (`flat_set::try_insert`, `flat_map::try_insert`). |
| `empty_pointer` | A smart pointer (`unique_ptr`, `shared_ptr`, `value_ptr`, ...) was empty when a non-empty one was required. |
| `pointer_expired` | A `weak_ptr::lock()` failed because the last owning `shared_ptr` has already released the object. |
| `no_owner` | An operation requiring an owning handle was attempted on a non-owning one. |
| `out_of_bounds` | A container index/iterator fell outside `[0, size())` (see `out_of_range` for non-index range checks). |
| `deadlock` | A locking operation detected it would deadlock (e.g. recursive non-recursive lock acquisition) and failed instead of blocking forever. |
| `invalid_owner` | An operation was attempted by a thread/handle that does not own the resource it is trying to operate on (e.g. unlocking a mutex you don't hold). |
| `still_locked` | An operation requiring an unlocked resource found it still locked (e.g. destroying/reclaiming a lock that is still held). |
| `not_locked` | An operation requiring a locked resource (unlocking, asserting exclusive access) found it was not locked. |
| `timed_out` | A bounded-wait operation (a timed lock acquisition) did not complete within its deadline. |
| `try_again` | The operation could not complete right now for a transient reason and may succeed if retried (contended non-blocking lock acquisition). |
| `not_initialized` | The object/subsystem was used before the initialization step its protocol requires (a two-phase `try_construct` shell that was never followed through, see `concepts.hpp`). |
| `container_empty` | An operation requiring at least one element (`front`/`back`/`pop_back`) was called on an empty container. |
| `not_found` | A lookup (`flat_set::try_find`, `flat_map::try_find`, ...) found no matching key/element. |
| `integer_overflow` | An arithmetic computation (a size/capacity calculation) would overflow its integer type. |
| `division_by_zero` | A division or remainder operation was attempted with a zero divisor (`checked_div`/`checked_rem`, see `int_ops.hpp`). |
| `capacity_exceeded` | A fixed-capacity container (`inline_vector`, `inline_flat_set`, `inline_flat_map`, `inplace_function`, ...) has no room left for another element and, unlike a heap-backed container, cannot grow. |
| `invalid_state` | The operation is not valid given the object's current state (a moved-from object, or an operation attempted in the wrong phase of a multi-step protocol). |
| `permission_denied` | An OS- or allocator-level access-control check failed (e.g. `mmap` with insufficient permissions). |
| `interrupted` | The underlying operation was interrupted (e.g. by a signal) and may be safely retried. |
| `resource_exhausted` | A system-imposed resource limit unrelated to heap memory (a handle/descriptor count, a thread count) was reached. |
| `busy` | The resource is currently in use by someone else and the operation could not proceed non-blockingly; distinct from `still_locked`/`not_locked` (lock state specifically) and `try_again` (any transient retryable failure). |
| `io_error` | A lower-level I/O operation (e.g. one performed by an allocator backend) failed for a reason not otherwise covered by a more specific member. |
| `operation_canceled` | The operation was explicitly canceled before it could complete. |
| `security_violation` | A trust/security boundary check on data from another, untrusted or compromised execution context failed (e.g. `reloco_ipc_ring.h`/`.hpp`'s spoofed-index detection). Unlike every other member above, this is not transient or locally recoverable: the caller must treat the shared resource as compromised and stop using it rather than retry. |
| `page_fault` | Accessing memory across a trust boundary (e.g. a user-space pointer handed to a syscall handler) would require resolving a page fault -- possibly blocking on demand-paging/swap-in -- and the calling context forbade that (a `_nofault` accessor, used e.g. with page faults disabled, in interrupt/atomic context, or while holding a spinlock). Distinct from `security_violation`: the address may be perfectly valid, just not resident/mapped right now. |

Several members (`no_owner`/`invalid_owner`, `deadlock`/`still_locked`/
`not_locked`/`timed_out`, `not_initialized`, `busy`/`interrupted`/
`io_error`/`operation_canceled`, ...) are not yet returned by any type in
this header-only core today; they exist so that locking primitives,
initialization protocols, and OS-backed allocators added later reuse the
same single enum instead of introducing their own.

### `<system_error>` interop (`error_std.hpp`)

`include/reloco/error_std.hpp` (opt-in; not included by `error.hpp` or any
other reloco header) makes `reloco::error` satisfy
`std::is_error_code_enum`, so it implicitly converts to `std::error_code`:

```cpp
#include <reloco/error_std.hpp>

std::error_code ec = reloco::error::not_found;      // implicit conversion
throw std::system_error(reloco::error::deadlock);   // ADL-found make_error_code
```

`reloco::error_category()` returns the singleton `std::error_category`
(`name() == "reloco"`) backing that conversion; its `message(int)` mirrors
the one-line descriptions in the table above. `reloco::error` also
satisfies `std::is_error_condition_enum` (via `make_error_condition`), so
it converts to `std::error_condition` too, and a `reloco::error`-based
`std::error_code` compares equal to `std::error_condition(reloco::error::x)`
directly:

```cpp
std::error_code ec = reloco::error::busy;
assert(ec == std::error_condition(reloco::error::busy));
```

`error_category_impl::equivalent()` (used for the `error_code ==
error_condition` comparison above) compares by `name()` string content
(`"reloco"`) instead of category-object identity, so the comparison still
works even if the category singleton ended up duplicated across a
shared-library boundary.

`reloco::error` is also plugged into the standard POSIX/`errno` bridge:
`error_category_impl::default_error_condition(int)` maps several members
(`allocation_failed`, `invalid_argument`, `out_of_range`, `out_of_bounds`,
`already_exists`, `deadlock`, `timed_out`, `try_again`,
`unsupported_operation`, `capacity_exceeded`, `permission_denied`,
`interrupted`, `busy`, `io_error`, `operation_canceled`,
`integer_overflow`, `division_by_zero`, `security_violation`) onto the closest matching `std::errc` value, so a
`reloco::error`-based `std::error_code` compares equal to that generic
condition *and* to any other category's code that reports the same
`errno`-derived condition (e.g. one built from `errno` via
`std::generic_category()`):

```cpp
std::error_code ec = reloco::error::timed_out;
assert(ec == std::errc::timed_out);

std::error_code errno_ec(ETIMEDOUT, std::generic_category());
assert(ec == errno_ec);
```

Members with no sufficiently precise POSIX equivalent (e.g. `not_found`,
`pointer_expired`) keep the default identity condition and only compare
equal to themselves. This does not change
how reloco itself reports errors -- every `try_*` operation still returns
`reloco::result<T>` -- it is purely a bridge for code that also needs to
hand a `reloco::error` to, or compare it against, `std::error_code`/
`std::error_condition`-based APIs.

## `span<T>`

`include/reloco/span.hpp`

Non-owning view over a contiguous range of `T`, with the checked/`try_*`/
`unsafe_*` tri-tier convention on every element/subrange accessor
(`operator[]`/`at()`, `try_at()`/`try_first()`/`try_last()`/`try_subspan()`,
`unsafe_at()`/...). See [Hardened containers](hardened-containers.md).

Rust `slice`-inspired algorithms operate directly on the viewed buffer (no
copy): `contains(value)` does a linear scan; `sort()`/`sort_by(Compare)`
sort in place with `std::stable_sort` (preserving relative order of equal
elements); `sort_unstable()` uses `std::sort` when stability isn't needed;
`binary_search(value)`/`binary_search_by(value, less)` assume the span is
already sorted and return an `optional<std::size_t>` index of a matching
element (empty if none is found).

```cpp
int values[] = {5, 3, 1, 4, 2};
reloco::span<int> view(values);
view.sort();
assert(view.contains(3));
assert(*view.binary_search(3) == 2);
```

## `array<T, N>`

`include/reloco/array.hpp`

Fixed-size, stack- or member-embeddable owning array. Same tri-tier element
access as `span`, plus `as_span()` to hand out a borrowed, checked view
without exposing the underlying storage directly. Storage alignment is
`alignment_of<T>`-controlled (see [Over-alignment](alignment.md)) for
SIMD-friendly over-alignment.

## `basic_string_view<CharT, TraitsT>` (`string_view`, `wstring_view`)

`include/reloco/string_view.hpp`

Non-owning, checked view over character data — `std::string_view`, plus the
tri-tier convention (`front()`/`try_front()`/`unsafe_front()`, etc.),
rejection of dangling prvalue `std::basic_string` temporaries at the
constructor, and interop with both `std::basic_string_view` and
`std::basic_string`. Storage is a self-contained pointer/size pair (not a
wrapped `std::basic_string_view`); comparisons and prefix/suffix checks are
implemented directly against `TraitsT`, and only substring/character-class
search delegates to `std::search`/`std::find_first_of`.

## `basic_string<CharT, TraitsT>` (`string`, `wstring`)

`include/reloco/string.hpp`

Move-only, allocator-backed, growable character buffer — the fallible,
allocator-explicit analogue of `std::string`. No small-string optimization:
every non-empty instance holds exactly one heap allocation, obtained and
released through a bound `reloco::allocator_ref`.

```cpp
auto s = reloco::string::try_create(reloco::string_view("hello"));
if (!s)
  return; // s.error() is a reloco::error.
auto ok = s->try_append(reloco::string_view(" world"));
assert(s->view() == "hello world");
```

Construction and cloning:

| Function | Behavior |
|---|---|
| `try_create(view_type sv = {})` | Builds from `sv` using `default_allocator()` |
| `try_allocate(allocator_ref alloc, view_type sv = {})` | Builds from `sv` using an explicit allocator |
| `try_clone(allocator_ref alloc)` / `try_clone()` | Fallible deep copy, explicit or own allocator |
| `try_clone_at(allocator_ref alloc, basic_string *storage, const basic_string &source)` | Fallible deep copy directly into uninitialized storage |

Because it implements this protocol itself (see
[Fallible construction](fallible-construction.md)), `basic_string` composes
with anything built on `has_try_create_v`/`construction_helpers`:
`reloco::unique_ptr<reloco::string>::try_create(...)` just works.

Mutation: `try_reserve`, `shrink_to_fit`, `try_assign`, `try_append`,
`try_push_back`, `pop_back`/`try_pop_back`, `try_insert`,
`erase`/`try_erase`, `try_resize`, `clear`. Every fallible one returns
`reloco::result<void>`.

Element access follows the same checked/`try_*`/`unsafe_*` tri-tier
convention as `string_view`/`span`/`array`: `operator[]`/`at()`/`front()`/
`back()` assert; `try_at()`/`try_front()`/`try_back()` return
`reloco::result<...>`; `unsafe_at()`/`unsafe_front()`/`unsafe_back()`/
`unsafe_c_str()` are `RELOCO_UNSAFE_BUFFER_USAGE`-gated escape hatches.
`data()` is always safe (points at a shared static null character when
empty, never `nullptr`). `view()` and implicit conversions to
`reloco::string_view`/`std::string_view` provide borrowed access; explicit
`operator std::basic_string<CharT, TraitsT>()` converts to an owning
standard-library string when one is actually needed at a boundary.

`reloco::is_trivially_relocatable<basic_string<CharT, TraitsT>>` is always
`true` (see [Trivial relocation](relocatable.md)).

## `basic_sso_string<CharT, TraitsT>` (`sso_string`, `wsso_string`)

`include/reloco/sso_string.hpp`

Move-only, allocator-backed, growable character buffer with a **small-string
optimization (SSO)**: strings of at most `sso_capacity` characters (15 by
default) live entirely inline inside the object and never allocate; growing
past that inline capacity transparently falls back to a heap allocation,
exactly like `basic_string`. `shrink_to_fit()` moves a string that has
shrunk back down to `sso_capacity` or fewer characters back into the inline
buffer, releasing the heap allocation.

`basic_sso_string` implements the exact same API as `basic_string` --
`try_create`/`try_allocate`/`try_clone`/`try_clone_at`, the same fallible
mutators (`try_reserve`, `shrink_to_fit`, `try_assign`, `try_append`,
`try_push_back`, `pop_back`/`try_pop_back`, `try_insert`, `erase`/
`try_erase`, `try_resize`, `clear`), and the same checked/`try_*`/`unsafe_*`
element-access tiers. Additionally, `is_inline()` reports whether the
current content lives inline or on the heap, and `sso_capacity` is a
`static constexpr` member exposing the compile-time inline limit.

```cpp
auto s = reloco::sso_string::try_create(reloco::string_view("hello"));
if (!s)
  return; // s.error() is a reloco::error.
assert(s->is_inline()); // "hello" fits inline, no allocation happened.
```

The inline capacity is a single process-wide compile-time constant, not a
template parameter: define `RELOCO_SSO_STRING_CAPACITY` (via a compiler
`-D` flag or `reloco_user_config.hpp`, see
[`reloco_config.hpp`](#reloco_confighpp)) to override the default of 15
characters.

Because the inline buffer is self-referencing while a `basic_sso_string` is
small (its `data()` can point inside the object itself),
`reloco::is_trivially_relocatable<basic_sso_string<CharT, TraitsT>>` is
always `false` -- unlike `basic_string`. Containers that branch on
`is_trivially_relocatable` (`vector`, `inline_vector`, `flat_set`,
`flat_map`, ...) always use their safe, per-element move path for
`basic_sso_string` elements, never `memcpy`.

Prefer `basic_sso_string` over `basic_string` when most expected values are
short and avoiding an allocation for them matters more than keeping the
type trivially relocatable; prefer `basic_string` when relocatability
matters (e.g. as the element type of a frequently grown/erased
`vector`/`flat_set`) or values are typically longer than the inline
capacity.

## `vector<T>`

`include/reloco/vector.hpp`

Move-only, allocator-backed, growable dynamic array — the fallible,
allocator-explicit analogue of `std::vector`. Every instance holds at most
one heap allocation, obtained and released through a bound
`reloco::allocator_ref`.

```cpp
auto v = reloco::vector<int>::try_create();
if (!v)
  return; // v.error() is a reloco::error.
auto ok = v->try_push_back(1);
ok = v->try_insert_at(0, 0);
assert((*v)[0] == 0 && (*v)[1] == 1);
```

Construction and cloning:

| Function | Behavior |
|---|---|
| `try_create(size_type initial_cap = 0)` | Builds, optionally reserving capacity, using `default_allocator()` |
| `try_allocate(allocator_ref alloc, size_type initial_cap = 0)` | Builds, optionally reserving capacity, using an explicit allocator |
| `try_clone(allocator_ref alloc)` / `try_clone()` | Fallible deep copy, explicit or own allocator |
| `try_clone_at(allocator_ref alloc, vector *storage, const vector &source)` | Fallible deep copy directly into uninitialized storage |

Because it implements this protocol itself (see
[Fallible construction](fallible-construction.md)), `vector<T>` composes
with anything built on `has_try_create_v`/`construction_helpers`:
`reloco::unique_ptr<reloco::vector<int>>::try_create(...)` just works.
Element construction (`try_emplace_back`/`try_insert_at`) and cloning both
delegate to `construction_helpers`, so element types that implement their
own fallible-construction protocol compose transparently; trivially
copyable element types with no custom `try_clone` take a single-`memcpy`
fast path when cloning.

Mutation: `try_reserve`, `shrink_to_fit`, `try_emplace_back`, `try_push_back`,
`try_pop_back`, `try_resize`, `try_insert_at`, `try_erase_at`, `clear`. Every
fallible one returns `reloco::result<...>`. Growth prefers
`allocator_ref::expand_in_place` first; when that fails it either
byte-relocates the whole buffer in one `reallocate` call (when
`is_trivially_relocatable_v<T>`) or falls back to move-constructing each
element into a freshly allocated block.

`try_resize(count)` / `try_resize(count, value)` grow or shrink the vector
to exactly `count` elements, reserving storage first if growing. Shrinking
destroys the trailing elements; growing default-constructs (`try_resize`)
or copy-constructs `value` into (`try_resize(count, value)`) each new slot.
`try_resize(count)` requires `T` to be default-constructible (a
`static_assert`); use the `value`-taking overload otherwise. When `T` is
trivially default-constructible/trivially copyable, the newly added range
is bulk zero-filled with a single `std::memset` or filled via a plain
assignment loop, instead of dispatching `construction_helpers::try_construct`
per element.

Element access follows the same checked/`try_*`/`unsafe_*` tri-tier
convention as `span`/`array`/`string`: `operator[]`/`front()`/`back()`
assert (there is no separately named `at()` — `operator[]` itself is the
checked tier); `try_at()`/`try_front()`/`try_back()` return
`reloco::result<...>`; `unsafe_at()`/`unsafe_data()` are
`RELOCO_UNSAFE_BUFFER_USAGE`-gated escape hatches. `data()`/`front()`/
`back()` assert the vector is non-empty; `try_data()`/`try_front()`/
`try_back()` fail with `error::container_empty` instead.

`reloco::is_trivially_relocatable<vector<T>>` is always `true` regardless of
`T` (see [Trivial relocation](relocatable.md)): the vector's own handle is
just an `allocator_ref` plus a pointer and two sizes, with no
self-reference into its own storage.

Every allocation requests `alignment_of<T>`-controlled alignment (see
[Over-alignment](alignment.md)) rather than plain `alignof(T)`, and
`data()`/`unsafe_data()`/`begin()` carry a matching `RELOCO_ASSUME_ALIGNED`
hint for the optimizer.

## `inline_vector<T, Capacity>`

`include/reloco/inline_vector.hpp`

Move-only, fixed-capacity, allocator-free growable array — `vector<T>`'s
counterpart for when the maximum element count is known at compile time
and heap allocation must be avoided entirely. Elements live directly
inside the object, in a raw `alignas(reloco::effective_alignment_v<T>)
std::byte` buffer sized for exactly `Capacity` elements (see
[Over-alignment](alignment.md)); unlike `array<T, N>`, `T` need not be
default-constructible, and `size()` is tracked independently of
`capacity()`. `Capacity` must be greater than zero — there is no
zero-capacity specialization.

```cpp
reloco::inline_vector<int, 4> v;
auto ok = v.try_push_back(1);
ok = v.try_insert_at(0, 0);
assert(v[0] == 0 && v[1] == 1);
```

Mutation mirrors `vector<T>` (`try_emplace_back`, `try_push_back`,
`try_pop_back`, `try_resize`, `try_insert_at`, `try_erase_at`, `clear`),
except there is no growth to fall back on: `try_emplace_back`/
`try_insert_at`/`try_resize` fail with `error::capacity_exceeded` once
`size() == capacity()` (respectively `count > capacity()`), instead of
allocating more space. Element construction and cloning delegate to
`construction_helpers` exactly like `vector<T>`, so nested element types
that implement their own fallible-construction protocol compose
transparently.

Cloning follows the same dual-tier shape as `vector<T>`:
`try_clone(allocator_ref alloc)` / `try_clone()` / `try_clone_at(...)`.
`try_clone(alloc)` forwards the given allocator to nested fallible
elements even though `inline_vector` itself never allocates; trivially
copyable element types with no custom `try_clone` take a single-`memcpy`
fast path.

Element access follows the same checked/`try_*`/`unsafe_*` tri-tier
convention as `vector`/`span`/`array`.

`inline_vector<T, Capacity>` can be "upgraded" to a heap-backed `vector<T>`
via `try_to_vector`, for callers that reach the fixed `Capacity` but need
to keep growing:

| Function | Behavior |
|---|---|
| `try_to_vector(allocator_ref alloc) const &` / `try_to_vector() const &` | Clones every element into a new `vector<T>`, leaving `*this` untouched |
| `try_to_vector(allocator_ref alloc) &&` / `try_to_vector() &&` | Moves every element out into a new `vector<T>`, consuming `*this` (left empty either way) |

`reloco::is_trivially_relocatable<inline_vector<T, Capacity>>` is
conditional on `is_trivially_relocatable_v<T>` (see
[Trivial relocation](relocatable.md)): unlike `vector<T>`'s heap pointer,
`inline_vector`'s storage is embedded directly in the object, so relocating
it via `memcpy` is only safe when every contained `T` is too.

## `sso_vector<T, InlineCapacity>`

`include/reloco/sso_vector.hpp`

Move-only, allocator-backed, growable dynamic array with a small-size
optimization (SSO): up to `InlineCapacity` elements live directly inside
the object, in the same kind of raw `alignas(reloco::effective_alignment_v<T>)
std::byte` buffer `inline_vector<T, Capacity>` uses; growing past
`InlineCapacity` transparently promotes to a heap allocation obtained
through a bound `reloco::allocator_ref`, exactly like `vector<T>`, and
`shrink_to_fit` demotes back to the inline buffer once `size()` fits within
`InlineCapacity` again. It sits between `vector<T>` and `inline_vector<T,
Capacity>`: unlike `inline_vector`, there is no hard capacity ceiling --
`try_push_back`/`try_emplace_back`/`try_insert_at` never fail with
`error::capacity_exceeded`, they simply grow onto the heap instead.
`InlineCapacity` must be greater than zero, and is a required template
parameter (there is no default), since a sensible inline capacity depends
on `T`.

```cpp
reloco::sso_vector<int, 4> v; // no allocation yet
auto ok = v.try_push_back(1); // still inline
ok = v.try_push_back(2);
assert(v.is_inline());
```

Construction, cloning, mutation, and element access all follow the same
protocol/shape as `vector<T>` (`try_allocate`/`try_create`/
`try_clone`/`try_clone_at`, `try_reserve`/`shrink_to_fit`,
`try_emplace_back`/`try_push_back`/`try_pop_back`/`try_resize`/
`try_insert_at`/`try_erase_at`/`clear`, checked/`try_*`/`unsafe_*` tri-tier
element access) -- see [`vector<T>`](#vectort) above for the full
breakdown. The one addition is `is_inline()`, which reports whether
`*this` currently holds its elements in the embedded buffer rather than a
heap allocation.

Because `try_reserve` has no existing heap allocation to
`expand_in_place`/`reallocate` the first time it promotes from inline to
heap, that particular growth step always allocates fresh and
move/`memcpy`'s the (at most `InlineCapacity`) inline elements across, the
same as `inline_vector`'s relocation logic; once heap-backed, growth
behaves exactly like `vector<T>`.

`reloco::is_trivially_relocatable<sso_vector<T, InlineCapacity>>` is always
`false`, regardless of `T` (see [Trivial relocation](relocatable.md)): a
small instance's internal pointer points into its own embedded buffer, so
relocating the object via `memcpy` would leave that pointer dangling into
the old location -- the same rationale `basic_sso_string` (see
[`basic_sso_string`](#basic_sso_stringcchart-traitst-sso_string-wsso_string))
documents for its own specialization.

## `outline_vector<T>`

`include/reloco/outline_vector.hpp`

Non-owning, growable-up-to-capacity dynamic array over a caller-supplied
byte buffer -- `vector<T>`'s non-owning counterpart, for backing bytes
`reloco` doesn't control (a memory-mapped region, a hardware DMA buffer, an
arena slab the caller manages directly, ...). Unlike every other reloco
vector flavor, `outline_vector<T>` is bound to a `span<std::byte>` exactly
once, at construction, and is afterwards **immovable and non-copyable**:
there is no default constructor, no rebind/reset method, and no move
constructor or move assignment operator either, since there is no
well-defined way to "steal" a borrowed span out from under whoever actually
owns it.

```cpp
alignas(reloco::effective_alignment_v<int>) std::byte buffer[sizeof(int) * 4];
reloco::outline_vector<int> v(reloco::span<std::byte>(buffer));
auto ok = v.try_push_back(1);
ok = v.try_insert_at(0, 0);
assert(v[0] == 0 && v[1] == 1);
// buffer itself is still the caller's to reuse once `v` is destroyed.
```

The bound span's capacity (`storage.size() / sizeof(T)`, computed once at
construction) is fixed for the object's entire lifetime:
`try_reserve`/`try_emplace_back`/`try_push_back`/`try_insert_at`/
`try_resize` fail with `error::capacity_exceeded` once `size() ==
capacity()` (respectively `count > capacity()`), exactly like
`inline_vector<T, Capacity>` -- there is no allocator to grow into.
`storage.data()` must already satisfy `effective_alignment_v<T>`; this is
checked with `RELOCO_ASSERT` (active even when `NDEBUG` is defined) at
construction time, since -- unlike `allocator_ref::allocate` -- there is no
allocator here to request a specific alignment from. Element construction
delegates to `construction_helpers` exactly like `vector<T>`/
`inline_vector<T, Capacity>`, passing `default_allocator()` for any nested
`T` that implements its own fallible-construction protocol.

Element access follows the same checked/`try_*`/`unsafe_*` tri-tier
convention as `vector`/`inline_vector`/`span`/`array`.

There is no `try_create`/`try_allocate`/`try_clone`/`try_clone_at`: binding
a caller-owned span can never itself fail (there is nothing to allocate),
so the plain constructor is sufficient, and a fallible deep copy would need
a *second* caller-owned destination span the type has no way to ask for on
its own.

`reloco::is_trivially_relocatable<outline_vector<T>>` is deliberately *not*
specialized: the primary template's `std::is_trivially_copyable<T>`
fallback already evaluates to `false` (copy is deleted), correctly
reporting that `outline_vector<T>` may neither be moved nor relocated by
any means.

## `vec_deque<T>`

`include/reloco/vec_deque.hpp`

Move-only, allocator-backed double-ended queue backed by a single
contiguous ring buffer -- the fallible, allocator-explicit analogue of
Rust's `std::collections::VecDeque` (not `std::deque`, which uses
fixed-size chunks). Push/pop/insert/erase at either end run in amortized
O(1); random access is O(1).

```cpp
auto d = reloco::vec_deque<int>::try_create();
if (!d)
  return; // d.error() is a reloco::error.
auto ok = d->try_push_back(2);
ok = d->try_push_front(1);
ok = d->try_insert_at(2, 3); // logically [1, 2, 3]
assert((*d)[0] == 1 && (*d)[1] == 2 && (*d)[2] == 3);
```

Construction/cloning mirror `vector<T>`: `try_create(initial_cap = 0)`,
`try_allocate(alloc, initial_cap = 0)`, `try_clone(alloc)`/`try_clone()`,
`try_clone_at(alloc, storage, source)`.

Mutation: `try_reserve`, `try_emplace_front`/`try_push_front`,
`try_emplace_back`/`try_push_back`, `try_pop_front`, `try_pop_back`,
`try_emplace_at`/`try_insert_at` (Rust `VecDeque::insert`), `try_erase_at`
(shortest-shift removal), `try_swap_remove_back`/`try_swap_remove_front`
(O(1) order-breaking removal), `rotate_left`/`rotate_right` (Rust
`VecDeque::rotate_left`/`rotate_right`, O(min(mid, len - mid)), zero
allocations), `truncate`, `clear`, `try_append(vec_deque &&)`.

Access: `operator[]`/`front()`/`back()` (checked tier, assert in bounds),
`try_at()` (returns `reloco::result<...>`), `contains(value)` (Rust
`slice::contains` equivalent), `as_slices()` (returns the up-to-two
contiguous `span<T>` chunks making up the logical range, second span empty
unless the ring buffer has wrapped), `try_make_contiguous()` (unwraps the
ring buffer in place, `head_ == 0`, returning a single `span<T>`).

`reloco::is_trivially_relocatable<vec_deque<T>>` is always `true`
regardless of `T`, for the same reason as `vector<T>`: its handle is just
an `allocator_ref` plus a pointer and three sizes, with no self-reference
into its own storage.

See [Deque containers](deque-containers.md) for the ring-buffer engine
design, wrap-around handling, and how `try_insert_at`/`rotate_left`/
`rotate_right` compose.

## `inline_vec_deque<T, Capacity>`

`include/reloco/inline_vec_deque.hpp`

`vec_deque<T>`'s fixed-capacity, allocator-free counterpart, exactly
mirroring how `inline_vector<T, Capacity>` relates to `vector<T>`:
elements live in an embedded ring buffer sized for exactly `Capacity`
elements, reusing `inline_vector.hpp`'s `detail::inline_vector_storage<T,
Capacity>` byte-buffer helper. `Capacity` must be greater than zero.

```cpp
reloco::inline_vec_deque<int, 4> d;
auto ok = d.try_push_back(2);
ok = d.try_push_front(1);
assert(d[0] == 1 && d[1] == 2);
```

Same mutation/access surface as `vec_deque<T>` (`try_push_front`/
`try_push_back`/`try_insert_at`/`try_erase_at`/`try_swap_remove_back`/
`try_swap_remove_front`/`rotate_left`/`rotate_right`/`as_slices`/
`try_make_contiguous`/`contains`), but every capacity-growing call fails
with `error::capacity_exceeded` once `size() == Capacity` instead of
allocating. `capacity()`/`full()` report the fixed `Capacity`.
`try_clone(alloc)`/`try_clone()`/`try_clone_at(...)` provide fallible deep
copying (there is nothing for `inline_vec_deque` itself to allocate, but a
nested `T` might). `try_to_vec_deque(alloc)`/`try_to_vec_deque()` "upgrade"
to a heap-backed `vec_deque<T>` once `Capacity` is reached, mirroring
`inline_vector<T, Capacity>::try_to_vector()` -- the `const &`-qualified
overload clones every element, the `&&`-qualified overload moves them out
and leaves `*this` empty.

`reloco::is_trivially_relocatable<inline_vec_deque<T, Capacity>>` is
conditional on `is_trivially_relocatable<T>` (unlike `vec_deque<T>`'s
unconditional specialization): its storage is embedded directly in the
object rather than behind a heap pointer.

## `outline_vec_deque<T>`

`include/reloco/outline_vec_deque.hpp`

`vec_deque<T>`'s non-owning counterpart, exactly mirroring how
`outline_vector<T>` relates to `vector<T>`: elements live in a
caller-supplied, caller-owned `span<std::byte>` bound exactly once at
construction; `outline_vec_deque<T>` never allocates, deallocates, or
frees that memory. There is no default constructor, no rebind, and --
like `outline_vector<T>` -- no move constructor/assignment either, since
there is no sound way to steal a borrowed span.

```cpp
alignas(int) std::byte buffer[sizeof(int) * 4];
reloco::outline_vec_deque<int> d(reloco::span<std::byte>(buffer));
```

Growth is capped at `storage.size() / sizeof(T)`, computed once at
construction; beyond that, mutation fails with `error::capacity_exceeded`.
There is no `try_create`/`try_allocate`/`try_clone`/`try_clone_at` (binding
a span can never fail), and no `is_trivially_relocatable` specialization
(the primary template's `std::is_trivially_copyable<T>` fallback already
reports `false` since copy is deleted).

## `sso_vec_deque<T, InlineCapacity>`

`include/reloco/sso_vec_deque.hpp`

`vec_deque<T>`'s small-size-optimized counterpart, exactly mirroring how
`sso_vector<T, InlineCapacity>` relates to `vector<T>`: up to
`InlineCapacity` elements live in an embedded ring buffer; growth beyond
that promotes to an `allocator_ref`-backed heap allocation, unwrapping and
linearizing the inline ring buffer in the process. `InlineCapacity` must
be greater than zero.

```cpp
auto d = reloco::sso_vec_deque<int, 4>::try_create();
```

Construction/cloning mirror `sso_vector<T, InlineCapacity>`:
`try_allocate(alloc, initial_cap = 0)`, `try_create(initial_cap = 0)`,
`try_clone(alloc)`/`try_clone()` (reuses this deque's own allocator),
`try_clone_at(alloc, storage, source)`. `is_inline()` reports whether
`*this` is still using its embedded buffer.

`reloco::is_trivially_relocatable<sso_vec_deque<T, InlineCapacity>>` is
unconditionally `false` regardless of `T`, for the same reason as
`sso_vector<T, InlineCapacity>`: a small instance's data pointer points
into its own embedded buffer.

## `ring_buffer<T>`

`include/reloco/ring_buffer.hpp`

Move-only, allocator-backed, growable circular buffer for byte/POD
streaming I/O: `T` must be `std::is_trivially_copyable`. Unlike
`vec_deque<T>`, the API is built around bulk transfer and zero-copy
framing rather than per-element construction.

```cpp
reloco::ring_buffer<char> rb;
auto ok = rb.try_reserve(64);
std::ignore = rb.try_write(reloco::span<const char>("hello", 5));
char out[5];
std::size_t n = rb.read(reloco::span<char>(out, 5)); // n == 5, consumes
```

Bulk transfer: `try_write(span<const T>)` (lossless), `write_overwrite`
(lossy, evicts oldest), `read(span<T>)` (consuming), `read_slices()`/
`write_slices()` (up to two contiguous spans, for `writev`/`readv`-style
zero-copy I/O), `peek`, `make_contiguous()` (Rust `VecDeque::
make_contiguous`), `transfer_to(dest)` (drains directly into another ring
buffer).

Single-element mutation: `push_back_overwrite`/`push_front_overwrite`
(lossy, never fail), `try_pop_front`/`try_pop_back`, `push_back`
(`std::back_inserter` support).

Access: `operator[]`/`front()`/`back()` (checked tier), `try_at`/
`try_front`/`try_back` (fallible tier), same tri-tier convention as every
other reloco container.

Heterogeneous object I/O: `try_write_object`/`write_object_overwrite`/
`try_write_span` (write a different trivially copyable `U` across
elements of `T`), `try_read_object`/`try_peek_object` (the reverse).

Frame decoding: `try_consume_frame<Header>(validator, processor)` and
`try_peek_frame` (zero-copy, length-prefixed protocol parsing directly
against the buffer, self-healing on corruption), `try_write_frame_evicting`
(write side: evicts complete frames to make room instead of shredding
data), `try_read_frame<Header>(get_total_size)` (linearizes and returns
one contiguous span per frame).

Search: `find(value, offset)`, `find_sequence(seq, offset)`,
`consume_until(predicate)`.

Zero-copy allocation: `allocate_contiguous`/`allocate_slices` (writable
spans) plus `commit(count)`, `try_allocate_object<U>()`,
`align_write_head(alignment)`. `begin_write(limit)` returns a `write_tx`
RAII transaction (typestate-checked via `-Wconsumed`, zero-overhead
rollback if never committed).

`reloco::is_trivially_relocatable<ring_buffer<T>>` is always `true`
regardless of `T`: its handle is just an `allocator_ref` plus a pointer
and three sizes.

See [Ring buffers](ring-buffer.md) for the full engine design, the
frame-parsing contract, and how `write_tx` composes.

## `inline_ring_buffer<T, Capacity>`

`include/reloco/ring_buffer.hpp`

`ring_buffer<T>`'s fixed-capacity, allocator-free counterpart: elements
live in an embedded byte array sized for exactly `Capacity` elements.
Unlike `inline_vector`/`inline_vec_deque`, this type **is** copyable
(copy construction/assignment deep-copy via `read_slices()` +
`try_write()`), since `T` is always trivially copyable. Every
growing operation fails with `error::capacity_exceeded` once
`size() == Capacity`. Movable (steals nothing -- copies the inline
elements and resets the source, since storage can't be stolen from an
embedded array).

`reloco::is_trivially_relocatable<inline_ring_buffer<T, Capacity>>` is
conditional on `is_trivially_relocatable<T>`: its storage is embedded
directly in the object.

## `outline_ring_buffer<T>`

`include/reloco/ring_buffer.hpp`

`ring_buffer<T>`'s non-owning counterpart, bound once to a caller-supplied
`span<U>` at construction (cross-casting: `U` need not equal `T`, as long
as the byte length divides evenly). Never copyable or movable -- there is
no sound way to steal a borrowed span. Growth is capped at the bound
span's byte capacity divided by `sizeof(T)`.

```cpp
alignas(char) std::byte buffer[64];
reloco::outline_ring_buffer<char> rb(reloco::span<std::byte>(buffer));
```

## `sso_ring_buffer<T, InlineCapacity>`

`include/reloco/ring_buffer.hpp`

`ring_buffer<T>`'s small-size-optimized counterpart: up to
`InlineCapacity` elements live in an embedded byte array; growth beyond
that promotes to an `allocator_ref`-backed heap allocation, linearizing
the inline data in the process. `is_inline()` reports whether `*this` is
still using its embedded buffer. Not copyable (use `try_clone`), movable
(steals the heap pointer if spilled, else copies inline elements).

`reloco::is_trivially_relocatable<sso_ring_buffer<T, InlineCapacity>>` is
unconditionally `false` regardless of `T`: a small instance's data pointer
points into its own embedded buffer.

## `ring_buffer_ref<T>`

`include/reloco/ring_buffer.hpp`

An independent, copyable cursor over any of the ring buffer flavors above:
takes an immutable borrow of a source buffer's current `data_`/`head_`/
`len_`/`cap_` and copies that state, so calling `consume()`/`read()` on
the reference advances only its own cursor without modifying the source.
Useful for speculative/lookahead frame parsing. Can also wrap raw external
memory directly via its `span<T>`-taking constructors.

```cpp
reloco::ring_buffer<char> rb;
reloco::ring_buffer_ref<char> cursor(rb); // independent snapshot
```

## `spsc_ring_buffer<T>` / `inline_spsc_ring_buffer<T, Capacity>` / `outline_spsc_ring_buffer<T>` / `heap_spsc_ring_buffer<T>`

`include/reloco/atomic_ring_buffer.hpp`

Lock-free, single-producer/single-consumer queue for trivially copyable
`T`, cache-line-split to avoid false sharing between producer and
consumer. Capacity must be a power of two (`inline_spsc_ring_buffer`
`static_assert`s it at compile time; `outline_spsc_ring_buffer` rounds the
bound span's capacity down; `heap_spsc_ring_buffer::try_initialize` rounds
up). Every container in this family is unconditionally non-copyable and
non-movable.

```cpp
reloco::heap_spsc_ring_buffer<int> q;
auto ok = q.try_initialize(16);
std::ignore = q.try_push(42); // producer thread
auto tx = q.begin_read();     // consumer thread
```

Producer API: `write_slices(min_space)`, `commit(count)`,
`try_write(span<const T>)`, `try_push(value)`, `try_write_object<U>`,
`try_emplace<U>` (`RELOCO_UNSAFE_BUFFER_USAGE`-gated placement-new),
`begin_write(min_space)` (returns a typestate-checked `write_tx`).

Consumer API: `read_slices(min_elements)`, `consume(count)`,
`try_read_object<U>`, `try_peek_object<U>`, `try_consume_frame<Header>`
(mirrors `ring_buffer<T>::try_consume_frame`), `read_with(func)`,
`consume_while(predicate)`, `begin_read(min_elements)` (returns a
typestate-checked `read_tx`).

`heap_spsc_ring_buffer<T>` default-constructs inert; call
`try_initialize(capacity)` once before use.

See [Lock-free SPSC ring buffers](atomic-ring-buffer.md) for the full
design writeup, including the demand-driven cache-refresh strategy behind
`write_slices`/`read_slices`.

## `boxed_slice<T>`

`include/reloco/boxed_slice.hpp`

Fixed-size, allocator-backed owned array with no spare capacity, matching
Rust's `Box<[T]>`. The allocation-free-of-slack counterpart to `vector<T>`:
once constructed, `boxed_slice<T>` can never grow/shrink and holds exactly
`size()` elements, so it never wastes capacity headroom and is one word
smaller than `vector<T>` (no separate capacity field). Move-only, with the
same checked/`try_*`/`unsafe_*` tri-tier element access as every other
reloco container.

```cpp
auto bs = reloco::boxed_slice<int>::try_create(4, 0); // 4 elements, all 0
```

Construct via `try_allocate`/`try_create` (default-constructs every
element), their `(count, value)` overloads (copy-constructs `value` into
every element), or `try_from_vector(vector<T> &&)` (Rust's
`Vec::into_boxed_slice`), which moves an already-built `vector<T>`'s
elements into a freshly, exactly-sized allocation and drops any spare
capacity it was holding.

## `binary_heap<T, Compare = std::less<T>>`

`include/reloco/binary_heap.hpp`

Allocator-backed priority queue matching Rust's
`std::collections::BinaryHeap<T>`: a thin wrapper around `vector<T>`
maintaining the standard binary heap invariant via `<algorithm>`'s
`push_heap`/`pop_heap`/`make_heap`/`sort_heap`. With the default
`Compare = std::less<T>` it's a max-heap (`try_pop()`/`peek()` return the
greatest element first, matching both `std::priority_queue` and Rust's
`BinaryHeap` defaults); pass `std::greater<T>` for a min-heap.

```cpp
auto heap = reloco::binary_heap<int>::try_create();
heap.value().try_push(5);
heap.value().try_push(9);
assert(heap.value().peek() == 9);
auto top = heap.value().try_pop(); // 9, restoring the heap invariant
```

`try_push(value)` inserts and restores the invariant; `try_pop()` removes
and returns the top element, failing with `error::container_empty` when
empty; `peek()`/`try_peek()` are the checked/fallible tier for reading the
top without removing it. `into_sorted_vec()` consumes the heap and returns
its elements as a `vector<T>` in ascending order. Iteration
(`begin()`/`end()`) walks unspecified heap order, not sorted order,
matching Rust's `BinaryHeap::iter()`, and is `const`-only since mutating an
element in place could silently break the invariant.

## `digraph`

`include/reloco/digraph.hpp`

Allocator-backed directed graph over dense `std::size_t` node indices,
purpose-built for lock-order/witness-style dependency tracking (see
FreeBSD's `witness(4)` and Linux's `lockdep`): nodes are allocated in order
by `try_add_node()` (identified by the index it returns), and
`try_add_edge(from, to)` records a directed edge `from -> to` only if doing
so does not close a cycle -- if `to` can already reach `from`, the new edge
would create one (in witness/lockdep terms, a lock-order inversion that
could deadlock), so it is rejected with `error::deadlock` and the graph is
left unchanged.

```cpp
auto g_res = reloco::digraph::try_create();
reloco::digraph g = std::move(g_res.value());
auto a = g.try_add_node().value(); // lock class A
auto b = g.try_add_node().value(); // lock class B
auto c = g.try_add_node().value(); // lock class C
g.try_add_edge(a, b); // "A observed locked before B"
g.try_add_edge(b, c); // "B observed locked before C"

auto rejected = g.try_add_edge(c, a); // would close A -> B -> C -> A
assert(rejected.error() == reloco::error::deadlock);
auto cycle = g.try_find_path(a, c); // {a, b, c}, useful for a diagnostic
```

`try_add_edge` fails with `error::out_of_bounds` if either node does not
exist, and with `error::deadlock` for a self-loop (`from == to`) or a
cycle-closing edge; adding an already-present edge succeeds without
duplicating it. `has_edge`/`try_remove_edge` query/retract a single edge;
nodes are never removed (matching lock-class identifiers, which persist
for the registering subsystem's lifetime). `try_is_reachable(from, to)`
and `try_find_path(from, to)` (shortest node sequence, or
`error::not_found`) answer reachability queries -- typically used after a
`try_add_edge` rejection, as `try_find_path(to, from)`, to recover the
existing path that together with the rejected edge forms the cycle.
`try_clone`/`try_clone(alloc)` deep-copy every node's adjacency list.

Every traversal is iterative (an explicit worklist `vector<size_type>`,
never recursion), so `digraph` stays safe to use from a kernel/freestanding
build (`RELOCO_KERNEL`) with a bounded stack. There is deliberately no
generic weighted-shortest-path/traversal-algorithm layer: reloco never uses
floating point, so this module stays scoped to what integer-weighted,
allocator-fallible reachability queries can express.

## `flat_set<T, Compare = std::less<T>>`

`include/reloco/flat_set.hpp`

Sorted, unique-element set backed directly by a `vector<T>`: a contiguous,
`std::lower_bound`-searched array kept in ascending `Compare` order rather
than a node-based tree. `try_insert`/`try_remove` are the only mutators and
return `reloco::result<...>`; `contains`/`try_find` do not allocate or
mutate. `begin()`/`end()`/`cbegin()`/`cend()` are read-only (mutating
through an iterator would break the sort invariant).

```cpp
auto s = reloco::flat_set<int>::try_create();
if (!s)
  return;
auto ok = s->try_insert(2);
ok = s->try_insert(1);
assert(s->contains(1) && s->contains(2));
```

`reloco::flat_set<T, Compare>` is trivially relocatable exactly when
`Compare` is (see [Trivial relocation](relocatable.md)): it wraps a
`vector<T>`, which is unconditionally relocatable regardless of `T`, so
only the (usually stateless, hence trivially relocatable) `Compare`
matters. It has `container_ref_traits`/`collection_view_traits` adapters
(see below): as an associative `mutable_container_ref` source
(`key_type == element_type == T`), and as a read-only
(`collection_view_traits::is_mutable == false`) collection view, since
mutating an element in place could break the sort order.

## `flat_map<Key, Mapped, Compare = std::less<Key>>`

`include/reloco/flat_map.hpp`

Sorted, unique-key map backed directly by a `vector<std::pair<Key,
Mapped>>`: `flat_set`'s key/value counterpart, kept in ascending `Compare`
order over `.first` rather than a node-based tree. `flat_set` and
`flat_map` share a common base, `detail::flat_container_base<Storage,
Compare, KeyOf>` (`include/reloco/detail/flat_container_base.hpp`), which
implements insertion, removal, lookup, iteration, and cloning once for
both `vector`- and `inline_vector`-backed storage; `flat_map` only adds the
key/value-specific surface described below.

```cpp
auto m = reloco::flat_map<int, std::string>::try_create();
if (!m)
  return;
auto ok = m->try_insert(2, "two");
ok = m->try_insert(1, "one");
auto found = m->try_at(1);
assert(found && found->get() == "one");
```

`try_insert(Key key, Mapped mapped)` is a fallible-insertion convenience
that fails with `error::already_exists` if `key` is already present, or
propagates the underlying storage's allocation failure; `try_at(const
K &key)` (mutable and `const`-qualified overloads) looks up `key` and
returns a reference to just the mapped value on success or
`error::not_found` otherwise. Mutating the mapped value through the
mutable `try_at` overload is always safe: unlike mutating a key, it cannot
break the sort invariant. There is deliberately no `operator[]`: unlike
`std::map`, reloco has no way to silently insert a default-constructed
value on a missing key without either allocating fallibly or aborting, both
of which conflict with the library's explicit-fallibility design — use
`try_insert`/`try_at` instead.

`reloco::flat_map<Key, Mapped, Compare>` is trivially relocatable exactly
when `Compare` is, for the same reason as `flat_set`. It has
`container_ref_traits`/`collection_view_traits` adapters: as an
associative `mutable_container_ref` source (`key_type == Key`,
`element_type == Mapped`), and as a read-only
(`collection_view_traits::is_mutable == false`) collection view of
`(Key, Mapped)` pairs.

## `inline_flat_set<T, Capacity, Compare = std::less<T>>`

`include/reloco/inline_flat_set.hpp`

`flat_set`'s allocation-free counterpart: a sorted, unique-element set
backed directly by an `inline_vector<T, Capacity>` instead of a `vector<T>`,
built on the same `detail::flat_container_base` shared with `flat_set`.
There is no `try_allocate`/`try_create` factory to reserve extra capacity
with (`Capacity` itself is the fixed reserved capacity, fixed at compile
time) — only a default constructor is available, and `try_insert` fails
with `error::capacity_exceeded` once `size() == Capacity`. Everything else
(`try_insert`, `try_remove`, `contains`, `try_find`, `clear`, iteration,
`try_clone(alloc)` / `try_clone()`) behaves exactly like `flat_set`.

```cpp
reloco::inline_flat_set<int, 4> s;
auto ok = s.try_insert(2);
ok = s.try_insert(1);
assert(s.contains(1) && s.contains(2));
```

`reloco::is_trivially_relocatable<inline_flat_set<T, Capacity, Compare>>`
is conditional on **both** `is_trivially_relocatable_v<T>` and
`is_trivially_relocatable_v<Compare>` (unlike `flat_set`, whose `vector<T>`
storage is unconditionally relocatable, `inline_flat_set`'s
`inline_vector<T, Capacity>` storage embeds `T` directly, so its
relocatability already depends on `T` — see `inline_vector<T, Capacity>`
above). It has the same `container_ref_traits`/`collection_view_traits`
adapters as `flat_set`.

Like `inline_vector`, `inline_flat_set<T, Capacity, Compare>` can be
"upgraded" to a heap-backed `flat_set<T, Compare>` via `try_to_flat_set`,
for callers that reach the fixed `Capacity` but need to keep growing:

| Function | Behavior |
|---|---|
| `try_to_flat_set(allocator_ref alloc) const &` / `try_to_flat_set() const &` | Clones every element into a new `flat_set<T, Compare>`, leaving `*this` untouched |
| `try_to_flat_set(allocator_ref alloc) &&` / `try_to_flat_set() &&` | Moves every element out into a new `flat_set<T, Compare>`, consuming `*this` (left empty either way) |

## `inline_flat_map<Key, Mapped, Capacity, Compare = std::less<Key>>`

`include/reloco/inline_flat_map.hpp`

`flat_map`'s allocation-free counterpart: a sorted, unique-key map backed
directly by an `inline_vector<std::pair<Key, Mapped>, Capacity>`. As with
`inline_flat_set`, there is no allocator-taking factory (only a default
constructor), and `try_insert`/`try_insert_at` fail with
`error::capacity_exceeded` once `size() == Capacity`. Otherwise it exposes
the same `try_insert(key, mapped)`/`try_at(key)` surface as `flat_map`
(including the same rationale for omitting `operator[]`).

```cpp
reloco::inline_flat_map<int, std::string, 4> m;
auto ok = m.try_insert(1, "one");
auto found = m.try_at(1);
assert(found && found->get() == "one");
```

`reloco::is_trivially_relocatable<inline_flat_map<Key, Mapped, Capacity,
Compare>>` is conditional on `is_trivially_relocatable_v<Key>`,
`is_trivially_relocatable_v<Mapped>`, **and**
`is_trivially_relocatable_v<Compare>` all holding, for the same
embedded-storage reason as `inline_flat_set`. It has the same
`container_ref_traits`/`collection_view_traits` adapters as `flat_map`.

Like `inline_flat_set`, `inline_flat_map<Key, Mapped, Capacity, Compare>`
can be "upgraded" to a heap-backed `flat_map<Key, Mapped, Compare>` via
`try_to_flat_map`, mirroring `inline_vector::try_to_vector`'s dual
clone/consume tiers:

| Function | Behavior |
|---|---|
| `try_to_flat_map(allocator_ref alloc) const &` / `try_to_flat_map() const &` | Clones every (key, mapped) entry into a new `flat_map<Key, Mapped, Compare>`, leaving `*this` untouched |
| `try_to_flat_map(allocator_ref alloc) &&` / `try_to_flat_map() &&` | Moves every (key, mapped) entry out into a new `flat_map<Key, Mapped, Compare>`, consuming `*this` (left empty either way) |

## `tree_set<T, Compare = std::less<T>>`

`include/reloco/tree_set.hpp`

Sorted, unique-element set backed by an allocator-managed, unbalanced
binary search tree (`detail::tree_base<T, Compare, KeyOf>`,
`include/reloco/detail/tree_base.hpp`) rather than `flat_set`'s contiguous
`vector<T>`: every element lives in its own single-allocation node, so
insertion/removal never shifts other elements and never invalidates
references to elements that are not themselves removed — the trade-off is
`O(log n)` average (`O(n)` worst case, since it is unbalanced)
insert/find/remove instead of `flat_set`'s cache-friendly `O(n)`
insert/`O(log n)` binary-search find. See [Tree containers](
tree-containers.md) for the node layout and engine design.

```cpp
auto s = reloco::tree_set<int>::try_create();
if (!s)
  return;
auto ok = s->try_insert(2);
ok = s->try_insert(1);
assert(s->contains(1) && s->contains(2));
```

`reloco::tree_set<T, Compare>` is trivially relocatable exactly when
`Compare` is: the container's own members (a node pointer, a size, and an
`allocator_ref`) never point into `*this`, so relocating its bytes is
always safe regardless of `T` — every element lives in a separately
allocated node untouched by a byte-copy of the container object itself.
It has a `container_ref_traits` adapter (associative
`mutable_container_ref` source, `key_type == element_type == T`), but no
`collection_view_traits` adapter: unlike `flat_set`, a tree has no `O(1)`
`at(index)`/`data()` to offer.

Because the tree is always kept in ascending `Compare` order, `tree_set`
also exposes Rust `BTreeSet`-flavored `try_first`/`try_last`/
`try_pop_first`/`try_pop_last`, `retain`, `append`, and `is_subset`/
`is_superset`/`is_disjoint` — see [Tree containers](tree-containers.md#rust-btreesetbtreemap-flavored-api-surface)
for the full table.

## `tree_map<Key, Mapped, Compare = std::less<Key>>`

`include/reloco/tree_map.hpp`

`tree_set`'s key/value counterpart: a sorted, unique-key map backed by the
same `detail::tree_base` engine over `std::pair<Key, Mapped>` nodes.
Exposes the same `try_insert(key, mapped)`/`try_at(key)` (mutable and
`const` overloads) surface as `flat_map`, plus the Rust-`entry`-flavored
`try_entry_or_insert`/`try_entry_or_insert_with`/`try_entry_and_modify`; it
also has no `operator[]`, for the same fallibility rationale as `flat_map`.

```cpp
auto m = reloco::tree_map<int, std::string>::try_create();
if (!m)
  return;
auto ok = m->try_insert(2, "two");
ok = m->try_insert(1, "one");
auto found = m->try_at(1);
assert(found && found->get() == "one");
```

`reloco::tree_map<Key, Mapped, Compare>` is trivially relocatable exactly
when `Compare` is, for the same reason as `tree_set`. It has a
`container_ref_traits` adapter (associative `mutable_container_ref`
source, `key_type == Key`, `element_type == Mapped`), but no
`collection_view_traits` adapter, for the same reason as `tree_set`.

Because the tree is always kept in ascending `Compare` order over `Key`,
`tree_map` also exposes Rust `BTreeMap`-flavored `try_first_key_value`/
`try_last_key_value`/`try_pop_first`/`try_pop_last`, `retain(pred)` (where
`pred` takes `(const Key &, Mapped &)`), and `append` — see [Tree
containers](tree-containers.md#rust-btreesetbtreemap-flavored-api-surface)
for the full table.

## `flat_hash_set<T, Hash = std::hash<T>, KeyEqual = std::equal_to<T>>`

`include/reloco/flat_hash_set.hpp`

Unordered, unique-element set backed by an open-addressing, linear-probing
hash table over a single `vector<optional<T>>` (`detail::flat_hash_base<T,
Hash, KeyEqual, KeyOf>`, `include/reloco/detail/flat_hash_base.hpp`),
matching Rust's `HashSet<T>`. Unlike `tree_set` (ordered by `Compare`,
node-based storage), iteration order is unspecified and depends on hash
values and insertion/removal history, but lookup/insert/remove are `O(1)`
average case instead of `O(log n)`. See [Flat hash containers](
flat-hash-containers.md) for the storage/deletion/growth design.

```cpp
auto s = reloco::flat_hash_set<int>::try_create();
if (!s)
  return;
auto ok = s->try_insert(2);
ok = s->try_insert(1);
assert(s->contains(1) && s->contains(2));
```

`reloco::flat_hash_set<T, Hash, KeyEqual>` is trivially relocatable exactly
when both `Hash` and `KeyEqual` are: its own members (a `vector<optional<
T>>`, a size, a mask, and `Hash`/`KeyEqual` themselves) are trivially
relocatable regardless of `T`, since `vector<U>` is unconditionally
trivially relocatable for any `U`. It has a `container_ref_traits` adapter
(associative `mutable_container_ref` source, `key_type == element_type ==
T`), but no `collection_view_traits` adapter: like a tree, a hash table has
no `O(1)` `at(index)`/`data()` to offer (elements are not laid out in
insertion order).

`flat_hash_set` also exposes `try_take` (Rust `HashSet::take` equivalent:
removes and returns the matching element), `retain`, and `is_subset`/
`is_superset`/`is_disjoint` — see [Flat hash containers](
flat-hash-containers.md#rust-hashsethashmap-flavored-api-surface) for the
full table.

## `flat_hash_map<Key, Mapped, Hash = std::hash<Key>, KeyEqual = std::equal_to<Key>>`

`include/reloco/flat_hash_map.hpp`

`flat_hash_set`'s key/value counterpart: an unordered, unique-key map
backed by the same `detail::flat_hash_base` engine over `std::pair<Key,
Mapped>` slots, matching Rust's `HashMap<K, V>`. Exposes the same
`try_insert(key, mapped)`/`try_at(key)` (mutable and `const` overloads)
surface as `tree_map`/`flat_map`, plus the Rust-`entry`-flavored
`try_entry_or_insert`/`try_entry_or_insert_with`/`try_entry_and_modify` and
`try_remove_entry` (Rust `HashMap::remove_entry` equivalent); it also has
no `operator[]`, for the same fallibility rationale as `flat_map`/
`tree_map`.

```cpp
auto m = reloco::flat_hash_map<int, std::string>::try_create();
if (!m)
  return;
auto ok = m->try_insert(2, "two");
ok = m->try_insert(1, "one");
auto found = m->try_at(1);
assert(found && found->get() == "one");
```

`reloco::flat_hash_map<Key, Mapped, Hash, KeyEqual>` is trivially
relocatable exactly when both `Hash` and `KeyEqual` are, for the same
reason as `flat_hash_set`. It has a `container_ref_traits` adapter
(associative `mutable_container_ref` source, `key_type == Key`,
`element_type == Mapped`), but no `collection_view_traits` adapter, for
the same reason as `flat_hash_set`.

## `lru_cache<Key, Mapped, Hash = std::hash<Key>, KeyEqual = std::equal_to<Key>>`

`include/reloco/lru_cache.hpp`

A fixed-capacity, unique-key cache that evicts its least-recently-used
entry once full, matching Rust's de-facto `lru` crate's `LruCache<K, V>`.
Built from a stable-index `vector<optional<node>>` slab (each node holding
one `Key`/`Mapped` pair plus intrusive `prev`/`next` list links) and a
`flat_hash_map<Key, size_type>` index for O(1) average-case lookup;
unlike `flat_hash_map`'s own backward-shift deletion, removal here never
relocates other live entries, since the intrusive list's indices must stay
stable.

- `try_put(key, value)` — inserts, or overwrites+promotes if @p key is
  already present; evicts the current LRU entry first if already at
  `capacity()`, matching `LruCache::put`.
- `try_get(key)` — looks up and promotes to most-recently-used, matching
  `LruCache::get_mut`.
- `try_peek(key)` / `try_peek_mut(key)` — look up without promoting,
  matching `LruCache::peek`/`::peek_mut`.
- `contains(key)` — never promotes, matching `LruCache::contains`.
- `try_remove(key)` — removes and returns the value, matching
  `LruCache::pop`.
- `begin()`/`end()` — a `const_iterator` walking every entry from most- to
  least-recently-used, matching `LruCache::iter()`.

```cpp
auto cache_res = reloco::lru_cache<int, reloco::string>::try_create(2);
if (!cache_res)
  return;
auto &cache = *cache_res;
std::ignore = cache.try_put(1, reloco::string("one"));
std::ignore = cache.try_put(2, reloco::string("two"));
std::ignore = cache.try_get(1);           // promotes 1 -- 2 is now LRU
std::ignore = cache.try_put(3, reloco::string("three")); // evicts 2
assert(!cache.contains(2));
assert(cache.contains(1) && cache.contains(3));
```

Capacity is fixed at `try_create(capacity, alloc)` and never grows: once
allocated, every subsequent `try_put`/`try_get`/`try_peek`/`try_remove`
call is itself allocation-free. Because the intrusive list needs a `Key`
to evict the tail's index entry, and the index needs its own owned `Key`
for hashing, **`Key` must be copy-constructible** (`Mapped` need not be —
it is only ever moved); this is the one place `lru_cache` asks more of
`Key` than the rest of reloco's containers.

`reloco::lru_cache<Key, Mapped, Hash, KeyEqual>` is trivially relocatable
exactly when both `Hash` and `KeyEqual` are, for the same reason as
`flat_hash_map`.

`flat_hash_map` also exposes `retain(pred)` (where `pred` takes `(const
Key &, Mapped &)`) — see [Flat hash containers](
flat-hash-containers.md#rust-hashsethashmap-flavored-api-surface) for the
full table.

## `intrusive_hash_hook<T>` / `intrusive_hash_table<T, Hook, KeyOf, Hash = std::hash<Key>, KeyEqual = std::equal_to<Key>>`

`include/reloco/intrusive_hash_table.hpp`

A unique-key hash table that never allocates -- the odd one out among
reloco's map/set family, matching Linux's `hlist_head`/`hlist_node`
(`<linux/list.h>`) or Boost.Intrusive's `unordered_set` rather than any
other reloco container. Every other map/set *owns* its element storage;
`intrusive_hash_table` owns nothing at all, which is the point: it is
meant for code that must run before an allocator subsystem is up (early
kernel boot, an interrupt handler, a porting layer's own internals --
see `include/reloco/detail/porting/README.md`). Nodes are ordinary caller-owned objects
(`static`, stack, a caller-managed pool) that embed an
`intrusive_hash_hook<T>` as a *named member* (not a CRTP base, so one
object type can carry independent hooks for several different tables):

```cpp
struct my_node {
  int key;
  reloco::intrusive_hash_hook<my_node> hook;
};
struct my_node_key_of {
  const int &operator()(const my_node &n) const noexcept { return n.key; }
};
using my_table = reloco::intrusive_hash_table<my_node, &my_node::hook, my_node_key_of>;

std::array<my_node *, 16> buckets{};
auto table_res = my_table::try_create(reloco::span<my_node *>(buckets.data(), buckets.size()));
if (!table_res)
  return;
auto &table = *table_res;

my_node a{1, {}};
std::ignore = table.try_insert(a);
assert(table.contains(1));
table.remove(a); // O(1): no hashing/bucket-chain walk needed
```

- `try_create(buckets)` — adopts a caller-owned `span<T *>` bucket array
  (zeroing every slot), failing with `error::invalid_argument` if empty.
- `try_insert(node)` — fails with `error::already_exists` on a duplicate
  key; `RELOCO_ASSERT`s @p node is not already linked into *this* or any
  other table.
- `try_find(key)` / `contains(key)` — hash+walk one bucket chain, same
  asymptotic cost as `flat_hash_map`.
- `try_remove(key)` — hashes+walks to find the node, then unlinks it.
- `remove(node)` — O(1) unlink given a node reference already in hand
  (matching Linux's `hlist_del`), via the hook's own `pprev` link -- no
  hashing or bucket-chain walk. Prefer this over `try_remove(key)`
  whenever the caller already has the node (e.g. from a prior
  `try_find`).
- `rehash(new_buckets)` — the caller-driven growth protocol (see below).
- `suggest_bucket_count_for_insert(n, min_buckets, max_buckets,
  elements_per_bucket = 1)` / `suggest_bucket_count_for_remove(n,
  min_buckets, max_buckets, elements_per_bucket = 1)` — purely advisory
  sizing hints for the caller's *next* `rehash()`: target an average
  chain length of `elements_per_bucket` at the projected `size() + n`
  (insert) or `size() - min(n, size())` (remove), clamped to the
  inclusive `[min_buckets, max_buckets]` range the caller considers
  sensible for their platform. Raising `elements_per_bucket` above `1`
  trades bucket-array size for longer chains -- e.g. 100 projected
  elements only need 25 buckets at `elements_per_bucket == 4` rather
  than 100 at the default `1`. Never touch the table or allocate --
  `intrusive_hash_table` cannot pick that range itself, since it never
  owns its own bucket storage; the result is never rounded to a power of
  two either, since `index_for` uses plain modulo, not a power-of-two
  mask, so any bucket count works. Both always compute the tight,
  path-independent linear fit described above; for an *amortized* growth
  curve instead (fewer rehashes overall, at the cost of some slack in
  the bucket array) see `bucket_growth.hpp`'s standalone
  `linear`/`doubling_then_ratio`/`fixed_ratio`/`sqrt_curve` strategies,
  callable directly against `bucket_count()`/`size()` independently of
  these two convenience methods.
- `clear()` — unlinks every node and zeroes every bucket;
  `bucket_count()` is unchanged.
- `begin()`/`end()`/`cbegin()`/`cend()` — forward iteration over every
  currently linked node (order unspecified, like every other reloco hash
  container), plus `iterator_to(node)` (rehashes @p node's key to locate
  its bucket, `RELOCO_ASSERT`ing it is linked) and `erase(iterator)`
  (O(1) unlink, returning an iterator to the following node, matching
  `std::list::erase`). This is exactly the `begin()`/`end()`/
  `erase(iterator)` surface `extract_if_iterator`/`isolated_node_tx`
  (`intrusive_iteration.hpp`) need from a `Container`, so
  `intrusive_hash_table` is usable with `extract_if()` the same way
  `boost::intrusive::list` is.

Growing the bucket array is a two-step, caller-driven protocol rather
than something `try_insert` ever does on its own: the caller allocates a
*new*, larger `span<T *>` however it likes -- critically, **without**
holding whatever lock guards concurrent access to the table, since
allocation may be slow/contended -- then calls `rehash(new_buckets)`,
which re-threads every currently linked node into the new array. Only
that O(n) relinking pass needs to happen while holding the lock:

```cpp
auto new_storage = allocate_bucket_array(old_bucket_count * 2); // outside the lock
{
  auto guard = table_mutex.lock(); // held only for the relink below
  std::ignore = table.rehash(reloco::span<my_node *>(new_storage, new_count));
} // old bucket array (now unreferenced) may be freed here
```

`rehash` itself never allocates either. Nothing in this file ever calls
an allocator, at any point.

`size()`/`empty()`/`bucket_count()`/`load_factor_permille()`/
`contains()`/const `try_find()` are all blocked on rvalue `*this` (a
dangling-reference footgun, since the table is a non-owning `RELOCO_POINTER`
view), and `try_insert`/`remove` explicitly reject rvalue node arguments
too, matching the library-wide rvalue-safety convention (see [Lifetime
and safety annotation macros](#lifetime-and-safety-annotation-macros)).

`load_factor_permille()` reports load as parts-per-thousand (e.g. `1500`
for an average chain length of `1.5`) rather than a `float` -- like every
other reloco diagnostic accessor, it is integer-only, since kernel/
bare-metal code (this file's whole reason to exist) frequently cannot use
the FPU at all without extra save/restore ceremony.

Unlike every other reloco container, `intrusive_hash_table` has no
`try_clone`: cloning would require deciding where the clone's nodes live,
which is exactly the decision this whole file exists to leave to the
caller.

## `intrusive_rbtree_hook<T>` / `intrusive_rbtree<T, Hook, KeyOf, Compare = std::less<Key>>`

`include/reloco/intrusive_rbtree.hpp`

The ordered counterpart to `intrusive_hash_table`: a unique-key red-black
tree that never allocates, matching Linux's `struct rb_node`
(`<linux/rbtree.h>`) or Boost.Intrusive's `set`. Classic CLRS red-black
balancing (`parent`/`left`/`right` links plus one `red` bool per node, no
sentinel "nil" node -- `nullptr` is treated as black throughout, the
null-aware style Linux's `rbtree.c` uses instead of a shared mutable
sentinel, which a header-only design has no good place to keep anyway).
Worst-case height stays `O(log n)` by construction, unlike
`detail::tree_base`'s deliberately unbalanced BST. Nodes embed an
`intrusive_rbtree_hook<T>` as a named member (not a CRTP base):

```cpp
struct my_node {
  int key;
  reloco::intrusive_rbtree_hook<my_node> hook;
};
struct my_node_key_of {
  const int &operator()(const my_node &n) const noexcept { return n.key; }
};
using my_tree = reloco::intrusive_rbtree<my_node, &my_node::hook, my_node_key_of>;

my_tree tree;
my_node a{1, {}};
std::ignore = tree.try_insert(a);
assert(tree.contains(1));
tree.remove(a); // no re-walk needed, node already in hand
```

- `try_insert(node)` — fails with `error::already_exists` on a duplicate
  key; `RELOCO_ASSERT`s @p node is not already linked into *this* or any
  other tree.
- `try_find(key)` / `contains(key)` — `O(log n)` walk from the root, same
  asymptotic cost as `tree_set`/`tree_map`.
- `try_remove(key)` — walks to find the node, then unlinks+rebalances it.
- `remove(node)` — unlinks+rebalances a node reference already in hand (no
  re-walk needed to *locate* it, unlike `try_remove(key)`).
- `try_first()`/`try_last()`/`try_pop_first()`/`try_pop_last()` — Rust
  `BTreeSet`-flavored smallest/greatest accessors, matching
  `detail::tree_base`'s own.
- `begin()`/`end()`/`iterator_to(node)`/`erase(iterator)`/`erase(first,
  last)` — ascending `Compare`-order iteration; exactly the surface
  `extract_if_iterator`/`isolated_node_tx` (`intrusive_iteration.hpp`)
  needs from a `Container`, so `intrusive_rbtree` is usable with
  `extract_if()` the same way `intrusive_hash_table` is. The range form
  matches `std::set::erase(first, last)`, unlinking every node in
  `[first, last)` one at a time and returning @p last.
- `lower_bound(key)`/`upper_bound(key)`/`bounded_range(lo, hi,
  left_closed = true, right_closed = false)` — `std::set`/
  `boost::intrusive::set`-flavored ordered range queries, plain
  `O(log n)` walks that never restructure the tree. `bounded_range`
  defaults to the half-open `[lo, hi)` convention.
- `find_containing(addr, end_of)` / `find_overlap(lo, hi, end_of)` /
  `find_gap(min_size, lo, hi, end_of)` — address-space/VM-subsystem
  helpers that treat each node's `KeyOf{}` key as the start of a `[start,
  end_of(node))` range (`end_of` is any `key_type(const T &)`-callable
  supplying the end). **Require every node's range to be non-overlapping
  and sorted by start** — true by construction for a VMA/address-space
  tree, where live mappings can never overlap — which is what lets
  `find_containing`/`find_overlap` stay plain `O(log n)` walks (no
  per-subtree max-end augmentation needed, unlike a general overlapping-
  interval tree) and lets `find_gap` do an `O(k)` first-fit linear scan
  for the first free span of at least `min_size` within `[lo, hi)`.
  `find_containing` is Linux's `find_vma()`/FreeBSD's
  `vm_map_lookup_entry()`; `find_gap` is the `mmap`/
  `get_unmapped_area`/`vm_map_findspace` free-space-search primitive.
  None of the three restructure the tree.
- `erase_and_dispose(iterator, disposer)` / `erase_and_dispose(first,
  last, disposer)` / `remove_and_dispose(node, disposer)` /
  `try_remove_and_dispose(key, disposer)` / `clear_and_dispose(disposer)`
  — `boost::intrusive::set`-flavored removal that also invokes a
  caller-supplied `void(T &)` `disposer` on each removed node (e.g. to
  return its storage to a pool); `clear()` is `clear_and_dispose` with a
  no-op disposer.
- `clear()` — unlinks every node, `O(n)` iterative (no recursion, so a
  degenerate/huge tree cannot blow the call stack).
- `splice(source, first, last)` / `splice(source)` — moves every node in
  `[first, last)` (or the whole `source` tree, for the no-range overload)
  out of another tree of the *same* concrete type and into `*this`, one
  `try_insert` at a time; a source node whose key already exists in
  `*this` is left untouched in `source` (`std::map::merge` semantics).
  Returns the count of nodes actually moved.
- `splice_replace(source, first, last, disposer)` /
  `splice_replace(source, disposer)` — same move, but on a key conflict
  the *existing node already in `*this`* is unlinked and passed to
  `disposer` so the incoming `source` node can always take its place;
  every node in the range always leaves `source`.
- `splice_discard(source, first, last, disposer)` /
  `splice_discard(source, disposer)` — same move, but on a key conflict
  the *source* node is unlinked and passed to `disposer` instead,
  leaving `*this`'s existing node untouched.
  All three require `&source != this` (`RELOCO_ASSERT`-checked) and walk
  the range node-by-node rather than relinking whole subtrees, since
  every moved node needs a real `try_insert` to stay correctly balanced.

Like `intrusive_hash_table`, every accessor is blocked on rvalue `*this`
(a dangling-reference footgun, since the tree is a non-owning
`RELOCO_POINTER` view), and has no `try_clone` -- cloning would require
deciding where the clone's nodes live, exactly the decision this whole
file exists to leave to the caller. Everything above except
`try_insert`/the red-black fixups is implemented once, in the
balancing-agnostic CRTP base `detail::intrusive_bst_base`
(`detail/intrusive_bst_base.hpp`) shared with `intrusive_splay_tree`
below -- an implementation detail, not part of the public API.

## `intrusive_splay_tree_hook<T>` / `intrusive_splay_tree<T, Hook, KeyOf, Compare = std::less<Key>>`

`include/reloco/intrusive_splay_tree.hpp`

A self-adjusting, non-allocating, unique-key binary search tree -- the
splay-tree counterpart to `intrusive_rbtree`, matching FreeBSD's `SPLAY_*`
macros (`sys/sys/tree.h`) or Boost.Intrusive's `splaytree`/`splay_set`.
Same hook-based, caller-owned-node shape as `intrusive_rbtree`, but
`intrusive_splay_tree_hook<T>` carries no color/balance metadata at all
(only `parent`/`left`/`right`) -- instead, every successful access
(`try_find`, `try_insert`, `remove`) *splays* the node up to the root via
Sleator-Tarjan bottom-up rotations, giving an amortized `O(log n)` bound
across any sequence of operations (the Balance Theorem) in exchange for
working-set locality, with a single worst-case access still possibly
`O(n)` -- unlike `intrusive_rbtree`'s strict per-operation worst case.

```cpp
using my_tree = reloco::intrusive_splay_tree<my_node, &my_node::hook, my_node_key_of>;
```

- Because splaying restructures the tree on every successful (or
  unsuccessful) search, the non-`const` (`&`-qualified) `try_find`/
  `try_first`/`try_last`/`remove` splay, unlike every other reloco
  associative container's `try_find`. `contains(key)` and the `const &`
  -qualified overloads of `try_find`/`try_first`/`try_last` are a plain,
  non-restructuring walk instead, for callers that only hold a `const
  intrusive_splay_tree &` or would rather not pay a rotation cost.
  Overload resolution picks the splaying version automatically for a
  non-`const` tree and the read-only peek for a `const` one. These `const
  &` overloads still return `result<std::reference_wrapper<T>>` -- a
  mutable reference, the same as their splaying counterparts -- rather
  than `reference_wrapper<const T>`: the tree doesn't own its nodes, so
  `const`-qualifying the tree itself says nothing about node mutability,
  and returning `const T &` would only force every caller into a
  `const_cast`.
- `try_remove(key)`/`remove(node)` splay the target to the root, then
  join its left/right subtrees (splaying the left subtree's maximum to
  its own root first) -- the standard splay-tree deletion-by-join.
- `begin()`/`end()`/`iterator_to(node)`/`erase(iterator)`/`erase(first,
  last)` — plain `++`/`--` never splays (it only follows existing links),
  so iterating the whole tree does not itself restructure it; only
  `try_find`/`try_insert`/`remove`/`erase` do (each erased node is still
  splayed-then-joined, as in single-element `erase`). **Caveat**: an
  `end()` iterator obtained before an intervening splay caches a
  now-stale root pointer (only used by `operator--`) -- don't mix
  mutation with a previously held `end()` iterator.
- `try_first()`/`try_last()`/`try_pop_first()`/`try_pop_last()`: the
  non-`const` `try_first`/`try_last` also splay the found node to the
  root, same as `try_find`; their `const &` overloads are a
  non-restructuring peek instead, like `contains`.
- `lower_bound(key)`/`upper_bound(key)`/`bounded_range(lo, hi,
  left_closed = true, right_closed = false)` — same shape as
  `intrusive_rbtree`'s own (inherited, unchanged, from the shared
  `detail::intrusive_bst_base`): plain `O(log n)` walks, never splaying.
- `find_containing(addr, end_of)`/`find_overlap(lo, hi, end_of)`/
  `find_gap(min_size, lo, hi, end_of)` — same VM-subsystem address-space
  helpers as `intrusive_rbtree`'s own (also inherited unchanged): plain
  walks, never splaying.
- `erase_and_dispose(iterator, disposer)` / `erase_and_dispose(first,
  last, disposer)` / `remove_and_dispose`/`try_remove_and_dispose`/
  `clear_and_dispose` — same `boost::intrusive::set`-flavored
  disposer-invoking removal as `intrusive_rbtree`'s own (also inherited
  unchanged); splaying still happens as part of the underlying unlink.
- `clear()` — same `O(n)` iterative, no-recursion teardown shape as
  `intrusive_rbtree::clear`.
- `splice`/`splice_replace`/`splice_discard` — same shape as
  `intrusive_rbtree`'s own (inherited, unchanged, from the shared
  `detail::intrusive_bst_base`); each moved node is still `try_insert`ed
  into `*this`, so it is splayed to the root as usual.

Same rvalue-blocking and no-`try_clone` conventions as
`intrusive_rbtree`/`intrusive_hash_table`, for the same reasons.

## Intrusive C list and queue adapters

`include/reloco/intrusive_c_list.hpp`,
`include/reloco/intrusive_c_list_head.hpp`,
`include/reloco/intrusive_c_slist.hpp`,
`include/reloco/intrusive_c_stailq.hpp`,
`include/reloco/intrusive_c_tailq.hpp`

The `intrusive_c_*` adapters expose BSD list, Linux list-head, singly linked
list, singly linked tail-queue, and doubly linked tail-queue layouts as C++
ranges. Nodes and hooks remain caller-owned; the associated hook-layout and
hook-access types customize how each adapter reaches a node's links.

## `contiguous_iterator<T, BoundsPolicy>`

`include/reloco/contiguous_iterator.hpp`

`contiguous_iterator` is a bounds-aware random-access iterator over
contiguous storage. `static_bounds_policy` uses a compile-time bound, while
`dynamic_bounds_policy` obtains the bound from a container.

## `iter()` / `iterator_adaptor<Derived, Item>` (`Fuse`, `Zip`, `Map`, `Filter`, `Enumerate`, `Take`, `Skip`, `Chain`, `Peekable`, `Flatten`, `StepBy`, `Dedup`, `Intersperse`, `Windows`, `Merge`, `from_fn`, `once`, `repeat`, `successors`, `iota`, `empty`)

`include/reloco/iterator.hpp`

Lazy, zero-allocation iterator adaptors mirroring Rust's
`std::iter::Iterator` adapter chain. `reloco::iter(range)` wraps any type
with `begin()`/`end()` (a `vector`, `span`, `flat_map`, plain array, ...)
as an adaptor that is, at the same time:

- **A Rust-style pull iterator**: `next()` returns `optional<Item>`,
  empty once exhausted, matching `Iterator::next(&mut self) -> Option<Item>`
  exactly -- polling `next()` again after exhaustion keeps returning empty
  rather than being undefined behavior (every adaptor in this file already
  behaves as if `.fuse()`d; see below).
- **A C++ range**: `begin()`/`end()` return a single-pass
  `std::input_iterator_tag` cursor plus a distinct sentinel type, so every
  adaptor chain is directly usable in a range-for loop.

```cpp
std::vector<int> v{1, 2, 3, 4, 5, 6};

for (auto &x : reloco::iter(v).filter([](int &x) { return x % 2 == 0; }))
  x *= 10; // real, mutable reference into `v` -- {1, 20, 3, 40, 5, 60}

int sum = reloco::iter(v)
              .filter([](int &x) { return x % 2 == 0; })
              .map([](int &x) { return x / 10; })
              .fold(0, [](int acc, int &x) { return acc + x; }); // 2+4+6 = 12
```

**Item shape.** An adaptor that only *borrows* from an underlying range
(`iter(range)` itself, `filter`, `take`, `skip`, `fuse`, `chain`) has
`item_type = std::reference_wrapper<T>` (or `<const T>`) -- reloco's
established "nullable reference" idiom for anything that must fit inside
an `optional<...>` (`optional<T&>` is not supported; the same shape
`variant<Ts...>::as<T>()` uses for the same reason, see `variant.hpp`).
The range-for cursor transparently unwraps this back to a plain `T &`
before handing it to the loop body, so callers only see the
`reference_wrapper` if they call `next()` directly. An adaptor that
*produces new values* (`map`, `enumerate`, `zip`) has a plain, owned
`item_type` instead (the callable's return type, or a `std::pair` of the
upstream item types). `iterator_adaptor` also exposes `value_type`: the
*unwrapped* element type (`T`, whether `item_type` is `T` itself or
`std::reference_wrapper<T>`) -- the type callables/predicates passed to
`.map()`/`.filter()`/`.min_by_key()`/etc. actually see, and the type used
by value-producing terminal ops like `.min()`/`.sum()`.

**Adapters** (each moves `*this` into the new adaptor, matching Rust's
`self`-by-value adapter methods -- the original binding is left
moved-from and should not be reused):

| Method | Rust/itertools equivalent | Behavior |
| --- | --- | --- |
| `.fuse()` | `Iterator::fuse()` | Guarantees "empty stays empty"; matters only when wrapping a foreign `next_impl()` that might not already guarantee it. |
| `.zip(other)` | `Iterator::zip()` | Pairs items from both sides; stops as soon as either is exhausted. |
| `.map(f)` | `Iterator::map()` | Applies `f` to every (unwrapped) item. |
| `.filter(pred)` | `Iterator::filter()` | Yields only items for which `pred` is `true`. |
| `.enumerate()` | `Iterator::enumerate()` | Pairs every item with its zero-based `std::size_t` position. |
| `.take(n)` | `Iterator::take(n)` | Yields at most `n` items, then stops. |
| `.skip(n)` | `Iterator::skip(n)` | Discards the first `n` items (lazily, on the first `next()` call, matching Rust). |
| `.chain(other)` | `Iterator::chain()` | Yields every item of `*this`, then every item of `other` (both sides must share `item_type`). |
| `.peekable()` | `Iterator::peekable()` | Adds `.peek()`: look at the next item without consuming it; repeated `.peek()` calls (with no intervening `.next()`) return the exact same item. |
| `.flatten()` | `Iterator::flatten()` | Item must itself have `begin()`/`end()` (e.g. a `vector<vector<int>>`'s items); yields every inner element in order, skipping empty inner ranges. |
| `.flat_map(f)` | `Iterator::flat_map()` | `.map(f).flatten()` in one step; `f` returns a range per item. |
| `.step_by(n)` | `Iterator::step_by(n)` | Yields every `n`-th item, starting with the first (`n` must be `> 0`; asserted). |
| `.dedup()` | itertools `Itertools::dedup()` | Collapses consecutive equal items (via `==` on the unwrapped `value_type`) down to a single copy; non-consecutive duplicates are left alone. |
| `.intersperse(sep)` | itertools `Itertools::intersperse()` | Inserts a copy of `sep` between every pair of adjacent items; a 0- or 1-item source yields no separator at all. |
| `.windows<N>()` | Rust slice `windows(N)` | Yields overlapping `std::array<value_type, N>` snapshots (`{0,1,2}`, `{1,2,3}`, ...); yields nothing if fewer than `N` items are available (`N` must be `> 0`; asserted). |
| `.merge(other)` | itertools `Itertools::merge()` | Merges two already-sorted (ascending, by `<`) same-`item_type` sources into one sorted stream, like the merge step of mergesort. |

**Terminal (consuming) operations**: `.for_each(f)`, `.fold(init, f)`,
`.count()`, `.nth(n)`, `.all(pred)`, `.any(pred)`, `.find(pred)`,
`.last()`, `.min()`, `.max()`, `.min_by_key(f)`, `.max_by_key(f)`,
`.sum<Acc = value_type>()`, `.product<Acc = value_type>()` -- all Rust
`Iterator`/itertools methods of the same name, all draining `*this`.
`.min()`/`.max()`/`.min_by_key()`/`.max_by_key()` return an empty
`optional<value_type>` for an empty source; ties keep the *first*
encountered extremum, matching Rust. `.sum()`/`.product()` return `Acc{}`
(the additive/multiplicative identity) for an empty source and default
`Acc` to `value_type` (pass an explicit `Acc` when accumulating into a
wider type, e.g. `.sum<std::int64_t>()` over `int` items).

**Lvalue-only `next()`/`begin()`/`end()`.** Unlike the adapter-construction
methods, `next()` and `begin()`/`end()` are `&`-qualified with an explicit
`= delete`d rvalue overload, matching `rvalue_safety.hpp`'s
`RELOCO_BLOCK_RVALUE_ACCESS` convention every reloco container's own
`begin()`/`end()` already follows: an adaptor's C++ cursor holds a pointer
back into the adaptor object itself (to call `next()` through it on every
`operator++`), so handing back a live cursor from a temporary adaptor
(`reloco::iter(v).map(f).begin()`) would dangle the instant that temporary
is destroyed at the end of the full expression. This does *not* get in the
way of `for (auto &x : reloco::iter(v).map(f))`: the range-for loop binds
the range-expression to a hidden `auto &&` local first (lifetime-extending
the temporary adaptor chain for the loop's duration), so `begin()`/`end()`
are always called through that lvalue. `reloco::iter(range)` itself follows
the same rule one level up: it only accepts an lvalue `range` (a deleted
forwarding-reference overload rejects a temporary container with a clear
"use of deleted function" diagnostic, since the adaptor stores plain C++
iterators into `range`'s own storage).

No allocation, no exceptions, no RTTI: every adaptor stores its upstream
adaptor(s)/iterators and callable(s) inline by value.

### Writing a generating (source) iterator

`iterator_adaptor` is a CRTP base, not a closed set of adaptors: any class
publicly derived from `iterator_adaptor<Derived, Item>` that implements
`optional<Item> next_impl() noexcept` (called once per `next()`/cursor
increment; never called again once it has returned empty) is a full
citizen of the adaptor chain -- `.map()`/`.take()`/range-for/etc. all work
on it for free, infinite generators included (pair one with `.take(n)` or
another early-stopping adaptor, exactly like Rust's own infinite iterators):

```cpp
class fibonacci : public reloco::iterator_adaptor<fibonacci, std::uint64_t> {
public:
  using item_type = std::uint64_t;

  optional<item_type> next_impl() noexcept {
    auto value = a_;
    auto next = a_ + b_;
    a_ = b_;
    b_ = next;
    return value;
  }

private:
  std::uint64_t a_{0}, b_{1};
};

for (auto x : fibonacci{}.take(10)) // 0, 1, 1, 2, 3, 5, 8, 13, 21, 34
  ...
```

Six ready-made source iterators (Rust's `std::iter` free functions) cover
the common cases without a hand-written class:

| Function | Rust equivalent | Behavior |
| --- | --- | --- |
| `from_fn(f)` | `std::iter::from_fn()` | Calls `optional<Item> f()` on every `next()`, forwarding its result as-is -- `f` itself decides when to stop. |
| `once(value)` | `std::iter::once()` | Yields exactly one item (a move of `value`), then stops. |
| `repeat(value)` | `std::iter::repeat()` | Infinite stream of copies of `value`; always pair with `.take(n)` or another early-stopping adaptor. |
| `successors(first, f)` | `std::iter::successors()` | Seeded with `optional<T> first`; each next item is `f(previous)`, stopping once `first`/`f(...)` is empty. |
| `iota(start, end)` | `std::iter::successors`/Python's `range()` | Yields the half-open integral range `[start, end)`, one value per `next()` (`T` must be integral; asserted). |
| `empty<T>()` | `std::iter::empty()` | Always immediately exhausted; a neutral placeholder wherever a concrete iterator type is required. |

```cpp
int n = 0;
auto counter = reloco::from_fn([n]() mutable -> reloco::optional<int> {
  return n < 5 ? reloco::optional<int>(n++) : reloco::nullopt;
}); // 0, 1, 2, 3, 4

auto powers = reloco::successors(reloco::optional<int>(1), [](int &prev) {
  return prev <= 32 ? reloco::optional<int>(prev * 2) : reloco::nullopt;
}); // 1, 2, 4, 8, 16, 32, 64

for (int i : reloco::iota(0, 5)) // 0, 1, 2, 3, 4
  ...
```

### Itertools-style adaptors

The `.peekable()`/`.flatten()`/`.flat_map()`/`.step_by()`/`.dedup()`/
`.intersperse()`/`.windows<N>()`/`.merge()` adaptors above round out the
adapter chain with the common "itertools" operations that need no
allocation or caller-supplied storage (unlike itertools' `unique()`,
`sorted()`, `group_by()`, or the Cartesian-product family, which need a
hash set/buffer/caller-supplied storage and are deliberately not provided
here, matching this library's allocation-averse design):

```cpp
std::vector<int> v{1, 2, 3};
auto it = reloco::iter(v).peekable();
assert(it.peek()->get() == 1); // look ahead without consuming
assert(it.peek()->get() == 1); // idempotent
auto first = it.next(); // consumes the peeked item -- 1

std::vector<std::vector<int>> vv{{1, 2}, {3}, {}, {4, 5}};
for (int x : reloco::iter(vv).flatten()) // 1, 2, 3, 4, 5 (empty inner range skipped)
  ...

for (int x : reloco::iter(v).flat_map([](int x) { return std::vector<int>{x, x * 10}; }))
  ... // 1, 10, 2, 20, 3, 30

for (int x : reloco::iter(v).step_by(2)) // every 2nd item, starting with the first
  ...

std::vector<int> d{1, 1, 2, 2, 2, 3, 1, 1};
for (int x : reloco::iter(d).dedup()) // 1, 2, 3, 1 (only consecutive duplicates collapse)
  ...

for (int x : reloco::iter(v).map([](int x) { return x; }).intersperse(0)) // 1, 0, 2, 0, 3
  ...

for (auto window : reloco::iter(v).map([](int x) { return x; }).windows<2>())
  ...; // {1,2}, {2,3}

std::vector<int> a{1, 3, 5}, b{2, 4, 6};
for (int x : reloco::iter(a).map([](int x) { return x; })
                 .merge(reloco::iter(b).map([](int x) { return x; })))
  ...; // 1, 2, 3, 4, 5, 6 (both sides must already be sorted ascending)

auto sum = reloco::iter(v).map([](int x) { return x; }).sum(); // 6
auto biggest = reloco::iter(v).map([](int x) { return x; }).max(); // optional<int>(3)
```

## `sso_flat_set<T, InlineCapacity, Compare = std::less<T>>`

`include/reloco/sso_flat_set.hpp`

A sorted, unique-element set backed by `sso_vector<T, InlineCapacity>`
instead of `vector<T>`: it holds up to `InlineCapacity` elements inline
without allocating, and transparently promotes to heap storage once that
capacity is exceeded. Unlike `inline_flat_set`, `try_insert` never fails
with `error::capacity_exceeded` — growth past `InlineCapacity` just
allocates, exactly like `flat_set`. Otherwise it exposes the same surface
as `flat_set` (`try_insert`, `try_find`, `contains`, `try_remove`,
`try_clone`, etc.), including the same allocator-taking `try_allocate`/
`try_create` factories.

```cpp
auto set = reloco::sso_flat_set<int, 4>::try_create();
auto ok = set->try_insert(42); // stays inline
auto grown = set->try_insert(1000); // may promote to heap once size > 4
assert(set->contains(42));
```

`reloco::is_trivially_relocatable<sso_flat_set<T, InlineCapacity, Compare>>`
is unconditionally `false`, since it wraps `sso_vector`, which is itself
never trivially relocatable (its inline buffer is self-referencing). It has
the same `container_ref_traits`/`collection_view_traits` adapters as
`flat_set`.

## `sso_flat_map<Key, Mapped, InlineCapacity, Compare = std::less<Key>>`

`include/reloco/sso_flat_map.hpp`

`flat_map`'s SSO-backed counterpart: a sorted, unique-key map backed by
`sso_vector<std::pair<Key, Mapped>, InlineCapacity>`. Like `sso_flat_set`,
it never fails with `error::capacity_exceeded` — it stays inline up to
`InlineCapacity` entries and transparently promotes to heap storage beyond
that. It exposes the same `try_insert(key, mapped)`/`try_at(key)` surface
as `flat_map` (including the same rationale for omitting `operator[]`).

```cpp
auto map = reloco::sso_flat_map<int, std::string, 4>::try_create();
auto ok = map->try_insert(1, "one");
auto found = map->try_at(1);
assert(found && found->get() == "one");
```

`reloco::is_trivially_relocatable<sso_flat_map<Key, Mapped, InlineCapacity,
Compare>>` is unconditionally `false`, for the same reason as
`sso_flat_set`. It has the same `container_ref_traits`/
`collection_view_traits` adapters as `flat_map`.

## `basic_inline_string<Capacity, CharT, TraitsT>` (`inline_string<Capacity>`, `inline_wstring<Capacity>`)

`include/reloco/inline_string.hpp`

Fixed-capacity, inline-allocated character buffer mirroring
`reloco::basic_string`'s fallible-mutation and tri-tier access convention,
without ever allocating or throwing. Unlike `basic_string`, it is trivially
copyable and movable (it holds no heap resource), and
`reloco::is_trivially_relocatable<basic_inline_string<...>>` is
unconditionally `true`.

```cpp
auto s = reloco::inline_string<32>::try_create(reloco::string_view("hello"));
if (!s)
  return; // s.error() is a reloco::error.
auto ok = s->try_append(reloco::string_view(" world"));
assert(s->view() == "hello world");
```

Every mutating operation that can exceed `Capacity` (`try_assign`,
`try_append`, `try_insert`, ...) returns `reloco::result<void>` instead of
trapping or reallocating — there is no growth path, unlike `basic_string`.
Self-aliasing appends/inserts (e.g. `s.try_append(s.view())`) are handled
correctly by temporarily materializing the overlapping source view, exactly
as `basic_string` does. Read-only access follows the same checked/`try_*`/
`unsafe_*` convention as `basic_string`/`string_view`, including a
`RELOCO_UNSAFE_BUFFER_USAGE`-gated `unsafe_c_str()` for interop with
null-terminated-string APIs.

Like the other fixed-capacity types, `basic_inline_string<Capacity, CharT,
TraitsT>` can be "upgraded" to a heap-backed `basic_string<CharT,
TraitsT>` via `try_to_string(allocator_ref alloc)` / `try_to_string()`,
which copy the current content into a new growable string, leaving `*this`
untouched (there is no consuming `&&` overload: unlike `vector`/
`inline_flat_set`/`inline_flat_map`, `basic_inline_string` is trivially
copyable, so there is no move-vs-clone distinction to make).

## `optional<T>`

`include/reloco/optional.hpp`

Zero-allocation, conditionally-present value wrapper replacing
`std::optional<T>` by eliminating undefined behavior on empty access.
Storage is inline (no heap allocation), and construction/assignment/`swap`
compose with `is_trivially_relocatable<T>` for zero-overhead moves where `T`
permits it.

```cpp
reloco::optional<int> maybe;
assert(!maybe.has_value());
maybe = 42;
assert(maybe.value() == 42);
```

Access follows the checked/fallible/unsafe convention used throughout
reloco: `value()`/`operator*`/`operator->` use `RELOCO_ASSERT` to trap on
empty access; `try_value()` returns `result<std::reference_wrapper<T>>` and
`ok_or(error)` bridges directly into a `result<T>` pipeline; `unsafe_value()`/
`unsafe_ptr()` are `RELOCO_UNSAFE_BUFFER_USAGE`-gated, debug-only-checked
escape hatches. `emplace(...)` destroys any held value and constructs a new
one in place, returning a borrowed reference to it.
`reloco::is_trivially_relocatable<optional<T>>` follows `T`'s own
relocatability.

Rust `Option`-inspired methods round out the fallible/`&`-qualified
mutation surface: `take()` moves the value out, leaving `*this` empty, and
returns it as a fresh `optional<T>`; `replace(value)` moves `value` in and
returns whatever was previously held (empty or not); `get_or_insert(value)`/
`get_or_insert_with(factory)` insert only if empty (the latter's `factory`
is never invoked when a value is already present) and return a reference to
the now-present value.

```cpp
reloco::optional<int> opt;
int &v = opt.get_or_insert_with([] { return 42; });
assert(v == 42);
reloco::optional<int> old = opt.replace(7);
assert(*old == 42 && *opt == 7);
```

`Ok(expected<T, E>)`/`Err(expected<T, E>)` free functions bridge a
`result<T>` back into an `optional`, mirroring Rust's `Result::ok()`/
`Result::err()`: `Ok` keeps the value and discards the error on failure;
`Err` keeps the error and discards the value on success.

```cpp
reloco::result<int> parsed = parse(s);
reloco::optional<int> value = reloco::Ok(parsed);
reloco::optional<reloco::error> failure = reloco::Err(parsed);
```

Two free functions mirror Rust's `bool::then`/`bool::then_some`:
`then(condition, f)` invokes `f()` only if `condition` is `true`, wrapping
its result in an `optional`; `then_some(condition, value)` always evaluates
`value` (an ordinary argument) but only keeps it if `condition` is `true` --
prefer `then()` when constructing the value has a cost worth skipping.

```cpp
reloco::optional<int> maybe = reloco::then(x > 0, [&] { return compute(x); });
```

Further Rust `Option<T>`-parity additions round out the API:

- `is_some()`/`is_none()` — aliases of `has_value()`/`!has_value()`.
- `is_some_and(f)` — `true` if a value is present *and* `f` applied to it
  returns `true`; `f` is not invoked when empty.
- `unwrap()` — alias of `value()`. `expect(msg)` — like `unwrap()`, but
  `msg` (a runtime `const char *`) is used as the assertion failure
  message.
- `unwrap_or(fallback)` — alias of `value_or(fallback)`.
  `unwrap_or_default()` — returns the value, or a default-constructed `T`
  if empty (SFINAE-disabled unless `T` is nothrow default constructible).
  `unwrap_or_else(f)` — returns the value, or invokes `f()` (no arguments)
  if empty.
- `map_or(default, f)`/`map_or_else(default_fn, f)` — apply `f` to the
  value if present, else return `default`/invoke `default_fn()`.
- `insert(value)` — unconditionally (re)constructs the held value from
  `value`, discarding any previous one, and returns a reference to it
  (unlike `get_or_insert`, which only inserts when empty); an alias of
  `emplace(value)` under Rust's name for this operation.
- `zip(other)` — if both `*this` and `other` hold a value, returns an
  `optional<std::pair<T, U>>` containing both; otherwise an empty
  `optional`.
- `logical_xor(other)` — Rust's `Option::xor`, renamed since `xor` is a
  reserved alternative operator token in C++: returns whichever of
  `*this`/`other` holds a value if exactly one of them does, otherwise an
  empty `optional<T>`.
- `flatten(opt)` — a free function (not a member, since C++ cannot
  partially specialize a member function on `T` itself being an
  `optional`) collapsing a nested `optional<optional<T>>` into an
  `optional<T>`, empty if either layer is empty.

## `variant<Ts...>` / `overloaded<Fs...>`

`include/reloco/variant.hpp`

A genuine `std::variant<Ts...>` (public inheritance, inherited
constructors, no additional data members -- it converts to/from and
interoperates with `std::variant<Ts...>` and everything that accepts one)
with a small set of Rust-inspired ergonomics layered on top. Nothing
`std::variant` already provides (`index()`, `get`/`get_if`,
`holds_alternative`, `visit`, comparisons, exception-safe assignment) is
re-derived; only genuine gaps are added:

- `match(fs...)` — Rust `match`-expression equivalent: dispatches to
  whichever callable in `fs...` accepts the currently active alternative,
  built from an ad-hoc overload set and `std::visit` under the hood --
  the "overloaded-lambda-set visitor" idiom every C++17 `std::variant`
  user ends up hand-rolling. As with `std::visit`, the call is ill-formed
  unless the overload set is callable with every alternative in `Ts...`
  (no silent "no match" case, matching Rust's requirement that a `match`
  be exhaustive). Available on `&`/`const &`/`&&` overloads.
- `is<T>()` — alias of `std::holds_alternative<T>(*this)`, matching the
  naming other Rust-inspired reloco APIs use (`is_ok`/`is_some`, etc.).
  `is<T>(pred)` — Rust `Option::is_some_and`-style overload: `true` if
  the active alternative is a `T` *and* `pred` applied to it returns
  `true`; `pred` is not invoked otherwise.
- The reloco checked/fallible/unsafe tri-tier access convention (see
  `optional.hpp`), which `std::variant` itself only offers a
  throwing/fallible pair for:
  1. **Checked (Default):** `get<T>()` behaves like `std::get<T>(*this)`,
     but uses `RELOCO_ASSERT` to trap on a mismatched alternative instead
     of throwing `std::bad_variant_access` -- reloco is exception-averse
     elsewhere, so a throwing-only checked accessor doesn't fit the rest
     of the library. Available on `&`/`const &`/`&&` overloads.
  2. **Fallible:** `try_get<T>()` returns
     `result<std::reference_wrapper<T>>` (`const T` on the `const &`
     overload) -- present and bound to the active alternative if it
     holds a `T`, `unexpected(error::not_found)` otherwise -- matching
     `optional<T>::try_value()`'s established convention for bridging a
     "maybe absent" reference-returning accessor into a `result<T>`
     pipeline. `as<T>()` is the `optional`-returning sibling of the same
     tier: like `std::get_if<T>(this)`, but bridges into
     `optional<std::reference_wrapper<T>>` instead of a raw pointer.
  3. **Unsafe:** `unsafe_get<T>()` is explicitly gated behind
     `RELOCO_UNSAFE_BUFFER_USAGE` and only checked via
     `RELOCO_DEBUG_ASSERT`, exactly like `optional<T>::unsafe_value()`.

`reloco::overloaded<Fs...>` (with its deduction guide) -- the "ad-hoc
overload set out of any number of callables" building block `match()`
itself is built on -- is also exposed standalone, for direct use with
`std::visit` on a plain `std::variant`.

```cpp
reloco::variant<int, std::string> v(42);

int doubled = v.match([](int i) { return i * 2; }, [](const std::string &s) { return (int)s.size(); });
assert(doubled == 84);

if (auto as_int = v.as<int>())
  assert(as_int.value().get() == 42);
```

`is_trivially_relocatable<variant<Ts...>>` follows every `Ts`'s own
relocatability, exactly like `is_trivially_relocatable<std::variant<Ts...>>`
(see `relocatable_std.hpp`), since `reloco::variant<Ts...>` adds no data
members over the base.

## `function_ref<R(Args...)>`

`include/reloco/function_ref.hpp`

Non-owning, zero-allocation, type-erased borrow of any callable (lambdas,
function pointers, functors, member-invocable objects), storing exactly two
pointers (a context payload and a trampoline function). It has no default
constructor and can never be null: the single converting constructor is
`RELOCO_LIFETIMEBOUND`-annotated so Clang rejects binding it to a temporary
callable (e.g. an inline lambda) that would immediately dangle.

```cpp
void call_it(reloco::function_ref<int(int)> f) { assert(f(1) == 2); }
call_it([](int x) { return x + 1; });
```

Prefer `function_ref` over `reloco::function<R(Args...)>` at a call boundary
that does not need to store the callable past the call: it never allocates
and has no ownership overhead, at the cost of the caller owning the bound
callable's lifetime for the duration of the call.

## `inplace_function<Signature, Capacity = 32>`

`include/reloco/inplace_function.hpp`

Zero-allocation, fixed-capacity, move-only callable wrapper: a deterministic
alternative to `std::function` that stores the type-erased callable entirely
inline in a `Capacity`-byte buffer. If a functor exceeds `Capacity` or its
alignment requirement, construction fails to compile via `static_assert`
rather than silently falling back to heap allocation.

```cpp
reloco::inplace_function<int(int)> f = [](int x) { return x + 1; };
assert(f(1) == 2);
```

`operator()` is the checked tier (`RELOCO_ASSERT`s if empty);
`unsafe_invoke()` is the `RELOCO_UNSAFE_BUFFER_USAGE`-gated unsafe tier that
skips the empty check. The class carries `-Wconsumed` typestate annotations
(`RELOCO_CONSUMABLE`/`RELOCO_CALLABLE_WHEN`/`RELOCO_SET_TYPESTATE`) so Clang
flags invoking a default-constructed or moved-from instance. Relocation
(move/swap) uses `is_trivially_relocatable<Functor>` to skip the
move-constructor/destructor pair for the stored callable when possible.

## `stack_allocator` / `stack_allocator_tag` / `stack_allocator_context`

`include/reloco/stack_allocator.hpp`

`allocator_traits<stack_allocator_tag>` backend implementing a bump-pointer
(arena) allocator over a single caller-owned buffer: `allocate` advances an
offset into the buffer and never frees individual blocks, only the whole
arena at once via `stack_allocator_context::reset()`. `reallocate` and
`advise` are intentionally unimplemented; `allocator_ref::can_reallocate()`/
`can_advise()` detect their absence and report `false`/
`error::unsupported_operation` automatically.

```cpp
alignas(std::max_align_t) std::byte buffer[1024];
reloco::stack_allocator arena(reloco::stack_allocator_context(buffer, sizeof(buffer)));
auto vec = reloco::vector<int>::try_allocate(arena.ref());
arena.context()->reset(); // Reclaim the entire arena at once.
```

Use `stack_allocator` for a scoped or per-frame arena backing any reloco
container that takes an `allocator_ref` (`vector<T>`, `basic_string`,
`flat_set<T>`, ...), instead of `heap_allocator_tag`, when allocation must
be deterministic and bounded to a caller-owned buffer.

## `unique_ptr<T>`

`include/reloco/unique_ptr.hpp`

Move-only, allocator-backed smart pointer. `try_allocate(allocator_ref,
Args...)`/`try_create(Args...)` allocate the box and then resolve the best
available construction strategy for `T` via
`construction_helpers::try_construct` (see
[Fallible construction](fallible-construction.md)) — so any `T` that
implements `try_construct`/`try_allocate`/`try_create`, or is simply
nothrow-constructible from `Args...`, works with no extra glue code.

```cpp
auto ptr = reloco::unique_ptr<widget>::try_create(arg1, arg2);
if (ptr)
  (*ptr)->do_something();
```

`operator*`/`operator->`/`get()` are checked (`RELOCO_ASSERT` on null);
`unsafe_get()` skips that check and is `RELOCO_UNSAFE_BUFFER_USAGE`-gated.
No copy constructor: use `T`'s own `try_clone`/`try_clone_at` explicitly for
a deep copy. `reloco::is_trivially_relocatable<unique_ptr<T>>` is always
`true`, regardless of `T` (see [Trivial relocation](relocatable.md)).

## `shared_ptr<T>` / `weak_ptr<T>` / `enable_shared_from_this<T>`

`include/reloco/shared_ptr.hpp`

Reference-counted, allocator-backed smart pointer. `shared_ptr<T>` is
copyable (sharing ownership, incrementing a refcount) and movable;
`weak_ptr<T>` observes without extending the object's lifetime, and
`lock()`s back into a `shared_ptr<T>` (or `error::pointer_expired` if the
object is already gone). Object construction goes through
`construction_helpers::try_construct`, exactly like `unique_ptr`, so the
same `try_construct`/`try_allocate`/`try_create`/nothrow-constructible tiers
apply.

Two allocation layouts are available, mirroring `std::allocate_shared` vs.
`std::make_shared`:

```cpp
// Single allocation (object + control block together); recommended default.
auto ptr = reloco::try_create_combined_shared<widget>(arg1, arg2);

// Object and control block allocated separately: the object's storage is
// freed as soon as the last shared_ptr releases it, independently of any
// surviving weak_ptr (which only keeps the small control block alive).
auto ptr2 = reloco::try_create_shared<widget>(arg1, arg2);
```

`try_allocate_shared`/`try_allocate_combined_shared` take an explicit
`allocator_ref`; `try_create_shared`/`try_create_combined_shared` use
`default_allocator()`. `static_pointer_cast`/`dynamic_pointer_cast`/
`const_pointer_cast`/`reinterpret_pointer_cast` mirror the `std::shared_ptr`
casts. `operator*`/`operator->`/`get()` are checked; `unsafe_get()` skips
the null check and is `RELOCO_UNSAFE_BUFFER_USAGE`-gated; `try_get()`
returns `result<T *>`. `reloco::is_trivially_relocatable<shared_ptr<T>>`
and `<weak_ptr<T>>` are always `true`, regardless of `T` (see
[Trivial relocation](relocatable.md)).

## `rc<T>` / `weak_rc<T>` / `enable_rc_from_this<T>`

`include/reloco/rc.hpp`

Single-threaded reference-counted, allocator-backed smart pointer,
matching Rust's `std::rc::Rc<T>`/`std::rc::Weak<T>`. Structurally identical
to `shared_ptr<T>`/`weak_ptr<T>` -- same two allocation layouts
(`try_allocate_rc`/`try_create_rc` for a separate control block,
`try_allocate_combined_rc`/`try_create_combined_rc` for a single
allocation), same checked/fallible/unsafe access tiers, same
`static_pointer_cast`/`dynamic_pointer_cast`/`const_pointer_cast`/
`reinterpret_pointer_cast` overloads, same `enable_rc_from_this<T>`/
`rc_from_this()` self-borrow idiom -- except its refcounts are plain
`std::size_t` increments/decrements instead of `std::atomic<std::size_t>`
operations, which is unsound if an `rc<T>`/`weak_rc<T>` is ever shared
across threads but noticeably cheaper when it never is.

```cpp
auto ptr = reloco::try_create_combined_rc<widget>(arg1, arg2);
reloco::rc<widget> shared = ptr.value();
reloco::weak_rc<widget> observer = shared;
```

Pick `rc<T>` over `shared_ptr<T>` purely for performance, whenever the
shared object only ever lives on one thread; switch to `shared_ptr<T>` the
moment it might cross a thread boundary. The two are unrelated types with
independent control blocks and cannot share ownership of the same object.

## `bytes` / `bytes_mut`

`include/reloco/bytes.hpp`

An immutable, reference-counted, cheaply-cloneable byte buffer (`bytes`)
and its growable, exclusively-owned mutable counterpart (`bytes_mut`),
matching the shape of Rust's `bytes` crate `Bytes`/`BytesMut`. Both are
allocator-backed and hold no self-references, so `is_trivially_relocatable`
is `true` for each.

`bytes_mut` is a move-only, `std::byte` buffer that grows the same way
`vector<T>` does for trivially relocatable `T` -- prefer
`allocator_ref::expand_in_place`, then `allocator_ref::reallocate`, and
only fall back to a fresh allocation + `memcpy` + deallocate:

```cpp
auto buf = reloco::bytes_mut::try_create(); // or try_allocate(alloc, initial_cap)
if (buf) {
  buf->try_put_u8(0xFF);
  buf->try_put_u32_be(0xDEADBEEF);
  buf->try_put_slice(reloco::span<const std::byte>(payload, payload_len));
}
```

`try_push`, `try_put_slice`/`try_extend_from_slice`, and the endian-aware
`try_put_u8`/`try_put_u16_le`/`try_put_u16_be`/`try_put_u32_le`/
`try_put_u32_be`/`try_put_u64_le`/`try_put_u64_be` (Rust `BufMut`-flavored)
all grow the buffer on demand via `try_reserve`, failing with
`result<void>` on allocation failure rather than throwing. `data()`/
`as_span()` expose the current contents; `clear()` resets the length
without releasing the allocation.

`bytes_mut::try_freeze() &&` consumes the buffer and converts it into an
immutable `bytes` without copying the byte payload: it moves the existing
heap pointer into a freshly allocated `rc<detail::bytes_storage>` control
block (that small control-block allocation is itself unavoidable -- it is
what makes the buffer shareable -- but the potentially large payload is
never copied, unlike a plain `vector<T>`-to-owned-copy conversion):

```cpp
reloco::result<reloco::bytes> frozen = std::move(*buf).try_freeze();
```

`bytes` clones by bumping an `rc<T>` refcount (never copying the payload),
and slices are the same O(1) operation, sharing the same backing
allocation while only adjusting a local pointer/length pair:

```cpp
reloco::bytes clone = *frozen;              // refcount bump, no copy
reloco::bytes view = frozen->slice(1, 4);    // shares storage
reloco::bytes front = frozen->split_to(2);   // Rust Bytes::split_to
reloco::bytes rest = frozen->split_off(2);   // Rust Bytes::split_off
```

`try_copy_from(span<const std::byte>, allocator_ref)` allocates a fresh
`bytes` and copies the given span into it (the usual entry point when data
does not already live in a `bytes_mut`). Checked `operator[]` and fallible
`try_at`/`try_slice`/`try_split_to`/`try_split_off` round out bounds-safe
access; `operator==`/`operator!=` compare contents, not identity.

Unlike the real `bytes` crate, `bytes_mut` does not support
`split_to`/`split_off`/`unsplit` (those would require the same shared,
refcounted storage `bytes` uses, turning every mutation into an
alias-check), and `bytes` has no `Buf`-style stateful advancing-cursor
reader -- only the `BufMut`-style `try_put_*` writers on `bytes_mut`. Both
gaps can be layered on top of `as_span()` if/when needed.

## `function<R(Args...)>`

`include/reloco/function.hpp`

Type-erased, allocator-backed callable wrapper (reloco's `std::function`
counterpart). `try_allocate(allocator_ref, F)`/`try_create(F)` wrap any
callable convertible to `R(Args...)`, choosing the cheapest storage tier at
construction time: a bare function pointer (or captureless lambda) stored
directly with no allocation, a small-object-optimization inline buffer
(`function<R(Args...)>::soo_capacity` bytes, alignment up to
`alignof(std::max_align_t)`), or a single heap allocation for anything
larger.

```cpp
auto fn = reloco::function<int(int)>::try_create([captured](int v) noexcept {
  return captured + v;
});
if (fn)
  int result = (*fn)(41);
```

Move-only: `try_clone()` performs an explicit fallible deep copy, failing
with `error::unsupported_operation` if the captured callable is not
`std::is_nothrow_copy_constructible_v` (or if the function is empty).
`operator()` asserts non-empty; `try_call(Args...)` is the checked
alternative, failing with `error::container_empty` instead -- and if `R` is
itself a `result<U>`, `try_call`'s own result is flattened rather than
double-wrapped. `reloco::is_trivially_relocatable<function<R(Args...)>>` is
always `false`: the captured callable may live inline in the SOO buffer, so
relocating the wrapper by copying bytes is only as safe as the (erased)
captured type itself (see [Trivial relocation](relocatable.md)).

## `type_id` / `type_id_of<T>()`

`include/reloco/type_id.hpp`

Opaque, process-wide type identity established without RTTI, matching
Rust's `std::any::TypeId`. `type_id::of<T>()` (or the free-function alias
`type_id_of<T>()`) returns the address of a per-instantiation static data
member as `T`'s identity -- no `typeid`/`<typeinfo>`, no mangled-name
string, no runtime registration. `T` is never decayed for you (unlike
`any::is<T>()`), so `type_id::of<int>()` and `type_id::of<const int>()` are
distinct identities.

```cpp
auto id = reloco::type_id::of<int>();
if (id == reloco::type_id::of<int>())
  // ...
```

A default-constructed `type_id` is a distinct "no type" sentinel
(`operator bool()` is `false`), never equal to `type_id::of<T>()` for any
`T` -- what a type-erased container like `any` returns from its own
`type_id()` accessor when empty. Unlike Rust's own `TypeId` (deliberately
`Eq`/`Hash` only, no `Ord`), reloco also provides
`operator<`/`operator<=`/`operator>`/`operator>=` (via `std::less<const
void *>`, so ordering is at least well-defined and total within a process)
so a `type_id` can be used directly as a `flat_set`/`flat_map` key;
`std::hash<reloco::type_id>` is specialized too, for
`std::unordered_map`/`unordered_set` interop.

`type_id::name()` returns an optional, game-engine-style (Unreal/EnTT
convention) human-readable debug name for `T`, or `nullptr` if none is
registered -- registering a name is always opt-in, never automatic (no
`__PRETTY_FUNCTION__`/`__FUNCSIG__` string-mangling trick), and never
affects equality/ordering/hashing, which are always based solely on the
identity, not the name:

```cpp
reloco::type_id::of<int>().name();          // "int"
reloco::type_id::of<my_widget>().name();    // nullptr, unless registered
```

Register a name for a type with `RELOCO_TYPE_ID_NAME(T, "name")` (usable
at namespace scope, inside or outside `namespace reloco`; does not end in
`;` -- add one at the call site):

```cpp
RELOCO_TYPE_ID_NAME(my_widget, "my_widget");
```

Names are already registered out of the box for every fundamental type
(`bool`, the integer/character types, `float`/`double`/`long double`,
`std::nullptr_t`), for `reloco::type_id` itself, and for a handful of
"classic" reloco types: `reloco::error`, `reloco::ordering`,
`reloco::string`/`wstring`, `reloco::string_view`/`wstring_view`, and
`reloco::sso_string`/`wsso_string` (each registered alongside its own
type's definition, in its own header, not centralized in `type_id.hpp`,
matching how this codebase's `std::hash<reloco::X>` specializations are
likewise defined alongside each `X`).

The identity trick relies on every translation unit that instantiates
`type_id_tag<T>` for the same `T` sharing one symbol; a consumer
building a shared library with `-fvisibility=hidden` can otherwise end up
with a separate, non-merged copy per shared object, silently breaking
`type_id`/`any::is<T>()` equality for a `T` shared across that boundary.
Define `RELOCO_ENABLE_EXPORT` (see `reloco_config.hpp`) to opt in to
keeping that symbol at default visibility regardless of the ambient
`-fvisibility` setting; it is opt-in, and a no-op on backends without an
equivalent attribute (e.g. MSVC), since it only matters when `type_id`/
`any` values for a shared `T` actually cross a shared-object boundary.
This only takes effect for a `T` that itself has default visibility (a
fundamental type like `int` always qualifies; an ordinary consumer class
under `-fvisibility=hidden` needs its own explicit default-visibility
annotation too), since a template instantiation's visibility is the
minimum of the template's own and each argument's.

## `any`

`include/reloco/any.hpp`

Type-erased, allocator-backed single-value container (reloco's `std::any`
counterpart), with no dependency on RTTI: type identity is established via
`reloco::type_id` (see `type_id.hpp` above) instead of
`typeid`/`<typeinfo>`. `try_allocate(allocator_ref, T)`/`try_create(T)` wrap
a copy/move of any decayed, constructible type, choosing the cheapest
storage tier at construction time: a small-object-optimization inline
buffer (`any::soo_capacity` bytes, alignment up to
`alignof(std::max_align_t)`), or a single heap allocation for anything
larger. `try_allocate(allocator_ref, std::in_place_type<T>, args...)`/
`try_create(std::in_place_type<T>, args...)` construct `T` in place instead,
avoiding the extra move/copy.

```cpp
auto value = reloco::any::try_create(42);
if (value && value->is<int>())
  int n = value->get<int>();
```

Move-only: `try_clone()` performs an explicit fallible deep copy, dispatching
through `construction_helpers::try_clone_at` (see `construction_helpers.hpp`)
-- the held type's own `try_clone`/`try_allocate`/`try_create` if it
implements one, falling back to plain nothrow copy-construction -- and
failing with `error::unsupported_operation` if none of those apply (or if
the instance is empty). `is<T>()` reports whether the instance is non-empty
and holds exactly `std::decay_t<T>`. Access follows the usual tri-tier
convention: `get<T>()` asserts non-empty and type-matching; `try_get<T>()` is
the checked alternative, returning `result<std::reference_wrapper<T>>`
(failing with `error::container_empty` if empty, or `error::invalid_argument`
on a type mismatch); `unsafe_get<T>()` skips the check entirely (only a
`RELOCO_DEBUG_ASSERT`). `reloco::is_trivially_relocatable<any>` is always
`false`: the held value may live inline in the SOO buffer, so relocating
the wrapper by copying bytes is only as safe as the (erased) held type
itself (see [Trivial relocation](relocatable.md)).

A parallel, Rust-flavored surface mirrors Rust's `std::any::Any` trait on
top of the same dispatch: `type_id()` (Rust's `Any::type_id`) returns the
held value's `reloco::type_id` (the "no type" sentinel if empty) --
`type_id().name()` additionally gives an optional debug name if one was
registered for the held type (see [`type_id`](#type_id--type_id_oft)
above), `nullptr` otherwise; `downcast_ref<T>()`/`downcast_mut<T>()` (Rust's `Any::downcast_ref`/
`downcast_mut`) return a nullable `const T *`/`T *` instead of asserting --
reloco's usual analog of Rust's `Option<&T>`/`Option<&mut T>` for a
checked-but-non-asserting accessor (compare `flat_map::find`); and
`downcast<T>() &&` (Rust's `Any::downcast`, consuming) moves the held `T`
out into a `result<T>` (`error::container_empty`/`error::invalid_argument`
on failure) rather than Rust's `Result<Box<T>, Box<dyn Any>>`, since every
fallible reloco operation returns `reloco::result<T>` and handing back the
original, differently-typed `any` on failure would require a second error
type.

## `collection_view<T>` / `mutable_collection_view<T>` / `collection_view_traits<Container>`

`include/reloco/collection_view.hpp`

Type-erased, non-owning views over an *adapted* sequence container
(`reloco::span<T>`, `reloco::array<T, N>`, `std::array<T, N>`,
`std::span<T>`, ...), matching `allocator_ref`'s customization-point shape:
a two-word handle (an untyped context pointer plus a `const vtable *`), no
virtual base class, no RTTI, no allocation of its own. There is
deliberately no structural detection ("any type that happens to have
`size()`/`begin()`/`end()`"): a container type is only usable through these
views once someone specializes `collection_view_traits<Container>` for it,
exactly like `allocator_ref` requires an explicit `allocator_traits<Tag>`
specialization. Every required accessor must be supplied explicitly by the
adapter author; optional capabilities (contiguous `data()` access, mutable
access) are switched on by required `static constexpr bool` flags
(`has_data`, `is_mutable`) on the traits specialization, mirroring how
`allocator_ref::can_expand_in_place()`/`can_reallocate()`/`can_advise()`
report an optional backend operation.

`collection_view<T>` is *unconditionally read-only*: every accessor
(`size`/`empty`/`at`/`try_at`/`unsafe_at`/`data`/`try_data`/`unsafe_data`/
`for_each`) returns or visits `const T &`/`const T *`, regardless of
whether the bound container or `T` itself is `const`-qualified.
`mutable_collection_view<T>`, derived from `collection_view<T>`, is the
*only* way to obtain write access: its converting constructor additionally
requires the adapter's `is_mutable` flag and a non-`const` lvalue container,
and it adds `T &`/`T *`-returning overloads of `at`/`try_at`/`unsafe_at`/
`data`/`try_data`/`unsafe_data`/`for_each` alongside the read-only ones it
inherits.

```cpp
reloco::array<int, 3> a{1, 2, 3};

reloco::collection_view<int> view(a);       // read-only
int total = 0;
view.for_each([&](const int &v) { total += v; });

reloco::mutable_collection_view<int> mview(a); // requires a non-const array
mview.at(0) = 100;                             // a[0] == 100
```

Both converting constructors are `explicit`: binding a container is always
a deliberate, visible step at the call site, never an implicit conversion.
Neither view allocates, copies, or owns the underlying container: like
`span`/`allocator_ref`, the referenced container must outlive every view
built from it.

Built-in `collection_view_traits` adapters ship for `reloco::span<T>`,
`reloco::array<T, N>`, `std::array<T, N>`, and `std::span<T>` (the last
gated behind `RELOCO_HAS_STD_SPAN`). No adapters for owning/node-based
containers (`std::vector`, `std::list`, `std::map`, ...) are provided yet;
see the `collection_view_traits` primary template's documentation for the
full contract required to add one.

## `mutable_container_ref<T, Key = void>` / `container_ref_traits<Container>`

`include/reloco/container_ref.hpp` (core, no built-in adapters),
`include/reloco/container_ref_std.hpp` (opt-in `std::vector`/`std::map`
adapters)

Type-erased, non-owning handle allowing *structural* mutation of an adapted
container (growing, inserting, erasing, clearing) as well as
indexed/keyed access to its existing elements -- unlike
`mutable_collection_view`, which only mutates elements already present and
never resizes the container. Same customization-point shape as
`allocator_ref`/`collection_view`: a container is only usable here once
someone specializes `container_ref_traits<Container>` for it; there is no
structural detection.

`mutable_container_ref<T, Key = void>` is an alias picking between two
implementations based on whether `Key` is `void`:

- `mutable_container_ref<T>` (`Key = void`): a *sequence* container handle
  (`container_ref_traits<Container>::is_associative == false`). Offers
  `try_push_back`/`try_push_front`/`try_insert_at(index,
  value)`/`try_erase_at(index)`/`clear()`, `for_each(Fn)` (implemented
  generically over `at`/`size` -- no separate trait hook needed, since this
  contract targets index-addressable "vector-like" containers), and
  `at`/`try_at`/`unsafe_at(index)` for existing elements.
- `mutable_container_ref<T, Key>` (`Key` other than `void`): an
  *associative* container handle (`is_associative == true`). Offers
  `try_insert_at(key, value)`/`try_erase(key)`/`clear()`, `for_each(Fn)`
  (via a required trait-level iteration hook, since there is no index
  concept to fall back on), and `at`/`try_at`/`unsafe_at(key)` for existing
  entries.

```cpp
std::vector<int> v{1, 2, 3};
reloco::mutable_container_ref<int> ref(v);
ref.try_push_back(4);          // v == {1, 2, 3, 4}
ref.try_insert_at(0, 0);       // v == {0, 1, 2, 3, 4}
ref.at(0) = 100;                // v == {100, 1, 2, 3, 4}
```

Every fallible trait function the adapted container cannot support (e.g. a
container with no efficient front-insertion, or inserting a key that
already exists) must still be implemented by the traits specialization --
it simply reports that through its own `result<void>` (e.g.
`unexpected(error::unsupported_operation)`, `error::already_exists`,
`error::not_found`) rather than being gated by a separate capability flag.

`container_ref_std.hpp` is deliberately a separate header from
`container_ref.hpp`: its `std::vector<T>`/`std::map<Key, Value>` adapters
wrap standard-library operations that allocate and may throw, which the
rest of reloco avoids; only a caller who explicitly `#include`s this header
opts into that behavior. Every adapter function in it wraps the
underlying call in `try`/`catch (...)`, converting any thrown exception
into `unexpected(error::allocation_failed)` -- except under
`-fno-exceptions` (`RELOCO_HAS_EXCEPTIONS == 0`), where the call is made
unguarded instead, since `try`/`catch` isn't valid syntax in that mode.

## `sink` / `Display<T>` / `Debug<T>`

`include/reloco/fmt.hpp`

A type-erased, zero-allocation output sink (`sink`: opaque `ctx` pointer +
stateless `write`/`put`/`push_back` callback, deliberately laid out to
match microfmt's own `sink` over the same `reloco::string_view`) plus two
opt-in customization points, matching Rust's `std::fmt::Display` and
`std::fmt::Debug`:

- `Display<T>`: how `T` renders itself for user-facing display.
- `Debug<T>`: how `T` renders itself for debugging/diagnostics.

Same customization-point shape as `collection_view_traits<Container>`:
both templates are intentionally left undefined, and a specialization must
supply `static void format(const T &value, const reloco::sink &out) noexcept;`.
`has_display_v<T>`/`has_debug_v<T>` SFINAE-detect whether a valid
specialization exists.

reloco itself never specializes `Display`/`Debug` for any of its own types
and this header contains no formatting code of any kind -- adapting a type
(and writing the formatting logic) is left entirely to the consumer, e.g. a
higher-level formatting library bridging its own customization points to
these.

```cpp
struct point { int x, y; };

template <> struct reloco::Display<point> {
  static void format(const point &p, const reloco::sink &out) noexcept {
    out.write("(");
    out.put(static_cast<char>('0' + p.x));
    out.write(", ");
    out.put(static_cast<char>('0' + p.y));
    out.write(")");
  }
};

static_assert(reloco::has_display_v<point>);
static_assert(!reloco::has_debug_v<point>);
```

## `value_ptr<T>` / `value_ref<T>`

`include/reloco/value_ptr.hpp`, `include/reloco/value_ref.hpp`

Non-owning pointer/reference wrappers that document a borrowed relationship
without taking ownership, and reject binding to prvalue temporaries at
compile time so a borrow can never quietly outlive its source. `value_ptr`
is nullable (`operator bool`, defaults to null); `value_ref` is always
non-null and convertible to/from `value_ptr`. See
[Lifetime safety](lifetime-safety.md).

## `checked_value<T>`

`include/reloco/checked_value.hpp`

Move-only wrapper giving Rust-like use-after-move checking to any nothrow
move-constructible `T` (plus a `T *` partial specialization with an
additional null check on dereference). A moved-from instance is poisoned:
further access asserts and traps, on every compiler; under Clang, `
-Wconsumed` additionally flags use-after-move at compile time. See
[Lifetime safety](lifetime-safety.md). `reloco::is_trivially_relocatable<
checked_value<T>>` mirrors `T`'s own relocatability; `checked_value<T *>` is
always relocatable (see [Trivial relocation](relocatable.md)).

## `cell<T>` / `ref_cell<T>`

`include/reloco/cell.hpp`

Interior-mutability wrappers matching Rust's `std::cell::Cell<T>`/
`std::cell::RefCell<T>`: both allow mutating the wrapped value through a
shared (`const`) reference, deliberately opting out of the ordinary
`const`/non-`const` reference rules every other reloco container enforces.
Both are single-threaded only (`!Sync`, in Rust's terms); see `mutex.hpp`
for a thread-safe alternative.

`cell<T>` has no runtime bookkeeping: `set(T)`/`replace(T)` only ever move
the value in and out, so they work for any `T`, callable through a `const
cell<T>&`:

```cpp
const reloco::cell<int> c(1);
c.set(2);                 // mutation through a const reference
const int old = c.replace(3);
assert(old == 2 && c.get() == 3);
```

`get()` additionally requires a `noexcept` copy constructor (Rust's `T:
Copy` bound), since it is the only accessor that hands back a copy rather
than moving; `take()` requires `T` to be default constructible (Rust's `T:
Default` bound on `Cell::take`) and replaces the value with `T()`,
returning the previous one.

`ref_cell<T>` instead borrows a reference to `T` in place, tracking the
borrow state at runtime: at most one exclusive borrow (`mut_guard`), or any
number of concurrent shared borrows (`ref_guard`), may be outstanding at
once. `try_borrow()`/`try_borrow_mut()` are the fallible tier, returning
`result<ref_guard>`/`result<mut_guard>` with `error::busy` on conflict;
`borrow()`/`borrow_mut()` are the checked tier that `RELOCO_ASSERT` instead,
matching Rust's own panicking `RefCell::borrow()`/`borrow_mut()`. Both guard
types are move-only RAII types that release their borrow on destruction,
and are `RELOCO_CONSUMABLE`-tagged so Clang's `-Wconsumed` flags
use-after-move of a guard at compile time, same as `checked_value<T>`.
`get_mut()` bypasses borrow tracking entirely for callers that already hold
an exclusive `ref_cell&` (Rust's `RefCell::get_mut()`, which borrows `&mut
self` at compile time instead).

```cpp
reloco::ref_cell<std::string> rc(std::string("hello"));
{
  auto shared = rc.try_borrow();
  assert(shared.has_value() && **shared == "hello");
  auto conflict = rc.try_borrow_mut();
  assert(!conflict.has_value() && conflict.error() == reloco::error::busy);
}
auto exclusive = rc.borrow_mut(); // checked tier: traps instead of failing
*exclusive = "world";
```

## `cow<T>` / `cow_traits<T>`

`include/reloco/cow.hpp`

Clone-on-write wrapper matching Rust's `std::borrow::Cow<'a, T>`: holds
either a borrowed reference to a `T` it doesn't own, or an owned `T` it
does, and defers the (fallible) clone until the borrowed case is actually
mutated. Move-only -- copying a `cow<T>` would either have to silently
clone (fallible, surprising for a copy constructor) or silently alias
(unsound), so `try_clone()` provides an explicit fallible deep copy
instead.

Which state a `cow<T>` starts in is selected by overload resolution, not a
tag type:

```cpp
std::string original = "hello";
reloco::cow<std::string> borrowed(original);            // const T&: borrowed
reloco::cow<std::string> owned(std::string("owned"));   // T&&: owned

assert(borrowed.is_borrowed());
assert(owned.is_owned());

auto &mutated = borrowed.to_mut(); // clones on first mutation, then owned
mutated += " world";
assert(borrowed.is_owned());
```

`get()`/`operator*`/`operator->` read through either state without
cloning. `to_mut()` clones-if-borrowed and returns a mutable reference,
transitioning `*this` to owned. `into_owned()` is rvalue-qualified and
consumes `*this`, returning a `T` by value (cloning only if still
borrowed). `try_clone()` performs an explicit deep copy regardless of
state.

The clone step itself is a customization point: `cow_traits<T>::try_clone`
defaults to forwarding to `construction_helpers::try_clone<T>` (the same
tiered `try_clone(alloc)`/`try_clone()`/copy-construct dispatch used
elsewhere in reloco), but can be specialized per-`T` to override how (or
whether) a borrowed value is cloned into an owned one:

```cpp
template <>
struct reloco::cow_traits<my_type> {
  static reloco::result<my_type> try_clone(reloco::allocator_ref alloc,
                                            const my_type &source) {
    return my_custom_clone(alloc, source);
  }
};
```

## `non_zero<T>`

`include/reloco/non_zero.hpp`

Rust `NonZeroU8`/`NonZeroI32`/... equivalent: wraps an integral `T` that is
statically known to never be `0`. The invariant is checked once, at
construction, rather than re-checked on every use, so it also documents
"this parameter is never zero" directly in a function signature. Follows
reloco's tri-tier construction convention: `try_create(value)` returns
`result<non_zero<T>>`, failing with `error::invalid_argument` for a `0`
argument; `unsafe_create(value)` is a debug-only-checked escape hatch for a
caller that has already proven the value is non-zero. `get()` and the
implicit conversion to `T` are always sound and `constexpr`.

```cpp
auto nz = reloco::non_zero<int>::try_create(4);
assert(nz.has_value());
int quotient = 100 / nz.value(); // implicit conversion to T
assert(!reloco::non_zero<int>::try_create(0).has_value());
```

## `int_ops.hpp`

`include/reloco/int_ops.hpp`

Free functions porting Rust's four explicit integer-arithmetic families to
`result<T>`/plain-`T` return types, plus a generic range-checked numeric
cast. No new class -- these mirror Rust's inherent integer methods
(`i32::checked_add`, `checked_div`, ...) directly, the same way
`wrapping<T>`/`saturating<T>` (below) build on top of them for
operator-overloaded newtypes:

- `checked_add/sub/mul/div/rem(a, b)` return `result<T>`, failing with
  `error::integer_overflow` (or `error::division_by_zero` for `div`/`rem`
  with a zero divisor) instead of invoking undefined behavior (signed) or
  silently wrapping (unsigned).
- `checked_neg(a)` returns `result<T>`, failing for signed
  `numeric_limits<T>::min()` and for any nonzero unsigned `T`.
- `checked_abs(a)` (signed `T` only) returns `result<T>`, failing for
  `numeric_limits<T>::min()`.
- `wrapping_add/sub/mul(a, b)` always return a `T`, with well-defined
  modulo-2^N wraparound (applied to signed types via a two's-complement
  bit-pattern reinterpretation, matching Rust's `wrapping_*`).
- `saturating_add/sub/mul(a, b)` always return a `T`, clamped to
  `[numeric_limits<T>::min(), numeric_limits<T>::max()]`.
- `overflowing_add/sub/mul(a, b)` always return an `overflowing_result<T>`
  (a `{T value; bool overflowed;}` pair).
- `checked_cast<To>(from)` converts an integral value to a different
  integral type `To`, failing with `error::integer_overflow` if it doesn't
  fit, matching Rust's `TryFrom`/`TryInto` for integers. Correct across
  every signed/unsigned and differing-width combination -- comparisons are
  widened to `intmax_t`/`uintmax_t` rather than narrowed into `From`/`To`
  directly, which would itself be able to overflow.

There is deliberately no `checked_div`/`checked_rem`/`checked_cast`
*operator* overload anywhere in reloco -- only `wrapping<T>`/`saturating<T>`
overload `+`/`-`/`*` (see below), since division and casting have no
sensible implicit wrap/saturate fallback.

```cpp
auto sum = reloco::checked_add<int32_t>(a, b);
if (!sum.has_value()) { /* handle reloco::error::integer_overflow */ }

auto q = reloco::checked_div<int>(10, 0);
assert(q.error() == reloco::error::division_by_zero);

auto narrowed = reloco::checked_cast<int8_t>(int32_t{300});
assert(!narrowed.has_value()); // doesn't fit in int8_t
```

Note: like `wrapping<T>`/`saturating<T>` below, every function here is
marked `constexpr` (always legal), but a call that goes through
`result<T>`'s `expected<T, error>` internals can't actually be *evaluated*
as a constant expression under C++17 specifically (`result<T>` isn't a
C++17 literal type) -- only C++20 and later can `static_assert` on the
outcome of e.g. `checked_add`.

## `atomic_ops.hpp`

`include/reloco/atomic_ops.hpp`

Free functions, in the `reloco::atomic` namespace, filling the handful of
gaps between C++17 `std::atomic<T>` and Rust's
`std::sync::atomic::Atomic*` API surface. There is deliberately no
`reloco::atomic<T>` wrapper type: Rust's `load`/`store`/`swap`/
`compare_exchange(_weak)`/`fetch_add`/`fetch_sub`/`fetch_and`/`fetch_or`/
`fetch_xor` already exist on `std::atomic<T>` with equivalent semantics, so
wrapping them again would add naming differences only, not capability --
per this project's own rule, a Rust API that doesn't extend anything over
what the standard library already provides isn't ported. Only the two
genuine gaps are ported, as free functions taking a `std::atomic<T> &`:

- `atomic::fetch_max(a, val, order)`/`atomic::fetch_min(a, val, order)`
  atomically replace `*a` with `std::max(*a, val)`/`std::min(*a, val)`,
  returning the *previous* value, matching Rust's `AtomicT::fetch_max`/
  `fetch_min`. `std::atomic<T>::fetch_max`/`fetch_min` only became
  standard in C++26; on an older standard library these fall back to a
  portable `load`+`compare_exchange_weak` retry loop (and simply forward
  to the native member function when it's already available). Defined for
  integral and pointer `T`, exactly like `std::atomic<T>::fetch_add`.
- `atomic::fetch_update(a, success_order, failure_order, f)` repeatedly
  reads `*a`, calls `f(current)` -- a callable returning `optional<T>`
  (see `optional.hpp`) -- and attempts to `compare_exchange_weak` the
  result in, retrying with the freshly observed value on a CAS failure.
  It stops immediately, without storing, if `f` returns an empty
  `optional<T>` ("give up"), and returns `expected<T, T>`: the value
  immediately before the successful update on success, or the last value
  `f` was given when it gave up, as the "error" -- exactly matching Rust's
  `AtomicT::fetch_update(set_order, fetch_order, f) -> Result<T, T>`
  (`f: FnMut(T) -> Option<T>`). `std::atomic<T>` has no equivalent
  CAS-loop convenience at any C++ version, so this one is a genuine
  capability gap rather than a naming difference. Two convenience
  overloads exist: a 3-argument one taking a single `order` used for both
  the success and (downgraded, where required) failure order, and a
  2-argument one defaulting to `std::memory_order_seq_cst`.

```cpp
std::atomic<int> counter{5};
int previous = reloco::atomic::fetch_max(counter, 10); // previous == 5, counter == 10

auto result = reloco::atomic::fetch_update(counter, [](int current) -> reloco::optional<int> {
  if (current >= 20)
    return reloco::optional<int>(); // give up, don't store
  return reloco::optional<int>(current + 1);
});
if (result)
  use(result.value()); // value immediately before the update
else
  use(result.error()); // last value `f` saw before giving up
```

## `wrapping<T>` / `saturating<T>`

`include/reloco/wrapping.hpp`, `include/reloco/saturating.hpp`

Rust `std::num::Wrapping<T>`/`std::num::Saturating<T>` equivalents: integral
newtypes whose `+`/`-`/`*`, unary `-`, and `++`/`--` operators always
resolve overflow a specific way, instead of relying on the caller
remembering to call the right free function from `int_ops.hpp` at every
arithmetic expression:

- `wrapping<T>` always wraps modulo-2^N (`wrapping_add`/`wrapping_sub`/
  `wrapping_mul`), exactly like plain unsigned arithmetic, but also for a
  signed `T` (avoiding the undefined behavior plain `T` arithmetic would
  invoke on signed overflow).
- `saturating<T>` always clamps to `[numeric_limits<T>::min(),
  numeric_limits<T>::max()]` (`saturating_add`/`saturating_sub`/
  `saturating_mul`) instead of overflowing.

Both get `get()` and an implicit conversion to `T` (so they compare/print
like a plain integer), a `std::hash` specialization, and are usable in a
`constexpr` context for values within range (an overflowing/saturating
operation internally goes through `checked_add`/`checked_sub`/
`checked_mul`'s `result<T>`, which isn't a literal type under C++17, so
only the non-overflowing, `get()`-only paths are guaranteed `constexpr`
under C++17 specifically). Neither type overloads `/`/`%` (division by
zero has no wrapping/saturating equivalent to fall back to) — use
`int_ops.hpp`'s free functions directly for division.

```cpp
reloco::wrapping<uint8_t> counter(250);
counter += reloco::wrapping<uint8_t>(10);
assert(counter.get() == 4); // wrapped, not UB or clamped

reloco::saturating<uint8_t> health(200);
health -= reloco::saturating<uint8_t>(255);
assert(health.get() == 0); // clamped to the type's minimum
```

## `checked<T>`

`include/reloco/checked.hpp`

A third integral newtype alongside `wrapping<T>`/`saturating<T>`, for
arithmetic that has no defined "always succeeds" answer worth baking into
an operator overload. `checked<T>` wraps a `T` and exposes
`try_add`/`try_sub`/`try_mul`/`try_div`/`try_rem`/`try_neg`/`try_abs`
(mirroring `int_ops.hpp`'s `checked_*` free functions), each returning
`result<checked<T>>` instead of a plain `checked<T>` -- deliberately named
methods rather than operator overloads, so a caller can't silently drop
the failure the way `a + b` discarding a `result<T>` return value would
(reloco's `[[nodiscard]]` on `expected<T, E>` still catches that mistake,
but a named `try_*` method reads as fallible at the call site the same way
every other `try_*` operation in reloco does). `try_div`/`try_rem` fail
with `error::division_by_zero` for a zero divisor; `try_abs` is signed-`T`
only, matching Rust (which has no `checked_abs` for unsigned integers).

```cpp
reloco::checked<int32_t> a(std::numeric_limits<int32_t>::max());
auto sum = a.try_add(reloco::checked<int32_t>(1));
assert(!sum.has_value());
assert(sum.error() == reloco::error::integer_overflow);

auto chained = reloco::checked<int>(10).try_div(reloco::checked<int>(2)).and_then(
    [](reloco::checked<int> half) { return half.try_mul(reloco::checked<int>(3)); });
assert(chained.value().get() == 15);
```

## `fixed_point<Rep, FracBits>`

`include/reloco/fixed_point.hpp`

A `Q(bits(Rep)-FracBits).FracBits` binary fixed-point number -- for
fractional quantities (decay factors, ratios, load averages, ...) on
freestanding/no-FPU targets that can never touch floating point, exactly
the same motivation `duration.hpp`'s own docs give for avoiding
`std::chrono::duration`'s floating-point-period conversions. `Rep` is a
plain integral storage type (or, once a native integer isn't wide enough,
a `fixed_int<N, Signed>` -- see `fixed_int.hpp` below); `FracBits` is how
many of `Rep`'s low bits represent the fraction -- `fixed_point<std::uint32_t,
11>` matches the classic Unix/Linux `calc_load()` `FIXED_1`/`FSHIFT`
convention (`Q21.11`) almost exactly.

`from_raw(Rep)`/`raw()` move a pre-scaled bit pattern in/out directly;
`from_int(value)`/`to_int()` convert to/from a plain integer (`to_int()`
truncates toward zero, never undefined behavior); `fractional_percent()`
renders the fractional part as a rounded `0..=100` integer, a
floating-point-free way to format a value the way `/proc/loadavg` does
(`"<to_int()>.<fractional_percent()>"`). `+`/`-`/`*`/`/` and the usual
comparisons operate on the scaled representation directly -- `*`/`/` use
a widened intermediate (`fixed_int.hpp`'s `next_wider_t<Rep>`, recursively
double-width all the way up) so the scale doesn't lose precision, but
every operator is otherwise plain, unchecked arithmetic: silent
wraparound/truncation on overflow, and dividing by a zero `fixed_point` is
undefined behavior, exactly matching plain integer `+`/`-`/`*`/`/`'s own
existing failure modes (see `int_ops.hpp` for explicit `checked_*`
alternatives for *integers*; this header deliberately does not duplicate
that machinery for the scaled fixed-point domain, matching
`wrapping<T>`/`saturating<T>`'s own choice not to overload `/`/`%` at
all). `fixed_point::pow(base, exponent)` raises `base` to `exponent` via
exponentiation by squaring (`O(log exponent)` multiplications) -- the
operation a geometric decay/compounding computation (an exponential
moving average fast-forwarded over many elapsed sampling quanta at once,
compound interest, ...) needs.

`sqrt()` is exact integer square-root math (the classic binary
"digit-by-digit" algorithm over the widened raw bit pattern -- no series,
no approximation error beyond `Rep`'s own fixed-point quantization);
defined as `0` for a negative value (signed `Rep` only) instead of
undefined behavior. `exp()` (`e^x`) has no such closed-form algorithm, so
it range-reduces its argument (`e^x = (e^(x/2^k))^(2^k)`, choosing `k` so
`|x/2^k| <= 1`, the same "scaling and squaring" trick real floating-point
`expm1`/matrix-exponential implementations use) and evaluates a 16-term
`1/n!` Taylor series over the reduced argument via `taylor_eval` (below);
both use a fixed, not adaptive/error-bounded, iteration count -- treat
them as a convenient approximation, not a numerically-rigorous `<cmath>`
replacement.

`taylor_eval(coefficients, x)` is the free function template `exp()`
itself is built on, exposed because it is independently useful: it
evaluates `coefficients[0] + coefficients[1]*x + coefficients[2]*x^2 + ...`
at `x` via Horner's method (`coefficients.size() - 1` multiply-adds, not a
separate `pow` per term) over *any* caller-supplied `span<const
fixed_point<Rep, FracBits>>` -- not tied to `exp()`'s own `1/n!` table at
all, so a caller with its own precomputed series (`sin`/`cos`/`log1p`/a
curve fit, ...) can reuse it directly; an empty `coefficients` evaluates
to `0`.

```cpp
using q21_11 = reloco::fixed_point<std::uint32_t, 11>;

auto decay = q21_11::from_raw(1884); // ~0.92, Linux's own EXP_1 constant
auto decayed_many_steps = q21_11::pow(decay, 1000); // O(log 1000), not 1000 multiplications

auto load = q21_11::from_int(2) + q21_11::from_raw(472); // ~2.23
assert(load.to_int() == 2);
assert(load.fractional_percent() == 23);

auto root = q21_11::from_int(2).sqrt();      // ~1.41421 (exact floor at this Q-format's precision)
auto e = q21_11::from_int(1).exp();          // ~2.71828

reloco::array<q21_11, 2> coefficients{q21_11::from_int(2), q21_11::from_int(3)};
auto linear = reloco::taylor_eval(reloco::span<const q21_11>(coefficients), q21_11::from_int(4)); // 2 + 3*4 == 14
```

## `fixed_int<N, Signed>`

`include/reloco/fixed_int.hpp`

An arbitrary fixed-precision integer, `N` bits wide (`N` must be a power
of two, at least 8), feedable to `fixed_point<Rep, FracBits>` as `Rep`
once `N` outgrows whatever native integer width the target compiler
offers. For every `N` a mainstream compiler can plausibly represent
natively -- 8/16/32/64 bits always, 128 bits when the `__int128`/
`unsigned __int128` compiler extension is available (`RELOCO_HAS_INT128`,
detected via the portable `__SIZEOF_INT128__` feature-test macro) --
`fixed_int<N, Signed>` is a plain alias for that native type, no wrapper,
no overhead. Only once `N` exceeds what the compiler natively offers
(`N >= 256`, or `N == 128` without the `__int128` extension) does it fall
back to `detail::wide_int<N, Signed>`, a portable, software,
two's-complement, 32-bit-limb bignum implementing the same
`+`/`-`/`*`/`/`/`%`/comparisons/shifts/`&`/`|`/`^`/`~` surface a native
integer has (schoolbook multiply, shift-subtract long division -- `O(N)`/
`O(N^2)`, not single-instruction, but otherwise a drop-in `Rep`).
`fixed_uint<N>` is `fixed_int<N, false>` spelled out.

`fixed_point<Rep, FracBits>` itself places no upper bound on `Rep`'s
width: its own widened `*`/`/` intermediate is `fixed_int.hpp`'s
`next_wider_t<Rep>`, an open-ended chain (`8 -> 16 -> 32 -> 64 -> 128 ->
256 -> 512 -> ...`) that recurses into `wide_int` only once (and exactly
as far as) the native chain runs out -- so picking a wider `fixed_int<N>`
as `Rep` is enough to get more precision everywhere `fixed_point` already
works, including `sqrt()`/`exp()`/`taylor_eval`.

```cpp
using u256 = reloco::fixed_uint<256>;
using big_q = reloco::fixed_point<u256, 64>;

auto a = big_q::from_int(1'000'000'000);
auto b = big_q::from_int(3);
auto product = a * b; // exact, no 64-bit overflow despite the huge scale
assert(product.to_int() == u256(3'000'000'000));
```

## `allocator_ref` / `allocator<Tag>` / `allocator_traits<Tag>`

`include/reloco/allocator.hpp`

Type-erased, two-word handle (`allocator_ref`) to an allocator backend
described by a `Tag` + `allocator_traits<Tag>` specialization — no virtual
base class, no vtable-carrying inheritance. Every operation
(`allocate`/`deallocate`/`expand_in_place`/`reallocate`/`advise`) returns
`reloco::result<T>` and is `RELOCO_UNSAFE_BUFFER_USAGE`-gated (raw sized
pointers, no bounds-tracked alternative). See
[Extending reloco](extending.md) for how to add a new backend.

## `heap_allocator_tag` / `default_allocator()`

`include/reloco/heap_allocator.hpp`, `include/reloco/default_allocator.hpp`

`heap_allocator_tag` is the stateless, built-in backend over the process
heap. `default_allocator()` is the process-wide default `allocator_ref`
(similar to Rust's `#[global_allocator]`): out of the box it returns the
heap backend, but an application can replace it wholesale by defining
`RELOCO_DEFAULT_ALLOCATOR_CUSTOM` — see the header's own documentation for
the exact override recipe (a customization-header include cycle makes it
slightly more involved than a plain `reloco_user_config.hpp` define).

`reloco_global_alloc` is `RELOCO_EXPORT`-annotated so a custom hook's
function-local `static` state stays one shared instance across a
`-fvisibility=hidden` shared-library boundary — see the
[`type_id`](#type_id--type_id_oft) note above and `RELOCO_ENABLE_EXPORT` in
`reloco_config.hpp`; the built-in, stateless heap-backed default has no
such state to share, so this only matters for a custom hook shared across
that boundary.

## `pool_allocator<Lock>` / `pool_allocator_tag<Lock>` / `pool_allocator_context<Lock>` / `null_mutex`

`include/reloco/pool_allocator.hpp`

Fixed-block-size `allocator_traits` backend: hands out blocks of exactly
one runtime-configured `block_size`/`block_alignment` (not a template
parameter — construct one `pool_allocator` per size class needed), carved
out of slabs obtained from an upstream `allocator_ref`. Rejects any request
bigger than `block_size` or more aligned than `block_alignment` with
`error::allocation_failed`, exactly like asking a fixed-size slab allocator
(FreeBSD's `uma_zone`, Linux's `kmem_cache`) for something outside its
zone.

```cpp
reloco::pool_allocator<> pool(64, alignof(std::max_align_t),
                              reloco::default_allocator(), 65536);
auto vec = reloco::vector<int>::try_allocate(pool.ref());
```

All bookkeeping — the free-block list and the list of slabs themselves
(for teardown) — is threaded intrusively through the blocks'/slabs' own
memory (the first `sizeof(void*)` bytes of a free block, or of a slab,
link to the next one): no separate tracking allocation is ever made. Each
slab reserves exactly one whole block's worth of space at its front for
that per-slab link. Slab size is configured as an exact `slab_bytes` byte
count (not a block count), so a caller can size each slab request to
whatever granularity matters to the upstream allocator (e.g. a page or
huge page) rather than an arbitrary number of blocks; the number of
*usable* blocks per slab (`slab_bytes / block_size - 1`, after reserving
the header block) is derived from it.

When the free list is empty, allocation releases the pool's internal
`Lock` *before* calling into the (potentially slow/blocking) upstream
allocator, and only reacquires it to splice the newly obtained slab's
blocks onto the free list — the "unlock, allocate, relock" discipline a
kernel slab allocator needs to avoid holding a spinlock across a call that
might block or itself try to acquire a sleepable lock. `Lock` (the one
template parameter) defaults to `null_mutex`, a no-op satisfying the same
`lock()`/`unlock()`/`try_lock()` surface as `reloco::mutex`/
`reloco::spin_lock` (either is a drop-in `Lock` for a genuinely
multi-threaded pool); keep the default when the pool is only ever touched
from a single thread or under some other external synchronization.

`pool_allocator<Lock>` is neither copyable nor movable — it only ever
exposes a type-erased `allocator_ref` via `.ref()`, so nothing needs (or
is able) to relocate the pool itself once other code may already hold
that handle (a real `Lock` like `reloco::mutex` is not movable/copyable
either). Construct it once, in place, for the lifetime it needs to serve;
its destructor returns every slab it is still holding back to the
upstream allocator. `expand_in_place`/`reallocate`/`advise` are
intentionally omitted (every block is a fixed size, so there is nothing
to grow/shrink in place) — `allocator_ref::can_reallocate()`/
`can_advise()` report their absence seamlessly.

## `bucket_allocator<Lock, BucketSizes...>` / `bucket_allocator_tag<Lock, BucketSizes...>` / `bucket_allocator_context<Lock, BucketSizes...>`

`include/reloco/bucket_allocator.hpp`

General-purpose `allocator_traits` backend combining a compile-time list
of `pool_allocator`s (see above), one per size "bucket", routing each
request to the smallest configured bucket that fits:

```cpp
reloco::bucket_allocator<reloco::null_mutex, 16, 32, 64, 128, 256> pool(
    alignof(std::max_align_t), reloco::default_allocator(), 65536);
auto vec = reloco::vector<int>::try_allocate(pool.ref());
```

`BucketSizes...` must be listed in strictly ascending order (a
`static_assert`, not a runtime check). Every bucket shares one
`alignment` (a constructor parameter, not part of `BucketSizes`) and one
upstream `allocator_ref` every bucket's own `pool_allocator_context<Lock>`
obtains its slabs from. A request that fits some bucket but whose
alignment exceeds `alignment` fails with `error::allocation_failed` --
that bucket's own pool rejects it, exactly like `pool_allocator` itself,
and it is not retried against upstream. A request bigger than the
*largest* configured bucket, however, is forwarded directly to the shared
upstream `allocator_ref` instead of failing -- once nothing configured
fits, there's no smaller size class left to round up to and reject
against, so falling back to upstream keeps `bucket_allocator` usable as a
general-purpose front end rather than one that hard-fails past its
configured range. Bucket selection is a plain linear scan bounded by
`sizeof...(BucketSizes)` -- deliberately simple and deterministic rather
than, say, a binary search, since the bucket count is expected to stay
small.

Deallocation's one subtlety: the `bytes` a caller passes back to
`deallocate()` is not always exactly the bucket size the block was
carved from (a container may record, and later hand back, any value
`<=` the actual capacity `allocate()` returned -- see `allocator.hpp`'s
`mem_block` contract). `deallocate()` re-runs the same bucket-selection
search to recover which bucket the block actually came from, then
forwards *that bucket's own exact block size* to it, since
`pool_allocator_context::deallocate_block` requires an exact match. If
`bytes` exceeds every bucket (the block was one of the direct-to-upstream
allocations above), `deallocate()` forwards straight to the upstream
`allocator_ref` instead, mirroring `allocate()`'s own fallback.

Unlike `pool_allocator` (whose fixed block size leaves no headroom to
grow into), `bucket_allocator` implements `expand_in_place`/`reallocate`:
a bucket's actual block size can exceed the `old_size` a caller records
for it (the same truncation `deallocate()` accounts for above), so
`expand_in_place` can grow a block in place, at no cost, as long as the
new size still fits the block's own bucket -- re-deriving the owning
bucket from `old_size` exactly like `deallocate()` does. This zero-copy
shortcut is compiled out under AddressSanitizer (`RELOCO_ASAN_ENABLED`)
so a sanitizer build keeps exercising `reallocate`'s real
allocate/copy/deallocate path instead of always taking it. `reallocate`
itself is a plain allocate-new/copy-`min(old_size, new_size)`-bytes/
deallocate-old sequence (it may land the new block in a different
bucket, the same bucket, or upstream directly). For a block that was one
of the direct-to-upstream allocations, both operations defer entirely to
the upstream `allocator_ref`'s own `expand_in_place`/`reallocate`
instead, failing if upstream doesn't support it.

`Lock` must always be given explicitly, even to pick the default
`null_mutex` (e.g. `bucket_allocator<null_mutex, 16, 32, 64>`): a
template parameter pack must be the last template parameter, so `Lock`,
preceding `BucketSizes...`, cannot itself default while still letting a
caller supply the (mandatory) bucket list after it. Neither copyable nor
movable, for the same reasons as `pool_allocator<Lock>`.

## `malloc_allocator<Lock>` / `malloc_allocator_tag<Lock>` / `malloc_allocator_context<Lock>`

`include/reloco/malloc_allocator.hpp`

General-purpose, variable-size `allocator_traits` backend providing
classic `malloc`/`free`/`memalign`/`realloc` semantics -- notably,
`free()` takes only a pointer, no explicit size -- by carving memory out
of "arenas" requested on demand from an upstream `allocator_ref`:

```cpp
reloco::malloc_allocator<> heap(reloco::default_allocator(), 65536);

// Direct malloc-style surface (free() needs no size):
auto blk = heap.malloc(128);
heap.free(blk->ptr);

// Or as a type-erased allocator_ref, like any other backend:
auto vec = reloco::vector<int>::try_allocate(heap.ref());
```

Unlike `pool_allocator`/`bucket_allocator` (one, or a fixed compile-time
list of, fixed block sizes), `malloc_allocator<Lock>` hands out blocks of
any runtime size/alignment from a single boundary-tag free list -- every
block, free or in-use, carries a size+in-use header at its front and a
matching footer at its end, so a neighbor in either direction can always
recover a block's extent without a side table, which is what lets
`free()` do without an explicit size. Every allocation also reserves one
hidden `size_t` immediately before the returned pointer, recording the
byte offset back to the block's header; this makes recovering a block's
header from nothing but a user pointer uniform for both naturally-aligned
and `memalign()`-style over-aligned requests, at the cost of that one
extra `size_t` of overhead per allocation.

This backend is meant for exactly the case `pool_allocator`/
`bucket_allocator` don't fit well: a short-lived, variable-size heap
needed only for early boot/bring-up or a bootloader stage, over whatever
backing store happens to be available this early (a fixed static pool via
`stack_allocator`, a bootloader-reserved region, or simply
`default_allocator()`) -- the upstream `allocator_ref` is used generically
and is not assumed to be fixed-size itself. Two behaviors follow from that
transient-heap framing:

- Arenas are requested from upstream lazily, each sized (via
  `default_arena_bytes`, a constructor parameter) to comfortably fit
  whatever request triggered the refill -- a single request too big for a
  `default_arena_bytes`-sized arena simply gets a bigger arena sized to
  fit it exactly, rather than being forwarded straight to upstream the way
  `bucket_allocator` forwards oversized requests.
- `free()` eagerly coalesces a freed block with any free neighbor in
  either direction, and if the result spans an *entire* arena's interior
  (the whole arena is idle again), that arena is returned to upstream
  immediately, rather than waiting for `malloc_allocator`'s own
  destructor the way `pool_allocator`'s slabs are. A transient heap that
  empties out partway through its lifetime gives that memory back
  promptly instead of holding it until teardown.

Each arena begins and ends with a tiny, permanently in-use "sentinel"
block (header+footer only, no payload) -- an ordinary minimum-size
in-use block, not a special case in the coalescing logic -- so a real
block at either end of an arena naturally fails to coalesce past it,
without ever needing an explicit arena-bounds check.

`expand_in_place`/`reallocate` are both supported (unlike `pool_allocator`,
whose fixed block size leaves no headroom to grow into): `expand_in_place`
absorbs an immediately-following free neighbor in place when there's
enough room; `reallocate` tries that first (only when the existing
pointer already satisfies the requested alignment, since in-place growth
never moves the block), then falls back to allocate-new/copy/free-old.
`advise` is intentionally unimplemented.

Like `pool_allocator`, every call into the upstream allocator happens
with this allocator's own `Lock` released first (the same "unlock,
allocate, relock" discipline, for the same reason: never call into a
potentially-blocking upstream allocator while holding the internal lock).
`Lock` defaults to `null_mutex`; pass `reloco::mutex`/`reloco::spin_lock`
for a genuinely multi-threaded heap.

`malloc_allocator<Lock>` is neither copyable nor movable, for the same
reasons as `pool_allocator<Lock>`/`bucket_allocator<Lock, BucketSizes...>`:
it only ever exposes a type-erased `allocator_ref` (via `.ref()`) plus its
own direct `malloc`/`memalign`/`free`/`realloc` surface.

## `masked_byte_region<Size, NoncePolicy>`

`include/reloco/masked_byte_region.hpp`

`masked_byte_region` stores a fixed-size byte region behind a nonce-based
masking policy. `security::inline_nonce_storage` supplies an in-object nonce.
`byte_iterator` and `reference` provide element access through the masked
representation.

## Tamper-detecting values

`include/reloco/tamper.hpp`

`masked_integral` stores an integral value in an XOR-masked representation.
`tamper_proof_state` stores enum or boolean state using dual masked
representations. `tamper_bool_impl` provides the internal boolean
implementation.

## `obfuscated_string<N>` / `obfuscated_string_ref` / `RELOCO_OBFUSCATED_STR(str)`

`include/reloco/obfuscated_string.hpp`

`RELOCO_OBFUSCATED_STR("literal")` XOR-masks a string literal entirely at
compile time (`obfuscated_string<N>`'s `constexpr` constructor, keyed from
`__FILE__`/`__LINE__`/`__TIME__`), so only ciphertext is ever emitted into
`.rodata`/`.data` -- never the plaintext. It expands to an
immediately-invoked lambda returning `obfuscated_string<N>::decrypted_view`,
a neither-copyable-nor-movable RAII handle that decodes the plaintext into
an inline stack buffer and volatile-wipes it on destruction, matching
`masked_byte_region::wipe()`. `c_str()`/`view()` are `RELOCO_LIFETIMEBOUND`,
so `-Wdangling-gsl` flags storing the pointer/view past the end of the
decoding temporary's full expression. Obfuscation against static analysis
(`strings(1)`, disassembler string-xrefs), not cryptographic secrecy -- see
the file-level "Threat model" documentation.

`obfuscated_string_ref` is a non-template, type-erased handle (ciphertext
pointer + encoded size + key) for cases the template parameter `N` can't
reach: `RELOCO_DECLARE_OBFUSCATED_STR(name)`/`RELOCO_DEFINE_OBFUSCATED_STR(
name, "literal")` forward-declare and define such a global, still backed
by an internal-linkage `obfuscated_string<N>` constant in `.rodata`, now
addressable from any translation unit without naming `N`.
`obfuscated_string<N>::as_ref()` converts explicitly; the templated
converting constructor does so implicitly. `for_each_byte(visitor)`
(available on both `obfuscated_string<N>` and `obfuscated_string_ref`)
decodes and visits one plaintext byte at a time via the same volatile-read
technique, without ever materializing the plaintext as a contiguous
buffer -- the decode path `microfmt`'s `formatter<reloco::obfuscated_string<N>>`
uses to stream straight to a format sink.

## Fallible construction: `concepts.hpp` / `construction_helpers.hpp`

`include/reloco/concepts.hpp`, `include/reloco/construction_helpers.hpp`

`has_try_create_v<T, Args...>`, `has_try_allocate_v<T, Args...>`,
`has_try_construct_v<T, Args...>`, `has_try_clone_v<T>` (and its
`_allocator_aware`/`_self_contained` halves), and `has_try_clone_at_v<T>`
detect which of the five fallible-construction functions a type implements
(plus matching C++20 `concept`s). `construction_helpers::try_construct`/
`try_allocate`/`try_clone`/`try_clone_at` pick the most efficient available
strategy at compile time so generic code never hand-writes the `if
constexpr` dispatch itself. See
[Fallible construction](fallible-construction.md) for the full protocol,
and `unique_ptr.hpp`/`string.hpp` for two complete, real-world examples.

## `fallible_singleton<T>` / `atomic_fallible_singleton<T>`

`include/reloco/fallible_singleton.hpp`

Lazily, fallibly initialized singletons built on
`construction_helpers::try_construct`, avoiding the static initialization
order fiasco for globals with non-trivial, potentially-failing setup.
`fallible_singleton<T>::instance()` (or `instance(allocator_ref)`)
constructs `T` in static storage on first call and returns the same `T *`
thereafter; it is **not thread-safe**. `atomic_fallible_singleton<T>::
instance()` is the thread-safe counterpart, using a `futex.hpp`
`futex_word` state (`empty`/`initializing`/`ready`) instead of an
externally supplied lock: a `compare_exchange` picks a single winner to
run construction, every other concurrent caller blocks via `futex_wait`
until the winner publishes the outcome and wakes them via
`futex_wake_all` -- exactly like `once_lock<T>`'s slow path (see
`once_lock.hpp`). A failed construction reverts the state to allow a
later retry from any thread. See
[Fallible construction](fallible-construction.md#lazy-singletons-fallible_singleton-atomic_fallible_singleton)
for the full explanation.

Both classes are `RELOCO_EXPORT`-annotated so their storage stays one
shared, process-wide instance per `T` even across a `-fvisibility=hidden`
shared-library boundary -- see the [`type_id`](#type_id--type_id_oft) note
above and `RELOCO_ENABLE_EXPORT` in `reloco_config.hpp`; opt in with that
macro if `instance()` for a shared `T` is called from more than one shared
object.

## `mutex` / `recursive_mutex` / `error_checking_mutex` / `shared_mutex` / `condition_variable`

`include/reloco/mutex.hpp`

Backend-selectable synchronization primitives. `mutex`/`recursive_mutex`/
`shared_mutex` have an infallible `void lock()`/`unlock()` shape (`void
lock_shared()`/`unlock_shared()` for `shared_mutex`): any underlying OS/library
failure indicates a programming error (relocking a non-recursive mutex
already held by the calling thread, unlocking one not held, ...) -- undefined
behavior per the standard in the first place -- and is reported via
`RELOCO_ASSERT` rather than a `result<void>`, so they drop in cleanly as
`std::unique_lock<T>`'s `Mutex` template parameter (whose destructor calls
`unlock()` unconditionally, with no way to check a return value). Plus
`[[nodiscard]] bool try_lock()` and a `native_handle()` escape hatch.
Two built-in backends are selected automatically -- `RELOCO_MUTEX_BACKEND_PTHREAD`
(POSIX `pthread_mutex_t`/`pthread_rwlock_t`/`pthread_cond_t`) when
`<pthread.h>` is available, else `RELOCO_MUTEX_BACKEND_STD`
(`std::mutex`/`std::shared_mutex`/`std::condition_variable`, which covers
Windows out of the box) -- or forced by defining exactly one of them in
`reloco_user_config.hpp`. `error_checking_mutex` is the deliberate exception:
implemented once, generically, on top of whichever `mutex` backend is active
plus an `std::atomic<std::thread::id>` owner tag, so it's fully portable (no
glibc-specific `PTHREAD_MUTEX_ERRORCHECK`/`_NP` initializer macros), its
`[[nodiscard]] result<void> lock()` returns `error::deadlock` when the
calling thread already owns it, `unlock()` returns `error::invalid_owner`
from a non-owning thread, and `try_lock()` returns `false` (not an error)
when self-owned -- turning exactly the misuses that `mutex` asserts on into
a reportable, recoverable error instead.

Under `-fno-exceptions`, the `RELOCO_MUTEX_BACKEND_STD` backend's internal
`std::system_error` translation is compiled out (detected via the
`RELOCO_HAS_EXCEPTIONS` macro in `detail/compat.hpp`): the underlying
`std::mutex`/`std::shared_mutex` calls are made unguarded instead, since
`try`/`catch` isn't valid syntax in that mode and the standard library's own
throwing paths become terminating calls anyway. `RELOCO_MUTEX_BACKEND_PTHREAD`
is unaffected -- POSIX mutex calls never throw.

Defining `RELOCO_MUTEX_BACKEND_CUSTOM` suppresses both built-in backends
(including the generic `error_checking_mutex`) entirely: `mutex.hpp`
`#include`s a fixed path, `detail/porting/mutex.hpp`, at the exact point
the built-in backend would otherwise define these classes -- see
[Porting custom backends (`detail/porting/`)](#porting-custom-backends-detailporting)
below for the full mechanism. The application must supply
`mutex`/`recursive_mutex`/`error_checking_mutex`/`shared_mutex`/
`condition_variable` matching the same public API there, e.g. for Win32
`SRWLOCK`/`CRITICAL_SECTION`/`CONDITION_VARIABLE` or an RTOS's native
primitives (not ported here by design -- see the header's file-level doc
comment). See `RELOCO_MUTEX_BACKEND_STD`/`_PTHREAD`/`_CUSTOM` in
`reloco_config.hpp` for the exact selection mechanism.

`condition_variable::wait_for(locker, timeout, pred)` is a bounded
counterpart of `wait(locker, pred)`: blocks until `pred()` is `true` or a
`reloco::duration timeout` (`duration.hpp`) elapses, whichever comes
first, returning `result<bool>` (`pred()`'s final value, or
`error::not_locked` if `locker` doesn't own its lock). The
`RELOCO_MUTEX_BACKEND_PTHREAD` backend is built on `pthread_cond_timedwait`
against an absolute deadline computed via `duration_cast<struct
timespec>`; its underlying `pthread_cond_t` is initialized with
`pthread_condattr_setclock(CLOCK_MONOTONIC)` whenever the platform
advertises POSIX Clock Selection support (immune to concurrent wall-clock
adjustments, unlike `CLOCK_REALTIME`), falling back to `CLOCK_REALTIME`
where clock selection isn't supported (e.g. Darwin/macOS). Define
`RELOCO_MUTEX_NO_MONOTONIC_CLOCK` (see `reloco_config.hpp`) to force
`CLOCK_REALTIME` even where monotonic support would otherwise be detected.
The `RELOCO_MUTEX_BACKEND_STD` backend delegates directly to
`std::condition_variable::wait_for`.

`mutex`/`recursive_mutex`/`error_checking_mutex`/`shared_mutex` (both
backends) are annotated for
[Clang Thread Safety Analysis](https://clang.llvm.org/docs/ThreadSafetyAnalysis.html)
via the `RELOCO_CAPABILITY`/`RELOCO_ACQUIRE`/`RELOCO_RELEASE`/
`RELOCO_TRY_ACQUIRE` (and `_SHARED` variants) macros in `detail/compat.hpp`:
building with `-Wthread-safety` on Clang statically flags mismatched
lock/unlock pairs (e.g. a `lock()` with no matching `unlock()` on some
path, or an `unlock()` on a mutex not currently held) at compile time. On
GCC/MSVC, `RELOCO_HAS_ATTRIBUTE` reports these attributes as unsupported,
so the macros expand to nothing there -- zero cost, zero portability
impact. `detail/compat.hpp` also exposes `RELOCO_GUARDED_BY`/
`RELOCO_PT_GUARDED_BY`/`RELOCO_REQUIRES`/`RELOCO_REQUIRES_SHARED`/
`RELOCO_LOCKS_EXCLUDED`/`RELOCO_ASSERT_CAPABILITY`/
`RELOCO_ASSERT_SHARED_CAPABILITY`/`RELOCO_SCOPED_CAPABILITY`/
`RELOCO_NO_THREAD_SAFETY_ANALYSIS` for annotating application code that
guards its own data with these mutex types.

## `guarded_mutex<T, MutexT = mutex>`

`include/reloco/guarded_mutex.hpp`

A mutex that owns the value it protects, matching Rust's
`std::sync::Mutex<T>`. `mutex`/`recursive_mutex`/`shared_mutex` above
protect nothing by themselves -- there is nothing stopping code from
touching a separately-declared guarded variable without holding the lock
at all. `guarded_mutex<T>` closes that gap: the `T` lives inside the
`guarded_mutex<T>` itself, and the only way to reach it is through the
RAII `guard` returned by `lock()`/`try_lock()`.

```cpp
reloco::guarded_mutex<int> counter(0);
{
  auto g = counter.lock(); // blocks until acquired
  *g += 1;
} // lock released automatically here

auto g = counter.try_lock(); // result<guard>, fails with error::busy if held
if (g)
  **g += 1;
```

`lock()` blocks until acquired and returns a `guard` (the checked tier: it
cannot fail, matching Rust's own `Mutex::lock()` once poisoning is
disregarded, which reloco has no equivalent of since it never unwinds
through a held lock). `try_lock()` is the fallible tier, returning
`result<guard>` and failing with `error::busy` if already held elsewhere
-- matching Rust's own `Mutex::try_lock() -> Result<MutexGuard<T>,
TryLockError<...>>` more closely than an `optional<guard>` would (and
`error::busy` carries more information than a bare "empty" would).
`get_mut()` bypasses locking entirely for callers that already hold an
exclusive `guarded_mutex&` (Rust's `Mutex::get_mut()`, which borrows `&mut
self` at compile time instead). `guard` is move-only and releases the
lock automatically on destruction, matching Rust's `MutexGuard<'a, T>`.

The lock backend is a template parameter (`MutexT = mutex` by default);
any type providing `lock()`/`unlock()`/`try_lock()` with the same shape as
`reloco::mutex` works, including `recursive_mutex`. Only exclusive access
is modeled -- `shared_mutex`'s `lock_shared()`/`unlock_shared()` are not
exposed through `guarded_mutex<T>`; use `shared_mutex` directly for
reader/writer locking without an owned value, or `rw_lock<T>` below for an
owned value with reader/writer locking. This is the thread-safe
counterpart of [`cell<T>`/`ref_cell<T>`](#celltref_cellt) above, which are
`!Sync`-equivalent (single-threaded only) by design.

## `spin_lock`

`include/reloco/spin_lock.hpp`

A busy-wait lock that never parks/blocks and never makes a syscall,
matching the ecosystem `spin` crate's `spin::Mutex` (Rust's own
`std::sync` has no spinlock -- this is deliberately not modeled on
anything in `std`). `mutex`/`guarded_mutex<T>` above ultimately block a
contended thread by parking it with the OS scheduler; that is the right
default virtually everywhere, but is unusable in a few specific contexts:

- Interrupt/exception handlers, and other contexts with no "current
  thread" to park.
- Before a kernel's scheduler/threading subsystem is initialized at all
  (early boot), or in a panic/fault handler that must not depend on one
  existing.
- SMP kernels protecting a data structure shared with an interrupt
  handler on another core, where the holder is never itself descheduled
  while holding the lock, so the wait is always provably short.

`spin_lock` is pure `std::atomic<bool>` + [`hint::spin_loop()`](#hintspin_loop)
with zero OS dependency, so it works unchanged in a freestanding/bare-
kernel build. It provides the same minimal `lock()`/`unlock()`/
`try_lock()` shape as `reloco::mutex`, so it slots directly into
`guarded_mutex<T, MutexT>` as a drop-in `MutexT`:

```cpp
reloco::guarded_mutex<int, reloco::spin_lock> counter;
auto guard = counter.lock(); // never parks; spins instead.
```

`is_locked()` returns a best-effort, inherently racy snapshot of whether
the lock is currently held -- matching the `spin` crate's own
`Mutex::is_locked()` -- useful only for diagnostics/assertions, never for
making a synchronization decision. `spin_lock` is never fair (no
queueing/ticketing) and never adaptive (always spins, never falls back to
parking), matching the `spin` crate's own simplifications; use
`mutex`/`guarded_mutex<T>` instead whenever one of the specific contexts
above does not apply. A kernel/RTOS that already ships its own spinlock
(most do, often tied into its own interrupt-masking/preemption-disabling
conventions) should keep using that one instead -- this is only for a
consumer that does not have one yet.

**`RELOCO_SPIN_LOCK_BACKEND_CUSTOM`**: even though the built-in
`std::atomic<bool>` implementation needs nothing OS-specific to work
correctly, a kernel target usually still wants its own native spinlock
instead -- one wired into that kernel's own interrupt-masking/preemption-
disabling/lock-order-verification conventions (e.g. FreeBSD's
`mtx_lock_spin`/`MTX_SPIN`, which disables interrupts on the current CPU
and integrates with `WITNESS`; Linux's `raw_spinlock_t`, which disables
preemption and is a distinct type from a regular `spinlock_t` on `-rt`
kernels) that a freestanding `std::atomic` cannot replicate and must not
silently omit. Define `RELOCO_SPIN_LOCK_BACKEND_CUSTOM` to suppress this
header's own `spin_lock` definition entirely; `spin_lock.hpp` then
`#include`s a fixed path, `detail/porting/spin_lock.hpp`, in its place --
see [Porting custom backends (`detail/porting/`)](#porting-custom-backends-detailporting)
below for the full mechanism:

```cpp
// reloco_user_config.hpp
#define RELOCO_SPIN_LOCK_BACKEND_CUSTOM

// include/reloco/detail/porting/spin_lock.hpp (this exact path/name),
// wrapping FreeBSD kernel's own MTX_SPIN mutex (sys/mutex.h).
namespace reloco {
class spin_lock {
public:
  spin_lock() noexcept { mtx_init(&mtx_, "reloco::spin_lock", nullptr, MTX_SPIN); }
  ~spin_lock() noexcept { mtx_destroy(&mtx_); }
  spin_lock(const spin_lock &) = delete;
  spin_lock &operator=(const spin_lock &) = delete;

  void lock() & noexcept { mtx_lock_spin(&mtx_); }
  void unlock() & noexcept { mtx_unlock_spin(&mtx_); }
  [[nodiscard]] bool try_lock() & noexcept { return mtx_trylock_spin(&mtx_) != 0; }
  [[nodiscard]] bool is_locked() const noexcept { return mtx_owned(&mtx_) != 0; }

private:
  mutable struct mtx mtx_{};
};
} // namespace reloco
```

A Linux kernel module would do the same wrapping `raw_spin_lock`/
`raw_spin_unlock`/`raw_spin_trylock` (`<linux/spinlock.h>`) instead. The
replacement must keep the same `lock()`/`unlock()`/`try_lock()` surface
used by `guarded_mutex<T, MutexT>`, but is free to add its own
construction requirements.

### Porting custom backends (`detail/porting/`)

`include/reloco/detail/porting/`

`RELOCO_MUTEX_BACKEND_CUSTOM`/`RELOCO_THREAD_BACKEND_CUSTOM`/
`RELOCO_SPIN_LOCK_BACKEND_CUSTOM`/`RELOCO_TLS_MODEL_OS`/
`RELOCO_FUTEX_BACKEND_CUSTOM` all suppress their header's own built-in
implementation and, in its exact place, `#include` a fixed path under
this directory (`detail/porting/mutex.hpp`, `thread.hpp`, `spin_lock.hpp`,
`tls_provider.hpp`, `futex.hpp` respectively) instead of relying on the
application/kernel to have "included its replacement somewhere before
first use" -- an include-*order* dependency that grows fragile the more
reloco headers reference the same primitive transitively. With a fixed
include path, whichever reloco header reaches (say) `reloco::mutex` first
triggers that one `#include` at that one spot, with the exact same
result, regardless of anything else -- order never matters.

None of those fixed-path files ship in this repository; only their
`*.template.hpp` counterparts do (`mutex.template.hpp`,
`thread.template.hpp`, `spin_lock.template.hpp`,
`tls_provider.template.hpp`, `futex.template.hpp`) -- unused,
documentation-only scaffolds, each loosely sketching what a FreeBSD
**kernel** port might look like (not exact or complete, and none is
compiled or exercised by this repository's own build/test suite). Copy
the relevant scaffold to its non-`.template` name in this same directory,
fill it in, and define the matching macro yourself -- or, more
conveniently, set the `JPLCZ_RELOCO_PORTING_HEADERS` CMake variable (see
`CMakeLists.txt`) to a directory containing your finished header(s)
before configuring reloco (top-level, or via
`add_subdirectory`/`FetchContent`): the build copies whichever of them
exist there into `detail/porting/` (staged in the build tree, then
installed alongside reloco's own headers -- never replacing any of
reloco's own headers, only adding files under `detail/porting/`) and
bakes the matching macro into a generated
`detail/porting/reloco_generated_config.hpp` file placed alongside them,
picked up automatically by `reloco_config.hpp` -- so every consumer
picks up the replacement with no further per-consumer configuration,
including one that never links `jplcz_reloco` as an actual CMake target
(a hand-copied `include/` directory, or a downstream
`find_package(jplcz_reloco)` consumer using a different build system
than the one that produced the install tree). See
`detail/porting/README.md` for the full table of macros/paths/scaffolds.

## `rw_lock<T, SharedMutexT>`

`include/reloco/rw_lock.hpp`

A reader-writer lock that owns the value it protects, matching Rust's
`std::sync::RwLock<T>` -- `rw_lock<T>` is to `shared_mutex` exactly what
`guarded_mutex<T>` is to `mutex`: the `T` lives inside the `rw_lock<T>`
itself, reachable only through one of two RAII guards.

```cpp
reloco::rw_lock<int> value(0);
{
  auto g = value.write(); // blocks until the exclusive lock is acquired
  *g += 1;
} // lock released automatically here

auto g = value.read(); // blocks until a shared lock is acquired
use(*g);

auto try_g = value.try_write(); // result<write_guard>, fails with error::busy if held
if (try_g)
  **try_g += 1;
```

`read()`/`write()` block until acquired and return a `read_guard`/
`write_guard` respectively (the checked tier: cannot fail). `try_read()`/
`try_write()` are the fallible tier, returning `result<read_guard>`/
`result<write_guard>` and failing with `error::busy` if the lock is
already held in a conflicting mode -- matching Rust's own
`RwLock::try_read()`/`RwLock::try_write() -> Result<RwLock*Guard<T>,
TryLockError<...>>`. Any number of `read_guard`s may be held concurrently
across any number of threads, so long as no `write_guard` is held at the
same time; `get_mut()` bypasses locking entirely for callers that already
hold an exclusive `rw_lock&`, matching Rust's `RwLock::get_mut()`. Both
guards are move-only and release their half of the lock automatically on
destruction, matching Rust's `RwLockReadGuard<'a, T>`/
`RwLockWriteGuard<'a, T>`.

The lock backend is a template parameter (`SharedMutexT = shared_mutex` by
default); any type providing `lock()`/`unlock()`/`try_lock()`/
`lock_shared()`/`unlock_shared()`/`try_lock_shared()` with the same shape
as `reloco::shared_mutex` works.

Unlike `guarded_mutex<T>` (which only requires `is_send_v<T>`, since
`Mutex<T>` only ever grants one thread at a time access), `rw_lock<T>`
`static_assert`s both `is_send_v<T>` **and** `is_sync_v<T>`: `read()` may
hand out any number of concurrent shared `const T &` references across
threads at once, so `T` itself must tolerate concurrent access -- matching
Rust's own `unsafe impl<T: ?Sized + Send + Sync> Sync for RwLock<T>`
bound.

## `is_trivially_relocatable<T>`

`include/reloco/relocatable.hpp`

Customization-point trait marking a type whose object representation can be
relocated by copying its bytes to a new address and abandoning the old one,
without running a move constructor or destructor at either address. See
[Trivial relocation](relocatable.md) for the full explanation and the
built-in specializations (`unique_ptr<T>`, `basic_string<CharT, TraitsT>`,
`checked_value<T>`/`checked_value<T *>`).

`include/reloco/relocatable_std.hpp` is a separate, opt-in header adding
specializations for `std::pair<T1, T2>`, `std::tuple<Ts...>`,
`std::optional<T>`, and `std::variant<Ts...>`, each forwarding to the
relocatability of their contained type(s) -- see
[Trivial relocation](relocatable.md#standard-library-wrapper-types-relocatable_stdhpp).

## `is_send<T>` / `is_sync<T>`

`include/reloco/send_sync.hpp`

Customization-point traits matching Rust's `Send`/`Sync` auto traits:
`is_send<T>` asks whether it is sound to transfer ownership of a `T` to
another thread, `is_sync<T>` whether it is sound to share a `T` across
threads through a `const T &`. Both default to `true`; reloco specializes
both to `false` (unconditionally) for `rc<T>`/`weak_rc<T>` (non-atomic
refcount), specializes `is_sync` to `false` (`is_send` forwarding to `T`)
for `cell<T>`/`ref_cell<T>` (unsynchronized interior mutability), and
specializes both to `is_send<T> && is_sync<T>` for `shared_ptr<T>`/
`weak_ptr<T>` (atomic refcount, but the shared `T` still needs to tolerate
concurrent access). `guarded_mutex<T, MutexT>` (see `guarded_mutex.hpp`)
`static_assert`s `is_send_v<T>`, matching Rust's `Mutex<T: Send>` bound.
`rw_lock<T, SharedMutexT>` (see `rw_lock.hpp`) `static_assert`s both
`is_send_v<T>` **and** `is_sync_v<T>`, matching Rust's
`RwLock<T: Send + Sync>` bound -- `read()` may hand out concurrently-held
shared references from multiple threads at once, so `T` itself must
tolerate concurrent access, unlike `Mutex<T>`/`guarded_mutex<T>` which only
ever grants one thread at a time access. See
[Thread-transfer/-sharing safety](send-sync.md) for the full rationale and
table.

## `duration`

`include/reloco/duration.hpp`

An integer-only, non-negative time span matching Rust's
`std::time::Duration`: a `(secs: std::uint64_t, subsec_nanos:
std::uint32_t)` pair, built and read via `from_secs`/`from_millis`/
`from_micros`/`from_nanos` factories and `as_secs`/`subsec_nanos`/
`subsec_micros`/`subsec_millis`/`as_millis`/`as_micros`/`as_nanos`
accessors, plus the usual comparison operators and `operator+`.
Deliberately *not* `std::chrono::duration<Rep, Period>`-based: some
`<chrono>` implementations compute a cross-`Period` conversion through a
floating-point intermediate when the ratio between the two periods isn't
exact, which is unusable in a kernel/freestanding build with no FPU (or
with FPU use disabled), and `<chrono>` itself may not exist at all on such
a target. `reloco::duration` only needs `<cstdint>` and every conversion
is plain integer arithmetic.

`duration_cast<T>(d)` converts a `duration` to a platform/target time type
`T` via the `duration_converter<T>` customization point (same shape as
`allocator_traits<Tag>`/`is_send<T>`/`alignment_of<T>`): the primary
template has no generic definition, so a new `T` requires its own
`duration_converter<T>` specialization exposing `static T convert(duration)
noexcept`. Built in: `struct timespec` (guarded on `<time.h>`'s
availability) and `struct timeval` (guarded on `<sys/time.h>`'s
availability, truncating any sub-microsecond remainder). A kernel target
can add its own specialization for a fixed-point type like FreeBSD's
`sbintime_t` in its own header, exactly like the
`RELOCO_MUTEX_BACKEND_CUSTOM`/`RELOCO_DEFAULT_ALLOCATOR_CUSTOM`
extensibility pattern. `mutex.hpp`'s `condition_variable::wait_for` uses
`duration`/`duration_cast<struct timespec>` for its timeout parameter and
deadline computation, rather than `<chrono>`.

## `instant`

`include/reloco/instant.hpp`

An opaque, monotonically non-decreasing point in time matching Rust's
`std::time::Instant`, built on `duration` rather than
`std::chrono::time_point<Clock>` for the same integer-only, `<chrono>`-free
rationale `duration` itself documents. Like Rust's `Instant`, a value
returned by `instant::now()` carries no defined epoch or meaning by
itself -- only the *difference* between two `instant` values matters:
`duration_since(earlier)`/`saturating_duration_since(earlier)` (an alias,
saturates to zero if `earlier` is actually later), `checked_duration_since
(earlier) -> result<duration>` (fails with `error::invalid_argument`
instead of saturating), `elapsed()` (`now().duration_since(*this)`), and
`operator-` between two `instant`s (`-> duration`, equivalent to
`duration_since`). `checked_add`/`checked_sub(duration) -> result<instant>`
and `operator+`/`operator-(duration)` (saturating) round out arithmetic;
full comparison operators are provided since `instant` values are always
ordered even though their absolute value isn't meaningful.

`instant::now()`'s actual clock reading is a customization point, keyed by
an empty tag type (same shape as `allocator_traits<Tag>`): the primary
`instant_clock_traits<Tag>` template has no generic definition, so a new
`Tag` requires its own specialization exposing `static duration now()
noexcept`. `RELOCO_INSTANT_CLOCK_TAG` selects which tag `instant::now()`
calls through to, defaulting to the built-in `posix_clock_tag` (backed by
`clock_gettime(3)` against `CLOCK_MONOTONIC` when available, else
`CLOCK_REALTIME` -- `RELOCO_MUTEX_NO_MONOTONIC_CLOCK` opts out the same way
it does for `mutex.hpp`'s own condition-variable deadlines) when `<time.h>`
is detected as available. When no clock source is configured,
`instant::now()`/`elapsed()` are simply not declared -- every other
`instant` member still works on values obtained some other way. A
freestanding/kernel target defines its own tag + specialization for
whatever clock it has (e.g. FreeBSD kernel `sbinuptime()`), matching the
`RELOCO_MUTEX_BACKEND_CUSTOM`/`RELOCO_TLS_MODEL_OS` extensibility pattern;
see `instant.hpp`'s own file-level doc comment for a worked example.

`instant`'s internal subtraction (used by `duration_since`/`checked_sub`/
etc.) is built on `int_ops.hpp`'s `checked_sub<std::uint32_t>` to detect
the one place it can actually borrow (the sub-second remainder) instead of
hand-rolled overflow checks.

## `tls_provider<T, Tag>`

`include/reloco/tls_provider.hpp`

A tag-differentiated thread-local storage provider: `tls_provider<T,
Tag>` gives every thread its own independent instance of `T`, with the
storage slot uniquely identified by both `T` and a caller-supplied unique
tag type (`tls_provider<int, struct foo_tag>` and `tls_provider<int,
struct bar_tag>` are two independent per-thread slots). Selected backend
is `RELOCO_TLS_MODEL` (same customization shape as
`RELOCO_MUTEX_BACKEND_*`/`RELOCO_THREAD_BACKEND_*`):

- `RELOCO_TLS_MODEL_THREAD_LOCAL` (default): backed by C++11
  `thread_local`. Portable to any hosted C++17 target; never actually
  fails.
- `RELOCO_TLS_MODEL_PTHREAD`: backed by `pthread_key_create`/
  `pthread_getspecific`/`pthread_setspecific`, for POSIX targets that want
  to avoid compiler `thread_local` support. Picks the cheapest
  representation per `T`: a raw pointer or small trivial value is stored
  directly in the key's `void *` slot (no heap allocation); anything else
  is heap-allocated through the `allocator_ref` passed to `get()`/`set()`
  (not `new`), alongside that same `allocator_ref`, so the
  `pthread_key_create` destructor that runs on thread exit can deallocate
  it correctly through the right allocator regardless of which allocator
  any particular call used.
- `RELOCO_TLS_MODEL_OS`: `#include`s a fixed path,
  `detail/porting/tls_provider.hpp`, in place of a built-in definition
  (see [Porting custom backends (`detail/porting/`)](#porting-custom-backends-detailporting)
  below); a kernel/RTOS port supplies one generic `tls_provider<T, Tag>`
  against its own per-task storage, the same escape hatch
  `RELOCO_MUTEX_BACKEND_CUSTOM`/`RELOCO_THREAD_BACKEND_CUSTOM` provide.
- `RELOCO_TLS_MODEL_SINGLE`: one global static instance (not actually
  per-thread), for single-threaded builds that still want to link against
  code written against the `tls_provider<T, Tag>` interface.

```cpp
struct my_tag {};
using my_slot = reloco::tls_provider<reloco::string, my_tag>;

auto value = my_slot::get(); // result<std::reference_wrapper<string>>
if (value)
  value->get() = "hello";
```

`get()`/`set(T, allocator_ref = default_allocator())` are both fallible
(`result<...>`): every model can fail if the one-time backend
initialization itself fails (e.g. `RELOCO_TLS_MODEL_PTHREAD`'s
`pthread_key_create`, reported as `error::resource_exhausted`), and
`RELOCO_TLS_MODEL_PTHREAD`'s heap-allocated specialization can
additionally fail with whatever `error` the allocator reports.
`get()` returns `result<std::reference_wrapper<T>>` wherever the model has
genuine addressable per-thread storage to reference (`THREAD_LOCAL`,
`SINGLE`, `OS`, and `PTHREAD`'s heap-allocated specialization) --
`expected<T, E>` requires a nothrow-move-constructible value type, which
no reference type satisfies, so a reference is wrapped rather than
returned directly (the same `result<std::reference_wrapper<T>>` idiom used
elsewhere for "fallibly return a reference"). `PTHREAD`'s raw-pointer/
small-trivial specializations have no such addressable storage (the value
lives only as a bit pattern inside the key itself), so their `get()`
returns `result<T>` by value instead.

## `keyed_intrusive_registry<T, OwnerKey, Tag, Lock, Hash, KeyEqual>`

`include/reloco/keyed_intrusive_registry.hpp`

A locked, self-allocating "one `T` per `OwnerKey`" registry, built directly
on `intrusive_hash_table` -- the hash-table-per-owner-key building block a
`RELOCO_TLS_MODEL_OS` kernel/RTOS port can use for `tls_provider<T, Tag>`
(its own `detail/porting/tls_provider.hpp` scaffold explicitly calls out
"a real port should ... use a proper hash table" in place of its
illustrative linked-list scan). Unlike `intrusive_hash_table` itself,
`keyed_intrusive_registry` *owns* its nodes: `get_or_create(owner_key)`
allocates a node through an `allocator_ref` and links it in; `erase()`
unlinks and deallocates it. Each node stores the exact `allocator_ref` it
was allocated with, so `erase()` never needs (and can't accidentally be
passed a mismatched) allocator parameter.

```cpp
struct thread_id_tag {};
reloco::intrusive_hash_hook<...> // (embedded via node internally)
reloco::keyed_intrusive_registry<my_value, std::uintptr_t, thread_id_tag>
    registry(bucket_span);

auto* slot = registry.get_or_create(current_thread_key, default_allocator());
if (slot)
  slot->get().touch();
```

Bucket growth is manual (the caller owns the bucket array's lifetime and
decides when/whether to `rehash()` into a larger one, mirroring
`intrusive_hash_table`'s own caller-sized-buckets design), but sizing that
decision is not left to guesswork: `suggest_bucket_count_for_insert(n)`/
`suggest_bucket_count_for_remove(n)` (inherited from the underlying
`intrusive_hash_table`) project the bucket count needed after `n` more
inserts/removes, bounded by a caller-supplied `[min_buckets, max_buckets]`
range and an `elements_per_bucket` load-factor target, using one of the
`bucket_growth.hpp` curves.

Locking is a template parameter (`Lock = null_mutex` by default, so a
single-threaded/uncontended caller pays nothing); like `pool_allocator`,
every method that must allocate follows an "unlock, allocate, relock"
discipline -- the lock is never held across a potentially slow/blocking
allocator call, and a double-checked lookup after reacquiring it handles
the case where another thread raced in and created the same key's node in
the meantime. A per-thread `OwnerKey` is usually reached from an ordinary
preemptible context, so `reloco::mutex` (blocking) is fine there; a
per-CPU `OwnerKey`, in a real kernel, is read/written with preemption
disabled specifically to prevent the caller migrating CPUs mid-access, so
`Lock` there must be `reloco::spin_lock` (never blocks/sleeps), never
`reloco::mutex` -- and a caller whose fast path runs with preemption
disabled must re-enable it before any `get_or_create`/`set` call that
might actually allocate, retrying the (preemption-disabled) fast path
afterwards. Because it embeds a non-copyable, non-movable `Lock`
directly, `keyed_intrusive_registry` itself is non-copyable/non-movable,
and its constructor is a plain `RELOCO_ASSERT`-guarded constructor rather
than a fallible `try_create()` factory (a fallible factory would need to
move-construct the result into a `result<T>`, which a non-movable `Lock`
member forbids) -- the same convention `pool_allocator_context` already
uses for the same reason.

## `tls_slot_vector<Lock, Growth>` / `tls_slot_index<T, Tag>` / `tls_local_slots<Lock, Growth, Tag>`

`include/reloco/tls_slot_vector.hpp`

The classic pthread-key design, as an alternative to
`keyed_intrusive_registry.hpp` for the same "back a `RELOCO_TLS_MODEL_OS`
kernel/RTOS `tls_provider<T, Tag>` port" use case, trading a hash lookup
per `get()`/`set()` for `O(1)` slot indexing:

- `detail::tls_slot_table` is a small, fixed-capacity (`RELOCO_TLS_MAX_SLOTS`,
  default 256), allocation-free global registry of destroy-thunks, one per
  distinct `(T, Tag)` pair ever used; its live count is an atomic
  "generation" counter that every per-context vector compares itself
  against to detect newly registered slots.
- `tls_slot_index<T, Tag>` lazily claims a slot index for `(T, Tag)` the
  first time it is needed, via `once_lock` (not a function-local `static`,
  which needs guard-variable support many bare-metal C++ runtimes lack)
  registering `T`'s destructor as the destroy-thunk for that slot.
- `tls_slot_vector<Lock, Growth>` is a per-context (typically per-thread)
  growable `void **` array: `get_or_create<T, Tag>()`/`set<T, Tag>()`/
  `try_find<T, Tag>()`/`erase<T, Tag>()` index straight into it by
  `tls_slot_index<T, Tag>`'s slot number, growing (via a `bucket_growth.hpp`
  curve, `Growth = doubling_then_ratio` by default) to catch up to the
  *global* generation whenever it falls behind -- so one grow leaves room
  for other `(T, Tag)` pairs already registered by other threads, not just
  the one slot currently being requested. `clear()` runs every populated
  slot's destroy-thunk and resets the vector to empty, for explicit reuse.
  `Lock` defaults to `null_mutex` (only the owning context ever touches its
  own vector); if some other context reaches the *same* instance
  concurrently, pass `reloco::mutex` for an ordinary preemptible caller
  or, for a per-CPU vector reached with preemption disabled, `reloco::
  spin_lock` instead (never `reloco::mutex` there -- blocking while
  preemption is disabled is illegal on most kernels; see
  `keyed_intrusive_registry.hpp`'s own "Locking" section for the same
  distinction). `get_or_create`/`set` already keep the allocator call
  itself outside `Lock` (the same "unlock, allocate, relock" discipline),
  but a preemption-disabled caller must still re-enable preemption itself
  before any call that might allocate, retrying a preemption-disabled-only
  `try_find` afterwards -- or sidestep the whole concern with
  `reserve(min_capacity)`: a per-CPU vector's final capacity (the CPU
  count) is typically known before any AP starts, so reserving it once,
  single-threaded, during that boot window means no AP-side call ever
  needs to grow (or allocate) again.
- `tls_local_state_traits<Tag>` is a deliberately minimal trait --
  `void *get()` / `void set(void *)`, never failing, no backend-model
  selection -- through which a kernel/RTOS port supplies "how do I find
  *this* context's `tls_slot_vector *`" (a `Tag = void` specialization
  backed by a hosted `thread_local void*` is provided out of the box,
  but only when `RELOCO_KERNEL` is not defined -- a kernel/bare-metal
  build must always supply its own, for whatever `Tag` it uses). It
  is intentionally *not* `tls_provider<tls_slot_vector*, Tag>`: that
  type's shape differs across backends for pointer `T` (a bare
  `result<T*>` under `RELOCO_TLS_MODEL_PTHREAD` vs.
  `result<reference_wrapper<T>>` elsewhere), which would complicate this
  otherwise backend-agnostic vector-indexing code for no benefit.
- `tls_local_slots<Lock, Growth, Tag>` ties the three pieces together into
  a static-only convenience API (`get_or_create/try_find/set/erase`,
  each finding-or-lazily-creating the current context's `tls_slot_vector`
  through `tls_local_state_traits<Tag>` first) plus an explicit
  `clear_current()`.

```cpp
struct my_tag {};
using my_slots = reloco::tls_local_slots<>; // Tag = void, hosted thread_local

auto* value = my_slots::get_or_create<reloco::string, my_tag>();
if (value)
  *value = "hello";
```

Dead-context cleanup is the caller's responsibility: nothing here hooks
thread-exit automatically (some bare-metal/RTOS targets have no such
hook, or a differently-shaped one) -- a caller that owns a "thread is
exiting" notification must call `tls_local_slots<...>::clear_current()`
(or `tls_slot_vector::clear()` directly, if managing the vector itself)
before the context's `tls_local_state_traits<Tag>` storage goes away or is
reused for another context.

## `thread` / `thread::spawn` / `join_handle<R>` / `thread_builder`

`include/reloco/thread.hpp`

An OS thread primitive with the same backend-selection shape as
`mutex.hpp`: `RELOCO_THREAD_BACKEND_PTHREAD` (wraps `<pthread.h>`
directly) or `RELOCO_THREAD_BACKEND_STD` (wraps `<thread>`), auto-selected
via `RELOCO_HAS_INCLUDE(<pthread.h>)`, or `RELOCO_THREAD_BACKEND_CUSTOM` to
suppress both -- `thread.hpp` then `#include`s a fixed path,
`detail/porting/thread.hpp` (see
[Porting custom backends (`detail/porting/`)](#porting-custom-backends-detailporting)
below), for an application/kernel supplying its own
`thread`/`thread_id`/`this_thread::get_id()`/`this_thread::yield()`
matching the same public surface (an RTOS task API, a freestanding target,
...).

`thread::try_spawn(function<void()> &&, allocator_ref)` is the raw,
fallible primitive (`result<thread>`, failing with
`error::resource_exhausted` if the OS refuses to create a new thread).
Move-only; the destructor/move-assignment `RELOCO_ASSERT`s if still
joinable, matching `std::thread`'s "must join or detach first" contract
as an assertion trap instead of an unconditional `std::terminate`.

```cpp
auto handle = reloco::spawn([]() noexcept -> int {
  return 42;
});
if (handle) {
  int result = std::move(*handle).join(); // blocks, returns 42
}
```

`spawn(F &&, allocator_ref = default_allocator())` is the Rust-facing
layer matching `std::thread::spawn`/`JoinHandle<T>`, built generically on
top of `thread` + `function<void()>` (see `function.hpp`) so it needs no
backend-specific code of its own. It `static_assert`s that `F` (and
everything it captures) is `is_send_v`, and that `F`'s return type is too
(see [`is_send<T>` / `is_sync<T>`](#is_sendtis_synct) above) --
matching Rust's `F: Send + 'static, F::Output: Send` bound (reloco has no
lifetime tracking to enforce the `'static` half). Unlike Rust (where
dropping a `JoinHandle` silently detaches the thread), `join_handle<R>`'s
destructor blocks and joins if still joinable, matching C++20
`std::jthread`'s safer default; call `detach()` explicitly to opt in to
Rust's original behavior.

`thread_builder` matches Rust's `std::thread::Builder`, letting a caller
request a thread name and/or stack size before spawning:

```cpp
auto handle = reloco::thread_builder()
                  .name("worker")
                  .stack_size(1 << 20)
                  .spawn([] { ... });
```

Every setter is `&&`-qualified and returns `*this` by value, so calls
chain directly off a temporary -- there is no way to hold a half-built
`thread_builder` in a variable and reuse it for more than one `spawn()`,
matching `std::thread::Builder::spawn`'s own consuming-`self` contract.
`name()` truncates to `inline_string<15>`'s 15-character capacity if
longer (matching Linux's own 16-byte-including-NUL `TASK_COMM_LEN`
limit); an oversized name is reported as `error::capacity_exceeded` from
`spawn()` itself rather than silently truncated. `stack_size()` is
honored by the `PTHREAD` backend (via `pthread_attr_setstacksize`); the
`STD` backend has no portable equivalent and reports
`error::unsupported_operation` from `spawn()` instead of silently
ignoring the request. Naming a thread is always best-effort: some
platforms/backends apply it via `pthread_setname_np`/
`pthread_set_name_np` from inside the spawned thread itself, others
silently do nothing.

Unlike `spawn`/`join_handle<R>`, `thread_builder` is *not*
backend-generic: naming a thread and sizing its stack are OS/libc-specific
facilities with no portable equivalent, so `thread_builder` is only
defined for the built-in `PTHREAD`/`STD` backends and does not exist at
all under `RELOCO_THREAD_BACKEND_CUSTOM`. This is deliberate: a
kernel/RTOS providing its own thread/task backend already has its own
native way to name a task and choose its stack size at creation time
(often mandatory, unlike POSIX's optional attributes), so there is no one
shared shape to standardize here -- a custom backend that wants this
ergonomic layer defines its own `thread_builder`-shaped type against its
own task-creation API, exactly like it already does for `thread`/
`thread_id` itself.

## `channel<T>` / `sender<T>` / `receiver<T>`

`include/reloco/channel.hpp`

A multi-producer, single-consumer channel matching Rust's
`std::sync::mpsc::channel`. `channel<T>(allocator_ref =
default_allocator())` returns a `result<std::pair<sender<T>, receiver<T>>>`
sharing one heap-allocated, intrusively-linked-list queue guarded by one
`mutex` + `condition_variable` (see `mutex.hpp`), kept alive by an
atomically-refcounted `shared_ptr` (not `rc<T>`, which is deliberately
`!Send`/`!Sync` -- see `send_sync.hpp`).

```cpp
auto ends = reloco::channel<int>();
if (ends) {
  auto &[tx, rx] = *ends;
  tx.try_send(42);              // result<void>
  auto value = rx.recv();       // blocks; result<int>
}
```

`sender<T>` is `Clone`-like via an ordinary copy constructor (matching
`rc<T>`/`shared_ptr<T>`'s own copy-is-clone convention, see above) -- each
clone increments a shared count and may call `try_send` from any thread,
independently. `try_send(T)` never blocks the sender: it fails with the
allocator's own error if the node allocation fails, or
`error::invalid_state` if the receiver has already been dropped (matching
Rust's `SendError<T>`, minus recovering the un-sent value -- reloco's
single `error` enum carries no payload).

`receiver<T>` is move-only: exactly one consumer is ever meant to call
`recv()`/`try_recv()`/`recv_timeout()`. `recv()` blocks until a value is
sent or every `sender<T>` clone has been dropped, failing with
`error::container_empty` in the latter case (matching Rust's
`RecvError`). `recv_timeout(duration)` is `recv()`'s bounded counterpart:
additionally fails with `error::timed_out` if the duration elapses first,
matching Rust's `mpsc::Receiver::recv_timeout` and its
`RecvTimeoutError::Timeout`/`RecvTimeoutError::Disconnected` cases.
`try_recv()` never blocks: it fails with `error::try_again` if the queue
is momentarily empty but at least one sender remains (matching
`TryRecvError::Empty`), or `error::container_empty` if it is empty and
every sender has already been dropped (matching
`TryRecvError::Disconnected`).

`T` must be `std::is_nothrow_move_constructible_v`, like every other
reloco container element requirement. `is_send<sender<T>>`/
`is_send<receiver<T>>` forward to `is_send<T>`; `is_sync<sender<T>>` is
`is_send<T>` too (every access is fully mutex-guarded, matching current
Rust where `mpsc::Sender<T>: Sync` when `T: Send`); `is_sync<receiver<T>>`
is always `false`, deliberately matching Rust's single-consumer API
contract rather than the implementation's own (looser) actual guarantee.

`receiver<T>` also supports range-`for` iteration, matching Rust's `impl
Iterator for Receiver<T>` and `Receiver::try_iter()`. `begin()`/`end()`
drive an input iterator via blocking `recv()` calls, stopping once every
`sender<T>` clone has been dropped:

```cpp
for (int value : rx) // recv() under the hood; ends when every sender drops
  use(value);
```

`try_iter()` returns a small view whose `begin()`/`end()` drive the same
iterator shape via non-blocking `try_recv()` instead, draining only what
is already queued without ever blocking for a value that may never
arrive:

```cpp
for (int value : rx.try_iter()) // try_recv() under the hood; never blocks
  use(value);
```

### `sync_channel<T>` / `sync_sender<T>`

`sync_channel<T>(capacity, allocator_ref = default_allocator())` returns a
`result<std::pair<sync_sender<T>, receiver<T>>>`, matching Rust's
`std::sync::mpsc::sync_channel`: a bounded counterpart of `channel<T>`
that reuses the exact same `receiver<T>` (`recv()`/`recv_timeout()`/
`try_recv()`/iteration all behave identically), sharing the same
`channel_shared<T>` internals plus a `not_full` condition variable and a
`queue_len`/`capacity` pair.

```cpp
auto ends = reloco::sync_channel<int>(2); // capacity: at most 2 queued values
if (ends) {
  auto &[tx, rx] = *ends;
  tx.send(1);                    // succeeds immediately (room available)
  tx.send(2);                    // succeeds immediately (now full)
  auto blocked = reloco::spawn([tx]() mutable noexcept { tx.send(3); }); // blocks until rx.recv() below
  auto value = rx.recv();        // drains a slot, unblocking the send() above
}
```

`sync_sender<T>` is `Clone`-like exactly like `sender<T>`; every clone
shares the same `capacity`.

- `send(T)` -> `result<void>`: blocks while the queue already holds
  `capacity` values, until room frees up (matching
  `SyncSender::send`); fails with `error::invalid_state` if the receiver
  has been dropped (before or while blocked).
- `try_send(T)` -> `result<void>`: never blocks; fails with
  `error::capacity_exceeded` if the queue is currently full (matching
  `TrySendError::Full`), or `error::invalid_state` if the receiver has
  been dropped (`TrySendError::Disconnected`).

A `capacity` of `0` is a *rendezvous* channel, matching
`sync_channel(0)`: `send()` blocks not merely until there is queue room,
but until the value it just handed over has actually been consumed by
the receiver -- so a successful `send()` return means the value has
provably already been received, not merely buffered. `try_send()` on a
rendezvous channel only ever succeeds when a `recv()`/`recv_timeout()`
call is already blocked waiting to receive it (tracked via an internal
waiting-receiver counter); otherwise it fails with
`error::capacity_exceeded`, exactly like any other momentarily-full
bounded channel.

`is_send<sync_sender<T>>`/`is_sync<sync_sender<T>>` match `sender<T>`'s
own specializations exactly, for the same reasons.

## `thread_handle` / `this_thread::current/park/park_timeout/sleep_for`

`include/reloco/park.hpp`

Rust's `std::thread::park`/`park_timeout`/`sleep`/`Thread`/
`thread::current()`. Every OS thread lazily owns exactly one *parker* --
a one-slot wake token backed by a `futex_word` (see `futex.hpp`) --
created on first use and cached for the thread's lifetime in a
`tls_provider<shared_ptr<...>, ...>` (`RELOCO_TLS_MODEL`-selected, see
`tls_provider.hpp`) slot.

```cpp
auto handle = reloco::this_thread::current(); // cloneable Send + Sync

auto worker = reloco::spawn([]() noexcept {
  reloco::this_thread::park(); // blocks until unparked
});
handle.unpark(); // wakes park() above, or makes its very next call return immediately
```

`thread_handle` (matching Rust's `std::thread::Thread`) is a cheap,
cloneable, `Send + Sync` reference to a specific thread's parker:
`this_thread::current()` (from any thread) captures a clone, and
`unpark()` called through it wakes the thread it was obtained from, even
after that thread has since exited (the underlying `shared_ptr` keeps the
parker itself alive regardless). Tokens do not accumulate -- calling
`unpark()` any number of times before the thread next parks is equivalent
to calling it once, matching Rust's own semantics exactly.

`this_thread::park()`/`park_timeout(duration)` block the calling thread
until its token becomes available (consuming it), returning immediately
(still consuming the token) if one is already available. Unlike Rust's
`park_timeout`/`park_deadline` (which return nothing -- the caller must
re-check its own condition after either call returns), `park_timeout`
here returns `bool`: `true` if a token was consumed, `false` if the
timeout elapsed first, matching `futex_wait_timeout`'s/
`condition_variable::wait_for`'s own boolean/`result<bool>` outcome shape
elsewhere in reloco. Still safe to ignore, exactly like Rust's
spurious-wakeup-tolerant contract.

`this_thread::sleep_for(duration)` is unrelated to parking: it blocks the
calling thread for (at least) the given duration unconditionally, waiting
on a throwaway, never-woken `futex_word` (see `futex.hpp`) private to the
call -- re-deriving the remaining time from a fixed deadline
(`instant::now() + timeout`) after every spurious wakeup -- so it never
consumes or is affected by the calling thread's own park token, and stays
available under every `futex.hpp` backend.

## `once_lock<T>`

`include/reloco/once_lock.hpp`

A cell that can be written at most once and read many times after that,
matching Rust's `std::sync::OnceLock<T>`. Unlike `fallible_singleton<T>`/
`atomic_fallible_singleton<T>` (`fallible_singleton.hpp`),
which each provide exactly one static, process-wide instance per `T`,
`once_lock<T>` is an ordinary value type -- usable as a struct field, a
local, or a container element -- so a program can have as many
independently-initialized cells as it needs.

```cpp
reloco::once_lock<reloco::string> config_path;

// From any thread, any number of times:
auto entry = config_path.get_or_try_init([]() -> reloco::result<reloco::string> {
  return load_config_path(); // returns result<string>
});
if (entry)
  use_path(**entry);
```

- `try_set(T)` -> `result<void>`: fails with `error::already_exists` if
  the cell is already initialized (matching Rust's `OnceLock::set`, minus
  recovering the rejected value -- reloco's single `error` enum carries no
  payload).
- `get_or_try_init(F)` -> `result<T *>`, where `F` is invocable as
  `result<T>()`: returns the existing value if already initialized,
  otherwise blocks concurrent callers while exactly one of them runs `F`
  and stores its result. If `F` fails, the cell reverts to empty so a
  later call (from any thread) may retry, matching Rust's
  `OnceLock::get_or_try_init`.
- `get_or_init(F)` -> `T &`, where `F` is invocable as `T()` (not
  `result<T>()`) and assumed to never fail: infallible convenience
  wrapper around `get_or_try_init`, matching Rust's stable
  `OnceLock::get_or_init`.
- `get()`/`get_mut()` -> `const T *`/`T *`: `nullptr` if not yet
  initialized, never blocking.
- `take()` -> `result<T>`: resets the cell to empty and returns the
  previous value, failing with `error::not_initialized` if the cell was
  already empty (matching Rust's `OnceLock::take(&mut self)`, which
  returns `Option<T>` -- reloco represents "nothing to take" via the
  file's own error case instead, consistent with every other fallible
  reloco operation). Like Rust's `&mut self` requirement, the caller must
  ensure no other thread concurrently reads/writes the cell.

Internally, a `futex_word` state (see `futex.hpp`) gives `get()`/
`get_mut()`, and the fast path of `try_set`/`get_or_try_init`/
`get_or_init`, a lock-free acquire-load once initialized; the slow path
claims the `empty` -> `initializing` transition via a single
`compare_exchange` and blocks on (or wakes via `futex_wake_all`) the same
state word -- no lock is ever held. `T` must be
`std::is_nothrow_move_constructible_v`, like
every other reloco container element requirement. `once_lock<T>` is
neither copyable nor movable.

`is_send<once_lock<T>>` forwards to `is_send<T>`. `is_sync<once_lock<T>>`
requires both `is_send<T>` and `is_sync<T>`, matching Rust's `unsafe impl
<T: Send + Sync> Sync for OnceLock<T>`: unlike `guarded_mutex<T>`
(`Mutex<T>`, only ever reached through an exclusive lock), a `const
once_lock<T> &` hands out a bare `const T *` once ready, so concurrent
readers need `T` itself to tolerate concurrent shared access.

## `once`

`include/reloco/once.hpp`

Runs a closure exactly once across any number of racing callers, blocking
every other caller until it completes, matching Rust's
`std::sync::Once`. Complements `once_lock<T>` rather than replacing it:
`once` carries no value at all -- it is purely a "has this run yet" gate
around a side-effecting closure, matching Rust's own split between `Once`
(side effects only) and `OnceLock<T>`/`LazyLock<T>` (a value).

```cpp
reloco::once init_logging;

// From any thread, any number of times:
init_logging.call_once([] { setup_logging(); });
```

- `call_once(F)`, where `F` is invocable as `void()`: runs `F` exactly
  once and is assumed to never fail, matching Rust's
  `Once::call_once`. Every caller -- including the one actually running
  `F` -- returns only once `F` has completed.
- `try_call_once(F)`, where `F` is invocable as `result<void>()`: same as
  `call_once`, but if `F` fails, `once` reverts to not-yet-run so a later
  call (from any thread) may retry. This is a reloco-specific extension:
  Rust's `Once` has no failure-recovery equivalent of its own, since
  `call_once`'s `F` cannot fail (only panic, which poisons the `Once`
  permanently) -- reloco has no panic/unwind mechanism, so there is no
  `call_once_force`/`OnceState` poisoning-recovery API to port.
- `is_completed()` -> `bool`: `true` once `F` has run to completion,
  matching Rust's `Once::is_completed()`. Never blocks.
- `unsafe_reset()`: unconditionally resets this `once` to not-yet-run, so
  the next `call_once`/`try_call_once` runs its closure again. **Unsafe**:
  only sound with no other thread concurrently calling
  `call_once`/`try_call_once`/`is_completed` on the same instance. Not
  part of Rust's `Once` (no reset at all), but matches
  `parking_lot::Once::reset(&mut self)`'s exclusive-access contract;
  useful for re-running one-time initialization after `fork()` in a
  freestanding/kernel context, or resetting a `once` in a test fixture.

Built directly on a single `futex_word` state (`not_started`/`running`/
`completed`, see `futex.hpp`) rather than a `mutex` + `condition_variable`
pair, with no heap allocation and no dependency on `mutex.hpp` at all --
`once` (and anything built on it) stays usable in a freestanding or
bare-kernel environment that provides its own `RELOCO_FUTEX_BACKEND_CUSTOM`
but has no OS-backed mutex/thread available at all. `once` is neither
copyable nor movable.

## `lazy_lock<T, F>`

`include/reloco/lazy_lock.hpp`

A value lazily initialized, at most once, from a closure captured at
construction time, matching Rust's stable `std::sync::LazyLock<T, F>`
(née `once_cell::sync::Lazy<T, F>`). Built directly on `once_lock<T>`:
`lazy_lock<T, F>` is exactly a `once_lock<T>` paired with the closure `F`
that knows how to fill it.

```cpp
reloco::lazy_lock config([]() -> reloco::string { return load_config(); });
use(*config); // first dereference anywhere runs the closure exactly once
```

Unlike `once_lock<T>::get_or_init(F)`, which takes the initializer at
every call site, `lazy_lock<T, F>` captures `F` once, at construction --
every `operator*`/`operator->`/`get()` call afterward takes no arguments,
the right shape for a lazily-initialized `static`/global or struct field.
A deduction guide lets a local `lazy_lock` be declared directly from a
closure (as above); a `static`/global or struct field, which must name a
concrete type, uses `function.hpp`'s `function<T()>` as `F` explicitly.

- `operator*()`/`operator->()`/`get()` -> `const T &`/`const T *`/`const
  T *`: run the captured closure first if this is the first call (from
  any thread; concurrent callers block until it completes), matching
  Rust's `LazyLock`'s `Deref`.

`F` must be invocable as `T()`, and is stored for the lifetime of the
`lazy_lock` (unlike Rust's `LazyLock`, which drops its closure in place
once it has run) -- reloco keeps `F` and `T` in separate members instead,
trading a few extra bytes for a substantially simpler implementation.
`is_send<lazy_lock<T, F>>` requires both `is_send<T>` and `is_send<F>`;
`is_sync<lazy_lock<T, F>>` additionally requires `is_sync<T>`, matching
Rust's `unsafe impl<T, F: Send> Sync for LazyLock<T, F> where OnceLock<T>:
Sync`.

## `hint::spin_loop()`

`include/reloco/hint.hpp`

A hardware hint that the calling thread is in a busy-wait spin loop,
matching Rust's `std::hint::spin_loop()`. Emits the target architecture's
dedicated spin-wait instruction where one exists (x86/x86-64 `pause`,
AArch64/AArch32 `yield`, POWER `or 27,27,27`, RISC-V `Zihintpause` --
emitted as the raw `0x0100000F` opcode rather than the `pause` mnemonic, so
it compiles on older toolchains and safely executes as a harmless `fence`
on silicon that predates the extension --, MIPS32r2+ `pause`, SPARC V9 `rd
%ccr, %g0`), which lets a hyperthreaded/SMT sibling core run without
actually yielding the CPU back to the scheduler; falls back to a plain
inline-asm compiler fence on an architecture with no such instruction
(including LoongArch, s390x, and WebAssembly, which are explicitly routed
to this fallback) or on an unrecognized/legacy target. No OS dependency at
all -- usable in a freestanding/bare-kernel build. A hand-rolled spinlock
(or any other busy-wait loop) should call this once per spin iteration,
exactly like Rust's own spinlock crates call `std::hint::spin_loop()`.

## `wait_group`

`include/reloco/wait_group.hpp`

Waits for an unknown-in-advance number of cloned handles to all be
dropped, matching crossbeam-utils's `WaitGroup` (Rust ecosystem, not
`std`) -- unlike `barrier`, which needs the exact participant count up
front.

```cpp
auto wg_result = reloco::wait_group::try_create();
reloco::wait_group wg = std::move(*wg_result);

for (auto &task : tasks) {
  reloco::wait_group clone = wg; // one outstanding unit per task
  reloco::spawn([clone = std::move(clone), &task]() mutable {
    task.run();
    // clone's destructor here signals this task's completion
  });
}

std::move(wg).wait(); // blocks until every clone above has been dropped
```

- `wait_group::try_create(allocator_ref)` -> `result<wait_group>`:
  allocates a fresh `wait_group` with one outstanding handle (this one).
- Copy constructor/assignment: clones the handle, one more outstanding
  unit of work, matching `Clone`.
- `wait() &&` -> `void`: consumes this handle (dropping its own
  outstanding unit, like every other clone's destructor) and blocks until
  every other clone has also been dropped, matching Rust's
  `WaitGroup::wait(self)`. Call via `std::move(wg).wait()`.

Built on a `shared_ptr<detail::wait_group_state>` (an atomic refcount
plus a `futex_word`) -- copying increments the shared count, dropping one
decrements it and wakes any blocked `wait()` once it reaches zero. No
mutex/condition_variable involved. `wait_group` carries no user data of
its own, so it is unconditionally `Send`/`Sync`.

## `scope` / `thread_scope` / `scoped_join_handle<R>`

`include/reloco/scope.hpp`

Matches Rust's `std::thread::scope`: runs a closure that may spawn
threads borrowing references to the caller's own stack frame, guaranteed
to have all finished running before `scope()` itself returns. `thread.hpp`'s
`spawn(F, allocator_ref)` requires `F: Send + 'static` (documented, not
statically enforced -- reloco has no lifetime tracking, so nothing stops
a captured reference from actually outliving the spawned thread if the
caller gets it wrong); `scope()` closes that gap the same way Rust does.

```cpp
int local = 0;
auto result = reloco::scope([&](reloco::thread_scope &s) {
  auto handle = s.spawn([&local]() noexcept { local = 42; });
  if (handle)
    std::move(*handle).join();
});
// result: result<void>; local == 42 here, guaranteed.
```

- `reloco::scope(body, allocator_ref)` -> `result<R>`, where `R` is
  `body`'s own return type (invoked as `body(thread_scope &)`): fails
  only if allocating the internal shared completion-tracking state
  fails; `body` is otherwise invoked unconditionally and its result is
  forwarded as-is.
- `thread_scope::spawn(F, allocator_ref)` -> `result<scoped_join_handle<R>>`,
  where `R = std::invoke_result_t<F &>`, matching Rust's `Scope::spawn`.
  `F` (and everything it captures) must be `is_send_v` (see
  `send_sync.hpp`), same as `reloco::spawn` -- but, unlike
  `reloco::spawn`, `F` may capture a reference to any value that outlives
  the enclosing `scope()` call, since every thread spawned through a
  `thread_scope` is guaranteed to have finished before `scope()` returns.
- `scoped_join_handle<R>::join() &&` blocks until the thread finishes and
  returns its result (consuming `*this`), matching Rust's
  `ScopedJoinHandle::join()`. A handle the caller never explicitly joins
  is still safely joined on its own destruction, exactly like
  `join_handle<R>` (see `thread.hpp`).

Internally, `scope()` allocates one shared, atomically-refcounted
completion counter (a `futex_word`, see `futex.hpp`) that
every `thread_scope::spawn()` call increments before handing its closure
to `reloco::spawn()`, and decrements (waking up `~thread_scope()`, which
blocks until it reaches zero, via `futex_wake_all`) right after the
closure returns, still running on the spawned thread. This matches Rust's
own `std::thread::scope` implementation, which also does not literally
join every spawned thread to know when it is safe to return, just waits
for this kind of completion signal -- the OS-level join
`scoped_join_handle<R>` performs is a separate, purely
resource-reclamation concern from this borrow-safety guarantee.

`is_send<scoped_join_handle<R>>` forwards to `is_send<R>`;
`is_sync<scoped_join_handle<R>>` is always `true`, both matching
`join_handle<R>`'s own specializations.

## `alignment_of<T>`

`include/reloco/alignment.hpp`

Customization-point trait controlling the allocation/storage alignment
`vector<T>`, `array<T, N>`, and `inline_vector<T, Capacity>` request for `T`,
independent of `alignof(T)`. Defaults to `alignof(T)`; specialize it (once,
per element type, like `is_trivially_relocatable<T>`) to request SIMD-width
over-alignment (e.g. 32 bytes for AVX) without redeclaring `T` itself with
`alignas(...)`. `effective_alignment_v<T>` is `std::max(alignof(T),
alignment_of_v<T>)` -- the value containers actually use -- and never lets a
specialization weaken alignment below `alignof(T)`. See
[Over-alignment](alignment.md).

## `scope_guard<Callable>` / `RELOCO_DEFER(fn)`

`include/reloco/scope_guard.hpp`

A zero-allocation RAII scope-exit guard matching Rust's `scopeguard` crate
(and Go's `defer`): stores a callable inline and invokes it exactly once,
when the guard is destroyed, unless `cancel()` was called first.

```cpp
{
  reloco::scope_guard guard([] { std::puts("cleanup"); }); // CTAD deduces Callable
  // ... "cleanup" runs here, at scope exit ...
}
```

`cancel()` disarms the guard (its callable no longer runs on destruction);
moving a `scope_guard` transfers responsibility for running the callable to
the destination and disarms the moved-from guard. Because a temporary
`reloco::scope_guard(...)` not bound to a variable would run its callable
*immediately* (at the end of the full expression) rather than at the
enclosing scope's exit, `RELOCO_DEFER(fn)` is the safe way to use it as a
statement: it expands to a uniquely-named local `scope_guard` variable (via
`__LINE__`-based name mangling), so `RELOCO_DEFER([] { ... });` always
defers to the actual end of the enclosing scope.

```cpp
void handle_request(connection &conn) {
  RELOCO_DEFER([&] { conn.close(); });
  // ... early returns below still close() exactly once ...
}
```

## `tx_guard<T, RollbackFn>` / `shadow_tx<T>` / `checkpoint_guard<T>`

`include/reloco/commit.hpp`

Three zero-allocation RAII transaction primitives for deterministic,
hard-real-time-safe state mutation, each committing an in-progress change
exactly once or automatically undoing it if the scope is left first
(exception-free, so "left early" means an early `return`, not unwinding).

- **`tx_guard<T, RollbackFn>`** owns an arbitrary payload `T` plus a
  rollback closure. `get()` grants access to the payload while the
  transaction is open; `commit()` disarms the rollback and returns the
  payload by value; `rollback()` runs the rollback closure immediately
  instead of waiting for destruction. If neither is called, the
  destructor invokes the rollback closure on the still-owned payload.
  Move-only (copying a linear/affine transaction handle makes no sense);
  CTAD deduces both template parameters.

  ```cpp
  reloco::tx_guard tx{allocate_buffer(), [](Buffer *b) { free_buffer(b); }};
  fill_buffer(tx.get());
  if (hardware_fail())
    return; // buffer is freed automatically here
  Buffer *final_buf = tx.commit(); // rollback disarmed, buffer handed back
  ```

- **`shadow_tx<T>`** (`T` must be trivially copyable) snapshots a *live*
  target into a local stack copy on construction; `get()` mutates only the
  shadow, leaving the live target completely untouched until `commit()`
  performs a single write-back assignment. If `commit()` is never called,
  the shadow is simply discarded -- the live target was never touched.

  ```cpp
  reloco::shadow_tx tx(global_config);
  tx.get().baud_rate = 115200;
  if (!validate(tx.get()))
    return; // global_config is left untouched
  tx.commit(); // global_config is now updated
  ```

- **`checkpoint_guard<T>`** (`T` must be trivially copyable) is the
  opposite strategy: it mutates the *live* target directly, but caches a
  backup snapshot on construction, and restores that backup automatically
  on destruction unless `commit()` was called first -- suited to hardware
  registers or other state that must be observed live while a change is
  tentative.

  ```cpp
  reloco::checkpoint_guard tx(UART1_CONFIG_REG);
  UART1_CONFIG_REG |= ENABLE_DMA;
  if (dma_timeout())
    return; // UART1_CONFIG_REG is automatically restored
  tx.commit(); // change becomes permanent
  ```

## `epoch_trackable` / `epoch_handle<T>` / `epoch_guard<T>`

`include/reloco/epoch.hpp`

An O(1), 8-byte "weak pointer" equivalent purpose-built for statically
allocated/RTOS memory pools that recycle fixed slots via placement-new,
where a full `shared_ptr`/`weak_ptr` refcount is unaffordable. It detects
*staleness* (the slot having been recycled since a handle was taken) --
it does **not** provide mutual exclusion; see the header's own extensive
doc comment for the required external-locking/IRQ-block/thread-confinement
patterns needed alongside it in a concurrent environment.

`epoch_trackable` is a 4-byte mixin (an `std::atomic<epoch_t>`, `epoch_t =
uint32_t`) that user RTOS objects inherit from; it is neither copyable nor
movable, since the generation is tied to the object's physical address.
`bump_epoch()` (called by the pool when recycling a slot) atomically
increments the generation, skipping `0` (reserved as "invalid"):

```cpp
struct RtosTask : reloco::epoch_trackable {
  explicit RtosTask(int id) : reloco::epoch_trackable(1), id(id) {}
  int id;
};

RtosTask task(42);
reloco::epoch_handle<RtosTask> handle(task); // snapshots address + generation
task.bump_epoch();                           // pool recycles the slot

auto guard = handle.lock();     // epoch_guard<RtosTask>
if (guard.is_alive())           // false: generation no longer matches
  guard.get().id;               // never reached
```

`epoch_handle<T>::lock()` returns an `epoch_guard<T>`: a `-Wconsumed`
typestate-tracked, `[[nodiscard]]` type starting in an unverified state.
`is_alive()` is the `RELOCO_TEST_TYPESTATE`-annotated check that, once it
returns `true`, transitions the guard to a state from which `get()`/
`operator->()` become callable -- Clang statically rejects dereferencing
the guard before that check. `epoch_handle<T>::is_empty()` checks only
whether the handle itself was ever bound to an object, independent of
whether that object has since been recycled.

## `uninit<T>`

`include/reloco/uninit.hpp`

A `-Wconsumed` typestate-tracked wrapper over `alignas(T) sizeof(T)` raw
storage, mathematically preventing (on Clang) both reading before
initialization and double-initialization without an intervening
`destroy()`. Every accessor is additionally `RELOCO_UNSAFE_BUFFER_USAGE`-
gated, since manual placement-new/explicit-destructor lifetime management
is exactly the kind of code that `-Wunsafe-buffer-usage` exists to make
visible at the call site.

```cpp
reloco::uninit<TelemetryPacket> pkt; // starts in the 'consumed' (empty) state
pkt.write(0x01, 100);                // constructs in place -> 'unconsumed'
transmit(pkt.get());                 // safe to read
pkt.destroy();                       // back to 'consumed'
```

`write(args...)` constructs `T` in place from `args...` and returns a
reference to it (callable only from `consumed`; transitions to
`unconsumed`). `assume_init()` is the escape hatch for memory filled by
something other than a C++ constructor (e.g. DMA/a memory-mapped
peripheral): it performs no construction, just tells the typestate
tracker the memory is now initialized, and returns a `T *` via
`std::launder`. `get()`/`get_mut()` provide checked (callable only from
`unconsumed`) read/write access; `extract()` move-constructs the value
out, destroys the original, and returns to the `consumed` state in one
step, matching `std::optional<T>::value()` combined with `reset()`.
`destroy()` explicitly runs `~T()` and returns to `consumed`. `uninit<T>`
is move-disabled (copy is deleted; no move constructor is provided), since
a bitwise-copied typestate would desynchronize from the actual storage --
prefer `write`/`extract` to transfer a value between slots.

## `guarded_seqlock<T, MutexT = mutex>`

`include/reloco/seqlock.hpp`

Fuses `guarded_mutex<T, MutexT>`-style hardware exclusion for writers with
a lock-free, sequence-counter-verified snapshot read path for readers,
matching the Linux kernel's `seqlock(9)`. `T` must be trivially copyable
(the read path is a `std::memcpy` snapshot) and `is_send<T>` (safe to hand
across the thread boundary between writer and readers).

- **Writers** call `write_lock()` (blocks until the underlying `MutexT` is
  acquired) or `try_write_lock()` (the fallible tier, `result<write_guard>`
  failing with `error::busy`); either returns a `RELOCO_SCOPED_CAPABILITY`
  `write_guard` that bumps the sequence counter to *odd* on acquisition
  (signaling "a write is in progress" to readers) and back to *even* plus
  releases the mutex on destruction.

  ```cpp
  reloco::guarded_seqlock<Telemetry> shared;
  {
    auto g = shared.write_lock();
    g->altitude = 1200; // exclusive access, same as guarded_mutex
  } // sequence becomes even again; mutex released
  ```

- **Readers** never touch the mutex at all. `read()` spins internally: it
  constructs a `read_tx` (a `-Wconsumed` typestate-tracked, `snapshot_`-
  carrying value), which `memcpy`s the payload only if the sequence was
  even at the start; `verify()` (`RELOCO_TEST_TYPESTATE`) re-checks that
  the sequence is still even and unchanged after the copy, and `extract()`
  (callable only once `verify()` has returned `true`) returns the
  snapshot. `read()` loops calling `hint::spin_loop()` between attempts
  until a torn-free snapshot is obtained -- there is no reader-side
  blocking or syscall at all.

  ```cpp
  Telemetry snapshot = shared.read(); // never blocks on the writer's mutex
  ```

Clang's thread-safety analysis (`RELOCO_CAPABILITY("mutex")`/
`RELOCO_GUARDED_BY(this)`) is applied to the payload as if only writers
ever touch it; the reader path is explicitly
`RELOCO_NO_THREAD_SAFETY_ANALYSIS`-annotated where it deliberately bypasses
the mutex, since that bypass is exactly what makes readers lock-free.

## `isolated_node_tx<Container, T>` / `extract_if_iterator<Container, Pred>`

`include/reloco/intrusive_iteration.hpp`

A typestate-enforced RAII handle for safely detaching a node from one
intrusive container (for example a `boost::intrusive::list`, where a node
is embedded directly in its owning object rather than heap-allocated
separately) and explicitly relinking it into another container or
disposing of it -- without ever leaving the node in a state reachable from
two containers, or leaking it if the caller forgets to route it anywhere.

`isolated_node_tx` is `RELOCO_CONSUMABLE(unconsumed)`-tagged: its
destructor `RELOCO_ASSERT`s if the transaction was dropped without being
consumed, so an un-routed extracted node is a hard, immediate failure
rather than a silent leak or dangling intrusive-hook state. Exactly one of
two consuming methods must be called:

- `relink_to(container, inserter)` hands the node to `inserter(container,
  node)` (a caller-supplied closure choosing *how* to insert -- `push_back`,
  a sorted insert, etc.) and asserts the destination is not the same
  container instance the node was extracted from.
- `release_to(disposer)` hands the raw node pointer to `disposer(node)`,
  the sink for returning it to an RTOS memory pool/slab allocator instead
  of another container.

Before either is called, `get()`/`get_mut()` grant checked (`unconsumed`-
only) read/write access to the isolated node -- safe because it is, by
construction, no longer linked into any container's traversal structure.

`extract_if_iterator<Container, Pred>` is an `iterator_adaptor` (see
`iterator.hpp`) whose `item_type` is `isolated_node_tx<Container,
Container::value_type>`: it walks `Container` once, and for every element
where `Pred` returns `true`, erases it from the container and yields an
`isolated_node_tx` wrapping it, ready to be routed via `relink_to`/
`release_to` in a `.for_each(...)` (or any other terminal operation).
`Container` only needs `begin()`/`end()` plus an `erase(iterator)` that
returns an iterator to the following element (matching `std::list::erase`/
`boost::intrusive::list::erase`) -- the traversal cursor advances directly
off of that return value, so nothing here ever needs `iterator_to` or a
lookahead increment. `reloco::intrusive_hash_table` (see above) implements
exactly this surface, so it is usable with `extract_if()` the same way
`boost::intrusive::list` is.

```cpp
reloco::extract_if(active_queue, [](Task &t) { return t.is_blocked || t.tick(); })
    .for_each([&](auto &&tx) {
      if (tx.get_mut().is_blocked)
        tx.relink_to(blocked_queue, [](TaskList &dest, Task &t) { dest.push_back(t); });
      else
        tx.release_to([](Task *t) { /* return to memory pool */ });
    });
```

See [`demos/intrusive_iteration_demo.cpp`](../demos/intrusive_iteration_demo.cpp)
for a complete RTOS-scheduler-style walkthrough built on
`boost::intrusive::list`.

## `fault_injector<Tag, Args...>` / `RELOCO_FAULT_POINT` / `RELOCO_FAULT_POINT_ARGS`

`include/reloco/fault_injection.hpp`

A header-based, C++-only fault injection framework for deterministically
reproducing concurrency races and other "impossible timing" bugs in
single-threaded tests, instead of relying on flaky, genuinely concurrent
repro attempts. See [Fault injection](fault-injection.md) for the full
guide; summary below.

Production code marks one location as an injectable fault point with a
caller-defined tag type identifying it (an ordinary otherwise-unused type,
matching every other reloco `Tag` template parameter's convention, e.g.
`tls_provider<T, Tag>`):

```cpp
struct commit_race_point {};

void producer_commit(std::uint64_t &write_idx, std::uint64_t count) {
  std::uint64_t w = write_idx;
  RELOCO_FAULT_POINT_ARGS(commit_race_point, w); // hook may mutate `w` right here
  write_idx = w + count;
}
```

A test arms that fault point by constructing a `fault_injector<Tag,
Args...>` -- a caller-owned, non-copyable, non-movable scoped control
block (ordinarily a plain stack local) wrapping a hook closure -- for as
long as it should intercept that fault point on the calling thread:

```cpp
TEST(Commit, SurvivesConcurrentIndexMutation) {
  auto hook = [](std::uint64_t &w) { w = 0xDEADBEEF; }; // named local: must outlive `fi`
  reloco::fault_injector<commit_race_point, std::uint64_t> fi(hook);
  // ... call producer_commit() and assert on the resulting (mis)behavior ...
}
```

Two convenience macros shrink the common case to one line each:
`RELOCO_FAULT_TAG(name)` expands to `struct name {};`, and
`RELOCO_FAULT_INJECTOR(var, Tag, ...)` declares a named hook local
(`var##_hook`) followed by `var`, a `fault_injector<Tag, Args...>` armed
with it, deducing `Args...` from the hook's own call signature -- so the
example above can instead be written as `RELOCO_FAULT_INJECTOR(fi,
commit_race_point, [](std::uint64_t &w) { w = 0xDEADBEEF; });`, which also
structurally rules out passing a dangling inline temporary (the macro
always names the hook local first). This deduction only supports a hook
whose call operator is not itself a template (so not a generic lambda);
construct the `fault_injector` directly, naming `Args...`, in that case.

By default -- whenever `RELOCO_ENABLE_FAULT_INJECTION` is not defined
before the first inclusion of this header -- `RELOCO_FAULT_POINT`/
`RELOCO_FAULT_POINT_ARGS` expand to nothing and `fault_armed<Tag,
Args...>()` always returns `false`: the entire mechanism compiles out at
zero cost in a normal build, with no `#ifdef` needed at any fault-point
call site.

The framework itself never allocates. Its own bookkeeping storage picks
one of two designs, chosen automatically (unless the consumer defines
`RELOCO_FAULT_INJECTION_UNLIMITED_TLS` itself, to `0` or `1`) based on
`RELOCO_TLS_MODEL`:

- Under `RELOCO_TLS_MODEL_PTHREAD`/`RELOCO_TLS_MODEL_OS` (where distinct
  TLS slots are a scarce, backend-limited resource -- e.g.
  `pthread_key_create()`'s `PTHREAD_KEYS_MAX`), it spends exactly **one**
  thread-local, pointer-sized slot for the entire program, shared by every
  `Tag`, backed by `tls_provider<void *, detail::fault_root_tag>`'s
  zero-allocation raw-pointer specialization. That slot holds the head of
  an intrusive, per-thread stack of *every* currently active
  `fault_injector`, across every `Tag`; each records its own `Tag`/
  `Args...` signature as a `reloco::type_id` (see [`type_id` /
  `type_id_of<T>()`](#type_id--type_id_oft)) so a fault point can find the
  right one by walking the stack and comparing signatures -- `O(number of
  currently active fault_injectors on this thread)`, trading a small
  linear scan for a constant, program-wide TLS budget. Because the stack
  is shared, *all* `fault_injector`s on a thread -- not just same-`Tag`
  ones -- must be destroyed in strict reverse-construction order.
- Under `RELOCO_TLS_MODEL_THREAD_LOCAL`/`RELOCO_TLS_MODEL_SINGLE` (where a
  TLS slot is just an ordinary `thread_local`/`static` variable, so slots
  are effectively unlimited) it instead spends one such slot **per
  `Tag`**, backed by `tls_provider<void *, Tag>`, each holding its own
  private per-`Tag` stack -- `O(1)` lookup, and only same-`Tag` nesting
  order is constrained.

Either way, the framework never owns the `fault_injector` object its
slot(s) point to, only the caller does. Constructing a `fault_injector`
links it onto its stack; destroying it unlinks itself and restores
whichever `fault_injector` (if any) was active before it, in strict LIFO
order -- so nesting composes exactly like nested `scope_guard`s, and a
`fault_injector` must never be copied, moved, or used as a temporary
(its own address is load-bearing).

Arming is thread-local by construction: a `fault_injector` armed on one
thread has no effect on a fault point reached on another thread. To
inject into a specific worker thread (e.g. one spawned via
`reloco::spawn`), arm the `fault_injector` from inside that thread's own
closure -- this is precisely what makes reproduction deterministic
instead of racing against genuine concurrency.

Under the default shared-stack design (`RELOCO_TLS_MODEL_PTHREAD`/`_OS`),
a mismatched `Args...` for the same `Tag` is safely detected at runtime by
the `type_id` signature check (the fault point simply won't find a
match); under the per-`Tag`-slot design
(`RELOCO_FAULT_INJECTION_UNLIMITED_TLS`), no such check is performed, so
`Tag` must be used with exactly one `Args...` signature throughout the
program. Either way, pick a distinct, single-purpose `Tag` per fault
point -- matching every other reloco `Tag` template parameter's
convention -- and this is never ambiguous in practice.

## `fault_injection_patterns.hpp`

`include/reloco/fault_injection_patterns.hpp`

Convenience macros for `fault_injection.hpp`'s most common and more
advanced fault-arming patterns, kept in their own header so
`fault_injection.hpp` itself stays minimal. Built entirely on top of its
public/`detail` API (`RELOCO_FAULT_INJECTOR`, `fault_hook_signature`,
`make_fault_injector`) -- nothing here needs any special access to
`fault_injection.hpp`'s internals.

- `RELOCO_FAULT_MUTATE(var, Tag, Type, ...)` / `RELOCO_FAULT_SET(var, Tag,
  Type, value)` -- mutate (arbitrary body) or unconditionally overwrite a
  single exposed value every time the fault point fires.
- `RELOCO_FAULT_TOGGLE(var, Tag, Type)` / `RELOCO_FAULT_INCREMENT(var,
  Tag, Type, delta)` -- `RELOCO_FAULT_MUTATE` sugar for the two other
  most common bodies: flip a `bool`-like value, or nudge a counter/index
  by a fixed amount.
- `RELOCO_FAULT_SPY(var, Tag, counter)` -- count how many times a fault
  point is reached, without touching any of its arguments.
- `RELOCO_FAULT_FIRE_N(var, Tag, count, ...)` / `RELOCO_FAULT_FIRE_ONCE(
  var, Tag, ...)` -- only actually fire the hook the first @p count
  times (once, for `FIRE_ONCE`); every later hit is a silent no-op.
- `RELOCO_FAULT_SKIP_N(var, Tag, skip, ...)` -- the mirror image of
  `FIRE_N`: silently skip the first @p skip hits, then fire on every hit
  after that, for as long as the injector stays alive.
- `RELOCO_FAULT_NTH(var, Tag, n, ...)` -- fire exactly once, on the
  single, exact @p n'th (1-based) hit, and never before or after.
- `RELOCO_FAULT_EVERY_N(var, Tag, n, ...)` -- fire periodically, once
  every @p n hits, for simulating an intermittent rather than one-shot
  fault.
- `RELOCO_FAULT_WHEN(var, Tag, pred, ...)` -- only fire when a
  caller-supplied predicate (receiving the fault point's own arguments,
  by reference) returns `true`.

```cpp
RELOCO_FAULT_TAG(commit_race_point);
TEST(Commit, SurvivesConcurrentIndexMutation) {
  RELOCO_FAULT_SET(fi, commit_race_point, std::uint64_t, 0xDEADBEEF);
  // ... call producer_commit() and assert on the resulting (mis)behavior ...
}
```

Every macro respects `fault_injection.hpp`'s own `RELOCO_ENABLE_FAULT_
INJECTION` opt-in: with it undefined, the `fault_injector` each ultimately
constructs is the same true no-op it always is. See [Fault
injection](fault-injection.md) for the full guide.

## Lifetime and safety annotation macros

`include/reloco/lifetime.hpp`, `include/reloco/rvalue_safety.hpp`

Compiler-feature-detected macros used throughout the library:
`RELOCO_LIFETIMEBOUND` (Clang `[[clang::lifetimebound]]`),
`RELOCO_OWNER`/`RELOCO_POINTER` (GSL owner/pointer annotations for static
analyzers), `RELOCO_UNSAFE_BUFFER_USAGE`/`RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/
`RELOCO_END_UNSAFE_BUFFER_USAGE` (Clang `-Wunsafe-buffer-usage` opt-in
gating), and `RELOCO_BLOCK_RVALUE_ACCESS(Type)` (deletes a container's
rvalue accessors so a borrow can never outlive a temporary). See
[Lifetime safety](lifetime-safety.md).

## `reloco_config.hpp`

`include/reloco/reloco_config.hpp`

The library-wide user-override mechanism: provide `reloco_user_config.hpp`
through `JPLCZ_RELOCO_PORTING_HEADERS`, which stages it at
`detail/porting/reloco_user_config.hpp`. Its presence automatically overrides
compile-time defaults (e.g. `RELOCO_DEFAULT_ALLOCATOR_CUSTOM`,
assertion-handling behavior) before any other reloco header is processed.
See the header's own documentation for the exact mechanism and its
constraints (why it cannot itself include headers like `allocator.hpp`).
