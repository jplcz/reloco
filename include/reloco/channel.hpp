// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file channel.hpp
 * @brief Multi-producer, single-consumer channel, matching Rust's
 * `std::sync::mpsc::channel`.
 *
 * `channel<T>(allocator_ref)` returns a `result<std::pair<sender<T>,
 * receiver<T>>>`: `sender<T>` is `Clone` (an ordinary copy constructor, like
 * `rc<T>`/`shared_ptr<T>`'s own copy-is-clone convention) so any number of
 * threads may each hold/drop their own clone; `receiver<T>` is move-only --
 * matching Rust's single-consumer contract, only one thread should ever
 * call `recv()`/`try_recv()` on a given channel.
 *
 * Internally, unbounded (an allocating, intrusively-linked-list queue --
 * `try_send` fails with the allocator's own error if the node allocation
 * itself fails, never blocking the sender), guarded by one `mutex` +
 * `condition_variable` pair (see `mutex.hpp`) shared between all senders
 * and the receiver via an atomically-refcounted `shared_ptr` (not `rc<T>`,
 * which is deliberately `!Send`/`!Sync` -- see `send_sync.hpp`).
 *
 * - `sender<T>::try_send(T)` -> `result<void>`: fails with
 *   `error::invalid_state` if the receiver has already been dropped
 *   (matching Rust's `SendError<T>`, minus recovering the un-sent value --
 *   reloco's single `error` enum carries no payload).
 * - `receiver<T>::recv()` -> `result<T>`: blocks until a value is sent or
 *   every sender has been dropped, in which case it fails with
 *   `error::container_empty` (matching Rust's `RecvError`).
 * - `receiver<T>::recv_timeout(duration)` -> `result<T>`: bounded
 *   `recv()`; additionally fails with `error::timed_out` if `duration`
 *   elapses first (matching Rust's `mpsc::Receiver::recv_timeout` and its
 *   `RecvTimeoutError::Timeout`/`RecvTimeoutError::Disconnected` cases).
 * - `receiver<T>::try_recv()` -> `result<T>`: never blocks; fails with
 *   `error::try_again` if the queue is momentarily empty but senders remain
 *   (matching Rust's `TryRecvError::Empty`), or `error::container_empty` if
 *   it is empty and permanently will be (matching
 *   `TryRecvError::Disconnected`).
 * - `receiver<T>::begin()`/`end()` -> input iterators consuming the
 *   channel via blocking `recv()` calls, matching Rust's `impl
 *   Iterator for Receiver<T>`/`for value in rx`; `try_iter()` returns the
 *   same iterator shape driven by non-blocking `try_recv()` instead,
 *   matching `Receiver::try_iter()`.
 *
 * `sync_channel<T>(capacity, allocator_ref)` returns a `result<
 * std::pair<sync_sender<T>, receiver<T>>>`: a bounded counterpart of
 * `channel<T>`, matching Rust's `std::sync::mpsc::sync_channel`. The same
 * `receiver<T>` is reused unchanged; only the sending half differs:
 *
 * - `sync_sender<T>::send(T)` -> `result<void>`: blocks while the queue
 *   already holds `capacity` values, until room frees up (matching Rust's
 *   `SyncSender::send`); fails with `error::invalid_state` if the
 *   receiver has been dropped (before or while blocked).
 * - `sync_sender<T>::try_send(T)` -> `result<void>`: never blocks; fails
 *   with `error::capacity_exceeded` if the queue is currently full
 *   (matching `TrySendError::Full`), or `error::invalid_state` if the
 *   receiver has been dropped (`TrySendError::Disconnected`).
 *
 * A `capacity` of `0` is a *rendezvous* channel, matching
 * `sync_channel(0)`: `send()` blocks not merely until there is queue room,
 * but until the value it just handed over has actually been consumed by
 * `recv()`/`try_recv()`/`recv_timeout()` -- so a successful `send()`
 * return means the value has provably already reached the receiver, not
 * merely been buffered. `try_send()` on a rendezvous channel only ever
 * succeeds when a `recv()`/`recv_timeout()` call is already blocked
 * waiting to receive it (tracked via an internal waiting-receiver
 * counter); otherwise it fails with `error::capacity_exceeded`, exactly
 * like any other momentarily-full bounded channel.
 *
 * `sync_sender<T>` is `Clone` exactly like `sender<T>` (an ordinary copy
 * constructor); every clone shares the same `capacity`.
 *
 * `T` must be `std::is_nothrow_move_constructible_v`, like every other
 * reloco container element requirement.
 *
 * `is_send<sender<T>>`/`is_send<receiver<T>>` forward to `is_send<T>` (a
 * `T` unsound to move to another thread is equally unsound to hand to
 * another thread through a channel). `is_sync<sender<T>>` is `is_send<T>`
 * too -- every access is fully mutex-guarded, so concurrently calling
 * `try_send` through a shared `const sender<T> &` from multiple threads is
 * sound whenever `T` itself is `Send` (matching current Rust, where
 * `mpsc::Sender<T>: Sync` when `T: Send`). `is_sync<receiver<T>>` is always
 * `false`: nothing about the implementation actually requires this (the
 * same mutex would serialize concurrent `recv()`/`try_recv()` calls just as
 * safely), but reloco deliberately matches Rust's API contract that a
 * channel has exactly one logical consumer, rather than letting multiple
 * threads race unpredictably over which one dequeues a given value.
 * `sync_sender<T>`'s `is_send`/`is_sync` specializations match `sender<T>`'s
 * exactly, for the same reasons.
 */

#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "duration.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "lifetime.hpp"
#include "mutex.hpp"
#include "optional.hpp"
#include "send_sync.hpp"
#include "shared_ptr.hpp"
#include "unique_ptr.hpp"

#include <cstddef>
#include <iterator>
#include <mutex>
#include <type_traits>
#include <utility>

namespace reloco {

template <typename T> class sender;
template <typename T> class receiver;
template <typename T> class sync_sender;

template <typename T>
[[nodiscard]] result<std::pair<sender<T>, receiver<T>>> channel(allocator_ref alloc = default_allocator()) noexcept;

template <typename T>
[[nodiscard]] result<std::pair<sync_sender<T>, receiver<T>>>
sync_channel(std::size_t capacity, allocator_ref alloc = default_allocator()) noexcept;

namespace detail {

/** @brief Singly-linked queue node, heap-allocated via the channel's own
 * allocator. `next` being a `unique_ptr<channel_node>` to its own type is
 * the ordinary self-referential-node idiom: only a pointer, never the
 * pointee, is stored inline, so the type is complete by the time any of
 * `unique_ptr`'s members are actually instantiated. */
template <typename T> struct channel_node {
  T value;
  unique_ptr<channel_node> next;

  template <typename... Args>
  explicit channel_node(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
      : value(std::forward<Args>(args)...) {}
};

/** @brief State shared between every `sender<T>`/`sync_sender<T>` clone
 * and the single `receiver<T>`, kept alive by an atomically-refcounted
 * `shared_ptr` (see `send_sync.hpp` for why `rc<T>` -- non-atomic --
 * would be unsound here). All fields below except `alloc` (set once at
 * construction, never mutated again) are guarded by `guard`.
 *
 * `bounded`/`capacity`/`queue_len`/`waiting_receivers`/`not_full` only do
 * anything when this state backs a `sync_channel<T>` (`bounded == true`):
 * an ordinary `channel<T>` leaves `bounded` `false` and never waits on
 * `not_full`, so the extra bookkeeping costs a few bytes and a couple of
 * always-cheap increments/decrements per operation, nothing more. */
template <typename T> struct channel_shared {
  mutex guard;
  condition_variable not_empty;
  condition_variable not_full;
  unique_ptr<channel_node<T>> head;
  channel_node<T> *tail = nullptr;
  std::size_t sender_count = 1;
  bool receiver_alive = true;
  bool bounded = false;
  std::size_t capacity = 0; // Meaningful only when bounded; 0 means a rendezvous channel.
  std::size_t queue_len = 0;
  std::size_t waiting_receivers = 0; // Meaningful only for a bounded, 0-capacity (rendezvous) channel.
  allocator_ref alloc;

  explicit channel_shared(allocator_ref a) noexcept : alloc(a) {}
};

/** @brief Appends `node` to the queue and increments `queue_len`. Caller
 * must already hold `shared.guard`. Shared between `sender<T>::try_send`
 * and `sync_sender<T>::send`/`try_send`. */
template <typename T> void push_node(channel_shared<T> &shared, unique_ptr<channel_node<T>> node) noexcept {
  auto *raw = node.get();
  if (shared.tail)
    shared.tail->next = std::move(node);
  else
    shared.head = std::move(node);
  shared.tail = raw;
  ++shared.queue_len;
}

} // namespace detail

/**
 * @brief Sending half of an mpsc channel, matching Rust's
 * `std::sync::mpsc::Sender<T>`. `Clone`-like via an ordinary copy
 * constructor (see `rc<T>`/`shared_ptr<T>`'s own copy-is-clone
 * convention); every clone may call `try_send` independently from any
 * thread.
 */
template <typename T> class RELOCO_OWNER sender {
public:
  sender(const sender &other) noexcept : shared_(other.shared_) {
    if (shared_) {
      std::lock_guard<mutex> lock(shared_->guard);
      ++shared_->sender_count;
    }
  }

  sender &operator=(const sender &other) noexcept {
    if (this != &other) {
      drop_ref();
      shared_ = other.shared_;
      if (shared_) {
        std::lock_guard<mutex> lock(shared_->guard);
        ++shared_->sender_count;
      }
    }
    return *this;
  }

  sender(sender &&other) noexcept : shared_(std::move(other.shared_)) {}

  sender &operator=(sender &&other) noexcept {
    if (this != &other) {
      drop_ref();
      shared_ = std::move(other.shared_);
    }
    return *this;
  }

  ~sender() noexcept { drop_ref(); }

  /**
   * @brief Enqueues `value` for the receiver. Never blocks the sender:
   * fails with the allocator's own error if the node allocation fails, or
   * `error::invalid_state` if the receiver has already been dropped.
   */
  [[nodiscard]] result<void> try_send(T value) noexcept {
    auto node_result = unique_ptr<detail::channel_node<T>>::try_allocate(shared_->alloc, std::move(value));
    if (!node_result)
      return unexpected(node_result.error());
    unique_ptr<detail::channel_node<T>> node = std::move(*node_result);

    {
      std::lock_guard<mutex> lock(shared_->guard);
      if (!shared_->receiver_alive)
        return unexpected(error::invalid_state);
      detail::push_node(*shared_, std::move(node));
    }
    shared_->not_empty.notify_one();
    return {};
  }

private:
  friend result<std::pair<sender<T>, receiver<T>>> channel<T>(allocator_ref) noexcept;

  explicit sender(shared_ptr<detail::channel_shared<T>> shared) noexcept : shared_(std::move(shared)) {}

  void drop_ref() noexcept {
    if (!shared_)
      return;
    bool last;
    {
      std::lock_guard<mutex> lock(shared_->guard);
      last = (--shared_->sender_count == 0);
    }
    if (last)
      shared_->not_empty.notify_all(); // Wake a blocked recv() so it observes the disconnect.
  }

  shared_ptr<detail::channel_shared<T>> shared_;
};

/**
 * @brief Receiving half of an mpsc channel, matching Rust's
 * `std::sync::mpsc::Receiver<T>`. Move-only: exactly one consumer is ever
 * meant to call `recv()`/`try_recv()`/`recv_timeout()`. Shared unchanged
 * between `channel<T>` and `sync_channel<T>` -- see the file-level
 * documentation for the bounded-specific behavior that only kicks in when
 * this receiver's `sync_sender<T>` counterpart exists.
 */
template <typename T> class RELOCO_OWNER receiver {
public:
  receiver(receiver &&) noexcept = default;
  receiver &operator=(receiver &&) noexcept = default;
  receiver(const receiver &) = delete;
  receiver &operator=(const receiver &) = delete;

  ~receiver() noexcept {
    if (!shared_)
      return;
    {
      std::lock_guard<mutex> lock(shared_->guard);
      shared_->receiver_alive = false;
    }
    // Wakes any sync_sender<T>::send() blocked waiting for queue room or
    // (capacity == 0) a rendezvous handoff; a no-op for a plain
    // channel<T>, where nothing ever waits on not_full.
    shared_->not_full.notify_all();
  }

  /**
   * @brief Blocks until a value is available or every `sender<T>`/
   * `sync_sender<T>` clone has been dropped, in which case it fails with
   * `error::container_empty`.
   */
  [[nodiscard]] result<T> recv() noexcept {
    optional<T> popped;
    {
      std::unique_lock<mutex> lock(shared_->guard);
      ++shared_->waiting_receivers;
      auto wait_result = shared_->not_empty.wait(
          lock, [this] { return static_cast<bool>(shared_->head) || shared_->sender_count == 0; });
      --shared_->waiting_receivers;
      if (!wait_result)
        return unexpected(wait_result.error());
      if (!shared_->head)
        return unexpected(error::container_empty);
      popped.emplace(pop_front());
    }
    shared_->not_full.notify_one(); // No-op unless a sync_sender<T> is waiting for the room/handoff we just made.
    return result<T>(std::move(*popped));
  }

  /**
   * @brief Bounded `recv()`: blocks until a value is available, every
   * `sender<T>` clone has been dropped (fails with
   * `error::container_empty`), or `timeout` elapses first (fails with
   * `error::timed_out`) -- matching Rust's `mpsc::Receiver::recv_timeout`
   * (`RecvTimeoutError::Disconnected`/`RecvTimeoutError::Timeout`
   * respectively).
   */
  [[nodiscard]] result<T> recv_timeout(duration timeout) noexcept {
    optional<T> popped;
    {
      std::unique_lock<mutex> lock(shared_->guard);
      ++shared_->waiting_receivers;
      auto wait_result = shared_->not_empty.wait_for(
          lock, timeout, [this] { return static_cast<bool>(shared_->head) || shared_->sender_count == 0; });
      --shared_->waiting_receivers;
      if (!wait_result)
        return unexpected(wait_result.error());
      if (!*wait_result)
        return unexpected(error::timed_out);
      if (!shared_->head)
        return unexpected(error::container_empty);
      popped.emplace(pop_front());
    }
    shared_->not_full.notify_one();
    return result<T>(std::move(*popped));
  }

  /**
   * @brief Never blocks: fails with `error::try_again` if the queue is
   * momentarily empty but at least one sender remains, or
   * `error::container_empty` if it is empty and every sender has already
   * been dropped.
   */
  [[nodiscard]] result<T> try_recv() noexcept {
    optional<T> popped;
    {
      std::lock_guard<mutex> lock(shared_->guard);
      if (!shared_->head)
        return unexpected(shared_->sender_count == 0 ? error::container_empty : error::try_again);
      popped.emplace(pop_front());
    }
    shared_->not_full.notify_one();
    return result<T>(std::move(*popped));
  }

  /**
   * @brief Single-pass input iterator over a `receiver<T>`, driven by
   * either blocking `recv()` (see `receiver::begin()`/`end()`) or
   * non-blocking `try_recv()` (see `receiver::try_iter()`) calls,
   * advancing one value per increment. Reaches `end()` once the
   * underlying `recv()`/`try_recv()` call fails (every sender dropped, or
   * -- for the non-blocking flavor -- the queue is momentarily empty).
   * Matches Rust's `impl Iterator for Receiver<T>`/`Receiver::try_iter()`.
   */
  class iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = const T *;
    using reference = const T &;

    /** @brief Constructs the `end()` sentinel: never dereferenced, never advanced. */
    iterator() noexcept = default;

    [[nodiscard]] reference operator*() const noexcept RELOCO_LIFETIMEBOUND { return *current_; }
    [[nodiscard]] pointer operator->() const noexcept RELOCO_LIFETIMEBOUND { return &*current_; }

    iterator &operator++() noexcept {
      advance();
      return *this;
    }

    // Rust/range-for only ever discards the previous value, so the
    // post-increment overload need not return a usable prior-state copy
    // (T need not even be copyable).
    void operator++(int) noexcept { advance(); }

    [[nodiscard]] friend bool operator==(const iterator &a, const iterator &b) noexcept {
      // Every live (non-end) iterator instance is only ever compared
      // against end() in a range-for loop, never against another live
      // iterator, so "both currently empty" is a sufficient definition of
      // equality here (matching std::istream_iterator's own convention).
      return !a.current_.has_value() && !b.current_.has_value();
    }
    [[nodiscard]] friend bool operator!=(const iterator &a, const iterator &b) noexcept { return !(a == b); }

  private:
    friend class receiver;

    iterator(receiver *r, bool blocking) noexcept : receiver_(r), blocking_(blocking) { advance(); }

    void advance() noexcept {
      auto next = blocking_ ? receiver_->recv() : receiver_->try_recv();
      if (next)
        current_.emplace(std::move(*next));
      else
        current_.reset();
    }

    receiver *receiver_ = nullptr;
    bool blocking_ = true;
    optional<T> current_;
  };

  /** @brief Blocking iteration: `for (auto &&value : rx) { ... }` calls
   * `recv()` under the hood, stopping once every `sender<T>` clone has
   * been dropped -- matching Rust's `for value in rx`. */
  [[nodiscard]] iterator begin() noexcept { return iterator(this, /*blocking=*/true); }
  [[nodiscard]] iterator end() noexcept { return iterator(); }

  /** @brief Non-blocking iteration view: `for (auto &&value :
   * rx.try_iter()) { ... }` drains whatever is already queued via
   * `try_recv()`, stopping (without blocking) once the queue is
   * momentarily empty -- matching Rust's `Receiver::try_iter()`. */
  struct try_iter_view {
    receiver *rx;
    [[nodiscard]] iterator begin() const noexcept { return iterator(rx, /*blocking=*/false); }
    [[nodiscard]] iterator end() const noexcept { return iterator(); }
  };
  [[nodiscard]] try_iter_view try_iter() noexcept { return try_iter_view{this}; }

private:
  friend result<std::pair<sender<T>, receiver<T>>> channel<T>(allocator_ref) noexcept;
  friend result<std::pair<sync_sender<T>, receiver<T>>> sync_channel<T>(std::size_t, allocator_ref) noexcept;

  explicit receiver(shared_ptr<detail::channel_shared<T>> shared) noexcept : shared_(std::move(shared)) {}

  // Caller must already hold shared_->guard.
  T pop_front() noexcept {
    unique_ptr<detail::channel_node<T>> node = std::move(shared_->head);
    shared_->head = std::move(node->next);
    if (!shared_->head)
      shared_->tail = nullptr;
    --shared_->queue_len;
    return std::move(node->value);
  }

  shared_ptr<detail::channel_shared<T>> shared_;
};

/**
 * @brief Sending half of a bounded (`sync_channel<T>`) mpsc channel,
 * matching Rust's `std::sync::mpsc::SyncSender<T>`. `Clone`-like via an
 * ordinary copy constructor, exactly like `sender<T>`; every clone shares
 * the same `capacity` and may call `send()`/`try_send()` independently
 * from any thread. See the file-level documentation above for the
 * blocking/rendezvous semantics.
 */
template <typename T> class RELOCO_OWNER sync_sender {
public:
  sync_sender(const sync_sender &other) noexcept : shared_(other.shared_) {
    if (shared_) {
      std::lock_guard<mutex> lock(shared_->guard);
      ++shared_->sender_count;
    }
  }

  sync_sender &operator=(const sync_sender &other) noexcept {
    if (this != &other) {
      drop_ref();
      shared_ = other.shared_;
      if (shared_) {
        std::lock_guard<mutex> lock(shared_->guard);
        ++shared_->sender_count;
      }
    }
    return *this;
  }

  sync_sender(sync_sender &&other) noexcept : shared_(std::move(other.shared_)) {}

  sync_sender &operator=(sync_sender &&other) noexcept {
    if (this != &other) {
      drop_ref();
      shared_ = std::move(other.shared_);
    }
    return *this;
  }

  ~sync_sender() noexcept { drop_ref(); }

  /**
   * @brief Enqueues `value` for the receiver, blocking while the queue
   * already holds `capacity` values. Fails with the allocator's own error
   * if the node allocation fails, or `error::invalid_state` if the
   * receiver has been dropped (before or while blocked). For a rendezvous
   * channel (`capacity == 0`), additionally blocks after enqueuing until
   * the value has actually been consumed by the receiver -- see the
   * file-level documentation above.
   */
  [[nodiscard]] result<void> send(T value) noexcept {
    auto node_result = unique_ptr<detail::channel_node<T>>::try_allocate(shared_->alloc, std::move(value));
    if (!node_result)
      return unexpected(node_result.error());
    unique_ptr<detail::channel_node<T>> node = std::move(*node_result);

    std::unique_lock<mutex> lock(shared_->guard);
    // A rendezvous (capacity == 0) channel still allows exactly one value
    // to be enqueued transiently while the handoff is in progress -- see
    // the rendezvous wait below, which is what actually enforces "not
    // delivered until consumed" for that case.
    std::size_t slot_capacity = shared_->capacity == 0 ? 1 : shared_->capacity;
    auto wait_result = shared_->not_full.wait(
        lock, [this, slot_capacity] { return shared_->queue_len < slot_capacity || !shared_->receiver_alive; });
    if (!wait_result)
      return unexpected(wait_result.error());
    if (!shared_->receiver_alive)
      return unexpected(error::invalid_state);

    detail::push_node(*shared_, std::move(node));
    shared_->not_empty.notify_one();

    if (shared_->capacity != 0)
      return {};

    auto rendezvous_wait =
        shared_->not_full.wait(lock, [this] { return shared_->queue_len == 0 || !shared_->receiver_alive; });
    if (!rendezvous_wait)
      return unexpected(rendezvous_wait.error());
    if (shared_->queue_len != 0)
      return unexpected(error::invalid_state); // Receiver dropped before consuming the value.
    return {};
  }

  /**
   * @brief Never blocks: fails with `error::capacity_exceeded` if the
   * queue is currently at `capacity` (matching Rust's
   * `TrySendError::Full`), or `error::invalid_state` if the receiver has
   * been dropped (`TrySendError::Disconnected`). On a rendezvous channel
   * (`capacity == 0`), succeeds only when a `recv()`/`recv_timeout()`
   * call is already blocked waiting to receive it -- otherwise fails with
   * `error::capacity_exceeded`, exactly like any other momentarily-full
   * bounded channel.
   */
  [[nodiscard]] result<void> try_send(T value) noexcept {
    auto node_result = unique_ptr<detail::channel_node<T>>::try_allocate(shared_->alloc, std::move(value));
    if (!node_result)
      return unexpected(node_result.error());
    unique_ptr<detail::channel_node<T>> node = std::move(*node_result);

    {
      std::lock_guard<mutex> lock(shared_->guard);
      if (!shared_->receiver_alive)
        return unexpected(error::invalid_state);

      bool has_room = shared_->capacity == 0 ? (shared_->queue_len == 0 && shared_->waiting_receivers > 0)
                                             : shared_->queue_len < shared_->capacity;
      if (!has_room)
        return unexpected(error::capacity_exceeded);

      detail::push_node(*shared_, std::move(node));
    }
    shared_->not_empty.notify_one();
    return {};
  }

private:
  friend result<std::pair<sync_sender<T>, receiver<T>>> sync_channel<T>(std::size_t, allocator_ref) noexcept;

  explicit sync_sender(shared_ptr<detail::channel_shared<T>> shared) noexcept : shared_(std::move(shared)) {}

  void drop_ref() noexcept {
    if (!shared_)
      return;
    bool last;
    {
      std::lock_guard<mutex> lock(shared_->guard);
      last = (--shared_->sender_count == 0);
    }
    if (last)
      shared_->not_empty.notify_all(); // Wake a blocked recv() so it observes the disconnect.
  }

  shared_ptr<detail::channel_shared<T>> shared_;
};

/**
 * @brief Creates a connected `sender<T>`/`receiver<T>` pair sharing one
 * heap-allocated, atomically-refcounted internal queue. `T` must be
 * `std::is_nothrow_move_constructible_v`.
 */
template <typename T> [[nodiscard]] result<std::pair<sender<T>, receiver<T>>> channel(allocator_ref alloc) noexcept {
  static_assert(std::is_nothrow_move_constructible_v<T>,
                "reloco::channel: T must be nothrow move constructible (queue nodes are moved through the channel "
                "without any error-recovery path)");

  auto shared = try_allocate_combined_shared<detail::channel_shared<T>>(alloc, alloc);
  if (!shared)
    return unexpected(shared.error());

  sender<T> tx(*shared);
  receiver<T> rx(std::move(*shared));
  return std::pair<sender<T>, receiver<T>>(std::move(tx), std::move(rx));
}

/**
 * @brief Creates a connected `sync_sender<T>`/`receiver<T>` pair sharing
 * one heap-allocated, atomically-refcounted internal queue, matching
 * Rust's `std::sync::mpsc::sync_channel`. `send()` blocks while the queue
 * already holds `capacity` values; a `capacity` of `0` is a rendezvous
 * channel (see the file-level documentation above). `T` must be
 * `std::is_nothrow_move_constructible_v`.
 */
template <typename T>
[[nodiscard]] result<std::pair<sync_sender<T>, receiver<T>>> sync_channel(std::size_t capacity,
                                                                          allocator_ref alloc) noexcept {
  static_assert(std::is_nothrow_move_constructible_v<T>,
                "reloco::sync_channel: T must be nothrow move constructible (queue nodes are moved through the "
                "channel without any error-recovery path)");

  auto shared = try_allocate_combined_shared<detail::channel_shared<T>>(alloc, alloc);
  if (!shared)
    return unexpected(shared.error());
  (*shared)->bounded = true;
  (*shared)->capacity = capacity;

  sync_sender<T> tx(*shared);
  receiver<T> rx(std::move(*shared));
  return std::pair<sync_sender<T>, receiver<T>>(std::move(tx), std::move(rx));
}

/** @brief `sender<T>` is `Send` exactly when `T` is (a clone may be handed
 * to another thread, which then sends `T` values across); always `Sync`
 * when `T` is `Send` too -- every access is fully mutex-guarded. */
template <typename T> struct is_send<sender<T>> : is_send<T> {};
template <typename T> struct is_sync<sender<T>> : is_send<T> {};

/** @brief `sync_sender<T>` matches `sender<T>`'s own `is_send`/`is_sync`
 * specializations exactly, for the same reasons. */
template <typename T> struct is_send<sync_sender<T>> : is_send<T> {};
template <typename T> struct is_sync<sync_sender<T>> : is_send<T> {};

/** @brief `receiver<T>` is `Send` exactly when `T` is; never `Sync` --
 * matching Rust's single-consumer API contract (see the file-level docs
 * above for why this is a deliberate API choice, not a soundness one). */
template <typename T> struct is_send<receiver<T>> : is_send<T> {};
template <typename T> struct is_sync<receiver<T>> : std::false_type {};

} // namespace reloco
