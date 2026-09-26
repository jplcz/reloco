// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file thread_pthread.ipp @brief Out-of-line bodies for the
 * RELOCO_THREAD_BACKEND_PTHREAD `thread` class (see thread.hpp). Included
 * from thread.hpp itself, guarded on RELOCO_SHARED_PROVIDE_DEFINITIONS
 * (see reloco/detail/compat.hpp). Never included directly. */

namespace detail {

// Applies box->name to the calling (i.e. the just-spawned) thread itself,
// best-effort: silently does nothing on a platform providing neither
// naming call below. Must run from inside the spawned thread -- macOS's
// pthread_setname_np only ever names the *calling* thread, so naming from
// the parent right after pthread_create is not a portable option here.
inline void apply_thread_name(const thread_entry_box *box) noexcept {
  if (box->name.empty())
    return;
#if defined(__APPLE__) || defined(__MACH__)
  pthread_setname_np(box->name.unsafe_c_str());
#elif defined(__FreeBSD__)
  pthread_set_name_np(pthread_self(), box->name.unsafe_c_str());
#elif defined(__linux__)
  pthread_setname_np(pthread_self(), box->name.unsafe_c_str());
#endif
}

// pthread_create's required `void *(*)(void *)` signature: unboxes and
// invokes the entry callable, then frees the box itself -- the spawned
// thread is responsible for its own cleanup since nothing else observes
// this pointer again.
inline void *thread_trampoline(void *arg) noexcept {
  auto *box = static_cast<thread_entry_box *>(arg);
  allocator_ref alloc = box->alloc;
  apply_thread_name(box);
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
  auto *box = new (block->ptr) detail::thread_entry_box{std::move(entry), alloc, inline_string<15>()};

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

RELOCO_API result<thread> thread::try_spawn_named(function<void()> &&entry, allocator_ref alloc,
                                                  optional<std::size_t> stack_size,
                                                  inline_string<15> name) noexcept {
  auto block = alloc.allocate(sizeof(detail::thread_entry_box), alignof(detail::thread_entry_box));
  if (!block)
    return unexpected(block.error());
  auto *box = new (block->ptr) detail::thread_entry_box{std::move(entry), alloc, name};

  pthread_attr_t attr;
  pthread_attr_t *attr_ptr = nullptr;
  if (stack_size.has_value()) {
    pthread_attr_init(&attr);
    if (pthread_attr_setstacksize(&attr, *stack_size) != 0) {
      pthread_attr_destroy(&attr);
      box->~thread_entry_box();
      alloc.deallocate(block->ptr, block->size);
      return unexpected(error::invalid_argument);
    }
    attr_ptr = &attr;
  }

  pthread_t handle;
  int rc = pthread_create(&handle, attr_ptr, &detail::thread_trampoline, box);
  if (attr_ptr != nullptr)
    pthread_attr_destroy(&attr);
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
