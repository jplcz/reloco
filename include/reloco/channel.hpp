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
 * - `receiver<T>::try_recv()` -> `result<T>`: never blocks; fails with
 *   `error::try_again` if the queue is momentarily empty but senders remain
 *   (matching Rust's `TryRecvError::Empty`), or `error::container_empty` if
 *   it is empty and permanently will be (matching
 *   `TryRecvError::Disconnected`).
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
 */

#include "default_allocator.hpp"
#include "detail/assert.hpp"
#include "error.hpp"
#include "expected.hpp"
#include "mutex.hpp"
#include "send_sync.hpp"
#include "shared_ptr.hpp"
#include "unique_ptr.hpp"

#include <mutex>
#include <type_traits>
#include <utility>

namespace reloco {

template <typename T> class sender;
template <typename T> class receiver;

template <typename T>
[[nodiscard]] result<std::pair<sender<T>, receiver<T>>> channel(allocator_ref alloc = default_allocator()) noexcept;

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

/** @brief State shared between every `sender<T>` clone and the single
 * `receiver<T>`, kept alive by an atomically-refcounted `shared_ptr` (see
 * `send_sync.hpp` for why `rc<T>` -- non-atomic -- would be unsound here).
 * All fields below except `alloc` (set once at construction, never
 * mutated again) are guarded by `guard`. */
template <typename T> struct channel_shared {
  mutex guard;
  condition_variable not_empty;
  unique_ptr<channel_node<T>> head;
  channel_node<T> *tail = nullptr;
  std::size_t sender_count = 1;
  bool receiver_alive = true;
  allocator_ref alloc;

  explicit channel_shared(allocator_ref a) noexcept : alloc(a) {}
};

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

      auto *raw = node.get();
      if (shared_->tail)
        shared_->tail->next = std::move(node);
      else
        shared_->head = std::move(node);
      shared_->tail = raw;
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
 * meant to call `recv()`/`try_recv()`.
 */
template <typename T> class RELOCO_OWNER receiver {
public:
  receiver(receiver &&) noexcept = default;
  receiver &operator=(receiver &&) noexcept = default;
  receiver(const receiver &) = delete;
  receiver &operator=(const receiver &) = delete;

  ~receiver() noexcept {
    if (shared_) {
      std::lock_guard<mutex> lock(shared_->guard);
      shared_->receiver_alive = false;
    }
  }

  /**
   * @brief Blocks until a value is available or every `sender<T>` clone
   * has been dropped, in which case it fails with
   * `error::container_empty`.
   */
  [[nodiscard]] result<T> recv() noexcept {
    std::unique_lock<mutex> lock(shared_->guard);
    auto wait_result = shared_->not_empty.wait(
        lock, [this] { return static_cast<bool>(shared_->head) || shared_->sender_count == 0; });
    if (!wait_result)
      return unexpected(wait_result.error());
    if (!shared_->head)
      return unexpected(error::container_empty);
    return result<T>(pop_front());
  }

  /**
   * @brief Never blocks: fails with `error::try_again` if the queue is
   * momentarily empty but at least one sender remains, or
   * `error::container_empty` if it is empty and every sender has already
   * been dropped.
   */
  [[nodiscard]] result<T> try_recv() noexcept {
    std::lock_guard<mutex> lock(shared_->guard);
    if (!shared_->head)
      return unexpected(shared_->sender_count == 0 ? error::container_empty : error::try_again);
    return result<T>(pop_front());
  }

private:
  friend result<std::pair<sender<T>, receiver<T>>> channel<T>(allocator_ref) noexcept;

  explicit receiver(shared_ptr<detail::channel_shared<T>> shared) noexcept : shared_(std::move(shared)) {}

  // Caller must already hold shared_->guard.
  T pop_front() noexcept {
    unique_ptr<detail::channel_node<T>> node = std::move(shared_->head);
    shared_->head = std::move(node->next);
    if (!shared_->head)
      shared_->tail = nullptr;
    return std::move(node->value);
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

/** @brief `sender<T>` is `Send` exactly when `T` is (a clone may be handed
 * to another thread, which then sends `T` values across); always `Sync`
 * when `T` is `Send` too -- every access is fully mutex-guarded. */
template <typename T> struct is_send<sender<T>> : is_send<T> {};
template <typename T> struct is_sync<sender<T>> : is_send<T> {};

/** @brief `receiver<T>` is `Send` exactly when `T` is; never `Sync` --
 * matching Rust's single-consumer API contract (see the file-level docs
 * above for why this is a deliberate API choice, not a soundness one). */
template <typename T> struct is_send<receiver<T>> : is_send<T> {};
template <typename T> struct is_sync<receiver<T>> : std::false_type {};

} // namespace reloco
