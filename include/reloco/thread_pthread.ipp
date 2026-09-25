// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file thread_pthread.ipp @brief Out-of-line bodies for the
 * RELOCO_THREAD_BACKEND_PTHREAD `thread` class (see thread.hpp). Included
 * from thread.hpp itself, guarded on RELOCO_SHARED_PROVIDE_DEFINITIONS
 * (see reloco/detail/compat.hpp). Never included directly. */

namespace detail {

// pthread_create's required `void *(*)(void *)` signature: unboxes and
// invokes the entry callable, then frees the box itself -- the spawned
// thread is responsible for its own cleanup since nothing else observes
// this pointer again.
inline void *thread_trampoline(void *arg) noexcept {
  auto *box = static_cast<thread_entry_box *>(arg);
  allocator_ref alloc = box->alloc;
  box->entry();
  box->~thread_entry_box();
  alloc.deallocate(box, sizeof(thread_entry_box));
  return nullptr;
}

} // namespace detail

RELOCO_API result<thread> thread::try_spawn(function<void()> &&entry, allocator_ref alloc) noexcept {
  auto block = alloc.allocate(sizeof(detail::thread_entry_box), alignof(detail::thread_entry_box));
  if (!block)
    return unexpected(block.error());
  auto *box = new (block->ptr) detail::thread_entry_box{std::move(entry), alloc};

  pthread_t handle;
  int rc = pthread_create(&handle, nullptr, &detail::thread_trampoline, box);
  if (rc != 0) {
    box->~thread_entry_box();
    alloc.deallocate(block->ptr, block->size);
    return unexpected(error::resource_exhausted);
  }

  thread t;
  t.handle_ = handle;
  t.joinable_ = true;
  return t;
}

RELOCO_API void thread::join() & noexcept {
  RELOCO_ASSERT(joinable_, "thread: join() called on a non-joinable thread");
  pthread_join(handle_, nullptr);
  joinable_ = false;
}

RELOCO_API void thread::detach() & noexcept {
  RELOCO_ASSERT(joinable_, "thread: detach() called on a non-joinable thread");
  pthread_detach(handle_);
  joinable_ = false;
}
